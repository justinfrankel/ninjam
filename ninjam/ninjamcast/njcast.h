#ifndef _NJCAST_H
#define _NJCAST_H

class JNL_Connection;
class LameEncoder;

class NJCast {
public:
  NJCast();
  ~NJCast();

  int Connect(char *servername, int port);
  void Disconnect();

  int sending();

  void Run();

  void AudioProc(float **inbuf, int innch, float **outbuf, int outnch, int len, int srate);

private:
  int state;
  JNL_Connection *conn;
  LameEncoder *encoder;
};

#endif
