#ifndef _NJCAST_H
#define _NJCAST_H

#include "../../wdl/string.h"

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
  int state;
  NJClient *client;
  JNL_Connection *conn;
  LameEncoder *encoder;

  int last_titleset;
  JNL_HTTPGet *titleset;
  WDL_String last_title_url;
};

#endif
