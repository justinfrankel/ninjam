#include <math.h>
#include <stdio.h>
#include <stdarg.h>
#include "njclient.h"
#include "mpb.h"
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

  m_status=-1;

  m_in_auth=0;
  config_autosubscribe=1;
  config_savelocalaudio=0;
  config_metronome=0.5f;
  config_metronome_pan=0.0f;
  config_metronome_mute=false;
  config_debug_level=0;
  config_mastervolume=1.0f;
  config_masterpan=0.0f;
  config_mastermute=false;


  LicenseAgreement_User32=0;
  LicenseAgreementCallback=0;

  waveWrite=0;
  m_logFile=0;

  m_issoloactive=0;
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
  m_netcon=0;


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

  if (m_logFile)
  {
    writeLog("end\n");
    fclose(m_logFile);
  }

  int x;
  for (x = 0; x < m_remoteusers.GetSize(); x ++) delete m_remoteusers.Get(x);
  m_remoteusers.Empty();
  for (x = 0; x < m_downloads.GetSize(); x ++) delete m_downloads.Get(x);
  m_downloads.Empty();
  for (x = 0; x < m_locchannels.GetSize(); x ++) delete m_locchannels.Get(x);
  m_locchannels.Empty();
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


void NJClient::AudioProc(float *buf, int len, int nch, int srate)
{
  m_srate=srate;
  if (!m_audio_enable) 
  {
    input_monitor_samples(buf,len,nch,srate);
    return;
  }


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
      on_new_interval(nch,srate);

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


void NJClient::Disconnect()
{
  m_host.Set("");
  m_user.Set("");
  m_pass.Set("");
  delete m_netcon;
  m_netcon=0;
  m_status=-1;

  int x; // all channels go away
  for (x=0;x<m_remoteusers.GetSize(); x++)
  {
    //m_remoteusers.Get(x)->chanpresentmask=0;
    m_remoteusers.Get(x)->submask=0;
  }
  for (x=0;x<m_locchannels.GetSize(); x++)
  {
    //m_locchannels.Get(x)->
  }
}

void NJClient::Connect(char *host, char *user, char *pass)
{
  m_host.Set(host);
  m_user.Set(user);
  m_pass.Set(pass);

  delete m_netcon;

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
  if (!m_netcon) return 1;
  if (m_netcon->GetStatus())
  {
    m_audio_enable=0;
    if (m_in_auth) m_status=1001;
    if (m_status > 0 && m_status < 1000) m_status=1002;
    if (m_status == 0) m_status=1000;
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


            if (cha.license_agreement)
            {
              if (LicenseAgreementCallback && LicenseAgreementCallback(LicenseAgreement_User32,cha.license_agreement))
              {
                repl.client_caps|=1;
              }
            }

            WDL_SHA1 tmp;
            tmp.add(cha.challenge,sizeof(cha.challenge));
            tmp.add(m_user.Get(),strlen(m_user.Get()));
            tmp.add(":",1);
            tmp.add(m_pass.Get(),strlen(m_pass.Get()));
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
            //printf("Got info update from server, bpm=%d, bpi=%d\n",m_bpm,m_bpi);
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
              m_users_cs.Enter();

              // todo: per-user autosubscribe option, or callback
              // todo: have volume/pan settings here go into defaults for the channel
              if (cid >= 0 && cid < MAX_USER_CHANNELS)
              {
                RemoteUser *theuser;
                for (x = 0; x < m_remoteusers.GetSize() && strcmp((theuser=m_remoteusers.Get(x))->name.Get(),un); x ++);

               // printf("user %s, channel %d \"%s\": %s v:%d.%ddB p:%d flag=%d\n",un,cid,chn,a?"active":"inactive",(int)v/10,abs((int)v)%10,p,f);


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


                  if (config_autosubscribe) // todo: autosubscribe option
                  {
                    theuser->submask |= 1<<cid;
                    //printf("autosubscribing to user %s channel %d...\n",un,cid);
                    mpb_client_set_usermask su;
                    su.build_add_rec(un,theuser->submask);// subscribe to everything for now
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
                    memset(theuser->channels[cid].cur_guid,0,sizeof(theuser->channels[cid].cur_guid));
                    delete theuser->channels[cid].ds.decode_codec;
                    theuser->channels[cid].ds.decode_codec=0;
                    if (theuser->channels[cid].ds.decode_fp)
                    {
                      fclose(theuser->channels[cid].ds.decode_fp);
                      theuser->channels[cid].ds.decode_fp=0;
                    }


                    if (!theuser->chanpresentmask) // user no longer exists, it seems
                    {
                      delete theuser;
                      m_remoteusers.Delete(x);
                    }
                  }
                }
              }
              m_users_cs.Leave();
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
        //printf("Got unknown message %02X\n",msg->get_type());
      break;
    }

    msg->releaseRef();
  }


  int u;
  for (u = 0; u < m_locchannels.GetSize(); u ++)
  {
    Local_Channel *lc=m_locchannels.Get(u);
    WDL_HeapBuf *p=0;
    if (!lc->GetBlock(&p))
    {
      s=0;
      if ((int)p == -1)
      {
        mpb_client_upload_interval_begin cuib;
        cuib.chidx=lc->channel_idx;
        memset(cuib.guid,0,sizeof(cuib.guid));
        memset(lc->m_curwritefile.guid,0,sizeof(lc->m_curwritefile.guid));
        cuib.fourcc=0;
        cuib.estsize=0;
        m_netcon->Send(cuib.build());
      }
      else if (p)
      {
        // encode data
        if (!lc->m_enc)
        {
          lc->m_enc = new NJ_ENCODER(m_srate,1,lc->m_enc_bitrate_used = lc->bitrate); // qval 0.25 = ~100kbps, 0.0 is ~70kbps, -0.1 = 45kbps
        }

        if (lc->m_need_header)
        {
          lc->m_need_header=false;
          if (lc->bcast_active)
          {
            WDL_RNG_bytes(lc->m_curwritefile.guid,sizeof(lc->m_curwritefile.guid));
            char guidstr[64];
            guidtostr(lc->m_curwritefile.guid,guidstr);
            writeLog("local %s %d\n",guidstr,lc->channel_idx);
            if (config_savelocalaudio) 
            {
              lc->m_curwritefile.Open(this); //only save other peoples for now
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
          lc->m_enc->Encode((float*)p->Get(),p->GetSize()/sizeof(float),1);

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
        lc->DisposeBlock(p);
      }
      else
      {
        if (lc->m_enc && lc->bcast_active)
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

  return s;

}

void NJClient::input_monitor_samples(float *buf, int len, int nch, int srate)
{
  WDL_HeapBuf srcbuf;
  int sbytes=len*sizeof(float)*nch;
  srcbuf.Resize(sbytes);
  memcpy(srcbuf.Get(),buf,sbytes);
  memset(buf,0,sbytes);
  int ch;
  for (ch = 0; ch < m_locchannels.GetSize(); ch ++)
  {
    Local_Channel *lc=m_locchannels.Get(ch);

    if ((!m_issoloactive && !lc->muted) || lc->solo)
    {
      float *src=(float *)srcbuf.Get();
      if (nch > 1 && lc->src_channel) src++; // right channel

      float vol1=lc->volume;
      float vol2=vol1;
      if (nch == 2)
      {
        if (lc->pan > 0.0f) vol1 *= 1.0f-lc->pan;
        else if (lc->pan < 0.0f) vol2 *= 1.0f+lc->pan;
      }

      
      if (nch == 2)
      {
        float maxf=(float) (lc->decode_peak_vol*0.99);
        float *obuf=buf;
        int x=len;
        while (x--) 
        {
          float f=src[0] * vol1;

          if (f > maxf) maxf=f;
          else if (f < -maxf) maxf=-f;

          obuf[0] += f;

          f=src[0]*vol2;

          if (f > maxf) maxf=f;
          else if (f < -maxf) maxf=-f;

          obuf[1] += f;
          obuf+=2;
          src+=2;
        }
        lc->decode_peak_vol=maxf;
      }
      else
      {
        float maxf=(float) (lc->decode_peak_vol*0.99);
        float *obuf=buf;
        int x=len;
        while (x--) 
        {
          float f=*src++ * vol1;
          if (f > maxf) maxf=f;
          else if (f < -maxf) maxf=-f;
          *obuf++ += f;
        }
        lc->decode_peak_vol=maxf;
      }
    }
    else lc->decode_peak_vol=0.0;
  }
}

void NJClient::process_samples(float *buf, int len, int nch, int srate)
{
  // encode my audio and send to server, if enabled
  int u;
  m_locchan_cs.Enter();
  for (u = 0; u < m_locchannels.GetSize(); u ++)
  {
    Local_Channel *lc=m_locchannels.Get(u);
    if (lc->bcast_active) 
    {
      // hackish, for stereo

      lc->AddBlock(buf+(lc->src_channel?1:0),len,nch);
    }
  }
  m_locchan_cs.Leave();


  input_monitor_samples(buf,len,nch,srate);

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

      mixInChannel(muteflag,
        user->volume*user->channels[ch].volume,lpan,
        &user->channels[ch].ds,buf,len,srate,nch);
    }
  }
  m_users_cs.Leave();


  // write out wave if necessary

  if (waveWrite)
  {
    waveWrite->WriteFloats(buf,len*nch);
  }


  // apply master volume, then
  {
    int x=len;
    float *ptr=buf;
    float maxf=output_peaklevel*0.99f;

    if (nch == 2)
    {
      float vol1=config_mastermute?0.0f:config_mastervolume;
      float vol2=vol1;
      if (config_masterpan > 0.0f) vol1 *= 1.0f-config_masterpan;
      else if (config_masterpan< 0.0f) vol2 *= 1.0f+config_masterpan;

      while (x--)
      {
        float f = *ptr++ *= vol1;
        if (f > maxf) maxf=f;
        else if (f < -maxf) maxf=-f;

        f = *ptr++ *= vol2;
        if (f > maxf) maxf=f;
        else if (f < -maxf) maxf=-f;
      }
    }
    else
    {
      float vol1=config_mastermute?0.0f:config_mastervolume;
      while (x--)
      {
        float f = *ptr++ *= vol1;
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
    if (nch > 1)
    {
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

          if (nch == 1) buf[x]+=(float)(val*vol1);
          else
          {
            buf[x+x]+=(float)(val*vol1);
            buf[x+x+1]+=(float)(val*vol2);
          }
        }
        if (++m_metronome_state >= metrolen) m_metronome_state=0;

      }
    }   
  }

}

void NJClient::mixInChannel(bool muted, float vol, float pan, DecodeState *chan, float *buf, int len, int srate, int nch)
{
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

      // process VU meter, yay for powerful CPUs
      if (vol > 0.0000001) 
      {
        float *p=sptr;
        int l=len*chan->decode_codec->GetNumChannels();
        float maxf=(float) (chan->decode_peak_vol*0.99/vol);
        while (l--)
        {
          float f=*p++;
          if (f > maxf) maxf=f;
          else if (f < -maxf) maxf=-f;
        }
        chan->decode_peak_vol=maxf*vol;
      }
      else 
        chan->decode_peak_vol*=0.99;

      if (!muted) mixFloats(sptr+chan->dump_samples,
                chan->decode_codec->GetSampleRate(),
                chan->decode_codec->GetNumChannels(),
                buf,
                srate,nch,len,
                vol,pan,&chan->resample_state);

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

}

void NJClient::on_new_interval(int nch, int srate)
{
  m_loopcnt++;
  writeLog("interval %d %.2f %d\n",m_loopcnt,GetActualBPM(),m_active_bpi);

  m_metronome_pos=0.0;

  int u;
  m_locchan_cs.Enter();
  for (u = 0; u < m_locchannels.GetSize(); u ++)
  {
    Local_Channel *lc=m_locchannels.Get(u);


    if (lc->bcast_active) 
    {
      lc->AddBlock(NULL,0,1);
    }

    int wasact=lc->bcast_active;

    lc->bcast_active = lc->broadcasting;

    if (wasact && !lc->bcast_active)
    {
      lc->AddBlock(NULL,-1,1);
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
      if ((user->submask & user->chanpresentmask) & (1<<ch))
      {
        if (chan->ds.decode_fp) 
        {
          /*
          if (chan->ds.decode_codec)
          {
            int ab;
            ab=fread(chan->ds.decode_codec->DecodeGetSrcBuffer(4096),1,4096,chan->ds.decode_fp);
            if (ab)
            {
              chan->ds.decode_codec->DecodeWrote(ab);
            }
          }
          */
          fclose(chan->ds.decode_fp);
        }
        chan->ds.decode_fp=0;
        //if (chan->decode_codec) 
//          printf("last samplesdec=%d\n",chan->decode_codec->m_samplesdec);
  //      printf("last samplesout=%d\n",chan->decode_samplesout);
        chan->ds.decode_samplesout=0;

        chan->ds.dump_samples=0;

        {
          char guidstr[64];
          guidtostr(chan->cur_guid,guidstr);
          writeLog("user %s \"%s\" %d \"%s\"\n",guidstr,user->name.Get(),ch,chan->name.Get());
        }

        if (memcmp(chan->cur_guid,zero_guid,sizeof(zero_guid)))
        {
          WDL_String s;
          //if (!memcmp(chan->decode_last_guid,zero_guid,sizeof(zero_guid))) // loop the first sample


          memcpy(chan->ds.decode_last_guid,chan->cur_guid,sizeof(chan->cur_guid));

          makeFilenameFromGuid(&s,chan->ds.decode_last_guid);
          chan->ds.decode_fp=fopen(s.Get(),"rb");
          if (chan->ds.decode_fp)
          {
            if (!chan->ds.decode_codec)
              chan->ds.decode_codec= new NJ_DECODER;
            else chan->ds.decode_codec->Reset();
            //chan->resample_state=0.0;
          }
        }
        if (!chan->ds.decode_fp)
        {
          delete chan->ds.decode_codec;
          chan->ds.decode_codec=0;
        }
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

      m_users_cs.Enter();
      memset(p->cur_guid,0,sizeof(p->cur_guid));
      delete p->ds.decode_codec;
      p->ds.decode_codec=0;
      if (p->ds.decode_fp)
      {
        fclose(p->ds.decode_fp);
        p->ds.decode_fp=0;
      }
      m_users_cs.Leave();
     
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

  return (float)p->ds.decode_peak_vol;
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
  m_workdir.Set(path);
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


RemoteUser_Channel::RemoteUser_Channel() : volume(1.0f), pan(0.0f)
{
  memset(cur_guid,0,sizeof(cur_guid));  
}

RemoteUser_Channel::~RemoteUser_Channel()
{
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


Local_Channel::Local_Channel() : channel_idx(0), src_channel(0), volume(1.0f), pan(0.0f), 
                muted(false), solo(false), broadcasting(false), m_enc(NULL), m_enc_header_needsend(NULL),
                bcast_active(false), m_enc_bitrate_used(0), bitrate(NJ_ENCODER_BITRATE), m_need_header(true), m_wavewritefile(NULL),
                decode_peak_vol(0.0)
{
}


int Local_Channel::GetBlock(WDL_HeapBuf **b) // return 0 if got one, 1 if none avail
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

void Local_Channel::DisposeBlock(WDL_HeapBuf *b)
{
  m_cs.Enter();
  if (b && (int)b != -1) m_emptybufs.Add(b);
  m_cs.Leave();
}


void Local_Channel::AddBlock(float *samples, int len, int spacing)
{
  WDL_HeapBuf *mybuf=0;
  if (len>0)
  {
    m_cs.Enter();
    int tmp;
    if ((tmp=m_emptybufs.GetSize()))
    {
      mybuf=m_emptybufs.Get(tmp-1);
      if (mybuf) m_emptybufs.Delete(tmp-1);
    }
    m_cs.Leave();
    if (!mybuf) mybuf=new WDL_HeapBuf;

    mybuf->Resize(len*sizeof(float));
    float *o=(float *)mybuf->Get();
    while (len--)
    {
      *o++=*samples;
      samples+=spacing;
    }
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

  delete m_wavewritefile;
  m_wavewritefile=0;

}
