#include <math.h>
#include "njclient.h"
#include "../mpb.h"
#include "../pcmfmtcvt.h"


#define MIN_ENC_BLOCKSIZE 2048
#define MAX_ENC_BLOCKSIZE (8192+1024)



unsigned char zero_guid[16];


char *guidtostr_tmp(unsigned char *guid)
{
  static char tmp[64];
  guidtostr(guid,tmp);
  return tmp;
}


void guidtostr(unsigned char *guid, char *str)
{
  int x;
  for (x = 0; x < 16; x ++) wsprintf(str+x*2,"%02x",guid[x]);
}

void makeFilenameFromGuid(WDL_String *s, unsigned char *guid)
{
  char buf[256];
  guidtostr(guid,buf);
  strcat(buf,"." ENCODER_FMT_STRING);
  s->Set(buf);
}




NJClient::NJClient()
{

  DWORD v=GetTickCount();
  WDL_RNG_addentropy(&v,sizeof(v));
  v=(DWORD)time(NULL);
  WDL_RNG_addentropy(&v,sizeof(v));

  m_status=-1;

  config_send=1;
  config_recv=1;
  config_savelocalaudio=0;
  config_monitor=1.0f;
  config_metronome=0.5f;
  config_debug_level=0;



  m_bpm=120;
  m_bpi=32;
  m_beatinfo_updated=1;

  m_audio_enable=0;

  m_active_bpm=120;
  m_active_bpi=32;
  m_interval_length=1000;
  m_interval_pos=-1;
  m_metronome_pos=0;
  m_metronome_state=0;
  m_metronome_tmp=0;
  m_metronome_interval=0;
  m_vorbisenc=0;
  m_curwritefile=new RemoteDownload;
  m_vorbisenc_header_needsend=0;
  InitializeCriticalSection(&m_net_cs);
  m_netcon=0;


}



NJClient::~NJClient()
{
  delete m_netcon;
  m_netcon=0;

  delete m_vorbisenc;
  m_vorbisenc=0;

  delete m_vorbisenc_header_needsend;
  m_vorbisenc_header_needsend=0;
  delete m_curwritefile;
  m_curwritefile=0;


  int x;
  for (x = 0; x < m_remoteusers.GetSize(); x ++) delete m_remoteusers.Get(x);
  m_remoteusers.Empty();
  for (x = 0; x < m_downloads.GetSize(); x ++) delete m_downloads.Get(x);
  m_downloads.Empty();
  DeleteCriticalSection(&m_net_cs);
}


void NJClient::updateBPMinfo(int bpm, int bpi)
{
  EnterCriticalSection(&m_net_cs);
  m_bpm=bpm;
  m_bpi=bpi;
  m_beatinfo_updated=1;
  LeaveCriticalSection(&m_net_cs);
}


void NJClient::AudioProc(float *buf, int len, int nch, int srate)
{
  if (!m_audio_enable) 
  {
    memset(buf,0,len*nch*sizeof(float));
    return;
  }


  EnterCriticalSection(&m_net_cs);
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
    m_interval_pos=-1;
    m_active_bpm=m_bpm;
    m_active_bpi=m_bpi;
    m_metronome_interval=(int) ((double)m_interval_length / (double)m_active_bpi);
  }
  LeaveCriticalSection(&m_net_cs);


  while (len > 0)
  {
    int x=m_interval_length-m_interval_pos;
    if (!x || m_interval_pos < 0)
    {
      // new buffer time
      on_new_interval(nch,srate);

      m_metronome_pos=0;
      m_interval_pos=0;
      x=m_interval_length;
    }

    if (x > len) x=len;

    process_samples(buf,x,nch,srate);


    m_interval_pos+=x;
    buf += x*nch;
    len -= x;    
  }  

}


void NJClient::Connect(char *host, char *user, char *pass)
{
  m_user.Set(user);
  m_pass.Set(pass);

  JNL_Connection *c=new JNL_Connection(JNL_CONNECTION_AUTODNS,65536,65536);
  c->connect(host,2049);
  m_netcon = new Net_Connection;
  m_netcon->attach(c);

  m_status=0;
}

int NJClient::GetStatus()
{
  if (!m_status || m_status == -1) return NJC_STATUS_PRECONNECT;
  if (m_status == 999) return NJC_STATUS_DISCONNECTED;
  if (m_status == 1000) return NJC_STATUS_CANTCONNECT;
  if (m_status == 1001) return NJC_STATUS_INVALIDAUTH;

  return NJC_STATUS_OK;
}


int NJClient::Run() // nonzero if sleep ok
{
  if (!m_netcon) return 1;
  if (m_netcon->GetStatus())
  {
    if (m_status == 0) m_status=1000;
    else if (m_status != 1001) m_status = 999;
    return 1;
  }

  EnterCriticalSection(&m_net_cs);
  int s=0;
  Net_Message *msg=m_netcon->Run(&s);
  LeaveCriticalSection(&m_net_cs);
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

            mpb_client_auth_user repl;
            repl.username=m_user.Get();

            WDL_SHA1 tmp;
            tmp.add(cha.challenge,sizeof(cha.challenge));
            tmp.add(m_user.Get(),strlen(m_user.Get()));
            tmp.add(":",1);
            tmp.add(m_pass.Get(),strlen(m_pass.Get()));
            tmp.result(repl.passhash);               

            EnterCriticalSection(&m_net_cs);
            m_netcon->Send(repl.build());
            LeaveCriticalSection(&m_net_cs);
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
              if (config_send)
              {
                mpb_client_set_channel_info sci;

                sci.build_add_rec("default channel",0,0,0);

                EnterCriticalSection(&m_net_cs);
                m_netcon->Send(sci.build());
                LeaveCriticalSection(&m_net_cs);
              }
              m_status=2;
            }
            else 
            {
              m_status = 1001;
              EnterCriticalSection(&m_net_cs);
              m_netcon->Kill();
              LeaveCriticalSection(&m_net_cs);
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

              if (cid >= 0 && cid < MAX_USER_CHANNELS && config_recv)
              {
                if (a)
                {
                  theuser->channels[cid].name.Set(chn);
                  theuser->channels[cid].active=1;

                  printf("subscribing to user...\n");
                  mpb_client_set_usermask su;
                  su.build_add_rec(un,~0);// subscribe to everything for now
                  EnterCriticalSection(&m_net_cs);
                  m_netcon->Send(su.build());
                  LeaveCriticalSection(&m_net_cs);
                }
                else
                {
                  EnterCriticalSection(&m_net_cs);
                  theuser->channels[cid].name.Set("");
                  theuser->channels[cid].active=0;
                  memset(theuser->channels[cid].cur_guid,0,sizeof(theuser->channels[cid].cur_guid));
                  delete theuser->channels[cid].decode_codec;
                  theuser->channels[cid].decode_codec=0;
                  if (theuser->channels[cid].decode_fp)
                  {
                    fclose(theuser->channels[cid].decode_fp);
                    theuser->channels[cid].decode_fp=0;
                  }
                  LeaveCriticalSection(&m_net_cs);
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
                if (config_debug_level>1) printf("RECV BLOCK %s\n",guidtostr_tmp(dib.guid));
                RemoteDownload *ds=new RemoteDownload;
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

  return s;

}

void NJClient::process_samples(float *buf, int len, int nch, int srate)
{

  // encode my audio and send to server, if enabled
  if (config_send && m_vorbisenc)
  {
    m_vorbisenc->Encode(buf,len);

    int s;
    while ((s=m_vorbisenc->outqueue.Available())>(m_vorbisenc_header_needsend?MIN_ENC_BLOCKSIZE*4:MIN_ENC_BLOCKSIZE))
    {
      if (s > MAX_ENC_BLOCKSIZE) s=MAX_ENC_BLOCKSIZE;

      {
        mpb_client_upload_interval_write wh;
        memcpy(wh.guid,m_curwritefile->guid,sizeof(m_curwritefile->guid));
        wh.flags=0;
        wh.audio_data=m_vorbisenc->outqueue.Get();
        wh.audio_data_len=s;
        m_curwritefile->Write(wh.audio_data,wh.audio_data_len);

        EnterCriticalSection(&m_net_cs);
        if (m_vorbisenc_header_needsend)
        {
          if (config_debug_level>1)
          {
            mpb_client_upload_interval_begin dib;
            dib.parse(m_vorbisenc_header_needsend);
            printf("SEND BLOCK HEADER %s\n",guidtostr_tmp(dib.guid));
          }
          m_netcon->Send(m_vorbisenc_header_needsend);
          m_vorbisenc_header_needsend=0;
        }

        if (config_debug_level>1) printf("SEND BLOCK %s%s %d bytes\n",guidtostr_tmp(wh.guid),wh.flags&1?"end":"",wh.audio_data_len);

        m_netcon->Send(wh.build());
        LeaveCriticalSection(&m_net_cs);
      }

      m_vorbisenc->outqueue.Advance(s);
    }
    m_vorbisenc->outqueue.Compact();
  }


  if (config_monitor < 0.00001f) // input monitoring
  {
    memset(buf,0,len*sizeof(float)*nch);// silence
  }
  else
  {
    int x;
    int l=len*nch;
    for (x = 0; x < l; x ++)
      buf[x] *= config_monitor;
  }

  // mix in (super shitty) metronome (fucko!!!!)
  {
    int metrolen=srate / 100;
    double sc=12000.0/(double)srate;
    int x;
    int um=config_metronome>0.0001f;
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
        if (um)
        {
          float val=(float) sin((double)m_metronome_state*sc)*config_metronome;
          if (!m_metronome_tmp) val *= 0.25f;
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
              (needed=resampleLengthNeeded(chan->decode_codec->GetSampleRate(),srate,len)*chan->decode_codec->GetNumChannels()))
        {
          int l=fread(chan->decode_codec->DecodeGetSrcBuffer(4096),1,4096,chan->decode_fp);
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

          if (!chan->muted) mixFloats(sptr+chan->dump_samples,
                    chan->decode_codec->GetSampleRate(),
                    chan->decode_codec->GetNumChannels(),
                    buf,
                    srate,nch,len,
                    chan->volume,chan->pan);

          // advance the queue
          chan->decode_codec->m_samples_used -= needed+chan->dump_samples;
          memcpy(sptr,sptr+needed+chan->dump_samples,chan->decode_codec->m_samples_used*sizeof(float));
          chan->dump_samples=0;
        }
        else
        {
          chan->dump_samples+=needed;

          if (config_debug_level>0)
          {
          static int cnt=0;

          char s[512];
          guidtostr(chan->decode_last_guid,s);

          char buf[512];
          sprintf(buf,"underrun %d on user %s at %d on %s, %d/%d samples\n",cnt++,user->name.Get(),ftell(chan->decode_fp),s,chan->decode_codec->m_samples_used,needed);
          OutputDebugString(buf);
          }
        }

      }
    }
  }
}


void NJClient::on_new_interval(int nch, int srate)
{
  printf("entered new interval (%d samples)\n",m_interval_length);
  if (m_vorbisenc && config_send)
  {
    // finish any encoding
    //if (1) m_vorbisenc->Encode(NULL,0);
    static float empty[2048];
    m_vorbisenc->Encode(empty,1024);

    // send any final message, with the last one with a flag 
    // saying "we're done"
    do
    {
      mpb_client_upload_interval_write wh;
      int l=m_vorbisenc->outqueue.Available();
      if (l>MAX_ENC_BLOCKSIZE) l=MAX_ENC_BLOCKSIZE;

      memcpy(wh.guid,m_curwritefile->guid,sizeof(wh.guid));
      wh.audio_data=m_vorbisenc->outqueue.Get();
      wh.audio_data_len=l;

      m_curwritefile->Write(wh.audio_data,wh.audio_data_len);

      m_vorbisenc->outqueue.Advance(l);
      wh.flags=m_vorbisenc->outqueue.GetSize()>0 ? 0 : 1;

      EnterCriticalSection(&m_net_cs);
      if (m_vorbisenc_header_needsend)
      {
        if (config_debug_level>1)
        {
          mpb_client_upload_interval_begin dib;
          dib.parse(m_vorbisenc_header_needsend);
          printf("SEND BLOCK HEADER %s\n",guidtostr_tmp(dib.guid));
        }
        m_netcon->Send(m_vorbisenc_header_needsend);
        m_vorbisenc_header_needsend=0;
      }


      if (config_debug_level>1) printf("SEND BLOCK %s%s %d bytes\n",guidtostr_tmp(wh.guid),wh.flags&1?"end":"",wh.audio_data_len);
      m_netcon->Send(wh.build());
      LeaveCriticalSection(&m_net_cs);
    }
    while (m_vorbisenc->outqueue.Available()>0);
    m_vorbisenc->outqueue.Compact(); // free any memory left

    //delete m_vorbisenc;
  //  m_vorbisenc=0;
    m_vorbisenc->reinit();
  }
  else if (config_send)
  {
    m_vorbisenc = new ENCODER(srate,nch,ENCODER_BITRATE); // qval 0.25 = ~100kbps, 0.0 is ~70kbps, -0.1 = 45kbps
  }


  WDL_RNG_bytes(&m_curwritefile->guid,sizeof(m_curwritefile->guid));

  if (config_savelocalaudio) m_curwritefile->Open(); //only save other peoples for now


  int u;
  for (u = 0; u < m_remoteusers.GetSize(); u ++)
  {
    RemoteUser *user=m_remoteusers.Get(u);
    int ch;
    for (ch = 0; ch < MAX_USER_CHANNELS; ch ++)
    {
      RemoteUser_Channel *chan=&user->channels[ch];
      if (chan->active)
      {
        if (chan->decode_fp) fclose(chan->decode_fp);
        chan->decode_fp=0;

        chan->dump_samples=0;

        if (memcmp(chan->cur_guid,zero_guid,sizeof(zero_guid)))
        {
          WDL_String s;
          //if (!memcmp(chan->decode_last_guid,zero_guid,sizeof(zero_guid))) // loop the first sample


          memcpy(chan->decode_last_guid,chan->cur_guid,sizeof(chan->cur_guid));

          makeFilenameFromGuid(&s,chan->decode_last_guid);
          chan->decode_fp=fopen(s.Get(),"rb");
          if (chan->decode_fp)
          {
            if (!chan->decode_codec)
              chan->decode_codec= new VorbisDecoder;
            else chan->decode_codec->Reset();
          }
        }
        if (!chan->decode_fp)
        {
          delete chan->decode_codec;
          chan->decode_codec=0;
        }
      }
    }
  }


  {
    mpb_client_upload_interval_begin cuib;
    cuib.chidx=0;
    memcpy(cuib.guid,m_curwritefile->guid,sizeof(cuib.guid));
    cuib.fourcc=ENCODER_FMT_TYPE;
    cuib.estsize=0;
    delete m_vorbisenc_header_needsend;
    m_vorbisenc_header_needsend=cuib.build();
  }
  
  //if (m_vorbisenc->isError()) printf("ERROR\n");
  //else printf("YAY\n");

}



RemoteUser_Channel::RemoteUser_Channel() : active(0), volume(1.0f), pan(0.0f), muted(false), decode_fp(0), decode_codec(0), dump_samples(0)
{
  memset(cur_guid,0,sizeof(cur_guid));
  memset(decode_last_guid,0,sizeof(decode_last_guid));
}

RemoteUser_Channel::~RemoteUser_Channel()
{
  delete decode_codec;
  decode_codec=0;
  if (decode_fp) fclose(decode_fp);
  decode_fp=0;
}


RemoteDownload::RemoteDownload() : fp(0)
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
}

void RemoteDownload::Open()
{    
  Close();
  WDL_String s;
  makeFilenameFromGuid(&s,guid);
  fp=fopen(s.Get(),"wb");
}

void RemoteDownload::Write(void *buf, int len)
{
  if (fp)
  {
    fwrite(buf,1,len,fp);
    fflush(fp);
  }
}
