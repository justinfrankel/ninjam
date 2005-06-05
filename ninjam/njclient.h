#ifndef _NJCLIENT_H_
#define _NJCLIENT_H_

#include <windows.h>
#include <stdio.h>
#include <time.h>
#include "../WDL/string.h"
#include "../WDL/ptrlist.h"
#include "../WDL/jnetlib/jnetlib.h"
#include "../WDL/sha.h"
#include "../WDL/rng.h"

#include "../WDL/wavwrite.h"

#include "../netmsg.h"
#include "../WDL/vorbisencdec.h"
#include "../WDL/lameencdec.h"


#if 1
#define NJ_ENCODER LameEncoder
#define NJ_ENCODER_BITRATE 64
#define NJ_ENCODER_FMT_STRING "mp3"
#define NJ_ENCODER_FMT_TYPE 'mp3 '

#define NJ_DECODER LameDecoder

#else
#define NJ_ENCODER VorbisEncoder
#define NJ_ENCODER_BITRATE -0.1f
#define NJ_ENCODER_FMT_STRING "ogg"
#define NJ_ENCODER_FMT_TYPE 'ogg '
#define NJ_DECODER VorbisDecoder

#endif

class RemoteDownload;
class RemoteUser;
class RemoteUser_Channel;
class Local_Channel;
class DecodeState;

class NJClient
{
public:
  NJClient();
  ~NJClient();

  void Connect(char *host, char *user, char *pass);

  // call Run() from your main (UI) thread
  int Run();// returns nonzero if sleep is OK

  // call AudioProc, (and only AudioProc) from your audio thread
  void AudioProc(float *buf, int len, int nch, int srate); // len is number of sample pairs or samples


  // basic configuration
  int   config_autosubscribe;
  int   config_savelocalaudio;
  float config_monitor;
  float config_metronome;
  int   config_debug_level; 


  enum { NJC_STATUS_INVALIDAUTH=-2, NJC_STATUS_CANTCONNECT=-1, NJC_STATUS_OK=0, NJC_STATUS_PRECONNECT, NJC_STATUS_RECONNECTING};
  int GetStatus();

  void SetWorkDir(char *path);
  char *GetWorkDir() { return m_workdir.Get(); }

  char *GetUserName() { return m_user.Get(); }
  char *GetHostName() { return m_host.Get(); }

  int GetBPM() { return m_active_bpm; }
  int GetBPI() { return m_active_bpi; }
  void GetPosition(int *pos, int *length);  // positions in samples

  int GetNumUsers() { return m_remoteusers.GetSize(); }
  char *GetUserState(int idx, float *vol=0, float *pan=0, bool *mute=0);
  void SetUserState(int idx, bool setvol, float vol, bool setpan, float pan, bool setmute, bool mute);
  char *GetUserChannelState(int useridx, int channelidx, bool *sub=0, float *vol=0, float *pan=0, bool *mute=0);
  void SetUserChannelState(int useridx, int channelidx, bool setsub, bool sub, bool setvol, float vol, bool setpan, float pan, bool setmute, bool mute);

  void DeleteLocalChannel(int ch);
  void SetLocalChannelInfo(int ch, char *name, bool setsrcch, int srcch, bool setsrcnch, int srcnch, bool setbitrate, int bitrate, bool setbcast, bool broadcast);
  char *GetLocalChannelInfo(int ch, int *srcch, int *srcnch, int *bitrate, bool *broadcast);
  void SetLocalChannelMonitoring(int ch, bool setvol, float vol, bool setpan, float pan, bool setmute, bool mute);
  int GetLocalChannelMonitoring(int ch, float *vol, float *pan, bool *mute); // 0 on success
  void NotifyServerOfChannelChange(); // call after any SetLocalChannel* that occur after initial connect

  void SetLogFile(char *name=NULL);

  WaveWriter *waveWrite;

  void makeFilenameFromGuid(WDL_String *s, unsigned char *guid);
private:

  void updateBPMinfo(int bpm, int bpi);
  void process_samples(float *buf, int len, int nch, int srate);
  void input_monitor_samples(float *buf, int len, int nch, int srate);
  void on_new_interval(int nch, int srate);

  void writeLog(char *fmt, ...);


  WDL_String m_workdir;
  int m_status;
  FILE *m_logFile;

  WDL_String m_user, m_pass, m_host;

  int m_bpm,m_bpi;
  int m_beatinfo_updated;
  int m_audio_enable;

  int m_active_bpm, m_active_bpi;
  int m_interval_length;
  int m_interval_pos, m_metronome_pos, m_metronome_state, m_metronome_tmp,m_metronome_interval;


  WDL_PtrList<Local_Channel> m_locchannels;

  void mixInChannel(bool muted, float vol, float pan, DecodeState *chan, float *buf, int len, int srate, int nch);

  CRITICAL_SECTION m_users_cs, m_locchan_cs, m_log_cs, m_misc_cs;
  Net_Connection *m_netcon;
  WDL_PtrList<RemoteUser> m_remoteusers;
  WDL_PtrList<RemoteDownload> m_downloads;
};


class DecodeState
{
  public:
    DecodeState() : decode_fp(0), decode_codec(0), dump_samples(0),
                                           decode_samplesout(0), resample_state(0.0)
    { 
      memset(decode_last_guid,0,sizeof(decode_last_guid));
    }
    ~DecodeState()
    {
      delete decode_codec;
      decode_codec=0;
      if (decode_fp) fclose(decode_fp);
      decode_fp=0;
    }

    FILE *decode_fp;
    NJ_DECODER *decode_codec;
    int decode_samplesout;
    int dump_samples;
    unsigned char decode_last_guid[16];
    double resample_state;

};


#define MAX_USER_CHANNELS 32
class RemoteUser_Channel
{
  public:
    RemoteUser_Channel();
    ~RemoteUser_Channel();

    float volume, pan;

    unsigned char cur_guid[16];
    WDL_String name;


    // decode/mixer state, used by mixer
    DecodeState ds;

};

class RemoteUser
{
public:
  RemoteUser() : muted(0), volume(1.0f), pan(0.0f), submask(0), mutedmask(0), chanpresentmask(0) { }
  ~RemoteUser() { }

  bool muted;
  float volume;
  float pan;
  WDL_String name;
  int submask;
  int chanpresentmask;
  int mutedmask;
  RemoteUser_Channel channels[MAX_USER_CHANNELS];
};


#define DOWNLOAD_TIMEOUT 8
class RemoteDownload
{
public:
  RemoteDownload();
  ~RemoteDownload();

  void Close();
  void Open(NJClient *parent);
  void Write(void *buf, int len);

  time_t last_time;
  unsigned char guid[16];

private:
  FILE *fp;
};



#define MAX_LOCAL_CHANNELS 2

class Local_Channel
{
public:
  Local_Channel();
  ~Local_Channel();

  int channel_idx;

  int src_channel; // 0 or 1
  int src_nch;     // 1 or 2
  int bitrate;

  float volume;
  float pan;
  bool muted;

  //?
  // mode flag. 0=silence, 1=broadcasting, 2=loop last
  bool broadcasting; //takes effect next loop



  // internal state
  bool bcast_active;
  NJ_ENCODER *m_enc;
  int m_enc_nch,m_enc_bitrate_used;

  Net_Message *m_enc_header_needsend;
  
  WDL_String name;
  RemoteDownload m_curwritefile;

  //DecodeState too, eventually
};



extern unsigned char zero_guid[16];;
char *guidtostr_tmp(unsigned char *guid);
void guidtostr(unsigned char *guid, char *str);




#endif//_NJCLIENT_H_