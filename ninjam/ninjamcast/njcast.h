#ifndef _NJCAST_H
#define _NJCAST_H

#include "../../WDL/wdlstring.h"

class JNL_Connection;
class JNL_HTTPGet;
class LameEncoder;
class NJClient;

class NJCast {
public:
  NJCast(NJClient *client);
  ~NJCast();

  int Connect(char *servername, int port);
  void Disconnect();

  int sending();

  int Run(); // return 1 if work was done

  void AudioProc(float **inbuf, int innch, float **outbuf, int outnch, int len, int srate);

private:
  void handleTitleSetting();

  WDL_String sc_address;
  int sc_port;

  int state;
  NJClient *client;
  JNL_Connection *conn;
  LameEncoder *encoder;

  time_t reconnect_timer;

  time_t last_titleset;
  JNL_HTTPGet *titleset;
  WDL_String last_title_url;
  time_t titleset_began;
};

#endif
