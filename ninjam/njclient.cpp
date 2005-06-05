#include <math.h>
#include "njclient.h"
#include "../mpb.h"
#include "../WDL/pcmfmtcvt.h"
#include "../WDL/wavwrite.h"


#define MIN_ENC_BLOCKSIZE 2048
#define MAX_ENC_BLOCKSIZE (8192+1024)


#define NJ_PORT 2049

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

void NJClient::makeFilenameFromGuid(WDL_String *s, unsigned char *guid)
{
  char buf[256];
  guidtostr(guid,buf);
  strcat(buf,"." NJ_ENCODER_FMT_STRING);

  s->Set(m_workdir.Get());
  s->Append(buf);
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


  waveWrite=0;
  m_logFile=0;

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
  m_enc=0;
  m_curwritefile=new RemoteDownload;
  m_enc_header_needsend=0;
  InitializeCriticalSection(&m_users_cs);
  InitializeCriticalSection(&m_log_cs);
  InitializeCriticalSection(&m_misc_cs);
  m_netcon=0;


}

void NJClient::writeLog(char *fmt, ...)
{
  if (m_logFile)
  {
    va_list ap;
    va_start(ap,fmt);

    EnterCriticalSection(&m_log_cs);
    if (m_logFile) vfprintf(m_logFile,fmt,ap);
    LeaveCriticalSection(&m_log_cs);

    va_end(ap);

  }


}

void NJClient::SetLogFile(char *name)
{
  EnterCriticalSection(&m_log_cs);
  if (m_logFile) fclose(m_logFile);
  m_logFile=0;
  if (name && *name)
  {
    if (!strstr(name,"\\") && !strstr(name,"/") && !strstr(name,":"))
    {
      WDL_String s(m_workdir);
      s.Append(name);
      m_logFile=fopen(s.Get(),"a+t");
    }
    else
      m_logFile=fopen(name,"a+t");
  }
  LeaveCriticalSection(&m_log_cs);
}


NJClient::~NJClient()
{
  delete m_netcon;
  m_netcon=0;

  delete m_enc;
  m_enc=0;

  delete m_enc_header_needsend;
  m_enc_header_needsend=0;
  delete m_curwritefile;
  m_curwritefile=0;

  delete waveWrite;

  if (m_logFile)
    fclose(m_logFile);

  int x;
  for (x = 0; x < m_remoteusers.GetSize(); x ++) delete m_remoteusers.Get(x);
  m_remoteusers.Empty();
  for (x = 0; x < m_downloads.GetSize(); x ++) delete m_downloads.Get(x);
  m_downloads.Empty();
  DeleteCriticalSection(&m_users_cs);
  DeleteCriticalSection(&m_log_cs);
  DeleteCriticalSection(&m_misc_cs);
}


void NJClient::updateBPMinfo(int bpm, int bpi)
{
  EnterCriticalSection(&m_misc_cs);
  m_bpm=bpm;
  m_bpi=bpi;
  m_beatinfo_updated=1;
  LeaveCriticalSection(&m_misc_cs);
}


void NJClient::GetPosition(int *pos, int *length)  // positions in samples
{ 
  if (length) *length=m_interval_length; 
  if (pos && (*pos=m_interval_length)<0) *pos=0;
}


void NJClient::AudioProc(float *buf, int len, int nch, int srate)
{
  if (!m_audio_enable) 
  {
    input_monitor_samples(buf,len,nch,srate);
    return;
  }


  EnterCriticalSection(&m_misc_cs);
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
    m_interval_length-=m_interval_length%1152;//hack
    m_interval_pos=-1;
    m_active_bpm=m_bpm;
    m_active_bpi=m_bpi;
    m_metronome_interval=(int) ((double)m_interval_length / (double)m_active_bpi);
  }
  LeaveCriticalSection(&m_misc_cs);


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
  m_host.Set(host);
  m_user.Set(user);
  m_pass.Set(pass);

  delete m_netcon;

  JNL_Connection *c=new JNL_Connection(JNL_CONNECTION_AUTODNS,65536,65536);
  c->connect(host,NJ_PORT);
  m_netcon = new Net_Connection;
  m_netcon->attach(c);

  m_status=0;
}

int NJClient::GetStatus()
{
  if (!m_status || m_status == -1) return NJC_STATUS_PRECONNECT;
  if (m_status == 999) return NJC_STATUS_RECONNECTING;
  if (m_status == 1000) return NJC_STATUS_CANTCONNECT;
  if (m_status == 1001) return NJC_STATUS_INVALIDAUTH;

  return NJC_STATUS_OK;
}


int NJClient::Run() // nonzero if sleep ok
{
  if (!m_netcon) return 1;
  if (m_netcon->GetStatus())
  {
    m_audio_enable=0;
    if (m_status == 0 || m_status == 999) m_status=1000;
    else if (m_status != 1001)  // try reconnecting if this is a disconnect
    {
      m_status = 999;
      JNL_Connection *c=new JNL_Connection(JNL_CONNECTION_AUTODNS,65536,65536);
      c->connect(m_host.Get(),NJ_PORT);
      delete m_netcon;
      m_netcon = new Net_Connection;
      m_netcon->attach(c);

    }
    return 1;
  }

  int s=0;
  Net_Message *msg=m_netcon->Run(&s);
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

            m_netcon->Send(repl.build());
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
              // todo: have source channel config
              if (config_send)
              {
                mpb_client_set_channel_info sci;

                sci.build_add_rec("default channel",0,0,0);

                m_netcon->Send(sci.build());
              }
              m_status=2;
            }
            else 
            {
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
            // todo: client callback for change
            // todo: reset audio state?
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
              EnterCriticalSection(&m_users_cs);

              RemoteUser *theuser;
              for (x = 0; x < m_remoteusers.GetSize() && strcmp((theuser=m_remoteusers.Get(x))->name.Get(),un); x ++);
              if (x == m_remoteusers.GetSize())
              {
                theuser=new RemoteUser;
                theuser->name.Set(un);
                m_remoteusers.Add(theuser);
              }

              printf("user %s, channel %d \"%s\": %s v:%d.%ddB p:%d flag=%d\n",un,cid,chn,a?"active":"inactive",(int)v/10,abs((int)v)%10,p,f);

              // todo: per-user autosubscribe option, or callback
              // todo: have volume/pan settings here go into defaults for the channel
              if (cid >= 0 && cid < MAX_USER_CHANNELS && config_recv)
              {
                if (a)
                {
                  theuser->channels[cid].name.Set(chn);
                  theuser->chanpresentmask |= ~(1<<cid);
                  theuser->submask |= 1<<cid;

                  printf("subscribing to user...\n");
                  mpb_client_set_usermask su;
                  su.build_add_rec(un,~0);// subscribe to everything for now
                  m_netcon->Send(su.build());
                }
                else
                {
                  theuser->channels[cid].name.Set("");
                  theuser->chanpresentmask &= ~(1<<cid);
                  theuser->submask &= ~(1<<cid);
                  memset(theuser->channels[cid].cur_guid,0,sizeof(theuser->channels[cid].cur_guid));
                  delete theuser->channels[cid].decode_codec;
                  theuser->channels[cid].decode_codec=0;
                  if (theuser->channels[cid].decode_fp)
                  {
                    fclose(theuser->channels[cid].decode_fp);
                    theuser->channels[cid].decode_fp=0;
                  }
                }
              }
              LeaveCriticalSection(&m_users_cs);
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
                ds->Open(this);
             
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

void NJClient::input_monitor_samples(float *buf, int len, int nch, int srate)
{
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
}

void NJClient::process_samples(float *buf, int len, int nch, int srate)
{

  // encode my audio and send to server, if enabled
  if (config_send && m_enc)
  {
    m_enc->Encode(buf,len);

    int s;
    while ((s=m_enc->outqueue.Available())>(m_enc_header_needsend?MIN_ENC_BLOCKSIZE*4:MIN_ENC_BLOCKSIZE))
    {
      if (s > MAX_ENC_BLOCKSIZE) s=MAX_ENC_BLOCKSIZE;

      {
        mpb_client_upload_interval_write wh;
        memcpy(wh.guid,m_curwritefile->guid,sizeof(m_curwritefile->guid));
        wh.flags=0;
        wh.audio_data=m_enc->outqueue.Get();
        wh.audio_data_len=s;
        m_curwritefile->Write(wh.audio_data,wh.audio_data_len);

        if (m_enc_header_needsend)
        {
          if (config_debug_level>1)
          {
            mpb_client_upload_interval_begin dib;
            dib.parse(m_enc_header_needsend);
            printf("SEND BLOCK HEADER %s\n",guidtostr_tmp(dib.guid));
          }
          m_netcon->Send(m_enc_header_needsend);
          m_enc_header_needsend=0;
        }

        if (config_debug_level>1) printf("SEND BLOCK %s%s %d bytes\n",guidtostr_tmp(wh.guid),wh.flags&1?"end":"",wh.audio_data_len);

        m_netcon->Send(wh.build());
      }

      m_enc->outqueue.Advance(s);
    }
    m_enc->outqueue.Compact();
  }

  input_monitor_samples(buf,len,nch,srate);

  // mix in all active (subscribed) channels
  EnterCriticalSection(&m_users_cs);
  int u;
  for (u = 0; u < m_remoteusers.GetSize(); u ++)
  {
    RemoteUser *user=m_remoteusers.Get(u);
    int ch;
    if (!user || user->muted) continue;
    float uservol=user->volume;
    float userpan=user->pan;

    for (ch = 0; ch < MAX_USER_CHANNELS; ch ++)
    {
      RemoteUser_Channel *chan=&user->channels[ch];
      if (chan->decode_codec && chan->decode_fp)
      {
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
          float lpan=chan->pan+userpan;
          if (lpan<-1.0)lpan=-1.0;
          else if (lpan>1.0)lpan=1.0;

          if (!(user->mutedmask & (1<<ch))) mixFloats(sptr+chan->dump_samples,
                    chan->decode_codec->GetSampleRate(),
                    chan->decode_codec->GetNumChannels(),
                    buf,
                    srate,nch,len,
                    chan->volume*uservol,lpan,&chan->resample_state);

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
          guidtostr(chan->decode_last_guid,s);

          char buf[512];
          sprintf(buf,"underrun %d on user %s at %d on %s, %d/%d samples\n",cnt++,user->name.Get(),ftell(chan->decode_fp),s,chan->decode_codec->m_samples_used,needed);
          OutputDebugString(buf);
          }

          chan->decode_samplesout += chan->decode_codec->m_samples_used/chan->decode_codec->GetNumChannels();
          chan->decode_codec->m_samples_used=0;
          chan->dump_samples+=needed;

        }

      }
    }
  }
  LeaveCriticalSection(&m_users_cs);


  // write out wave if necessary

  if (waveWrite)
  {
    waveWrite->WriteFloats(buf,len*nch);
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

}


void NJClient::on_new_interval(int nch, int srate)
{
  printf("entered new interval (%d samples)\n",m_interval_length);

  writeLog("new interval (%d,%d)\n",m_active_bpm,m_active_bpi);

  if (m_enc && config_send)
  {
    // finish any encoding
    m_enc->Encode(NULL,0);

    // send any final message, with the last one with a flag 
    // saying "we're done"
    do
    {
      mpb_client_upload_interval_write wh;
      int l=m_enc->outqueue.Available();
      if (l>MAX_ENC_BLOCKSIZE) l=MAX_ENC_BLOCKSIZE;

      memcpy(wh.guid,m_curwritefile->guid,sizeof(wh.guid));
      wh.audio_data=m_enc->outqueue.Get();
      wh.audio_data_len=l;

      m_curwritefile->Write(wh.audio_data,wh.audio_data_len);

      m_enc->outqueue.Advance(l);
      wh.flags=m_enc->outqueue.GetSize()>0 ? 0 : 1;

      if (m_enc_header_needsend)
      {
        if (config_debug_level>1)
        {
          mpb_client_upload_interval_begin dib;
          dib.parse(m_enc_header_needsend);
          printf("SEND BLOCK HEADER %s\n",guidtostr_tmp(dib.guid));
        }
        m_netcon->Send(m_enc_header_needsend);
        m_enc_header_needsend=0;
      }


      if (config_debug_level>1) printf("SEND BLOCK %s%s %d bytes\n",guidtostr_tmp(wh.guid),wh.flags&1?"end":"",wh.audio_data_len);
      m_netcon->Send(wh.build());
    }
    while (m_enc->outqueue.Available()>0);
    m_enc->outqueue.Compact(); // free any memory left

    //delete m_enc;
  //  m_enc=0;
    m_enc->reinit();
  }
  else if (config_send)
  {
    m_enc = new NJ_ENCODER(srate,nch,NJ_ENCODER_BITRATE); // qval 0.25 = ~100kbps, 0.0 is ~70kbps, -0.1 = 45kbps
  }


  WDL_RNG_bytes(m_curwritefile->guid,sizeof(m_curwritefile->guid));

  if (config_send)
  {
    char guidstr[64];
    guidtostr(m_curwritefile->guid,guidstr);
    writeLog(":%s\n",guidstr);
  }

  if (config_savelocalaudio) m_curwritefile->Open(this); //only save other peoples for now


  int u;
  EnterCriticalSection(&m_users_cs);
  for (u = 0; u < m_remoteusers.GetSize(); u ++)
  {
    RemoteUser *user=m_remoteusers.Get(u);
    int ch;
    for (ch = 0; ch < MAX_USER_CHANNELS; ch ++)
    {
      RemoteUser_Channel *chan=&user->channels[ch];
      if ((user->submask & user->chanpresentmask) & (1<<ch))
      {
        if (chan->decode_fp) 
        {
          /*
          if (chan->decode_codec)
          {
            int ab;
            ab=fread(chan->decode_codec->DecodeGetSrcBuffer(4096),1,4096,chan->decode_fp);
            if (ab)
            {
              chan->decode_codec->DecodeWrote(ab);
            }
          }
          */
          fclose(chan->decode_fp);
        }
        chan->decode_fp=0;
        //if (chan->decode_codec) 
//          printf("last samplesdec=%d\n",chan->decode_codec->m_samplesdec);
  //      printf("last samplesout=%d\n",chan->decode_samplesout);
        chan->decode_samplesout=0;

        chan->dump_samples=0;

        {
          char guidstr[64];
          guidtostr(chan->cur_guid,guidstr);
          writeLog("%s:%s:%s\n",user->name.Get(),chan->name.Get(),guidstr);
        }

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
              chan->decode_codec= new NJ_DECODER;
            else chan->decode_codec->Reset();
            //chan->resample_state=0.0;
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
  LeaveCriticalSection(&m_users_cs);


  {
    mpb_client_upload_interval_begin cuib;
    cuib.chidx=0;
    memcpy(cuib.guid,m_curwritefile->guid,sizeof(cuib.guid));
    cuib.fourcc=NJ_ENCODER_FMT_TYPE;
    cuib.estsize=0;
    delete m_enc_header_needsend;
    m_enc_header_needsend=cuib.build();
  }
  
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

char *NJClient::GetUserChannelState(int useridx, int channelidx, bool *sub, float *vol, float *pan, bool *mute)
{
  if (useridx<0 || useridx>=m_remoteusers.GetSize()||channelidx<0||channelidx>=MAX_USER_CHANNELS) return NULL;
  RemoteUser_Channel *p=m_remoteusers.Get(useridx)->channels + channelidx;
  RemoteUser *user=m_remoteusers.Get(useridx);
  if (!user->chanpresentmask & (1<<channelidx)) return 0;

  if (sub) *sub=!!(user->submask & (1<<channelidx));
  if (vol) *vol=p->volume;
  if (pan) *pan=p->pan;
  if (mute) *mute=!!(user->mutedmask & (1<<channelidx));
  
  return p->name.Get();
}


void NJClient::SetUserChannelState(int useridx, int channelidx, 
                                   bool setsub, bool sub, bool setvol, float vol, bool setpan, float pan, bool setmute, bool mute)
{
  if (useridx<0 || useridx>=m_remoteusers.GetSize()||channelidx<0||channelidx>=MAX_USER_CHANNELS) return;
  RemoteUser *user=m_remoteusers.Get(useridx);
  RemoteUser_Channel *p=user->channels + channelidx;
  if (!user->chanpresentmask & (1<<channelidx)) return;

  if (setsub && !!(user->submask&(1<<channelidx)) != sub) 
  {
    // toggle subscription
    if (!sub)
    {     
      mpb_client_set_usermask su;
      su.build_add_rec(user->name.Get(),(user->submask&=~(1<<channelidx)));
      m_netcon->Send(su.build());

      EnterCriticalSection(&m_users_cs);
      memset(p->cur_guid,0,sizeof(p->cur_guid));
      delete p->decode_codec;
      p->decode_codec=0;
      if (p->decode_fp)
      {
        fclose(p->decode_fp);
        p->decode_fp=0;
      }
      LeaveCriticalSection(&m_users_cs);
     
    }
    else
    {
      mpb_client_set_usermask su;
      su.build_add_rec(user->name.Get(),(user->submask|=~(1<<channelidx)));
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
}

void NJClient::SetWorkDir(char *path)
{
  m_workdir.Set(path);
  if (path[0] && path[strlen(path)-1] != '/' && path[strlen(path)-1] != '\\') m_workdir.Append("\\");
}


RemoteUser_Channel::RemoteUser_Channel() : volume(1.0f), pan(0.0f), decode_fp(0), decode_codec(0), dump_samples(0),
                                           decode_samplesout(0), resample_state(0.0)
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

void RemoteDownload::Open(NJClient *parent)
{    
  Close();
  WDL_String s;
  parent->makeFilenameFromGuid(&s,guid);
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
