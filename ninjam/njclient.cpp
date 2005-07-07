#include <math.h>
#include <stdio.h>
#include <stdarg.h>
#include "njclient.h"
#include "mpb.h"
#include "../WDL/pcmfmtcvt.h"
#include "../WDL/wavwrite.h"



// todo: make an interface base class for vorbis enc/dec
#define VorbisEncoder I_NJEncoder 
#define VorbisDecoder I_NJDecoder 
#define NJ_ENCODER_FMT_TYPE MAKE_NJ_FOURCC('O','G','G','v')
#include "../WDL/vorbisencdec.h"
#undef VorbisEncoder
#undef VorbisDecoder


#define MAKE_NJ_FOURCC(A,B,C,D) ((A) | ((B)<<8) | ((C)<<16) | ((D)<<24))

class DecodeState
{
  public:
    DecodeState() : decode_fp(0), decode_codec(0), dump_samples(0),
                                           decode_samplesout(0), resample_state(0.0), decode_peak_vol(0.0)
    { 
      memset(guid,0,sizeof(guid));
    }
    ~DecodeState()
    {
      delete decode_codec;
      decode_codec=0;
      if (decode_fp) fclose(decode_fp);
      decode_fp=0;
    }

    unsigned char guid[16];
    double decode_peak_vol;

    FILE *decode_fp;
    I_NJDecoder *decode_codec;
    int decode_samplesout;
    int dump_samples;
    double resample_state;

};


class RemoteUser_Channel
{
  public:
    RemoteUser_Channel();
    ~RemoteUser_Channel();

    float volume, pan;

    WDL_String name;

    // decode/mixer state, used by mixer
    DecodeState *ds;
    DecodeState *next_ds[2]; // prepared by main thread, for audio thread

};

class RemoteUser
{
public:
  RemoteUser() : muted(0), volume(1.0f), pan(0.0f), submask(0), mutedmask(0), solomask(0), chanpresentmask(0) { }
  ~RemoteUser() { }

  bool muted;
  float volume;
  float pan;
  WDL_String name;
  int submask;
  int chanpresentmask;
  int mutedmask;
  int solomask;
  RemoteUser_Channel channels[MAX_USER_CHANNELS];
};


class RemoteDownload
{
public:
  RemoteDownload();
  ~RemoteDownload();

  void Close();
  void Open(NJClient *parent, unsigned int fourcc);
  void Write(void *buf, int len);
  void startPlaying(int force=0); // call this with 1 to make sure it gets played ASAP, or let RemoteDownload call it automatically

  time_t last_time;
  unsigned char guid[16];

  int chidx;
  WDL_String username;
  int playtime;

private:
  unsigned int m_fourcc;
  NJClient *m_parent;
  FILE *fp;
};



class BufferQueue
{
  public:
    BufferQueue() { }
    ~BufferQueue() 
    { 
      int x;
      for (x = 0; x < m_emptybufs.GetSize(); x ++)
        delete m_emptybufs.Get(x);
      m_emptybufs.Empty();
      int l=m_samplequeue.Available()/4;
      WDL_HeapBuf **bufs=(WDL_HeapBuf **)m_samplequeue.Get();
      if (bufs) while (l--)
      {
        if ((int)*bufs != 0 && (int)*bufs != -1) delete *bufs;
        bufs++;
      }
      m_samplequeue.Advance(m_samplequeue.Available());
      m_samplequeue.Compact();
    }

    void AddBlock(float *samples, int len, float *samples2=NULL);
    int GetBlock(WDL_HeapBuf **b); // return 0 if got one, 1 if none avail
    void DisposeBlock(WDL_HeapBuf *b);

  private:
    WDL_Queue m_samplequeue; // a list of pointers, with NULL to define spaces
    WDL_PtrList<WDL_HeapBuf> m_emptybufs;
    WDL_Mutex m_cs;
};


class Local_Channel
{
public:
  Local_Channel();
  ~Local_Channel();

  int channel_idx;

  int src_channel; // 0 or 1
  int bitrate;

  float volume;
  float pan;
  bool muted;
  bool solo;

  //?
  // mode flag. 0=silence, 1=broadcasting
  bool broadcasting; //takes effect next loop



  // internal state. should ONLY be used by the audio thread.
  bool bcast_active;


  void (*cbf)(float *, int ns, void *);
  void *cbf_inst;

  BufferQueue m_bq;

  double decode_peak_vol;
  bool m_need_header;
  I_NJEncoder  *m_enc;
  int m_enc_bitrate_used;
  Net_Message *m_enc_header_needsend;
  
  WDL_String name;
  RemoteDownload m_curwritefile;
  WaveWriter *m_wavewritefile;

  //DecodeState too, eventually
};







#define MIN_ENC_BLOCKSIZE 2048
#define MAX_ENC_BLOCKSIZE (8192+1024)


#define NJ_PORT 2049

static unsigned char zero_guid[16];


static void guidtostr(unsigned char *guid, char *str)
{
  int x;
  for (x = 0; x < 16; x ++) wsprintf(str+x*2,"%02x",guid[x]);
}
static char *guidtostr_tmp(unsigned char *guid)
{
  static char tmp[64];
  guidtostr(guid,tmp);
  return tmp;
}


static int is_type_char_valid(int c)
{
  c&=0xff;
  return (c >= 'a' && c <= 'z') ||
         (c >= 'A' && c <= 'Z') ||
         (c >= '0' && c <= '9') ||
         c == ' ' || c == '-' || 
         c == '.' || c == '_';
}

static int is_type_valid(unsigned int t)
{
  return (t&0xff) != ' ' &&
          is_type_char_valid(t>>24) &&
          is_type_char_valid(t>>16) &&
          is_type_char_valid(t>>8) &&
          is_type_char_valid(t);
}


static void type_to_string(unsigned int t, char *out)
{
  if (is_type_valid(t))
  {
    out[0]=(t)&0xff;
    out[1]=(t>>8)&0xff;
    out[2]=(t>>16)&0xff;
    out[3]=' ';//(t>>24)&0xff;
    out[4]=0;
    int x=3;
    while (out[x]==' ' && x > 0) out[x--]=0;
  }
  else *out=0;
}

static unsigned int string_to_type(char *in)
{
  int n;
  unsigned int ret=*in;
  if (*in == ' ' || !is_type_char_valid(*in)) return 0;
  in++;
  for (n = 0; n < 3; n ++)
  {
    if (!is_type_char_valid(*in)) break;
    ret|=(*in<<(8+8*n));
    in++;
  }
  if (*in) return 0;
  return ret;
}


void NJClient::makeFilenameFromGuid(WDL_String *s, unsigned char *guid)
{
  char buf[256];
  guidtostr(guid,buf);

  s->Set(m_workdir.Get());
#ifdef _WIN32
  char tmp[3]={buf[0],'\\',0};
#else
  char tmp[3]={buf[0],'/',0};
#endif
  s->Append(tmp);
  s->Append(buf);
}




NJClient::NJClient()
{
  m_wavebq=new BufferQueue;
  m_userinfochange=0;
  m_loopcnt=0;
  m_srate=48000;
#ifdef _WIN32
  DWORD v=GetTickCount();
  WDL_RNG_addentropy(&v,sizeof(v));
  v=(DWORD)time(NULL);
  WDL_RNG_addentropy(&v,sizeof(v));
#else
  time_t v=time(NULL);
  WDL_RNG_addentropy(&v,sizeof(v));
#endif

  config_autosubscribe=1;
  config_savelocalaudio=0;
  config_metronome=0.5f;
  config_metronome_pan=0.0f;
  config_metronome_mute=false;
  config_debug_level=0;
  config_mastervolume=1.0f;
  config_masterpan=0.0f;
  config_mastermute=false;
  config_play_prebuffer=8192;


  LicenseAgreement_User32=0;
  LicenseAgreementCallback=0;
  ChatMessage_Callback=0;
  ChatMessage_User32=0;

  waveWrite=0;
  m_oggWrite=0;
  m_oggComp=0;
  m_logFile=0;

  m_issoloactive=0;
  m_netcon=0;

  _reinit();

  m_session_pos_ms=m_session_pos_samples=0;
}

void NJClient::_reinit()
{
  m_max_localch=MAX_LOCAL_CHANNELS;
  output_peaklevel=0.0;

  m_connection_keepalive=0;
  m_status=-1;

  m_in_auth=0;

  m_bpm=120;
  m_bpi=32;
  
  m_beatinfo_updated=1;

  m_audio_enable=0;

  m_active_bpm=120;
  m_active_bpi=32;
  m_interval_length=1000;
  m_interval_pos=-1;
  m_metronome_pos=0.0;
  m_metronome_state=0;
  m_metronome_tmp=0;
  m_metronome_interval=0;

  m_issoloactive&=~1;

  int x;
  for (x = 0; x < m_locchannels.GetSize(); x ++)
    m_locchannels.Get(x)->decode_peak_vol=0.0f;

}


void NJClient::writeLog(char *fmt, ...)
{
  if (m_logFile)
  {
    va_list ap;
    va_start(ap,fmt);

    m_log_cs.Enter();
    if (m_logFile) vfprintf(m_logFile,fmt,ap);
    m_log_cs.Leave();

    va_end(ap);

  }


}

void NJClient::SetLogFile(char *name)
{
  m_log_cs.Enter();
  if (m_logFile) fclose(m_logFile);
  m_logFile=0;
  if (name && *name)
  {
    if (!strstr(name,"\\") && !strstr(name,"/") && !strstr(name,":"))
    {
      WDL_String s(m_workdir.Get());
      s.Append(name);
      m_logFile=fopen(s.Get(),"a+t");
    }
    else
      m_logFile=fopen(name,"a+t");
  }
  m_log_cs.Leave();
}


NJClient::~NJClient()
{
  delete m_netcon;
  m_netcon=0;

  delete waveWrite;
  SetOggOutFile(NULL,0,0);

  if (m_logFile)
  {
    writeLog("end\n");
    fclose(m_logFile);
    m_logFile=0;
  }

  int x;
  for (x = 0; x < m_remoteusers.GetSize(); x ++) delete m_remoteusers.Get(x);
  m_remoteusers.Empty();
  for (x = 0; x < m_downloads.GetSize(); x ++) delete m_downloads.Get(x);
  m_downloads.Empty();
  for (x = 0; x < m_locchannels.GetSize(); x ++) delete m_locchannels.Get(x);
  m_locchannels.Empty();

  delete m_wavebq;
}


void NJClient::updateBPMinfo(int bpm, int bpi)
{
  m_misc_cs.Enter();
  m_bpm=bpm;
  m_bpi=bpi;
  m_beatinfo_updated=1;
  m_misc_cs.Leave();
}


void NJClient::GetPosition(int *pos, int *length)  // positions in samples
{ 
  if (length) *length=m_interval_length; 
  if (pos && (*pos=m_interval_pos)<0) *pos=0;
}

unsigned int NJClient::GetSessionPosition()// returns milliseconds
{
  unsigned int a=m_session_pos_ms;
  if (m_srate)
    a+=(m_session_pos_samples*1000)/m_srate;
  return a;
}

void NJClient::AudioProc(float **inbuf, int innch, float **outbuf, int outnch, int len, int srate)
{
  m_srate=srate;
  // zero output
  int x;
  for (x = 0; x < outnch; x ++) memset(outbuf[x],0,sizeof(float)*len);
  if (!m_audio_enable) return;

  if (srate>0)
  {
    unsigned int spl=m_session_pos_samples;
    unsigned int sec=m_session_pos_ms;

    spl += len;
    if (spl >= (unsigned int)srate)
    {
      sec += (spl/srate)*1000;
      spl %= srate;
    }
    // writing these both like this reduces the chance that the 
    // main thread will read them and get a mix. still possible, tho,
    // but super unlikely
    m_session_pos_samples=spl;
    m_session_pos_ms=sec;
  }



  int offs=0;

  while (len > 0)
  {
    int x=m_interval_length-m_interval_pos;
    if (!x || m_interval_pos < 0)
    {
      m_misc_cs.Enter();
      if (m_beatinfo_updated)
      {
        double v=(double)m_bpm*(1.0/60.0);
        // beats per second

        // (beats/interval) / (beats/sec)
        v = (double) m_bpi / v;

        // seconds/interval

        // samples/interval
        v *= (double) srate;

        m_beatinfo_updated=0;
        m_interval_length = (int)v;
        //m_interval_length-=m_interval_length%1152;//hack
        m_active_bpm=m_bpm;
        m_active_bpi=m_bpi;
        m_metronome_interval=(int) ((double)m_interval_length / (double)m_active_bpi);
      }
      m_misc_cs.Leave();

      // new buffer time
      on_new_interval();

      m_interval_pos=0;
      x=m_interval_length;
    }

    if (x > len) x=len;

    process_samples(inbuf,innch,outbuf,outnch,x,srate,offs);

    m_interval_pos+=x;
    offs += x;
    len -= x;    
  }  

}


void NJClient::Disconnect()
{
  m_errstr.Set("");
  m_host.Set("");
  m_user.Set("");
  m_pass.Set("");
  delete m_netcon;
  m_netcon=0;

  int x;
  for (x=0;x<m_remoteusers.GetSize(); x++) delete m_remoteusers.Get(x);
  m_remoteusers.Empty();
  if (x) m_userinfochange=1; // if we removed users, notify parent

  for (x = 0; x < m_downloads.GetSize(); x ++) delete m_downloads.Get(x);
  m_downloads.Empty();

  _reinit();
}

void NJClient::Connect(char *host, char *user, char *pass)
{
  Disconnect();

  m_session_pos_ms=m_session_pos_samples=0;

  m_host.Set(host);
  m_user.Set(user);
  m_pass.Set(pass);

  WDL_String tmp(m_host.Get());
  int port=NJ_PORT;
  char *p=strstr(tmp.Get(),":");
  if (p)
  {
    *p=0;
    port=atoi(++p);
    if (!port) port=NJ_PORT;
  }
  JNL_Connection *c=new JNL_Connection(JNL_CONNECTION_AUTODNS,65536,65536);
  c->connect(tmp.Get(),port);
  m_netcon = new Net_Connection;
  m_netcon->attach(c);

  m_status=0;
}

int NJClient::GetStatus()
{
  if (!m_status || m_status == -1) return NJC_STATUS_PRECONNECT;
  if (m_status == 1000) return NJC_STATUS_CANTCONNECT;
  if (m_status == 1001) return NJC_STATUS_INVALIDAUTH;
  if (m_status == 1002) return NJC_STATUS_DISCONNECTED;

  return NJC_STATUS_OK;
}


int NJClient::Run() // nonzero if sleep ok
{
  WDL_HeapBuf *p=0;
  while (!m_wavebq->GetBlock(&p))
  {
    if (p)
    {
      float *f=(float*)p->Get();
      int hl=p->GetSize()/(2*sizeof(float));
      float *outbuf[2]={f,f+hl};
      if (m_oggWrite&&m_oggComp)
      {
        m_oggComp->Encode(f,hl,1,hl);
        if (m_oggComp->outqueue.Available())
        {
          fwrite((char *)m_oggComp->outqueue.Get(),1,m_oggComp->outqueue.Available(),m_oggWrite);
          m_oggComp->outqueue.Advance(m_oggComp->outqueue.Available());
          m_oggComp->outqueue.Compact();
        }
      }
      if (waveWrite)
      {
        waveWrite->WriteFloatsNI(outbuf,0,hl);
      }
      m_wavebq->DisposeBlock(p);
    }
  }
//    
  int wantsleep=1;

  if (m_netcon)
  {
    Net_Message *msg=m_netcon->Run(&wantsleep);
    if (!msg)
    {
      if (m_netcon->GetStatus())
      {
        m_audio_enable=0;
        if (m_in_auth)  m_status=1001;
        if (m_status > 0 && m_status < 1000) m_status=1002;
        if (m_status == 0) m_status=1000;
        return 1;
      }
    }
    else
    {
      msg->addRef();

      switch (msg->get_type())
      {
        case MESSAGE_SERVER_AUTH_CHALLENGE:
          {
            mpb_server_auth_challenge cha;
            if (!cha.parse(msg))
            {
              if (cha.protocol_version < PROTO_VER_MIN || cha.protocol_version >= PROTO_VER_MAX)
              {
                m_errstr.Set("server is incorrect protocol version");
                m_status = 1001;
                m_netcon->Kill();
                return 0;
              }

              mpb_client_auth_user repl;
              repl.username=m_user.Get();
              repl.client_version=PROTO_VER_CUR; // client version number

              m_connection_keepalive=(cha.server_caps>>8)&0xff;

//              printf("Got keepalive of %d\n",m_connection_keepalive);

              if (cha.license_agreement)
              {
                m_netcon->SetKeepAlive(45);
                if (LicenseAgreementCallback && LicenseAgreementCallback(LicenseAgreement_User32,cha.license_agreement))
                {
                  repl.client_caps|=1;
                }
              }
              m_netcon->SetKeepAlive(m_connection_keepalive);

              WDL_SHA1 tmp;
              tmp.add(m_user.Get(),strlen(m_user.Get()));
              tmp.add(":",1);
              tmp.add(m_pass.Get(),strlen(m_pass.Get()));
              tmp.result(repl.passhash);

              tmp.reset(); // new auth method is SHA1(SHA1(user:pass)+challenge)
              tmp.add(repl.passhash,sizeof(repl.passhash));
              tmp.add(cha.challenge,sizeof(cha.challenge));
              tmp.result(repl.passhash);               

              m_netcon->Send(repl.build());

              m_in_auth=1;
            }
          }
        break;
        case MESSAGE_SERVER_AUTH_REPLY:
          {
            mpb_server_auth_reply ar;
            if (!ar.parse(msg))
            {
              if (ar.flag) // send our channel information
              {
                mpb_client_set_channel_info sci;
                int x;
                for (x = 0; x < m_locchannels.GetSize(); x ++)
                {
                  Local_Channel *ch=m_locchannels.Get(x);
                  sci.build_add_rec(ch->name.Get(),0,0,0);
                }
                m_netcon->Send(sci.build());
                m_status=2;
                m_in_auth=0;
                m_max_localch=ar.maxchan;
                if (ar.errmsg)
                  m_user.Set(ar.errmsg); // server gave us an updated name
              }
              else 
              {
                if (ar.errmsg)
                    m_errstr.Set(ar.errmsg);
                m_status = 1001;
                m_netcon->Kill();
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
              m_audio_enable=1;
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

                m_userinfochange=1;

                int x;
                // todo: per-user autosubscribe option, or callback
                // todo: have volume/pan settings here go into defaults for the channel. or not, kinda think it's pointless
                if (cid >= 0 && cid < MAX_USER_CHANNELS)
                {
                  RemoteUser *theuser;
                  for (x = 0; x < m_remoteusers.GetSize() && strcmp((theuser=m_remoteusers.Get(x))->name.Get(),un); x ++);

                 // printf("user %s, channel %d \"%s\": %s v:%d.%ddB p:%d flag=%d\n",un,cid,chn,a?"active":"inactive",(int)v/10,abs((int)v)%10,p,f);


                  m_users_cs.Enter();
                  if (a)
                  {
                    if (x == m_remoteusers.GetSize())
                    {
                      theuser=new RemoteUser;
                      theuser->name.Set(un);
                      m_remoteusers.Add(theuser);
                    }

                    theuser->channels[cid].name.Set(chn);
                    theuser->chanpresentmask |= 1<<cid;


                    if (config_autosubscribe)
                    {
                      theuser->submask |= 1<<cid;
                      mpb_client_set_usermask su;
                      su.build_add_rec(un,theuser->submask);
                      m_netcon->Send(su.build());
                    }
                  }
                  else
                  {
                    if (x < m_remoteusers.GetSize())
                    {
                      theuser->channels[cid].name.Set("");
                      theuser->chanpresentmask &= ~(1<<cid);
                      theuser->submask &= ~(1<<cid);

                      delete theuser->channels[cid].ds;
                      delete theuser->channels[cid].next_ds[0];
                      delete theuser->channels[cid].next_ds[1];
                      theuser->channels[cid].ds=0;
                      theuser->channels[cid].next_ds[0]=0;
                      theuser->channels[cid].next_ds[1]=0;


                      if (!theuser->chanpresentmask) // user no longer exists, it seems
                      {
                        delete theuser;
                        m_remoteusers.Delete(x);
                      }
                    }
                  }
                  m_users_cs.Leave();
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
                //printf("Getting interval for %s, channel %d\n",dib.username,dib.chidx);
                if (!memcmp(dib.guid,zero_guid,sizeof(zero_guid)))
                {
                  m_users_cs.Enter();
                  int useidx=!!theuser->channels[dib.chidx].next_ds[0];
                  DecodeState *tmp=theuser->channels[dib.chidx].next_ds[useidx];
                  theuser->channels[dib.chidx].next_ds[useidx]=0;
                  m_users_cs.Leave();
                  delete tmp;
                }
                else if (dib.fourcc) // download coming
                {                
                  if (config_debug_level>1) printf("RECV BLOCK %s\n",guidtostr_tmp(dib.guid));
                  RemoteDownload *ds=new RemoteDownload;
                  memcpy(ds->guid,dib.guid,sizeof(ds->guid));
                  ds->Open(this,dib.fourcc);

                  ds->playtime=config_play_prebuffer;
                  ds->chidx=dib.chidx;
                  ds->username.Set(dib.username);

                  m_downloads.Add(ds);
                }
                else
                {
                  DecodeState *tmp=start_decode(dib.guid);
                  m_users_cs.Enter();
                  int useidx=!!theuser->channels[dib.chidx].next_ds[0];
                  DecodeState *t2=theuser->channels[dib.chidx].next_ds[useidx];
                  theuser->channels[dib.chidx].next_ds[useidx]=tmp;
                  m_users_cs.Leave();
                  delete t2;
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
                RemoteDownload *ds=m_downloads.Get(x);
                if (ds)
                {
                  if (!memcmp(ds->guid,diw.guid,sizeof(ds->guid)))
                  {
                    if (config_debug_level>1) printf("RECV BLOCK DATA %s%s %d bytes\n",guidtostr_tmp(diw.guid),diw.flags&1?":end":"",diw.audio_data_len);

                    ds->last_time=now;
                    if (diw.audio_data_len > 0 && diw.audio_data)
                    {
                      ds->Write(diw.audio_data,diw.audio_data_len);
                    }
                    if (diw.flags & 1)
                    {
                      delete ds;
                      m_downloads.Delete(x);
                    }
                    break;
                  }

                  if (now - ds->last_time > DOWNLOAD_TIMEOUT)
                  {
                    ds->chidx=-1;
                    delete ds;
                    m_downloads.Delete(x--);
                  }
                }
              }
            }
          }
        break;
        case MESSAGE_CHAT_MESSAGE:
          if (ChatMessage_Callback)
          {
            mpb_chat_message foo;
            if (!foo.parse(msg))
            {
              ChatMessage_Callback(ChatMessage_User32,this,foo.parms,sizeof(foo.parms)/sizeof(foo.parms[0]));
            }
          }
        break;
        default:
          //printf("Got unknown message %02X\n",msg->get_type());
        break;
      }

      msg->releaseRef();
    }
  }

  int u;
  for (u = 0; u < m_locchannels.GetSize(); u ++)
  {
    Local_Channel *lc=m_locchannels.Get(u);
    WDL_HeapBuf *p=0;
    while (!lc->m_bq.GetBlock(&p))
    {
      wantsleep=0;
      if (u >= m_max_localch)
      {
        if (p && (int)p != -1)
          lc->m_bq.DisposeBlock(p);
        p=0;
        continue;
      }

      if ((int)p == -1)
      {
        mpb_client_upload_interval_begin cuib;
        cuib.chidx=lc->channel_idx;
        memset(cuib.guid,0,sizeof(cuib.guid));
        memset(lc->m_curwritefile.guid,0,sizeof(lc->m_curwritefile.guid));
        cuib.fourcc=0;
        cuib.estsize=0;
        m_netcon->Send(cuib.build());
        p=0;
      }
      else if (p)
      {
        // encode data
        if (!lc->m_enc)
        {
          lc->m_enc = new I_NJEncoder(m_srate,1,lc->m_enc_bitrate_used = lc->bitrate,WDL_RNG_int32());
        }

        if (lc->m_need_header)
        {
          lc->m_need_header=false;
          {
            WDL_RNG_bytes(lc->m_curwritefile.guid,sizeof(lc->m_curwritefile.guid));
            char guidstr[64];
            guidtostr(lc->m_curwritefile.guid,guidstr);
            writeLog("local %s %d\n",guidstr,lc->channel_idx);
            if (config_savelocalaudio) 
            {
              lc->m_curwritefile.Open(this,NJ_ENCODER_FMT_TYPE);
              if (lc->m_wavewritefile) delete lc->m_wavewritefile;
              lc->m_wavewritefile=0;
              if (config_savelocalaudio>1)
              {
                WDL_String fn;

                fn.Set(m_workdir.Get());
              #ifdef _WIN32
                char tmp[3]={guidstr[0],'\\',0};
              #else
                char tmp[3]={guidstr[0],'/',0};
              #endif
                fn.Append(tmp);
                fn.Append(guidstr);
                fn.Append(".wav");

                lc->m_wavewritefile=new WaveWriter(fn.Get(),24,1,m_srate);
              }
            }

            mpb_client_upload_interval_begin cuib;
            cuib.chidx=lc->channel_idx;
            memcpy(cuib.guid,lc->m_curwritefile.guid,sizeof(cuib.guid));
            cuib.fourcc=NJ_ENCODER_FMT_TYPE;
            cuib.estsize=0;
            delete lc->m_enc_header_needsend;
            lc->m_enc_header_needsend=cuib.build();
          }
        }

        if (lc->m_enc)
        {
          if (lc->m_wavewritefile)
          {
            lc->m_wavewritefile->WriteFloats((float*)p->Get(),p->GetSize()/sizeof(float));
          }
          lc->m_enc->Encode((float*)p->Get(),p->GetSize()/sizeof(float));

          int s;
          while ((s=lc->m_enc->outqueue.Available())>(lc->m_enc_header_needsend?MIN_ENC_BLOCKSIZE*4:MIN_ENC_BLOCKSIZE))
          {
            if (s > MAX_ENC_BLOCKSIZE) s=MAX_ENC_BLOCKSIZE;

            {
              mpb_client_upload_interval_write wh;
              memcpy(wh.guid,lc->m_curwritefile.guid,sizeof(lc->m_curwritefile.guid));
              wh.flags=0;
              wh.audio_data=lc->m_enc->outqueue.Get();
              wh.audio_data_len=s;
              lc->m_curwritefile.Write(wh.audio_data,wh.audio_data_len);

              if (lc->m_enc_header_needsend)
              {
                if (config_debug_level>1)
                {
                  mpb_client_upload_interval_begin dib;
                  dib.parse(lc->m_enc_header_needsend);
                  printf("SEND BLOCK HEADER %s\n",guidtostr_tmp(dib.guid));
                }
                m_netcon->Send(lc->m_enc_header_needsend);
                lc->m_enc_header_needsend=0;
              }

              if (config_debug_level>1) printf("SEND BLOCK %s%s %d bytes\n",guidtostr_tmp(wh.guid),wh.flags&1?"end":"",wh.audio_data_len);

              m_netcon->Send(wh.build());
            }

            lc->m_enc->outqueue.Advance(s);
          }
          lc->m_enc->outqueue.Compact();
        }
        lc->m_bq.DisposeBlock(p);
        p=0;
      }
      else
      {
        if (lc->m_enc)
        {
          // finish any encoding
          lc->m_enc->Encode(NULL,0);

          // send any final message, with the last one with a flag 
          // saying "we're done"
          do
          {
            mpb_client_upload_interval_write wh;
            int l=lc->m_enc->outqueue.Available();
            if (l>MAX_ENC_BLOCKSIZE) l=MAX_ENC_BLOCKSIZE;

            memcpy(wh.guid,lc->m_curwritefile.guid,sizeof(wh.guid));
            wh.audio_data=lc->m_enc->outqueue.Get();
            wh.audio_data_len=l;

            lc->m_curwritefile.Write(wh.audio_data,wh.audio_data_len);

            lc->m_enc->outqueue.Advance(l);
            wh.flags=lc->m_enc->outqueue.GetSize()>0 ? 0 : 1;

            if (lc->m_enc_header_needsend)
            {
              if (config_debug_level>1)
              {
                mpb_client_upload_interval_begin dib;
                dib.parse(lc->m_enc_header_needsend);
                printf("SEND BLOCK HEADER %s\n",guidtostr_tmp(dib.guid));
              }
              m_netcon->Send(lc->m_enc_header_needsend);
              lc->m_enc_header_needsend=0;
            }

            if (config_debug_level>1) printf("SEND BLOCK %s%s %d bytes\n",guidtostr_tmp(wh.guid),wh.flags&1?"end":"",wh.audio_data_len);
            m_netcon->Send(wh.build());
          }
          while (lc->m_enc->outqueue.Available()>0);
          lc->m_enc->outqueue.Compact(); // free any memory left

          //delete m_enc;
        //  m_enc=0;
          lc->m_enc->reinit();
        }

        if (lc->m_enc && lc->bitrate != lc->m_enc_bitrate_used)
        {
          delete lc->m_enc;
          lc->m_enc=0;
        }
        lc->m_need_header=true;

        // end the last encode
      }
    }
  }

  return wantsleep;

}


DecodeState *NJClient::start_decode(unsigned char *guid, unsigned int fourcc)
{
  DecodeState *newstate=new DecodeState;
  memcpy(newstate->guid,guid,sizeof(newstate->guid));

  WDL_String s;

  makeFilenameFromGuid(&s,guid);

  // todo: make plug-in system to allow encoders to add types allowed
  // todo: with a preference for 'fourcc' if specified
  unsigned int types[]={MAKE_NJ_FOURCC('O','G','G','v')}; // only types we understand

  int oldl=strlen(s.Get())+1;
  s.Append(".XXXXXXXXX");
  unsigned int x;
  for (x = 0; !newstate->decode_fp && x < sizeof(types)/sizeof(types[0]); x ++)
  {
    type_to_string(types[x],s.Get()+oldl);
    newstate->decode_fp=fopen(s.Get(),"rb");
  }

  if (newstate->decode_fp)
  {
    newstate->decode_codec= new I_NJDecoder;
    // run some decoding

    while (newstate->decode_codec->m_samples_used <= 0)
    {
      int l=fread(newstate->decode_codec->DecodeGetSrcBuffer(128),1,128,newstate->decode_fp);          
      if (l) newstate->decode_codec->DecodeWrote(l);
      if (!l) 
      {
        clearerr(newstate->decode_fp);
        break;
      }
    }
  }

  return newstate;
}

float NJClient::GetOutputPeak()
{
  return (float)output_peaklevel;
}

void NJClient::ChatMessage_Send(char *parm1, char *parm2, char *parm3, char *parm4, char *parm5)
{
  if (m_netcon)
  {
    mpb_chat_message m;
    m.parms[0]=parm1;
    m.parms[1]=parm2;
    m.parms[2]=parm3;
    m.parms[3]=parm4;
    m.parms[4]=parm5;
    m_netcon->Send(m.build());
  }
}

void NJClient::process_samples(float **inbuf, int innch, float **outbuf, int outnch, int len, int srate, int offset)
{
                   // -36dB/sec
  double decay=pow(.25*0.25*0.25,len/(double)srate);
  // encode my audio and send to server, if enabled
  int u;
  m_locchan_cs.Enter();
  for (u = 0; u < m_locchannels.GetSize() && u < m_max_localch; u ++)
  {
    Local_Channel *lc=m_locchannels.Get(u);
    int sc=lc->src_channel;
    float *src=NULL;
    if (sc >= 0 && sc < innch) src=inbuf[sc]+offset;

    if (lc->cbf || !src)
    {
      int bytelen=len*(int)sizeof(float);
      if (tmpblock.GetSize() < bytelen) tmpblock.Resize(bytelen);

      if (src) memcpy(tmpblock.Get(),src,bytelen);
      else memset(tmpblock.Get(),0,bytelen);

      src=(float* )tmpblock.Get();

      // processor
      if (lc->cbf)
      {
        lc->cbf(src,len,lc->cbf_inst);
      }
    }

    if (lc->bcast_active) 
    {
      lc->m_bq.AddBlock(src,len);
    }


    // monitor this channel
    if ((!m_issoloactive && !lc->muted) || lc->solo)
    {
      float *out1=outbuf[0]+offset;

      float vol1=lc->volume;
      if (outnch > 1)
      {
        float vol2=vol1;
        float *out2=outbuf[1]+offset;
        if (lc->pan > 0.0f) vol1 *= 1.0f-lc->pan;
        else if (lc->pan < 0.0f) vol2 *= 1.0f+lc->pan;

        float maxf=(float) (lc->decode_peak_vol*decay);

        int x=len;
        while (x--) 
        {
          float f=src[0] * vol1;

          if (f > maxf) maxf=f;
          else if (f < -maxf) maxf=-f;

          if (f > 1.0) f=1.0;
          else if (f < -1.0) f=-1.0;

          *out1++ += f;

          f=src[0]*vol2;

          if (f > maxf) maxf=f;
          else if (f < -maxf) maxf=-f;

          if (f > 1.0) f=1.0;
          else if (f < -1.0) f=-1.0;

          *out2++ += f;
          src++;
        }
        lc->decode_peak_vol=maxf;
      }
      else
      {
        float maxf=(float) (lc->decode_peak_vol*decay);
        int x=len;
        while (x--) 
        {
          float f=*src++ * vol1;
          if (f > maxf) maxf=f;
          else if (f < -maxf) maxf=-f;

          if (f > 1.0) f=1.0;
          else if (f < -1.0) f=-1.0;

          *out1++ += f;
        }
        lc->decode_peak_vol=maxf;
      }
    }
    else lc->decode_peak_vol=0.0;
  }

  m_locchan_cs.Leave();


  // mix in all active (subscribed) channels
  m_users_cs.Enter();
  for (u = 0; u < m_remoteusers.GetSize(); u ++)
  {
    RemoteUser *user=m_remoteusers.Get(u);
    int ch;
    if (!user) continue;

    for (ch = 0; ch < MAX_USER_CHANNELS; ch ++)
    {
      float lpan=user->pan+user->channels[ch].pan;
      if (lpan<-1.0)lpan=-1.0;
      else if (lpan>1.0)lpan=1.0;

      bool muteflag;
      if (m_issoloactive) muteflag = !(user->solomask & (1<<ch));
      else muteflag=(user->mutedmask & (1<<ch)) || user->muted;

      if (user->channels[ch].ds)
        mixInChannel(muteflag,
          user->volume*user->channels[ch].volume,lpan,
            user->channels[ch].ds,outbuf,len,srate,outnch,offset,decay);
    }
  }
  m_users_cs.Leave();


  // write out wave if necessary

  if (waveWrite||(m_oggWrite&&m_oggComp))
  {
    m_wavebq->AddBlock(outbuf[0]+offset,len,outbuf[outnch>1]+offset);
  }


  // apply master volume, then
  {
    int x=len;
    float *ptr1=outbuf[0]+offset;
    float maxf=(float)(output_peaklevel*decay);

    if (outnch >= 2)
    {
      float *ptr2=outbuf[1]+offset;
      float vol1=config_mastermute?0.0f:config_mastervolume;
      float vol2=vol1;
      if (config_masterpan > 0.0f) vol1 *= 1.0f-config_masterpan;
      else if (config_masterpan< 0.0f) vol2 *= 1.0f+config_masterpan;

      while (x--)
      {
        float f = *ptr1++ *= vol1;
        if (f > maxf) maxf=f;
        else if (f < -maxf) maxf=-f;

        f = *ptr2++ *= vol2;
        if (f > maxf) maxf=f;
        else if (f < -maxf) maxf=-f;
      }
    }
    else
    {
      float vol1=config_mastermute?0.0f:config_mastervolume;
      while (x--)
      {
        float f = *ptr1++ *= vol1;
        if (f > maxf) maxf=f;
        else if (f < -maxf) maxf=-f;
      }
    }
    output_peaklevel=maxf;
  }

  // mix in (super shitty) metronome (fucko!!!!)
  {
    int metrolen=srate / 100;
    double sc=6000.0/(double)srate;
    int x;
    int um=config_metronome>0.0001f;
    double vol1=config_metronome_mute?0.0:config_metronome,vol2=vol1;
    float *ptr1=outbuf[0]+offset;
    float *ptr2=NULL;
    if (outnch > 1)
    {
        ptr2=outbuf[1]+offset;
        if (config_metronome_pan > 0.0f) vol1 *= 1.0f-config_metronome_pan;
        else if (config_metronome_pan< 0.0f) vol2 *= 1.0f+config_metronome_pan;
    }
    for (x = 0; x < len; x ++)
    {
      if (m_metronome_pos <= 0.0)
      {
        m_metronome_state=1;
        m_metronome_tmp=(m_interval_pos+x)<m_metronome_interval;
        m_metronome_pos += (double)m_metronome_interval;
      }
      m_metronome_pos-=1.0;

      if (m_metronome_state>0)
      {
        if (um)
        {
          double val=0.0;
          if (!m_metronome_tmp) val = sin((double)m_metronome_state*sc*2.0) * 0.25;
          else val = sin((double)m_metronome_state*sc);

          ptr1[x]+=(float)(val*vol1);
          if (ptr2) ptr2[x]+=(float)(val*vol2);
        }
        if (++m_metronome_state >= metrolen) m_metronome_state=0;

      }
    }   
  }

}

void NJClient::mixInChannel(bool muted, float vol, float pan, DecodeState *chan, float **outbuf, int len, int srate, int outnch, int offs, double vudecay)
{
  if (!chan->decode_codec || !chan->decode_fp) return;

  int needed;
  while (chan->decode_codec->m_samples_used <= 
        (needed=resampleLengthNeeded(chan->decode_codec->GetSampleRate(),srate,len,&chan->resample_state)*chan->decode_codec->GetNumChannels()))
  {
    int l=fread(chan->decode_codec->DecodeGetSrcBuffer(128),1,128,chan->decode_fp);          
    chan->decode_codec->DecodeWrote(l);
    if (!l) 
    {
      clearerr(chan->decode_fp);
      break;
    }
  }

  if (chan->decode_codec->m_samples_used >= needed+chan->dump_samples)
  {
    float *sptr=(float *)chan->decode_codec->m_samples.Get();

    // process VU meter, yay for powerful CPUs
    if (vol > 0.0000001) 
    {
      float *p=sptr;
      int l=(needed+chan->dump_samples)*chan->decode_codec->GetNumChannels();
      float maxf=(float) (chan->decode_peak_vol*vudecay/vol);
      while (l--)
      {
        float f=*p++;
        if (f > maxf) maxf=f;
        else if (f < -maxf) maxf=-f;
      }
      chan->decode_peak_vol=maxf*vol;
    }

    if (!muted)
    {
      float *tmpbuf[2]={outbuf[0]+offs,outnch > 1 ? (outbuf[1]+offs) : 0};
      mixFloatsNIOutput(sptr+chan->dump_samples,
              chan->decode_codec->GetSampleRate(),
              chan->decode_codec->GetNumChannels(),
              tmpbuf,
              srate,outnch>1?2:1,len,
              vol,pan,&chan->resample_state);
    }

    // advance the queue
    chan->decode_samplesout += needed/chan->decode_codec->GetNumChannels();
    chan->decode_codec->m_samples_used -= needed+chan->dump_samples;
    memcpy(sptr,sptr+needed+chan->dump_samples,chan->decode_codec->m_samples_used*sizeof(float));
    chan->dump_samples=0;
  }
  else
  {

    if (config_debug_level>0)
    {
    static int cnt=0;

    char s[512];
    guidtostr(chan->guid,s);

    char buf[512];
    sprintf(buf,"underrun %d at %d on %s, %d/%d samples\n",cnt++,ftell(chan->decode_fp),s,chan->decode_codec->m_samples_used,needed);
#ifdef _WIN32
    OutputDebugString(buf);
#endif
    }

    chan->decode_samplesout += chan->decode_codec->m_samples_used/chan->decode_codec->GetNumChannels();
    chan->decode_codec->m_samples_used=0;
    chan->dump_samples+=needed;

  }
}

void NJClient::on_new_interval()
{
  m_loopcnt++;
  writeLog("interval %d %.2f %d\n",m_loopcnt,GetActualBPM(),m_active_bpi);

  m_metronome_pos=0.0;

  int u;
  m_locchan_cs.Enter();
  for (u = 0; u < m_locchannels.GetSize() && u < m_max_localch; u ++)
  {
    Local_Channel *lc=m_locchannels.Get(u);


    if (lc->bcast_active) 
    {
      lc->m_bq.AddBlock(NULL,0);
    }

    int wasact=lc->bcast_active;

    lc->bcast_active = lc->broadcasting;

    if (wasact && !lc->bcast_active)
    {
      lc->m_bq.AddBlock(NULL,-1);
    }

  }
  m_locchan_cs.Leave();

  m_users_cs.Enter();
  for (u = 0; u < m_remoteusers.GetSize(); u ++)
  {
    RemoteUser *user=m_remoteusers.Get(u);
    int ch;
//    printf("submask=%d,cpm=%d\n",user->submask , user->chanpresentmask);
    for (ch = 0; ch < MAX_USER_CHANNELS; ch ++)
    {
      RemoteUser_Channel *chan=&user->channels[ch];
      delete chan->ds;
      chan->ds=0;
      if ((user->submask & user->chanpresentmask) & (1<<ch)) chan->ds = chan->next_ds[0];
      else delete chan->next_ds[0];
      chan->next_ds[0]=chan->next_ds[1]; // advance queue
      chan->next_ds[1]=0;
      ;
      if (chan->ds)
      {
        char guidstr[64];
        guidtostr(chan->ds->guid,guidstr);
        writeLog("user %s \"%s\" %d \"%s\"\n",guidstr,user->name.Get(),ch,chan->name.Get());
      }
    }
  }
  m_users_cs.Leave();
  
  //if (m_enc->isError()) printf("ERROR\n");
  //else printf("YAY\n");

}


char *NJClient::GetUserState(int idx, float *vol, float *pan, bool *mute)
{
  if (idx<0 || idx>=m_remoteusers.GetSize()) return NULL;
  RemoteUser *p=m_remoteusers.Get(idx);
  if (vol) *vol=p->volume;
  if (pan) *pan=p->pan;
  if (mute) *mute=p->muted;
  return p->name.Get();
}

void NJClient::SetUserState(int idx, bool setvol, float vol, bool setpan, float pan, bool setmute, bool mute)
{
  if (idx<0 || idx>=m_remoteusers.GetSize()) return;
  RemoteUser *p=m_remoteusers.Get(idx);
  if (setvol) p->volume=vol;
  if (setpan) p->pan=pan;
  if (setmute) p->muted=mute;
}

int NJClient::EnumUserChannels(int useridx, int i)
{
  if (useridx<0 || useridx>=m_remoteusers.GetSize()||i<0||i>=MAX_USER_CHANNELS) return -1;
  RemoteUser *user=m_remoteusers.Get(useridx);

  int x;
  for (x = 0; x < 32; x ++)
  {
    if ((user->chanpresentmask & (1<<x)) && !i--) return x;
  }
  return -1;
}

char *NJClient::GetUserChannelState(int useridx, int channelidx, bool *sub, float *vol, float *pan, bool *mute, bool *solo)
{
  if (useridx<0 || useridx>=m_remoteusers.GetSize()||channelidx<0||channelidx>=MAX_USER_CHANNELS) return NULL;
  RemoteUser_Channel *p=m_remoteusers.Get(useridx)->channels + channelidx;
  RemoteUser *user=m_remoteusers.Get(useridx);
  if (!(user->chanpresentmask & (1<<channelidx))) return 0;

  if (sub) *sub=!!(user->submask & (1<<channelidx));
  if (vol) *vol=p->volume;
  if (pan) *pan=p->pan;
  if (mute) *mute=!!(user->mutedmask & (1<<channelidx));
  if (solo) *solo=!!(user->solomask & (1<<channelidx));
  
  return p->name.Get();
}


void NJClient::SetUserChannelState(int useridx, int channelidx, 
                                   bool setsub, bool sub, bool setvol, float vol, bool setpan, float pan, bool setmute, bool mute, bool setsolo, bool solo)
{
  if (useridx<0 || useridx>=m_remoteusers.GetSize()||channelidx<0||channelidx>=MAX_USER_CHANNELS) return;
  RemoteUser *user=m_remoteusers.Get(useridx);
  RemoteUser_Channel *p=user->channels + channelidx;
  if (!(user->chanpresentmask & (1<<channelidx))) return;

  if (setsub && !!(user->submask&(1<<channelidx)) != sub) 
  {
    // toggle subscription
    if (!sub)
    {     
      mpb_client_set_usermask su;
      su.build_add_rec(user->name.Get(),(user->submask&=~(1<<channelidx)));
      m_netcon->Send(su.build());

      DecodeState *tmp,*tmp2,*tmp3;
      m_users_cs.Enter();
      tmp=p->ds; p->ds=0;
      tmp2=p->next_ds[0]; p->next_ds[0]=0;
      tmp3=p->next_ds[1]; p->next_ds[1]=0;
      m_users_cs.Leave();

      delete tmp;
      delete tmp2;   
      delete tmp3;   
    }
    else
    {
      mpb_client_set_usermask su;
      su.build_add_rec(user->name.Get(),(user->submask|=(1<<channelidx)));
      m_netcon->Send(su.build());
    }

  }
  if (setvol) p->volume=vol;
  if (setpan) p->pan=pan;
  if (setmute) 
  {
    if (mute)
      user->mutedmask |= (1<<channelidx);
    else
      user->mutedmask &= ~(1<<channelidx);
  }
  if (setsolo)
  {
    if (solo) user->solomask |= (1<<channelidx);
    else user->solomask &= ~(1<<channelidx);

    if (user->solomask) m_issoloactive|=1;
    else
    {
      int x;
      for (x = 0; x < m_remoteusers.GetSize(); x ++)
      {
        if (m_remoteusers.Get(x)->solomask)
          break;
      }
      if (x == m_remoteusers.GetSize()) m_issoloactive&=~1;
    }
  }
}


float NJClient::GetUserChannelPeak(int useridx, int channelidx)
{
  if (useridx<0 || useridx>=m_remoteusers.GetSize()||channelidx<0||channelidx>=MAX_USER_CHANNELS) return 0.0f;
  RemoteUser_Channel *p=m_remoteusers.Get(useridx)->channels + channelidx;
  RemoteUser *user=m_remoteusers.Get(useridx);
  if (!(user->chanpresentmask & (1<<channelidx))) return 0.0f;
  if (!p->ds) return 0.0f;

  return (float)p->ds->decode_peak_vol;
}

float NJClient::GetLocalChannelPeak(int ch)
{
  int x;
  for (x = 0; x < m_locchannels.GetSize() && m_locchannels.Get(x)->channel_idx!=ch; x ++);
  if (x == m_locchannels.GetSize()) return 0.0f;
  Local_Channel *c=m_locchannels.Get(x);
  return (float)c->decode_peak_vol;
}

void NJClient::DeleteLocalChannel(int ch)
{
  m_locchan_cs.Enter();
  int x;
  for (x = 0; x < m_locchannels.GetSize() && m_locchannels.Get(x)->channel_idx!=ch; x ++);
  if (x < m_locchannels.GetSize())
  {
    delete m_locchannels.Get(x);
    m_locchannels.Delete(x);
  }
  m_locchan_cs.Leave();
}

void NJClient::SetLocalChannelProcessor(int ch, void (*cbf)(float *, int ns, void *), void *inst)
{
  int x;
  for (x = 0; x < m_locchannels.GetSize() && m_locchannels.Get(x)->channel_idx!=ch; x ++);
  if (x < m_locchannels.GetSize()) 
  {
     m_locchan_cs.Enter();
     Local_Channel *c=m_locchannels.Get(x);
     c->cbf=cbf;
     c->cbf_inst=inst;
     m_locchan_cs.Leave();
  }
}

void NJClient::GetLocalChannelProcessor(int ch, void **func, void **inst)
{
  int x;
  for (x = 0; x < m_locchannels.GetSize() && m_locchannels.Get(x)->channel_idx!=ch; x ++);
  if (x == m_locchannels.GetSize()) 
  {
    if (func) *func=0;
    if (inst) *inst=0;
    return;
  }

  Local_Channel *c=m_locchannels.Get(x);
  if (func) *func=(void *)c->cbf;
  if (inst) *inst=c->cbf_inst; 
}

void NJClient::SetLocalChannelInfo(int ch, char *name, bool setsrcch, int srcch,
                                   bool setbitrate, int bitrate, bool setbcast, bool broadcast)
{  
  m_locchan_cs.Enter();
  int x;
  for (x = 0; x < m_locchannels.GetSize() && m_locchannels.Get(x)->channel_idx!=ch; x ++);
  if (x == m_locchannels.GetSize())
  {
    m_locchannels.Add(new Local_Channel);
  }

  Local_Channel *c=m_locchannels.Get(x);
  c->channel_idx=ch;
  if (name) c->name.Set(name);
  if (setsrcch) c->src_channel=srcch;
  if (setbitrate) c->bitrate=bitrate;
  if (setbcast) c->broadcasting=broadcast;
  m_locchan_cs.Leave();
}

char *NJClient::GetLocalChannelInfo(int ch, int *srcch, int *bitrate, bool *broadcast)
{
  int x;
  for (x = 0; x < m_locchannels.GetSize() && m_locchannels.Get(x)->channel_idx!=ch; x ++);
  if (x == m_locchannels.GetSize()) return 0;
  Local_Channel *c=m_locchannels.Get(x);
  if (srcch) *srcch=c->src_channel;
  if (bitrate) *bitrate=c->bitrate;
  if (broadcast) *broadcast=c->broadcasting;

  return c->name.Get();
}

int NJClient::EnumLocalChannels(int i)
{
  if (i<0||i>=m_locchannels.GetSize()) return -1;
  return m_locchannels.Get(i)->channel_idx;
}


void NJClient::SetLocalChannelMonitoring(int ch, bool setvol, float vol, bool setpan, float pan, bool setmute, bool mute, bool setsolo, bool solo)
{
  m_locchan_cs.Enter();
  int x;
  for (x = 0; x < m_locchannels.GetSize() && m_locchannels.Get(x)->channel_idx!=ch; x ++);
  if (x == m_locchannels.GetSize())
  {
    m_locchannels.Add(new Local_Channel);
  }

  Local_Channel *c=m_locchannels.Get(x);
  c->channel_idx=ch;
  if (setvol) c->volume=vol;
  if (setpan) c->pan=pan;
  if (setmute) c->muted=mute;
  if (setsolo) 
  {
    c->solo = solo;
    if (solo) m_issoloactive|=2;
    else
    {
      int x;
      for (x = 0; x < m_locchannels.GetSize(); x ++)
      {
        if (m_locchannels.Get(x)->solo) break;
      }
      if (x == m_locchannels.GetSize())
        m_issoloactive&=~2;
    }
  }
  m_locchan_cs.Leave();
}

int NJClient::GetLocalChannelMonitoring(int ch, float *vol, float *pan, bool *mute, bool *solo)
{
  int x;
  for (x = 0; x < m_locchannels.GetSize() && m_locchannels.Get(x)->channel_idx!=ch; x ++);
  if (x == m_locchannels.GetSize()) return -1;
  Local_Channel *c=m_locchannels.Get(x);
  if (vol) *vol=c->volume;
  if (pan) *pan=c->pan;
  if (mute) *mute=c->muted;
  if (solo) *solo=c->solo;
  return 0;
}



void NJClient::NotifyServerOfChannelChange()
{
  if (m_netcon)
  {
    int x;
    mpb_client_set_channel_info sci;
    for (x = 0; x < m_locchannels.GetSize(); x ++)
    {
      Local_Channel *ch=m_locchannels.Get(x);
      sci.build_add_rec(ch->name.Get(),0,0,0);
    }
    m_netcon->Send(sci.build());
  }
}

void NJClient::SetWorkDir(char *path)
{
  m_workdir.Set(path?path:"");

  if (!path || !*path) return;


  if (path[0] && path[strlen(path)-1] != '/' && path[strlen(path)-1] != '\\') 
#ifdef _WIN32
	m_workdir.Append("\\");
#else
	m_workdir.Append("/");
#endif

  // create subdirectories for ogg files
  int a;
  for (a = 0; a < 16; a ++)
  {
    WDL_String tmp(m_workdir.Get());
    char buf[5];
    sprintf(buf,"%x",a);
    tmp.Append(buf);
#ifdef _WIN32
    CreateDirectory(tmp.Get(),NULL);
#else
    mkdir(tmp.Get(),0700);
#endif
  }
}


RemoteUser_Channel::RemoteUser_Channel() : volume(1.0f), pan(0.0f), ds(NULL)
{
  memset(next_ds,0,sizeof(next_ds));
}

RemoteUser_Channel::~RemoteUser_Channel()
{
  delete ds;
  ds=NULL;
  delete next_ds[0];
  delete next_ds[1];
  memset(next_ds,0,sizeof(next_ds));
}


RemoteDownload::RemoteDownload() : chidx(-1), playtime(0), fp(0)
{
  memset(&guid,0,sizeof(guid));
  time(&last_time);
}

RemoteDownload::~RemoteDownload()
{
  Close();
}

void RemoteDownload::Close()
{
  if (fp) fclose(fp);
  fp=0;
  startPlaying(1);
}

void RemoteDownload::Open(NJClient *parent, unsigned int fourcc)
{    
  m_parent=parent;
  Close();
  WDL_String s;
  parent->makeFilenameFromGuid(&s,guid);


  // append extension from fourcc
  char buf[8];
  type_to_string(fourcc, buf);
  s.Append(".");
  s.Append(buf);

  m_fourcc=fourcc;
  fp=fopen(s.Get(),"wb");
}

void RemoteDownload::startPlaying(int force)
{
  if (m_parent && chidx >= 0 && (force || (playtime && fp && ftell(fp)>playtime))) 
    // wait until we have config_play_prebuffer of data to start playing, or if config_play_prebuffer is 0, we are forced to play (download finished)
  {
    int x;
    RemoteUser *theuser;
    for (x = 0; x < m_parent->m_remoteusers.GetSize() && strcmp((theuser=m_parent->m_remoteusers.Get(x))->name.Get(),username.Get()); x ++);
    if (x < m_parent->m_remoteusers.GetSize() && chidx >= 0 && chidx < MAX_USER_CHANNELS)
    {
       DecodeState *tmp=m_parent->start_decode(guid,m_fourcc);

       DecodeState *tmp2;
       m_parent->m_users_cs.Enter();
       int useidx=!!theuser->channels[chidx].next_ds[0];
       tmp2=theuser->channels[chidx].next_ds[useidx];
       theuser->channels[chidx].next_ds[useidx]=tmp;
       m_parent->m_users_cs.Leave();
       delete tmp2;
    }
    chidx=-1;
  }
}

void RemoteDownload::Write(void *buf, int len)
{
  int pos=len;
  if (fp)
  {
    fwrite(buf,1,len,fp);
    fflush(fp);
    pos = ftell(fp);
  }

  startPlaying();  
}


Local_Channel::Local_Channel() : channel_idx(0), src_channel(0), volume(1.0f), pan(0.0f), 
                muted(false), solo(false), broadcasting(false), m_enc(NULL), m_enc_header_needsend(NULL),
                bcast_active(false), cbf(NULL), cbf_inst(NULL), m_enc_bitrate_used(0), 
                bitrate(64), m_need_header(true), m_wavewritefile(NULL),
                decode_peak_vol(0.0)
{
}


int BufferQueue::GetBlock(WDL_HeapBuf **b) // return 0 if got one, 1 if none avail
{
  m_cs.Enter();
  if (m_samplequeue.Available())
  {
    *b=*(WDL_HeapBuf **)m_samplequeue.Get();
    m_samplequeue.Advance(sizeof(WDL_HeapBuf *));
    if (m_samplequeue.Available()<256) m_samplequeue.Compact();
    m_cs.Leave();
    return 0;
  }
  m_cs.Leave();
  return 1;
}

void BufferQueue::DisposeBlock(WDL_HeapBuf *b)
{
  m_cs.Enter();
  if (b && (int)b != -1) m_emptybufs.Add(b);
  m_cs.Leave();
}


void BufferQueue::AddBlock(float *samples, int len, float *samples2)
{
  WDL_HeapBuf *mybuf=0;
  if (len>0)
  {
    m_cs.Enter();

    if (m_samplequeue.Available() > 512)
    {
      m_cs.Leave();
      return;
    }
    int tmp;
    if ((tmp=m_emptybufs.GetSize()))
    {
      mybuf=m_emptybufs.Get(tmp-1);
      if (mybuf) m_emptybufs.Delete(tmp-1);
    }
    m_cs.Leave();
    if (!mybuf) mybuf=new WDL_HeapBuf;

    int uselen=len*sizeof(float);
    if (samples2)
    {
      uselen+=uselen;
    }

    mybuf->Resize(uselen);

    memcpy(mybuf->Get(),samples,len*sizeof(float));
    if (samples2)
      memcpy((float*)mybuf->Get()+len,samples2,len*sizeof(float));
  }
  else if (len == -1) mybuf=(WDL_HeapBuf *)-1;

  m_cs.Enter();
  m_samplequeue.Add(&mybuf,sizeof(mybuf));
  m_cs.Leave();
}

Local_Channel::~Local_Channel()
{
  delete m_enc;
  m_enc=0;
  delete m_enc_header_needsend;
  m_enc_header_needsend=0;

  delete m_wavewritefile;
  m_wavewritefile=0;

}

void NJClient::SetOggOutFile(FILE *fp, int srate, int nch, int bitrate)
{
  if (m_oggWrite)
  {
    if (m_oggComp)
    {
      m_oggComp->Encode(NULL,0);
      if (m_oggComp->outqueue.Available())
        fwrite((char *)m_oggComp->outqueue.Get(),1,m_oggComp->outqueue.Available(),m_oggWrite);
    }
    fclose(m_oggWrite);
    m_oggWrite=0;
  }
  delete m_oggComp;
  m_oggComp=0;

  if (fp)
  {
    //fucko
    m_oggComp=new I_NJEncoder(srate,nch,bitrate,WDL_RNG_int32());
    m_oggWrite=fp;
  }
}

