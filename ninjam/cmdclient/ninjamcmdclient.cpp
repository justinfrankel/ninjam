#include <windows.h>
#include <math.h>
#include <signal.h>

#include "../../WDL/jnetlib/jnetlib.h"
#include "../netmsg.h"
#include "../mpb.h"

#include "../audiostream.h"

#include "../vorbisencdec.h"

#include "../pcmfmtcvt.h"
#include "../../WDL/sha.h"
#include "../../WDL/rng.h"
#include "../../WDL/string.h"

#include "../../WDL/timing.h"

extern char *get_asio_configstr();

unsigned char zero_guid[16];


void makeFilenameFromGuid(WDL_String *s, unsigned char *guid)
{
  char buf[256];
  int x;
  for (x = 0; x < 16; x ++) wsprintf(buf+x*2,"%02x",guid[x]);
  strcpy(buf+x*2,".ogg");
  s->Set(buf);
}

#define DOWNLOAD_TIMEOUT 8

class downloadState
{
public:
  downloadState() : fp(0)
  {
    memset(&guid,0,sizeof(guid));
    time(&last_time);
  }
  ~downloadState()
  {
    Close();
  }

  void Close()
  {
    if (fp) fclose(fp);
    fp=0;
  }

  void Open()
  {    
    Close();
    WDL_String s;
    makeFilenameFromGuid(&s,guid);
    fp=fopen(s.Get(),"wb");
  }

  void Write(void *buf, int len)
  {
    if (fp)
    {
      fwrite(buf,1,len,fp);
      fflush(fp);
    }
  }

  time_t last_time;
  unsigned char guid[16];

private:
  FILE *fp;
};


#define MAX_USER_CHANNELS 32
class RemoteUser_Channel
{
  public:
    RemoteUser_Channel() : active(0), volume(1.0f), pan(0.0f), muted(false), decode_fp(0), decode_codec(0)
    {
      memset(cur_guid,0,sizeof(cur_guid));
    }
    ~RemoteUser_Channel()
    {
      delete decode_codec;
      decode_codec=0;
      if (decode_fp) fclose(decode_fp);
      decode_fp=0;
    }

    int active;
    float volume, pan;
    bool muted;

    unsigned char cur_guid[16];
    WDL_String name;


    // decode/mixer state, used by mixer
    FILE *decode_fp;
    VorbisDecoder *decode_codec;

};

class RemoteUser
{
public:
  RemoteUser()
  {
  }
  ~RemoteUser()
  {
  }

  WDL_String name;
  RemoteUser_Channel channels[MAX_USER_CHANNELS];

};



WDL_PtrList<RemoteUser> m_remoteusers;

CRITICAL_SECTION net_cs;
audioStreamer *g_audio;
Net_Connection *netcon=0;


// per-channel encoding stuff
VorbisEncoder *g_vorbisenc, *g_vorbisenc_2;
int g_vorbisenc_2_needreinit;
downloadState g_curwritefile;



int g_audio_enable;
void audiostream_onunder() { }
void audiostream_onover() { }


#define MIN_ENC_BLOCKSIZE 8192
#define MAX_ENC_BLOCKSIZE (16384-128)

int m_bpm=120,m_bpi=32;
int m_beatinfo_updated=1;



WDL_PtrList<downloadState> m_downloads;

// only used by the audio thread
int m_active_bpm=120, m_active_bpi=32;
int m_interval_length=1000;
int m_interval_pos=-1, m_metronome_pos=0, m_metronome_state=0, m_metronome_tmp=0,m_metronome_interval;

void updateBPMinfo(int bpm, int bpi)
{
  EnterCriticalSection(&net_cs);
  m_bpm=bpm;
  m_bpi=bpi;
  m_beatinfo_updated=1;
  LeaveCriticalSection(&net_cs);
}


void process_samples(float *buf, int len, int nch)
{

  // encode my audio and send to server, if enabled
  if (g_vorbisenc)
  {
    g_vorbisenc->Encode(buf,len);

    int s;
    while ((s=g_vorbisenc->outqueue.Available())>MIN_ENC_BLOCKSIZE)
    {
      if (s > MAX_ENC_BLOCKSIZE) s=MAX_ENC_BLOCKSIZE;

      {
        mpb_client_upload_interval_write wh;
        memcpy(wh.guid,g_curwritefile.guid,sizeof(g_curwritefile.guid));
        wh.flags=0;
        wh.audio_data=g_vorbisenc->outqueue.Get();
        wh.audio_data_len=s;
        g_curwritefile.Write(wh.audio_data,wh.audio_data_len);

        EnterCriticalSection(&net_cs);
        netcon->Send(wh.build());
        LeaveCriticalSection(&net_cs);
      }

      g_vorbisenc->outqueue.Advance(s);
    }
    g_vorbisenc->outqueue.Compact();
  }


  if (! (1)) // input monitoring
  {
    memset(buf,0,len*sizeof(float)*nch);// silence
  }

  // mix in (super shitty) metronome (fucko!!!!)
  {
    int metrolen=g_audio->m_srate / 100;
    double sc=12000.0/(double)g_audio->m_srate;
    int x;
    for (x = 0; x < len; x ++)
    {
      if (m_metronome_pos <= 0)
      {
        m_metronome_state=1;
        m_metronome_tmp=m_interval_pos+x<m_metronome_interval;
        m_metronome_pos = m_metronome_interval;
      }
      m_metronome_pos--;

      if (m_metronome_state>0)
      {
        if (1)
        {
          float val=(float) sin((double)m_metronome_state*sc)*0.5f;
          if (!m_metronome_tmp) val *= 0.5f;
          if (nch == 1) buf[x]+=val;
          else
          {
            buf[x+x]+=val;
            buf[x+x+1]+=val;
          }
        }
        if (++m_metronome_state >= metrolen) m_metronome_state=0;

      }
    }   
  }


  // mix in all active channels
  int u;
  for (u = 0; u < m_remoteusers.GetSize(); u ++)
  {
    RemoteUser *user=m_remoteusers.Get(u);
    int ch;
    for (ch = 0; ch < MAX_USER_CHANNELS; ch ++)
    {
      RemoteUser_Channel *chan=&user->channels[ch];
      if (chan->decode_codec && chan->decode_fp)
      {
        int needed;
        while (chan->decode_codec->m_samples_used < 
              (needed=resampleLengthNeeded(chan->decode_codec->GetSampleRate(),g_audio->m_srate,len)*chan->decode_codec->GetNumChannels()) && 
              !feof(chan->decode_fp))
        {
          char buf[4096];
          int l=fread(buf,1,sizeof(buf),chan->decode_fp);
          chan->decode_codec->Decode(buf,l);
        }


        if (chan->decode_codec->m_samples_used >= needed)
        {
          float *sptr=(float *)chan->decode_codec->m_samples.Get();

          if (!chan->muted) mixFloats(sptr,
                    chan->decode_codec->GetSampleRate(),
                    chan->decode_codec->GetNumChannels(),
                    buf,
                    g_audio->m_srate,nch,len,
                    chan->volume,chan->pan);

          // advance the queue
          chan->decode_codec->m_samples_used -= needed;
          memcpy(sptr,sptr+needed,chan->decode_codec->m_samples_used*sizeof(float));
        }

      }
    }
  }


}

void on_new_interval()
{
  if (g_vorbisenc)
  {
    // finish any encoding
    if (1) g_vorbisenc->Encode(NULL,0);

    // send any final message, with the last one with a flag 
    // saying "we're done"
    do
    {
      mpb_client_upload_interval_write wh;
      int l=g_vorbisenc->outqueue.Available();
      if (l>MAX_ENC_BLOCKSIZE) l=MAX_ENC_BLOCKSIZE;

      memcpy(wh.guid,g_curwritefile.guid,sizeof(wh.guid));
      wh.audio_data=g_vorbisenc->outqueue.Get();
      wh.audio_data_len=l;

      g_curwritefile.Write(wh.audio_data,wh.audio_data_len);

      g_vorbisenc->outqueue.Advance(l);
      wh.flags=g_vorbisenc->outqueue.GetSize()>0 ? 0 : 1;

      EnterCriticalSection(&net_cs);
      netcon->Send(wh.build());
      LeaveCriticalSection(&net_cs);
    }
    while (g_vorbisenc->outqueue.Available()>0);
    g_vorbisenc->outqueue.Compact(); // free any memory left

    //delete g_vorbisenc;
  //  g_vorbisenc=0;

    if (1)
    {
      EnterCriticalSection(&net_cs);
      VorbisEncoder *t;
      // swap g_vorbisenc and g_vorbisenc_2
      t=g_vorbisenc;
      g_vorbisenc=g_vorbisenc_2;
      g_vorbisenc_2=t;
      if (g_vorbisenc_2_needreinit) g_vorbisenc->reinit();
      g_vorbisenc_2_needreinit=1;
      LeaveCriticalSection(&net_cs);
    }
  }

  WDL_RNG_bytes(&g_curwritefile.guid,sizeof(g_curwritefile.guid));

  g_curwritefile.Open(); //only save other peoples for now


  int u;
  // disable playback
  for (u = 0; u < m_remoteusers.GetSize(); u ++)
  {
    RemoteUser *user=m_remoteusers.Get(u);
    int ch;
    for (ch = 0; ch < MAX_USER_CHANNELS; ch ++)
    {
      RemoteUser_Channel *chan=&user->channels[ch];
      if (chan->active)
      {
        if (chan->decode_codec) delete chan->decode_codec;
        chan->decode_codec=0;
        if (chan->decode_fp) fclose(chan->decode_fp);
        chan->decode_fp=0;

        if (memcmp(chan->cur_guid,zero_guid,sizeof(zero_guid)))
        {
          WDL_String s;
          makeFilenameFromGuid(&s,chan->cur_guid);
          chan->decode_fp=fopen(s.Get(),"rb");
          if (chan->decode_fp)
          {
            chan->decode_codec= new VorbisDecoder;
          }
        }
      }
    }
  }


  EnterCriticalSection(&net_cs);

  {
    mpb_client_upload_interval_begin cuib;
    cuib.chidx=0;
    memcpy(cuib.guid,g_curwritefile.guid,sizeof(cuib.guid));
    cuib.fourcc='oggv';
    cuib.estsize=0;
    netcon->Send(cuib.build());
  }

  LeaveCriticalSection(&net_cs);
  
  //if (g_vorbisenc->isError()) printf("ERROR\n");
  //else printf("YAY\n");

}

void audiostream_onsamples(float *buf, int len, int nch)
{
  if (!g_audio_enable) 
  {
    memset(buf,0,len*nch*sizeof(float));
    return;
  }


  EnterCriticalSection(&net_cs);
  if (m_beatinfo_updated)
  {
    double v=(double)m_bpm*(1.0/60.0);
    // beats per second

    // (beats/interval) / (beats/sec)
    v = (double) m_bpi / v;

    // seconds/interval

    // samples/interval
    v *= (double) g_audio->m_srate;

    m_beatinfo_updated=0;
    m_interval_length = (int)v;
    m_interval_pos=-1;
    m_active_bpm=m_bpm;
    m_active_bpi=m_bpi;
    m_metronome_interval=(int) ((double)m_interval_length / (double)m_active_bpi);
  }
  LeaveCriticalSection(&net_cs);


  while (len > 0)
  {
    int x=m_interval_length-m_interval_pos;
    if (!x || m_interval_pos < 0)
    {
      // new buffer time
      on_new_interval();

      m_metronome_pos=0;
      m_interval_pos=0;
      x=m_interval_length;
    }

    if (x > len) x=len;

    process_samples(buf,x,nch);


    m_interval_pos+=x;
    buf += x*nch;
    len -= x;    
  }  

}


int g_done=0;

void sigfunc(int sig)
{
  printf("Got Ctrl+C\n");
  g_done++;
}

int main(int argc, char **argv)
{
  timingInit();
  signal(SIGINT,sigfunc);

  DWORD v=GetTickCount();
  WDL_RNG_addentropy(&v,sizeof(v));
  v=(DWORD)time(NULL);
  WDL_RNG_addentropy(&v,sizeof(v));


  InitializeCriticalSection(&net_cs);
  printf("Ninjam v0.0 command line client starting up...\n");

  if (argc != 2)
  {
    printf("Usage: ninjam hostname\n");
    return 0;
  }

  g_audio_enable=0;

  {
    audioStreamer_ASIO *audio;
    char *dev_name_in;
    
    dev_name_in=get_asio_configstr();
    audio=new audioStreamer_ASIO;

    int nbufs=2,bufsize=4096;
    if (audio->Open(&dev_name_in,48000,2,16,-1,&nbufs,&bufsize))
    {
      printf("Error opening audio!\n");
      return 0;
    }
    printf("Opened %s (%dHz %dch %dbps)\n",dev_name_in,
      audio->m_srate, audio->m_nch, audio->m_bps);
    g_audio=audio;
  }

  g_vorbisenc = new VorbisEncoder(g_audio->m_srate,g_audio->m_nch,0.25);
  g_vorbisenc_2 = new VorbisEncoder(g_audio->m_srate,g_audio->m_nch,0.25);

  JNL::open_socketlib();

  {
    printf("Connecting to %s...\n",argv[1]);
    JNL_Connection *con=new JNL_Connection(JNL_CONNECTION_AUTODNS,65536,65536);
    con->connect(argv[1],2049);

    for (;;)
    {
      con->run();
      if (con->get_state() < JNL_Connection::STATE_RESOLVING || con->get_state() >= JNL_Connection::STATE_CONNECTED)
        break;
      Sleep(100);
    }
    if (con->get_state() != JNL_Connection::STATE_CONNECTED)
    {
      printf("Error connecting!\n");
      delete con;
    }
    else
    {
      printf("Connected!\n");

      netcon = (Net_Connection*)new Net_Connection;
      netcon->attach(con);
    }
  }

  if (netcon)
  {

    while (!netcon->GetStatus() && !g_done)
    {
      EnterCriticalSection(&net_cs);

      if (g_vorbisenc_2_needreinit) 
      {
        g_vorbisenc_2_needreinit=0;
        g_vorbisenc_2->reinit();
      }

      Net_Message *msg=netcon->Run();
      LeaveCriticalSection(&net_cs);
      if (msg)
      {
        msg->addRef();

        switch (msg->get_type())
        {
          case MESSAGE_SERVER_AUTH_CHALLENGE:
            {

              mpb_server_auth_challenge cha;
              if (!cha.parse(msg))
              {
                printf("Got challenge.\n");

                char buf[512],pass[512];
                printf("Enter username: ");
                fflush(stdout);
                gets(buf);
                printf("Enter password: ");
                fflush(stdout);
                gets(pass);

                mpb_client_auth_user repl;
                repl.username=buf;
                // fucko: make password hash

                WDL_SHA1 tmp;
                tmp.add(cha.challenge,sizeof(cha.challenge));
                tmp.add(buf,strlen(buf));
                tmp.add(":",1);
                tmp.add(pass,strlen(pass));
                tmp.result(repl.passhash);               


                EnterCriticalSection(&net_cs);
                netcon->Send(repl.build());
                LeaveCriticalSection(&net_cs);
              }
            }
          break;
          case MESSAGE_SERVER_AUTH_REPLY:
            {
              mpb_server_auth_reply ar;
              if (!ar.parse(msg))
              {
                printf("Got auth reply of %d\n",ar.flag);


                if (ar.flag) // send our channel information
                {
                  mpb_client_set_channel_info sci;

                  sci.build_add_rec("default channel",0,0,0);

                  EnterCriticalSection(&net_cs);
                  netcon->Send(sci.build());
                  LeaveCriticalSection(&net_cs);
                }
                else 
                {
                  EnterCriticalSection(&net_cs);
                  netcon->Kill();
                  LeaveCriticalSection(&net_cs);
                }
              }
            }
          break;
          case MESSAGE_SERVER_CONFIG_CHANGE_NOTIFY:
            {
              mpb_server_config_change_notify ccn;
              if (!ccn.parse(msg))
              {
                updateBPMinfo(ccn.beats_minute,ccn.beats_interval);
                printf("Got info update from server, bpm=%d, bpi=%d\n",m_bpm,m_bpi);
                g_audio_enable=1;
              }
            }

          break;
          case MESSAGE_SERVER_USERINFO_CHANGE_NOTIFY:
            {
              mpb_server_userinfo_change_notify ucn;
              if (!ucn.parse(msg))
              {
                int offs=0;
                int a=0, cid=0, p=0,f=0;
                short v=0;
                char *un=0,*chn=0;
                while ((offs=ucn.parse_get_rec(offs,&a,&cid,&v,&p,&f,&un,&chn))>0)
                {
                  if (!un) un="";
                  if (!chn) chn="";

                  int x;
                  RemoteUser *theuser;
                  for (x = 0; x < m_remoteusers.GetSize() && strcmp((theuser=m_remoteusers.Get(x))->name.Get(),un); x ++);
                  if (x == m_remoteusers.GetSize())
                  {
                    theuser=new RemoteUser;
                    theuser->name.Set(un);
                    m_remoteusers.Add(theuser);
                  }

                  printf("user %s, channel %d \"%s\": %s v:%d.%ddB p:%d flag=%d\n",un,cid,chn,a?"active":"inactive",(int)v/10,abs((int)v)%10,p,f);

                  if (cid >= 0 && cid < MAX_USER_CHANNELS)
                  {
                    if (a)
                    {
                      theuser->channels[cid].name.Set(chn);
                      theuser->channels[cid].active=1;

                      printf("subscribing to user...\n");
                      mpb_client_set_usermask su;
                      su.build_add_rec(un,~0);// subscribe to everything for now
                      EnterCriticalSection(&net_cs);
                      netcon->Send(su.build());
                      LeaveCriticalSection(&net_cs);
                    }
                    else
                    {
                      theuser->channels[cid].name.Set("");
                      theuser->channels[cid].active=0;
                    }
                  }
                }
              }
            }
          break;
          case MESSAGE_SERVER_DOWNLOAD_INTERVAL_BEGIN:
            {
              mpb_server_download_interval_begin dib;
              if (!dib.parse(msg) && dib.username)
              {
                int x;
                RemoteUser *theuser;
                for (x = 0; x < m_remoteusers.GetSize() && strcmp((theuser=m_remoteusers.Get(x))->name.Get(),dib.username); x ++);
                if (x < m_remoteusers.GetSize() && dib.chidx >= 0 && dib.chidx < MAX_USER_CHANNELS)
                {

                  memcpy(theuser->channels[dib.chidx].cur_guid,dib.guid,sizeof(dib.guid));

                  //printf("Getting interval for %s, channel %d\n",dib.username,dib.chidx);
                  if (dib.fourcc && memcmp(dib.guid,zero_guid,sizeof(zero_guid))) // download coming
                  {
                    downloadState *ds=new downloadState;
                    memcpy(ds->guid,dib.guid,sizeof(ds->guid));
                    ds->Open();
                    m_downloads.Add(ds);
                  }

                }
              }
            }
          break;
          case MESSAGE_SERVER_DOWNLOAD_INTERVAL_WRITE:
            {
              mpb_server_download_interval_write diw;
              if (!diw.parse(msg)) 
              {
                time_t now;
                time(&now);
                int x;
                for (x = 0; x < m_downloads.GetSize(); x ++)
                {
                  downloadState *ds=m_downloads.Get(x);
                  if (x)
                  {
                    if (!memcmp(ds->guid,diw.guid,sizeof(ds->guid)))
                    {
                      ds->last_time=now;
                      if (diw.audio_data_len > 0 && diw.audio_data) ds->Write(diw.audio_data,diw.audio_data_len);
                      if (diw.flags & 1)
                      {
                        delete ds;
                        m_downloads.Delete(x);
                      }
                      break;
                    }

                    if (now - ds->last_time > DOWNLOAD_TIMEOUT)
                    {
                      delete ds;
                      m_downloads.Delete(x--);
                    }
                  }
                }
              }
            }
          break;
          default:
            printf("Got unknown message %02X\n",msg->get_type());
          break;
        }

        msg->releaseRef();
      }
      else Sleep(10);
    }
  }

  printf("Shutting down\n");
  timingPrint();

  delete g_audio;

  JNL::close_socketlib();
  return 0;
}
