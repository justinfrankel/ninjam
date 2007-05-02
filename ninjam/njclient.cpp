/*
    NINJAM - njclient.cpp
    Copyright (C) 2005 Cockos Incorporated

    NINJAM is free software; you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation; either version 2 of the License, or
    (at your option) any later version.

    NINJAM is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU General Public License for more details.

    You should have received a copy of the GNU General Public License
    along with NINJAM; if not, write to the Free Software
    Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
*/

/*

  For a full description of everything here, see njclient.h
*/


#include <math.h>
#include <stdio.h>
#include <stdarg.h>
#include "njclient.h"
#include "mpb.h"
#include "../WDL/pcmfmtcvt.h"
#include "../WDL/wavwrite.h"


#define NJ_ENCODER_FMT_TYPE MAKE_NJ_FOURCC('O','G','G','v')

#ifdef REANINJAM
#define WDL_VORBIS_INTERFACE_ONLY
#endif

#define VorbisEncoderInterface I_NJEncoder 
#define VorbisDecoderInterface I_NJDecoder 
#include "../WDL/vorbisencdec.h"
#undef VorbisEncoderInterface
#undef VorbisDecoderInterface

#ifdef REANINJAM
  extern void *(*CreateVorbisEncoder)(int srate, int nch, int serno, float qv, int cbr, int minbr, int maxbr);
  extern void *(*CreateVorbisDecoder)();
  static void *__CreateVorbisEncoder(int srate, int nch, int bitrate, int serno)
  {
    float qv=0.0;
    if (nch == 2) bitrate=  (bitrate*5)/8;
    // at least for mono 44khz
    //-0.1 = ~40kbps
    //0.0 == ~64kbps
    //0.1 == 75
    //0.3 == 95
    //0.5 == 110
    //0.75== 140
    //1.0 == 240
 
    if (bitrate < 40) qv=-0.1f;
    else if (bitrate < 64) qv=-0.10f + (bitrate-40)*(0.10f/24.0f);
    else if (bitrate < 75) qv=(bitrate-64)*(0.1f/9.0f);
    else if (bitrate < 95) qv=0.1f+(bitrate-75)*(0.2f/20.0f);
    else if (bitrate < 110) qv=0.3f+(bitrate-95)*(0.2f/15.0f);
    else if (bitrate < 140) qv=0.5f+(bitrate-110)*(0.25f/30.0f);
    else qv=0.75f+(bitrate-140)*(0.25f/100.0f);

    if (qv<-0.10f)qv=-0.10f;
    if (qv>1.0f)qv=1.0f;
    return CreateVorbisEncoder(srate,nch,serno,qv,-1,-1,-1);

  }
  #define CreateNJEncoder(srate,ch,br,id) ((I_NJEncoder *)__CreateVorbisEncoder(srate,ch,br,id))
  #define CreateNJDecoder() ((I_NJDecoder *)CreateVorbisDecoder())
#else
  #define CreateNJEncoder(srate,ch,br,id) ((I_NJEncoder *)new VorbisEncoder(srate,ch,br,id))
  #define CreateNJDecoder() ((I_NJDecoder *)new VorbisDecoder)
#endif


#define SESSION_CHUNK_SIZE 2.0

#define MAKE_NJ_FOURCC(A,B,C,D) ((A) | ((B)<<8) | ((C)<<16) | ((D)<<24))

class DecodeMediaBuffer
{
public:
  DecodeMediaBuffer()
  {
    refcnt=1;
    rdpos=0;
  }
  ~DecodeMediaBuffer()
  {
  }
  void AddRef() { refcnt++; }
  void Release() { if (!--refcnt) delete this; }

  void Write(const void *buf, int len)
  {
    mutex.Enter();
    m_buf.Add(buf,len);
    mutex.Leave();
  }

  int Avail() { return m_buf.Available() - rdpos; }
  int Size() { return m_buf.Available(); }
  int Read(void *buf, int len)
  {
    mutex.Enter();
    int l=Avail();
    if (l >len)l=len;
    if (l>0) 
    {
      memcpy(buf,(char *)m_buf.Get()+rdpos,l);
      rdpos+=l;
    }
    mutex.Leave();
    return l;
  }

private:
  WDL_Mutex mutex;
  int rdpos;
  int refcnt;
  WDL_Queue m_buf;
};

class DecodeState
{
  public:
    DecodeState() : decode_fp(0), decode_buf(0), decode_codec(0), 
                                           decode_samplesout(0), resample_state(0.0)
    { 
      memset(guid,0,sizeof(guid));
    }
    ~DecodeState()
    {
      delete decode_codec;
      decode_codec=0;
      if (decode_fp ) fclose(decode_fp);
      decode_fp=0;
      if (decode_buf) decode_buf->Release();
      decode_buf=0;

    }

    unsigned char guid[16];

    FILE *decode_fp;
    DecodeMediaBuffer *decode_buf;
    I_NJDecoder *decode_codec;
    int decode_samplesout;
    double resample_state;

};

class ChannelSessionInfo
{
public:
  ChannelSessionInfo(const unsigned char *_guid, double st, double len)
  {
    memcpy(guid,_guid,16);
    start_time=st;
    length=len;
    offset=0.0;
  }
  ~ChannelSessionInfo()
  {
  }

  double start_time;
  double length;
  double offset;
  unsigned char guid[16];

};

class RemoteUser_Channel
{
  public:
    RemoteUser_Channel();
    ~RemoteUser_Channel();

    float volume, pan;
    int out_chan_index;

    int flags;

    WDL_String name;

    // decode/mixer state, used by mixer
    int dump_samples;
    DecodeState *ds;
    DecodeState *next_ds[2]; // prepared by main thread, for audio thread

    double decode_peak_vol[2];

    double curds_lenleft;

    void AddSessionInfo(const unsigned char *guid, double st, double len);
    bool GetSessionInfo(double time, unsigned char *guid, double *offs, double *len, double mv);

  private:
    WDL_Mutex sessionlist_mutex;
    WDL_PtrList<ChannelSessionInfo> sessioninfo;

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
  void Open(NJClient *parent, unsigned int fourcc, bool forceToDisk);
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
  FILE *m_fp;
  DecodeMediaBuffer *m_decbuf;
};



class BufferQueue
{
  public:
    BufferQueue() { }
    ~BufferQueue() 
    { 
      Clear();
    }

    void AddBlock(int attr, double blockstart, float *samples, int len, float *samples2=NULL);
    int GetBlock(WDL_HeapBuf **b, int *attr=NULL, double *startpos=NULL); // return 0 if got one, 1 if none avail
    void DisposeBlock(WDL_HeapBuf *b);

    typedef struct
    {
      int attr;
      double startpos;
    } AttrStruct;

    void Clear()
    {
      m_emptybufs.Empty(true);
      m_emptybufs_attr.Empty(true);
      m_samplequeue.Empty(true);
    }

//  private:
    WDL_PtrList<WDL_HeapBuf> m_samplequeue; // a list of pointers, with NULL to define spaces
    WDL_PtrList<WDL_HeapBuf> m_emptybufs;
    WDL_PtrList<WDL_HeapBuf> m_emptybufs_attr;
    WDL_Mutex m_cs;
};


class Local_Channel
{
public:
  Local_Channel();
  ~Local_Channel();

  int channel_idx;

  int src_channel; // 0 or 1 etc.. &1024 = stereo!
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

  double decode_peak_vol[2];
  bool m_need_header;
  int out_chan_index;
  int flags;

#ifndef NJCLIENT_NO_XMIT_SUPPORT
  I_NJEncoder  *m_enc;
  int m_enc_bitrate_used;
  int m_enc_nch_used;
  Net_Message *m_enc_header_needsend;
#endif
  
  WDL_String name;
  RemoteDownload m_curwritefile;
  double m_curwritefile_starttime;
  double m_curwritefile_writelen;
  double m_curwritefile_curbuflen;
  WaveWriter *m_wavewritefile;

  //DecodeState too, eventually
};







#define MIN_ENC_BLOCKSIZE 2048
#define MAX_ENC_BLOCKSIZE (8192+1024)
#define DEFAULT_CONFIG_PREBUFFER  8192
#define LIVE_PREBUFFER 1024
#define LIVE_ENC_BLOCKSIZE1 2048
#define LIVE_ENC_BLOCKSIZE2 64


#define NJ_PORT 2049

static unsigned char zero_guid[16];


static void guidtostr(const unsigned char *guid, char *str)
{
  int x;
  for (x = 0; x < 16; x ++) wsprintf(str+x*2,"%02x",guid[x]);
}
static bool strtoguid(const char *str, unsigned char *guid)
{
  int n=16;
  while(n--)
  {
    unsigned char v=0;
    if (str[0]>='0' && str[0]<='9') v+=str[0]-'0';
    else if (str[0]>='a' && str[0]<='f') v+=10 + str[0]-'a';
    else if (str[0]>='A' && str[0]<='F') v+=10 + str[0]-'A';
    else return false;
    v<<=4;

    str++;
    if (str[0]>='0' && str[0]<='9') v+=str[0]-'0';
    else if (str[0]>='a' && str[0]<='f') v+=10 + str[0]-'a';
    else if (str[0]>='A' && str[0]<='F') v+=10 + str[0]-'A';
    else return false;

    str++;
    *guid++=v;
  }
  return true;
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
  //if (config_savelocalaudio>0)
  {
  #ifdef _WIN32
    char tmp[3]={buf[0],'\\',0};
  #else
    char tmp[3]={buf[0],'/',0};
  #endif
    s->Append(tmp);
  }
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
  config_play_prebuffer=DEFAULT_CONFIG_PREBUFFER;


  LicenseAgreement_User32=0;
  LicenseAgreementCallback=0;
  ChatMessage_Callback=0;
  ChatMessage_User32=0;
  ChannelMixer=0;
  ChannelMixer_User32=0;

  waveWrite=0;
#ifndef NJCLIENT_NO_XMIT_SUPPORT
  m_oggWrite=0;
  m_oggComp=0;
#endif
  m_logFile=0;

  m_issoloactive=0;
  m_netcon=0;

  _reinit();

  m_session_pos_ms=m_session_pos_samples=0;
}

void NJClient::_reinit()
{
  m_max_localch=MAX_LOCAL_CHANNELS;
  output_peaklevel[0]=output_peaklevel[1]=0.0;

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
  {
    m_locchannels.Get(x)->decode_peak_vol[0]=0.0f;
    m_locchannels.Get(x)->decode_peak_vol[1]=0.0f;
  }

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

void NJClient::AudioProc(float **inbuf, int innch, float **outbuf, int outnch, int len, int srate, bool justmonitor, bool isPlaying, bool isSeek, double cursessionpos)
{
  m_srate=srate;
  // zero output
  int x;
  for (x = 0; x < outnch; x ++) memset(outbuf[x],0,sizeof(float)*len);

  if (!m_audio_enable||justmonitor)
  {
    process_samples(inbuf,innch,outbuf,outnch,len,srate,0,1,isPlaying,isSeek,cursessionpos);
    return;
  }

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

    process_samples(inbuf,innch,outbuf,outnch,x,srate,offs,false,isPlaying,isSeek,cursessionpos);

    m_interval_pos+=x;
    offs += x;
    len -= x;    

    if (len>0 && cursessionpos > -1.0)
    {
      isSeek=false;
      cursessionpos += x/(double)srate;
    }
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


  for (x = 0; x < m_locchannels.GetSize(); x ++) 
  {
    Local_Channel *c=m_locchannels.Get(x);
    delete c->m_wavewritefile;
    c->m_wavewritefile=0;
    c->m_curwritefile.Close();

#ifndef NJCLIENT_NO_XMIT_SUPPORT
    delete c->m_enc;
    c->m_enc=0;
    delete c->m_enc_header_needsend;
    c->m_enc_header_needsend=0;
#endif

    c->m_bq.Clear();
  }
  m_downloads.Empty();

  m_wavebq->Clear();

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
#ifndef NJCLIENT_NO_XMIT_SUPPORT
      if (m_oggWrite&&m_oggComp)
      {
        m_oggComp->Encode(f,hl,1,hl);
        if (m_oggComp->Available())
        {
          fwrite((char *)m_oggComp->Get(),1,m_oggComp->Available(),m_oggWrite);
          m_oggComp->Advance(m_oggComp->Available());
          m_oggComp->Compact();
        }
      }
#endif
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
                  sci.build_add_rec(ch->name.Get(),0,0,ch->flags);
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
                {
                    m_errstr.Set(ar.errmsg);
                }
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

                    if ((theuser->channels[cid].flags^f)&2) // if flags changed instamode, flush out the samples
                    {
                      delete theuser->channels[cid].ds;
                      delete theuser->channels[cid].next_ds[0];
                      delete theuser->channels[cid].next_ds[1];
                      theuser->channels[cid].ds=0;
                      theuser->channels[cid].next_ds[0]=0;
                      theuser->channels[cid].next_ds[1]=0;
                    }
                    theuser->channels[cid].flags = f;
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

                      int chksolo=theuser->solomask == (1<<cid);
                      theuser->solomask &= ~(1<<cid);

                      delete theuser->channels[cid].ds;
                      delete theuser->channels[cid].next_ds[0];
                      delete theuser->channels[cid].next_ds[1];
                      theuser->channels[cid].ds=0;
                      theuser->channels[cid].next_ds[0]=0;
                      theuser->channels[cid].next_ds[1]=0;

                      if (!theuser->chanpresentmask) // user no longer exists, it seems
                      {
                        chksolo=1;
                        delete theuser;
                        m_remoteusers.Delete(x);
                      }

                      if (chksolo)
                      {
                        int i;
                        for (i = 0; i < m_remoteusers.GetSize() && !m_remoteusers.Get(i)->solomask; i ++);

                        if (i < m_remoteusers.GetSize()) m_issoloactive|=1;
                        else m_issoloactive&=~1;
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
                  if (!(theuser->channels[dib.chidx].flags&4))
                  {
                    m_users_cs.Enter();
                    int useidx=!!theuser->channels[dib.chidx].next_ds[0];
                    DecodeState *tmp=theuser->channels[dib.chidx].next_ds[useidx];
                    theuser->channels[dib.chidx].next_ds[useidx]=0;
                    m_users_cs.Leave();
                    delete tmp;
                  }
                }
                else if (dib.fourcc) // download coming
                {                
                  if (config_debug_level>1) printf("RECV BLOCK %s\n",guidtostr_tmp(dib.guid));
                  RemoteDownload *ds=new RemoteDownload;
                  memcpy(ds->guid,dib.guid,sizeof(ds->guid));
                  ds->Open(this,dib.fourcc,!!(theuser->channels[dib.chidx].flags&4));

                  ds->playtime=(theuser->channels[dib.chidx].flags&2)?LIVE_PREBUFFER:config_play_prebuffer;
                  ds->chidx=dib.chidx;
                  ds->username.Set(dib.username);

                  m_downloads.Add(ds);
                }
                else if (!(theuser->channels[dib.chidx].flags&4))
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
              if (foo.parms[0] && !strcmp(foo.parms[0],"SESSION"))
              {
                if (foo.parms[1] && foo.parms[2] && foo.parms[3] && foo.parms[4])
                {
                  int x;
                  RemoteUser *theuser;
                  for (x = 0; x < m_remoteusers.GetSize() && strcmp((theuser=m_remoteusers.Get(x))->name.Get(),foo.parms[1]); x ++);
                  int chanidx=atoi(foo.parms[3]);
                  if (x < m_remoteusers.GetSize() && chanidx >= 0 && chanidx < MAX_USER_CHANNELS && (theuser->channels[chanidx].flags&4))
                  {
                    unsigned char guid[16];
                    if (strtoguid(foo.parms[2],guid))
                    {
                      const char *p=foo.parms[4];
                      double st=atof(p);
                      while (*p != ' ' && *p) p++;
                      while (*p == ' ') p++;
                      if (*p) 
                      {
                        double len=atof(p);

                        // add to this channel's session list
                        theuser->channels[chanidx].AddSessionInfo(guid,st,len);

                        char guidstr[64];
                        guidtostr(guid,guidstr);
                        writeLog("sessionlog %s \"%s\" %d \"%s\" %.10f %.10f\n",guidstr,theuser->name.Get(),chanidx,theuser->channels[chanidx].name.Get(),st,len);
                      }
                    }
                  }
                }               
              }
              else ChatMessage_Callback(ChatMessage_User32,this,foo.parms,sizeof(foo.parms)/sizeof(foo.parms[0]));
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

#ifndef NJCLIENT_NO_XMIT_SUPPORT
  int u;
  for (u = 0; u < m_locchannels.GetSize(); u ++)
  {
    Local_Channel *lc=m_locchannels.Get(u);
    WDL_HeapBuf *p=0;
    int block_nch=1;

#if 0
    {
      char buf[512];
      int sz=0;
      int x;
      lc->m_bq.m_cs.Enter();
      for (x = 0; x < lc->m_bq.m_samplequeue.GetSize(); x += 2)
      {
        WDL_HeapBuf *p=lc->m_bq.m_samplequeue.Get(x);
        if (p && p != (WDL_HeapBuf*)-1)
          sz+=p->GetSize();
      }
      lc->m_bq.m_cs.Leave();
      sprintf(buf,"bq size=%d\n",sz); 
      if (sz) OutputDebugString(buf);
    }
#endif
    
    double blockstarttime=0.0;
    while (!lc->m_bq.GetBlock(&p,&block_nch,&blockstarttime))
    {
      wantsleep=0;
      if (u >= m_max_localch)
      {
        if (p && p != (WDL_HeapBuf*)-1)
          lc->m_bq.DisposeBlock(p);
        p=0;
        continue;
      }

      if (p == (WDL_HeapBuf*)-1)
      {
        // context 
        lc->m_curwritefile_starttime = blockstarttime;
        lc->m_curwritefile_writelen=0.0;
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
          lc->m_enc = CreateNJEncoder(m_srate,lc->m_enc_nch_used=block_nch,lc->m_enc_bitrate_used = lc->bitrate+(block_nch>1?lc->bitrate/3:0),WDL_RNG_int32());
        }

        if (lc->m_need_header)
        {
          lc->m_need_header=false;
          {
            WDL_RNG_bytes(lc->m_curwritefile.guid,sizeof(lc->m_curwritefile.guid));
            char guidstr[64];
            guidtostr(lc->m_curwritefile.guid,guidstr);
            if (!(lc->flags&4)) writeLog("local %s %d\n",guidstr,lc->channel_idx);
            if (config_savelocalaudio>0) 
            {
              lc->m_curwritefile.Open(this,NJ_ENCODER_FMT_TYPE,false);
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
          {
            int sz=p->GetSize()/sizeof(float);
            if (block_nch>1)  sz/=2;

            if (lc->m_wavewritefile)
            {
              float *ps[2]={(float *)p->Get(),0};
              if (block_nch>1) ps[1]=ps[0]+sz;
              else ps[1]=ps[0]; 

              lc->m_wavewritefile->WriteFloatsNI(ps,0,sz,2);
            }

            lc->m_enc->Encode((float*)p->Get(),sz,1,block_nch>1 ? sz:0);
            lc->m_curwritefile_writelen+=sz/(double)m_srate;
          }

          int s;
          while ((s=lc->m_enc->Available())>=
            ((lc->m_enc_header_needsend?(lc->flags&2)?LIVE_ENC_BLOCKSIZE1:MIN_ENC_BLOCKSIZE*4:(lc->flags&2)?LIVE_ENC_BLOCKSIZE2:MIN_ENC_BLOCKSIZE))
            )
          {
            if (s > MAX_ENC_BLOCKSIZE) s=MAX_ENC_BLOCKSIZE;

            {
              mpb_client_upload_interval_write wh;
              memcpy(wh.guid,lc->m_curwritefile.guid,sizeof(lc->m_curwritefile.guid));
              wh.flags=0;
              wh.audio_data=lc->m_enc->Get();
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

            lc->m_enc->Advance(s);
          }
          lc->m_enc->Compact();
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
            int l=lc->m_enc->Available();
            if (l>MAX_ENC_BLOCKSIZE) l=MAX_ENC_BLOCKSIZE;

            memcpy(wh.guid,lc->m_curwritefile.guid,sizeof(wh.guid));
            wh.audio_data=lc->m_enc->Get();
            wh.audio_data_len=l;

            lc->m_curwritefile.Write(wh.audio_data,wh.audio_data_len);

            lc->m_enc->Advance(l);
            wh.flags=lc->m_enc->Available()>0 ? 0 : 1;

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
          while (lc->m_enc->Available()>0);
          lc->m_enc->Compact(); // free any memory left

          if (lc->flags&4)
          {
            if (lc->m_curwritefile_writelen > 0.2 && lc->m_curwritefile_starttime > -1.0)
            {
              char guidstr[64],idxstr[64],offslenstr[128];
              guidtostr(lc->m_curwritefile.guid,guidstr);
              sprintf(idxstr,"%d",u);
              sprintf(offslenstr,"%.10f %.10f",lc->m_curwritefile_starttime,lc->m_curwritefile_writelen);
              // send "SESSION" chat message

    //          char buf[512];
  //            sprintf(buf,"SESSION %s %d %f %f\n",guidstr,u,lc->m_curwritefile_starttime,lc->m_curwritefile_writelen);
//              OutputDebugString(buf);
              ChatMessage_Send("SESSION",guidstr,idxstr,offslenstr);
            }
          }

          //delete m_enc;
        //  m_enc=0;
          if (lc->m_enc_nch_used != ((lc->src_channel&1024)?2:1))
          {
            delete lc->m_enc;
            lc->m_enc=0;
          }
          else
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
#endif

  return wantsleep;

}


DecodeState *NJClient::start_decode(unsigned char *guid, unsigned int fourcc, DecodeMediaBuffer *decbuf)
{
  DecodeState *newstate=new DecodeState;  
  if (decbuf) 
  {
    decbuf->AddRef();
    newstate->decode_buf=decbuf;
  }
  memcpy(newstate->guid,guid,sizeof(newstate->guid));


  // todo: make plug-in system to allow encoders to add types allowed
  // todo: with a preference for 'fourcc' if specified
  unsigned int types[]={MAKE_NJ_FOURCC('O','G','G','v')}; // only types we understand

  if (!newstate->decode_buf)
  {
    WDL_String s;

    makeFilenameFromGuid(&s,guid);
    int oldl=strlen(s.Get())+1;
    s.Append(".XXXXXXXXX");
    unsigned int x;
    for (x = 0; !newstate->decode_fp && x < sizeof(types)/sizeof(types[0]); x ++)
    {
      type_to_string(types[x],s.Get()+oldl);
      newstate->decode_fp=fopen(s.Get(),"rb");
    }
  }

  if (newstate->decode_fp||newstate->decode_buf)
  {
    newstate->decode_codec= CreateNJDecoder();
    // run some decoding

    if (newstate->decode_codec) while (newstate->decode_codec->Available() <= 0)
    {
      if (newstate->decode_fp)
      {
        int l=fread(newstate->decode_codec->DecodeGetSrcBuffer(128),1,128,newstate->decode_fp);          
        newstate->decode_codec->DecodeWrote(l);
        if (!l) 
        {
          clearerr(newstate->decode_fp);
          break;
        }
      }
      else if (newstate->decode_buf)
      {
        int l=newstate->decode_buf->Read(newstate->decode_codec->DecodeGetSrcBuffer(128),128);
        newstate->decode_codec->DecodeWrote(l);
        if (!l) break;
      }
      else break;
    }
  }

  return newstate;
}

float NJClient::GetOutputPeak(int ch)
{
  if (ch==0) return (float)output_peaklevel[0];
  else if(ch==1) return (float)output_peaklevel[1];
  return (float)(output_peaklevel[0]+output_peaklevel[1])*0.5f;
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

void NJClient::process_samples(float **inbuf, int innch, float **outbuf, int outnch, int len, int srate, int offset, int justmonitor, bool isPlaying, bool isSeek, double cursessionpos)
{
                   // -36dB/sec
  double decay=pow(.25*0.25*0.25,len/(double)srate);
  // encode my audio and send to server, if enabled
  int u;
  m_locchan_cs.Enter();
  for (u = 0; u < m_locchannels.GetSize() && u < m_max_localch; u ++)
  {
    Local_Channel *lc=m_locchannels.Get(u);
    int sc=lc->src_channel&1023;
    int sc_nch=(lc->src_channel&1024)?2:1;

    float *src=NULL,*src2=NULL;
    if (sc >= 0 && sc < innch) src=inbuf[sc]+offset;
    if (sc_nch>1) 
    {
      if (sc+1 >= 0 && sc+1 < innch) src2=inbuf[sc+1]+offset;
      if (!src2) src2=src;
    }

    if (lc->cbf || !src || ChannelMixer)
    {
      // todo: support stereo on chanmixer, silent, and effect processing stuff
      int bytelen=len*(int)sizeof(float);
      if (tmpblock.GetSize() < bytelen) tmpblock.Resize(bytelen);

      if (ChannelMixer && ChannelMixer(ChannelMixer_User32,inbuf,offset,innch,sc,(float*)tmpblock.Get(),len))
      {
        // channelmixer succeeded
      }
      else if (src) memcpy(tmpblock.Get(),src,bytelen);
      else memset(tmpblock.Get(),0,bytelen);

      src2=src=(float* )tmpblock.Get();

      // processor
      if (lc->cbf)
      {
        lc->cbf(src,len,lc->cbf_inst);
      }
    }


#ifndef NJCLIENT_NO_XMIT_SUPPORT
    if (!justmonitor)
    {
      if (lc->flags&4)
      {
        if (isSeek|| // if seeked, too long, playing and not broadcasting, or not playing and broadcasting
            lc->m_curwritefile_curbuflen>=SESSION_CHUNK_SIZE || 
            (isPlaying && !lc->bcast_active && lc->broadcasting) ||
            (!isPlaying && lc->bcast_active)
          )
        {
          if (lc->bcast_active)
          {
            lc->m_bq.AddBlock(0,0.0,NULL,0);
          }

          if (lc->broadcasting&&isPlaying)
          {
            lc->bcast_active=true;
            lc->m_bq.AddBlock(0,cursessionpos,NULL,-1); 
          }
          else
            lc->bcast_active=false;

          lc->m_curwritefile_curbuflen=0.0;
        }
      }
      else
        lc->m_curwritefile_curbuflen=0.0;

      if (lc->bcast_active) 
      {
        lc->m_bq.AddBlock(sc_nch,0.0,src,len,src2);
        lc->m_curwritefile_curbuflen += len/(double)m_srate;
      }
    }
#endif

    if (!src2) src2=src;

    // monitor this channel
    if ((!m_issoloactive && !lc->muted) || lc->solo)
    {
      int use_nch=2;
      if (outnch < 2 || (lc->out_chan_index&1024)) use_nch=1;
      int idx=(lc->out_chan_index&1023);
      if (idx+use_nch>outnch) idx=outnch-use_nch;
      if (idx< 0)idx=0;

      float *out1=outbuf[idx]+offset;

      float vol1=lc->volume;
      if (use_nch > 1)
      {
        float vol2=vol1;
        float *out2=outbuf[idx+1]+offset;
        if (lc->pan > 0.0f) vol1 *= 1.0f-lc->pan;
        else if (lc->pan < 0.0f) vol2 *= 1.0f+lc->pan;

        float maxf=(float) (lc->decode_peak_vol[0]*decay);
        float maxf2=(float) (lc->decode_peak_vol[1]*decay);

        int x=len;
        while (x--) 
        {
          float f=src[0] * vol1;

          if (f > maxf) maxf=f;
          else if (f < -maxf) maxf=-f;

//          if (f > 1.0) f=1.0;
  //        else if (f < -1.0) f=-1.0;

          *out1++ += f;

          f=src2[0]*vol2;

          if (f > maxf2) maxf2=f;
          else if (f < -maxf2) maxf2=-f;

//          if (f > 1.0) f=1.0;
  //        else if (f < -1.0) f=-1.0;

          *out2++ += f;
          src++;
          src2++;
        }
        lc->decode_peak_vol[0]=maxf;
        lc->decode_peak_vol[1]=maxf2;
      }
      else
      {
        float maxf=(float) (lc->decode_peak_vol[0]*decay);
        int x=len;
        while (x--) 
        {
          float f=(*src++ + *src2++)*0.5f * vol1;
          if (f > maxf) maxf=f;
          else if (f < -maxf) maxf=-f;

          if (f > 1.0) f=1.0;
          else if (f < -1.0) f=-1.0;

          *out1++ += f;
        }
        lc->decode_peak_vol[1]=lc->decode_peak_vol[0]=maxf;
      }
    }
    else lc->decode_peak_vol[0]=lc->decode_peak_vol[1]=0.0;
  }

  m_locchan_cs.Leave();


  if (!justmonitor)
  {
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

        mixInChannel(&user->channels[ch],muteflag,
          user->volume*user->channels[ch].volume,lpan,
            outbuf,user->channels[ch].out_chan_index,len,srate,outnch,offset,decay,isPlaying,isSeek,cursessionpos);
      }
    }
    m_users_cs.Leave();


    // write out wave if necessary

    if (waveWrite
#ifndef NJCLIENT_NO_XMIT_SUPPORT
      ||(m_oggWrite&&m_oggComp)
#endif
      )
    {
      m_wavebq->AddBlock(2,0.0,outbuf[0]+offset,len,outbuf[outnch>1]+offset);
    }
  }

  // apply master volume, then
  {
    int x=len;
    float *ptr1=outbuf[0]+offset;
    float maxf1=(float)(output_peaklevel[0]*decay);
    float maxf2=(float)(output_peaklevel[1]*decay);

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
        if (f > maxf1) maxf1=f;
        else if (f < -maxf1) maxf1=-f;

        f = *ptr2++ *= vol2;
        if (f > maxf2) maxf2=f;
        else if (f < -maxf2) maxf2=-f;
      }
    }
    else
    {
      float vol1=config_mastermute?0.0f:config_mastervolume;
      while (x--)
      {
        float f = *ptr1++ *= vol1;
        if (f > maxf1) maxf1=f;
        else if (f < -maxf1) maxf1=-f;
      }
      maxf2=maxf1;
    }
    output_peaklevel[0]=maxf1;
    output_peaklevel[1]=maxf2;
  }

  // mix in (super shitty) metronome (fucko!!!!)
  if (!justmonitor)
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

void NJClient::mixInChannel(RemoteUser_Channel *userchan, bool muted, float vol, float pan, float **outbuf, int out_channel, 
                            int len, int srate, int outnch, int offs, double vudecay,
                            bool isPlaying, bool isSeek, double playPos)
{
  if (!userchan) return;
  userchan->decode_peak_vol[0]*=vudecay;
  userchan->decode_peak_vol[1]*=vudecay;

  int llmode=(userchan->flags&2);
  int sessionmode = !llmode && (userchan->flags&4);

  if (sessionmode)
  {
    if (!isPlaying)
    {
      delete userchan->ds;
      userchan->ds=0;
      return;
    }

    if (isSeek || userchan->curds_lenleft <= 0.0)
    {
      delete userchan->ds;
      userchan->ds=0;
      
      unsigned char guid[16];
      double offs=0.0;

      char buf[512];
      sprintf(buf,"querying %f\n",playPos);
      OutputDebugString(buf);
      if (userchan->GetSessionInfo(playPos,guid,&offs,&userchan->curds_lenleft,1.0/srate))
      {
        char guidstr[256];
        guidtostr(guid,guidstr);
        sprintf(buf,"at %f got %s %.10f %.10f\n",playPos,guidstr,offs,userchan->curds_lenleft);
        OutputDebugString(buf);
        userchan->ds=start_decode(guid);
        if (userchan->ds&&userchan->ds->decode_codec)
        {
          userchan->dump_samples = ((int) (offs * userchan->ds->decode_codec->GetSampleRate()))*userchan->ds->decode_codec->GetNumChannels();
          if (userchan->dump_samples<0)userchan->dump_samples=0;
        }
        else
        {
          delete userchan->ds;
          userchan->ds=0;
        }
      }
      else
      {
        sprintf(buf,"failed (%.10f)\n",userchan->curds_lenleft);
        OutputDebugString(buf);
      }


    }

  }

  DecodeState *chan=userchan->ds;
/*  if (llmode && userchan->next_ds[0])
  {
    delete userchan->ds;
    chan = userchan->ds = userchan->next_ds[0];
    userchan->next_ds[0]=userchan->next_ds[1]; // advance queue
    userchan->next_ds[1]=0;
  }
  */
  if (!chan || !chan->decode_codec || (!chan->decode_fp && !chan->decode_buf)) 
  {
    if (llmode && userchan->next_ds[0])
    {
      delete userchan->ds;
      chan = userchan->ds = userchan->next_ds[0];
      userchan->next_ds[0]=userchan->next_ds[1]; // advance queue
      userchan->next_ds[1]=0;
    }
    if (!chan || !chan->decode_codec || (!chan->decode_fp&&!chan->decode_buf)) 
    {
      userchan->curds_lenleft -= len/(double)srate;
      return;
    }
  }

  int mdump=llmode?2048:0;

  if (userchan->dump_samples>mdump)
  {
    int av=chan->decode_codec->Available();
    if (av > userchan->dump_samples-mdump) av=userchan->dump_samples-mdump;
    chan->decode_codec->Skip(av);
    userchan->dump_samples-=av;
  }

  int needed=0;
  int srcnch=chan->decode_codec->GetNumChannels();
  while (chan->decode_codec->Available() <= (needed=resampleLengthNeeded(chan->decode_codec->GetSampleRate(),srate,len,&chan->resample_state))*srcnch)
  {
    int l=0;
    
    if (chan->decode_fp)
    {
      l=fread(chan->decode_codec->DecodeGetSrcBuffer(256),1,256,chan->decode_fp);          
    }
    else if (chan->decode_buf)
    {
      l=chan->decode_buf->Read(chan->decode_codec->DecodeGetSrcBuffer(256),256);          
    }
    else break;

    chan->decode_codec->DecodeWrote(l);

    if (userchan->dump_samples>mdump)
    {
      int av=chan->decode_codec->Available();
      if (av > userchan->dump_samples-mdump) av=userchan->dump_samples-mdump;
      chan->decode_codec->Skip(av);
      userchan->dump_samples-=av;
    }

    if (!l) 
    {
      if (chan->decode_fp) clearerr(chan->decode_fp);
      break;
    }
  }


  int codecavail=chan->decode_codec->Available();
  if (sessionmode) 
  {
    double sr=chan->decode_codec->GetSampleRate();
    int a= (int)(userchan->curds_lenleft * sr);
    if (a<1) a=1;
    userchan->curds_lenleft -= needed/(double)sr;

    a*=srcnch;
    if (codecavail > a) codecavail=a;
  }


  int len_out=len;
  if ((llmode||sessionmode) && codecavail>0 && codecavail <= needed*srcnch)
  {
    int oneeded=needed;
    needed=codecavail/srcnch;  
    len_out = ((int) ((double)srate / (double)chan->decode_codec->GetSampleRate() * (double) (needed-chan->resample_state)));
    if (len_out<0)len_out=0;
    else if (len_out>len)len_out=len;    

    if (llmode)
      userchan->dump_samples+=oneeded-needed;
  }

  if (codecavail && codecavail >= needed*srcnch)
  {
    float *sptr=chan->decode_codec->Get();

    // process VU meter, yay for powerful CPUs
    if (!muted && vol > 0.0000001) 
    {
      float *p=sptr;
      int l=(needed)*srcnch;
      float maxf=(float) (userchan->decode_peak_vol[0]/vol);
      float maxf2=(float) (userchan->decode_peak_vol[1]/vol);
      if (srcnch>=2) // vu meter + clipping
      {
        l/=2;
        while (l--)
        {
          float f=*p;
          if (f<-1.0f) f=*p=-1.0f;
          else if (f>1.0f) f=*p=1.0f;
          if (f > maxf) maxf=f;
          else if (f < -maxf) maxf=-f;

          f=*++p;
          if (f<-1.0f) f=*p=-1.0f;
          else if (f>1.0f) f=*p=1.0f;
          if (f > maxf2) maxf2=f;
          else if (f < -maxf2) maxf2=-f;
          p++;
        }
      }
      else
      {
        while (l--)
        {
          float f=*p;
          if (f<-1.0f) f=*p=-1.0f;
          else if (f>1.0f) f=*p=1.0f;
          if (f > maxf) maxf=f;
          else if (f < -maxf) maxf=-f;
          p++;
        }
        maxf2=maxf;
      }
      userchan->decode_peak_vol[0]=maxf*vol;
      userchan->decode_peak_vol[1]=maxf2*vol;

      int use_nch=2;
      if (outnch < 2 || (out_channel&1024)) use_nch=1;
      int idx=(out_channel&1023);
      if (idx+use_nch>outnch) idx=outnch-use_nch;
      if (idx< 0)idx=0;

      float lvol=vol;
      float *tmpbuf[2]={outbuf[idx]+offs,use_nch > 1 ? (outbuf[idx+1]+offs) : 0};
      if (use_nch==1 && srcnch>1)
      {
        tmpbuf[1]=tmpbuf[0];
        lvol*=0.5f;
        use_nch=2;
      }

      mixFloatsNIOutput(sptr,
              chan->decode_codec->GetSampleRate(),
              srcnch,
              tmpbuf,
              srate,use_nch,len_out,
              lvol,pan,&chan->resample_state);
    }

    // advance the queue
    chan->decode_samplesout += needed;
    chan->decode_codec->Skip(needed*srcnch);
  }
  else if (needed>0)
  {

    if (config_debug_level>0)
    {
      static int cnt=0;

      char s[512];
      guidtostr(chan->guid,s);

      char buf[512];
      sprintf(buf,"underrun %d at %d on %s, %d/%d samples\n",cnt++,chan->decode_fp ? ftell(chan->decode_fp) : -1,s,chan->decode_codec->Available(),needed);
  #ifdef _WIN32
      OutputDebugString(buf);
  #endif
    }

    if (!llmode&&!sessionmode)
    {
      userchan->dump_samples+=needed*srcnch - chan->decode_codec->Available();
      chan->decode_samplesout += chan->decode_codec->Available()/srcnch;
      chan->decode_codec->Skip(chan->decode_codec->Available());
    }
    else
    {
    }

  }
  if ((llmode||sessionmode) && len_out < len && (userchan->next_ds[0]||(sessionmode&&len_out>0)))
  {
    // call again 
    userchan->curds_lenleft=-1.0;
    delete userchan->ds;
    chan = userchan->ds = userchan->next_ds[0];
    userchan->next_ds[0]=userchan->next_ds[1]; // advance queue
    userchan->next_ds[1]=0;
    if (sessionmode || (chan && chan->decode_codec && (chan->decode_fp||chan->decode_buf))) 
      mixInChannel(userchan,muted,vol,pan,outbuf,out_channel,len-len_out,srate,outnch,offs+len_out,vudecay,
        isPlaying,false,playPos + len_out/(double)srate);
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

    if (!(lc->flags&4)) 
    {
      if (lc->bcast_active) 
      {
        lc->m_bq.AddBlock(0,0.0,NULL,0);
      }

      int wasact=lc->bcast_active;

      lc->bcast_active = lc->broadcasting;

      if (wasact && !lc->bcast_active)
      {
        lc->m_bq.AddBlock(0,-1.0,NULL,-1);
      }
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

      if (!(chan->flags&2) && !(chan->flags&4))
      {
        chan->dump_samples=0;
        delete chan->ds;
        chan->ds=0;
        if ((user->submask & user->chanpresentmask) & (1<<ch)) chan->ds = chan->next_ds[0];
        else delete chan->next_ds[0];
        chan->next_ds[0]=chan->next_ds[1]; // advance queue
        chan->next_ds[1]=0;
        
        if (chan->ds)
        {
          char guidstr[64];
          guidtostr(chan->ds->guid,guidstr);
          writeLog("user %s \"%s\" %d \"%s\"\n",guidstr,user->name.Get(),ch,chan->name.Get());
        }
      }
    }
  }
  m_users_cs.Leave();
}  //if (m_enc->isError()) printf("ERROR\n");
  //else printf("YAY\n");


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

char *NJClient::GetUserChannelState(int useridx, int channelidx, bool *sub, float *vol, float *pan, bool *mute, bool *solo, int *outchannel, int *flags)
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
  if (outchannel) *outchannel=p->out_chan_index;
  if (flags) *flags=p->flags;
  
  return p->name.Get();
}


void NJClient::SetUserChannelState(int useridx, int channelidx, 
                                   bool setsub, bool sub, bool setvol, float vol, bool setpan, float pan, bool setmute, bool mute, bool setsolo, bool solo, bool setoutch, int outchannel)
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

      p->dump_samples=0;

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
  if (setoutch) p->out_chan_index=outchannel;
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


float NJClient::GetUserChannelPeak(int useridx, int channelidx, int whichch)
{
  if (useridx<0 || useridx>=m_remoteusers.GetSize()||channelidx<0||channelidx>=MAX_USER_CHANNELS) return 0.0f;
  RemoteUser_Channel *p=m_remoteusers.Get(useridx)->channels + channelidx;
  RemoteUser *user=m_remoteusers.Get(useridx);
  if (!(user->chanpresentmask & (1<<channelidx))) return 0.0f;

  if (whichch==0) return (float)p->decode_peak_vol[0];
  if (whichch==1) return (float)p->decode_peak_vol[1];
  return (float) (p->decode_peak_vol[0]+p->decode_peak_vol[1])*0.5f;

}

float NJClient::GetLocalChannelPeak(int ch, int whichch)
{
  int x;
  for (x = 0; x < m_locchannels.GetSize() && m_locchannels.Get(x)->channel_idx!=ch; x ++);
  if (x == m_locchannels.GetSize()) return 0.0f;
  Local_Channel *c=m_locchannels.Get(x);
  if (whichch==0) return (float)c->decode_peak_vol[0];
  if (whichch==1) return (float)c->decode_peak_vol[1];
  return (float) (c->decode_peak_vol[0]+c->decode_peak_vol[1])*0.5f;
}

void NJClient::DeleteLocalChannel(int ch)
{
  m_locchan_cs.Enter();
  int x;
  for (x = 0; x < m_locchannels.GetSize() && m_locchannels.Get(x)->channel_idx!=ch; x ++);
  if (x < m_locchannels.GetSize())
  {
    bool spoo=m_locchannels.Get(x)->solo;
    delete m_locchannels.Get(x);
    m_locchannels.Delete(x);
    if (spoo)
    {
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
                                   bool setbitrate, int bitrate, bool setbcast, bool broadcast, bool setoutch, int outch, bool setflags, int flags)
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
  if (setoutch) c->out_chan_index=outch;
  if (setflags) c->flags=flags;
  m_locchan_cs.Leave();
}

char *NJClient::GetLocalChannelInfo(int ch, int *srcch, int *bitrate, bool *broadcast, int *outch, int *flags)
{
  int x;
  for (x = 0; x < m_locchannels.GetSize() && m_locchannels.Get(x)->channel_idx!=ch; x ++);
  if (x == m_locchannels.GetSize()) return 0;
  Local_Channel *c=m_locchannels.Get(x);
  if (srcch) *srcch=c->src_channel;
  if (bitrate) *bitrate=c->bitrate;
  if (broadcast) *broadcast=c->broadcasting;
  if (outch) *outch=c->out_chan_index;
  if (flags) *flags=c->flags;

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
      sci.build_add_rec(ch->name.Get(),0,0,ch->flags);
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
//  if (config_savelocalaudio>0) 

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


RemoteUser_Channel::RemoteUser_Channel() : volume(0.25f), pan(0.0f), out_chan_index(0), flags(0), dump_samples(0), ds(NULL)
{
  decode_peak_vol[0]=decode_peak_vol[1]=0.0;
  memset(next_ds,0,sizeof(next_ds));
  curds_lenleft=0.0;
}

RemoteUser_Channel::~RemoteUser_Channel()
{
  delete ds;
  ds=NULL;
  delete next_ds[0];
  delete next_ds[1];
  memset(next_ds,0,sizeof(next_ds));
  sessioninfo.Empty(true);
}


bool RemoteUser_Channel::GetSessionInfo(double time, unsigned char *guid, double *offs, double *len, double mv)
{
  WDL_MutexLock lock(&sessionlist_mutex);

  // todo: binary search
  int x;
  for (x = 0; x < sessioninfo.GetSize(); x ++)
  {
    if (time < sessioninfo.Get(x)->start_time-mv) 
    {
      *len = sessioninfo.Get(x)->start_time-time;
      if (*len > 1.0) *len=1.0;
      return false;
    }

    if (time+mv < sessioninfo.Get(x)->start_time+ sessioninfo.Get(x)->length) 
    {
      memcpy(guid,sessioninfo.Get(x)->guid,16);
      if (time < sessioninfo.Get(x)->start_time) time=sessioninfo.Get(x)->start_time;

      *offs=(time - sessioninfo.Get(x)->start_time) + sessioninfo.Get(x)->offset;
      *len = (sessioninfo.Get(x)->start_time+sessioninfo.Get(x)->length)-time;
      return true;
    }
  }
  *len = 1.0;
  return false;
}

void RemoteUser_Channel::AddSessionInfo(const unsigned char *guid, double st, double len)
{
  if (st<0.0 || len < 0.2) return;
  const double min_length=0.1;
  const int max_entries=65536;

  // todo: binary search?
  int x;
  for (x = 0; x < sessioninfo.GetSize(); x ++)
  {
    if (st < sessioninfo.Get(x)->start_time) break;
  }
  ChannelSessionInfo *prev=sessioninfo.Get(x-1);
  ChannelSessionInfo *next=sessioninfo.Get(x);



  WDL_MutexLock lock(&sessionlist_mutex);
  // merge this in as a channel

  if (prev)
  {
    if (st < prev->start_time + prev->length)
    {
      if (st+len <= prev->start_time + prev->length-0.001) // if completely contained by previous item, don't insert
        return;

      prev->length = st-prev->start_time;
      if (prev->length < min_length)
      {
        sessioninfo.Delete(--x);
        delete prev;
        prev=0;
      }
    }
  }
  if (next)
  {
    if (st+len > next->start_time)
    {
      double adj=(st+len) - next->start_time;
      next->start_time += adj;
      next->length -= adj;
      next->offset += adj;

      if (next->length < min_length)
      {
        sessioninfo.Delete(x);
        delete next;
        next=0;
      }

    }
  }
  if (len >= min_length && sessioninfo.GetSize()<max_entries)
  {
    sessioninfo.Insert(x,new ChannelSessionInfo(guid,st,len));
  }

}



RemoteDownload::RemoteDownload() : chidx(-1), playtime(0), m_fp(0), m_decbuf(0)
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
  if (m_fp) fclose(m_fp);
  m_fp=0;
  startPlaying(1);
  if (m_decbuf)
  {
    m_decbuf->Release();
    m_decbuf=0;
  }

}

void RemoteDownload::Open(NJClient *parent, unsigned int fourcc, bool forceToDisk)
{    
  m_parent=parent;
  Close();
  m_fp=0;
  m_decbuf=new DecodeMediaBuffer;
  if (!m_decbuf || !parent || parent->config_savelocalaudio>0 || forceToDisk) 
  {
    WDL_String s;
    parent->makeFilenameFromGuid(&s,guid);


    // append extension from fourcc
    char buf[8];
    type_to_string(fourcc, buf);
    s.Append(".");
    s.Append(buf);

    m_fourcc=fourcc;
    m_fp=fopen(s.Get(),"wb");
  }
}

void RemoteDownload::startPlaying(int force)
{
  if (!m_parent || chidx<0) return;
  if (!force)
  {
    if (playtime)
    {
      if (m_fp && ftell(m_fp)>playtime) force=1;
      else if (m_decbuf && m_decbuf->Size()>playtime) force=1;
    }

  }

  if (force)
    // wait until we have config_play_prebuffer of data to start playing, or if config_play_prebuffer is 0, we are forced to play (download finished)
  {
    int x;
    RemoteUser *theuser;
    for (x = 0; x < m_parent->m_remoteusers.GetSize() && strcmp((theuser=m_parent->m_remoteusers.Get(x))->name.Get(),username.Get()); x ++);
    if (x < m_parent->m_remoteusers.GetSize() && chidx >= 0 && chidx < MAX_USER_CHANNELS)
    {

      if (!(theuser->channels[chidx].flags&4)) // only "play" if not a session channel
      {
        DecodeState *tmp=m_parent->start_decode(guid,m_fourcc,m_decbuf);

        DecodeState *tmp2;
        m_parent->m_users_cs.Enter();
        int useidx=!!theuser->channels[chidx].next_ds[0];
        tmp2=theuser->channels[chidx].next_ds[useidx];
        theuser->channels[chidx].next_ds[useidx]=tmp;
        m_parent->m_users_cs.Leave();
        delete tmp2;
      }
    }
    chidx=-1;
  }
}

void RemoteDownload::Write(void *buf, int len)
{
  if (m_fp)
  {
    fwrite(buf,1,len,m_fp);
    fflush(m_fp);
  }
  if (m_decbuf)
  {
    m_decbuf->Write(buf,len);
  }

  startPlaying();  
}


Local_Channel::Local_Channel() : channel_idx(0), src_channel(0), volume(1.0f), pan(0.0f), 
                muted(false), solo(false), broadcasting(false), 
#ifndef NJCLIENT_NO_XMIT_SUPPORT
                m_enc(NULL), 
                m_enc_bitrate_used(0), 
                m_enc_nch_used(0),
                m_enc_header_needsend(NULL),
#endif
                bcast_active(false), cbf(NULL), cbf_inst(NULL), 
                bitrate(64), m_need_header(true), out_chan_index(0), flags(0), 
                m_curwritefile_starttime(0.0), 
                m_curwritefile_writelen(0.0),
                m_curwritefile_curbuflen(0.0),
                m_wavewritefile(NULL)
{
  decode_peak_vol[0]=decode_peak_vol[1]=0.0;
}


int BufferQueue::GetBlock(WDL_HeapBuf **b, int *attr, double *startpos) // return 0 if got one, 1 if none avail
{
  m_cs.Enter();
  if (m_samplequeue.GetSize()>1)
  {
    *b=m_samplequeue.Get(0);
    WDL_HeapBuf *oa=m_samplequeue.Get(1);
    if (oa && oa->GetSize() == sizeof(AttrStruct))
    {
      AttrStruct *as=(AttrStruct *)oa->Get();
      if (attr)  *attr= as->attr;
      if (startpos) *startpos = as->startpos;
    }
    else 
    {
      if (attr)  *attr=0;
      if (startpos) *startpos = 0.0;
    }

    if (oa) m_emptybufs_attr.Add(oa);
    m_samplequeue.Delete(0);
    m_samplequeue.Delete(0);
    m_cs.Leave();
    return 0;
  }
  m_cs.Leave();
  return 1;
}

void BufferQueue::DisposeBlock(WDL_HeapBuf *b)
{
  m_cs.Enter();
  if (b && b != (WDL_HeapBuf*)-1) m_emptybufs.Add(b);
  m_cs.Leave();
}


void BufferQueue::AddBlock(int attr, double startpos, float *samples, int len, float *samples2)
{
  WDL_HeapBuf *mybuf=0;
  if (len>0)
  {
    m_cs.Enter();

    if (m_samplequeue.GetSize() > 512*2)
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

  WDL_HeapBuf *attrbuf=NULL;
  int esz=m_emptybufs_attr.GetSize();
  if (esz)
  {
    attrbuf=m_emptybufs_attr.Get(esz-1);
    m_emptybufs_attr.Delete(esz-1);
  }

  if (!attrbuf) attrbuf=new WDL_HeapBuf;
  AttrStruct *as=(AttrStruct *)attrbuf->Resize(sizeof(AttrStruct));

  as->attr=attr;
  as->startpos=startpos;

  m_samplequeue.Add(mybuf);
  m_samplequeue.Add(attrbuf);
  
  m_cs.Leave();
}

Local_Channel::~Local_Channel()
{
#ifndef NJCLIENT_NO_XMIT_SUPPORT
  delete m_enc;
  m_enc=0;
  delete m_enc_header_needsend;
  m_enc_header_needsend=0;
#endif

  delete m_wavewritefile;
  m_wavewritefile=0;

}

void NJClient::SetOggOutFile(FILE *fp, int srate, int nch, int bitrate)
{
#ifndef NJCLIENT_NO_XMIT_SUPPORT
  if (m_oggWrite)
  {
    if (m_oggComp)
    {
      m_oggComp->Encode(NULL,0);
      if (m_oggComp->Available())
        fwrite((char *)m_oggComp->Get(),1,m_oggComp->Available(),m_oggWrite);
    }
    fclose(m_oggWrite);
    m_oggWrite=0;
  }
  delete m_oggComp;
  m_oggComp=0;

  if (fp)
  {
    //fucko
    m_oggComp=CreateNJEncoder(srate,nch,bitrate,WDL_RNG_int32());
    m_oggWrite=fp;
  }
#endif
}

