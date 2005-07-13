#ifndef _NJCLIENT_H_
#define _NJCLIENT_H_

#ifdef _WIN32
#include <windows.h>
#else
#include <stdlib.h>
#include <memory.h>
#endif
#include <stdio.h>
#include <time.h>
#include "../WDL/string.h"
#include "../WDL/ptrlist.h"
#include "../WDL/jnetlib/jnetlib.h"
#include "../WDL/sha.h"
#include "../WDL/rng.h"
#include "../WDL/mutex.h"

#include "../WDL/wavwrite.h"

#include "netmsg.h"


class I_NJEncoder;
class RemoteDownload;
class RemoteUser;
class Local_Channel;
class DecodeState;
class BufferQueue;

class NJClient
{
  friend class RemoteDownload;
public:
  NJClient();
  ~NJClient();

  void Connect(char *host, char *user, char *pass);
  void Disconnect();

  // call Run() from your main (UI) thread
  int Run();// returns nonzero if sleep is OK

  char *GetErrorStr() { return m_errstr.Get(); }

  int IsAudioRunning() { return m_audio_enable; }
  // call AudioProc, (and only AudioProc) from your audio thread
  void AudioProc(float **inbuf, int innch, float **outbuf, int outnch, int len, int srate); // len is number of sample pairs or samples


  // basic configuration
  int   config_autosubscribe;
  int   config_savelocalaudio; // set 1 to save compressed files, set to 2 to save .wav files as well
  float config_metronome,config_metronome_pan; // volume of metronome
  bool  config_metronome_mute;
  float config_mastervolume,config_masterpan; // master volume
  bool  config_mastermute;
  int   config_debug_level; 
  int   config_play_prebuffer; // -1 means play instantly, 0 means play when full file is there, otherwise refers to how many
                               // bytes of compressed source to have before play. the default value is 4096.

  float GetOutputPeak();

  enum { NJC_STATUS_DISCONNECTED=-3,NJC_STATUS_INVALIDAUTH=-2, NJC_STATUS_CANTCONNECT=-1, NJC_STATUS_OK=0, NJC_STATUS_PRECONNECT};
  int GetStatus();

  void SetWorkDir(char *path);
  char *GetWorkDir() { return m_workdir.Get(); }

  char *GetUserName() { return m_user.Get(); }
  char *GetHostName() { return m_host.Get(); }

  float GetActualBPM() { return (float) m_active_bpm; }
  int GetBPI() { return m_active_bpi; }
  void GetPosition(int *pos, int *length);  // positions in samples
  int GetLoopCount() { return m_loopcnt; }  
  unsigned int GetSessionPosition(); // returns milliseconds

  int HasUserInfoChanged() { if (m_userinfochange) { m_userinfochange=0; return 1; } return 0; }
  int GetNumUsers() { return m_remoteusers.GetSize(); }
  char *GetUserState(int idx, float *vol=0, float *pan=0, bool *mute=0);
  void SetUserState(int idx, bool setvol, float vol, bool setpan, float pan, bool setmute, bool mute);

  float GetUserChannelPeak(int useridx, int channelidx);
  char *GetUserChannelState(int useridx, int channelidx, bool *sub=0, float *vol=0, float *pan=0, bool *mute=0, bool *solo=0);
  void SetUserChannelState(int useridx, int channelidx, bool setsub, bool sub, bool setvol, float vol, bool setpan, float pan, bool setmute, bool mute, bool setsolo, bool solo);
  int EnumUserChannels(int useridx, int i); // returns <0 if out of channels. start with i=0, and go upwards

  int GetMaxLocalChannels() { return m_max_localch; }
  void DeleteLocalChannel(int ch);
  int EnumLocalChannels(int i);
  float GetLocalChannelPeak(int ch);
  void SetLocalChannelProcessor(int ch, void (*cbf)(float *, int ns, void *), void *inst);
  void GetLocalChannelProcessor(int ch, void **func, void **inst);
  void SetLocalChannelInfo(int ch, char *name, bool setsrcch, int srcch, bool setbitrate, int bitrate, bool setbcast, bool broadcast);
  char *GetLocalChannelInfo(int ch, int *srcch, int *bitrate, bool *broadcast);
  void SetLocalChannelMonitoring(int ch, bool setvol, float vol, bool setpan, float pan, bool setmute, bool mute, bool setsolo, bool solo);
  int GetLocalChannelMonitoring(int ch, float *vol, float *pan, bool *mute, bool *solo); // 0 on success
  void NotifyServerOfChannelChange(); // call after any SetLocalChannel* that occur after initial connect

  int IsASoloActive() { return m_issoloactive; }

  void SetLogFile(char *name=NULL);

  void SetOggOutFile(FILE *fp, int srate, int nch, int bitrate=128);
  WaveWriter *waveWrite;


  int LicenseAgreement_User32;
  int (*LicenseAgreementCallback)(int user32, char *licensetext); // return TRUE if user accepts


  // messages you can send:
  // "MSG" "text"  - broadcast "text" to everybody
  // "PRIVMSG" "username" "text"  - send text to "username"
  void ChatMessage_Send(char *parm1, char *parm2, char *parm3=NULL, char *parm4=NULL, char *parm5=NULL);

  // messages you can receive from this:
  // "MSG" "user" "text"   - message from user to everybody (including you!), or if user is empty, from the server
  // "PRIVMSG "user" "text"   - private message from user

  // usernames are not case sensitive, but message names ARE.

  // note that nparms is the MAX number of parms, you still can get NULL parms entries in there (though rarely)
  void (*ChatMessage_Callback)(int user32, NJClient *inst, char **parms, int nparms); 
  int ChatMessage_User32;


  // set these if you want to mix multiple channels into the output channel
  // return 0 if you want the default behavior
  int (*ChannelMixer)(int user32, int chi, float **inbuf, int innch, float *outbuf, int len, int offset);
  int ChannelMixer_User32;


protected:
  double output_peaklevel;

  void _reinit();

  void makeFilenameFromGuid(WDL_String *s, unsigned char *guid);

  void updateBPMinfo(int bpm, int bpi);
  void process_samples(float **inbuf, int innch, float **outbuf, int outnch, int len, int srate, int offset, int justmonitor=0);
  void on_new_interval();

  void writeLog(char *fmt, ...);

  WDL_String m_errstr;

  WDL_String m_workdir;
  int m_status;
  int m_max_localch;
  int m_connection_keepalive;
  FILE *m_logFile;
  FILE *m_oggWrite;
  I_NJEncoder *m_oggComp;

  WDL_String m_user, m_pass, m_host;

  int m_in_auth;
  int m_bpm,m_bpi;
  int m_beatinfo_updated;
  int m_audio_enable;
  int m_srate;
  int m_userinfochange;
  int m_issoloactive;

  unsigned int m_session_pos_ms,m_session_pos_samples; // samples just keeps track of any samples lost to precision errors

  int m_loopcnt;
  int m_active_bpm, m_active_bpi;
  int m_interval_length;
  int m_interval_pos, m_metronome_state, m_metronome_tmp,m_metronome_interval;
  double m_metronome_pos;

  DecodeState *start_decode(unsigned char *guid, unsigned int fourcc=0);

  BufferQueue *m_wavebq;


  WDL_PtrList<Local_Channel> m_locchannels;

  void mixInChannel(bool muted, float vol, float pan, DecodeState *chan, float **outbuf, int len, int srate, int outnch, int offs, double vudecay);

  WDL_Mutex m_users_cs, m_locchan_cs, m_log_cs, m_misc_cs;
  Net_Connection *m_netcon;
  WDL_PtrList<RemoteUser> m_remoteusers;
  WDL_PtrList<RemoteDownload> m_downloads;

  WDL_HeapBuf tmpblock;
};



#define MAX_USER_CHANNELS 32
#define MAX_LOCAL_CHANNELS 32 // probably want to use NJClient::GetMaxLocalChannels() if determining when it's OK to add a channel,etc
#define DOWNLOAD_TIMEOUT 8


#endif//_NJCLIENT_H_
