#include <windows.h>
#include <stdio.h>

#include "njcast.h"

#include "../../wdl/jnetlib/connection.h"
#include "../../wdl/lameencdec.h"
#include "../../wdl/string.h"

enum {
  CONNECTING,
  WAITFOROK,
  SENDSTREAMINFO,
  SENDDATA,
  DONE,
};

char sc_pass[4096]="changeme";//FUCKO config

extern int g_bitrate;

NJCast::NJCast() {
  conn = NULL;
  encoder = NULL;
  state = -1;
}

NJCast::~NJCast() {
  Disconnect();
}

int NJCast::Connect(char *servername, int port) {
  Disconnect();

  conn = new JNL_Connection(JNL_CONNECTION_AUTODNS,65536,65536);
  conn->connect(servername, port+1);

  state = CONNECTING;

  return 1;
}

void NJCast::Disconnect() {
  delete conn; conn = NULL;
  delete encoder; encoder = NULL;
  state = -1;
}

int NJCast::sending() {
  return (state == SENDDATA);
}

void NJCast::Run() {
  if (!conn) return;	// not connected?

  conn->run();

#if 0
  // eat any random incoming
  while (conn->recv_bytes_available() > 0) {
    char bla;
    conn->recv_bytes(&bla, 1);
  }
#endif

  int s = conn->get_state();
  switch (s) {
    case JNL_Connection::STATE_ERROR:
    case JNL_Connection::STATE_CLOSED:
if (state != DONE) printf("connection fuct\n");
      state = DONE;
  }

  switch (state) {
    case CONNECTING: {
      char buf[4096];
      sprintf(buf, "%s\r\n", sc_pass);
      // send pw
      if (conn->send_string(buf) < 0) {
printf("send pass fail\n");
        return;	// try again
      }
      state = WAITFOROK;
printf("->WAITFOROK\n");
    }
    break;
    case WAITFOROK: {
      char buf[4096];
//FUCKO timeout
//CUT printf("where's my ok\n");
      int avail = conn->recv_lines_available();
      if (conn->recv_lines_available()<1) return;	// try again
      conn->recv_line(buf, 4095);
      buf[4095] = 0;
//CUT printf("got line '%s'\n", buf);
      if (strcmp(buf, "OK2")) {
//FUCKO log error
        state = DONE;
        return;
      }
      state = SENDSTREAMINFO;
printf("->SENDSTREAMINFO\n");
    }
    break;
    case SENDSTREAMINFO: {
      WDL_String info;
char *servergenre="ninjam";
char *serverpub="0";
      extern char g_servername[4096];
      info.Append("icy-name:"); info.Append(g_servername); info.Append("\r\n");
      info.Append("icy-genre:"); info.Append(servergenre); info.Append("\r\n");
      info.Append("icy-pub:"); info.Append(serverpub); info.Append("\r\n");
      info.Append("\r\n");
      if (conn->send_bytes_available() < strlen(info.Get())) return;// try again
      conn->send_string(info.Get());
      state = SENDDATA;	// woot
printf("->SENDDATA\n");
    }
    break;
    case SENDDATA: {
      if (encoder == NULL) return;  // not jet

      // push whatever we have
      int send_avail = conn->send_bytes_available();
      int avail_to_send = encoder->outqueue.GetSize();
      int nbytes = min(send_avail, avail_to_send);
//if (nbytes > 0) printf("availtosend %d, nbytes %d\n", avail_to_send, nbytes);
      if (nbytes > 0) {
        conn->send_bytes(encoder->outqueue.Get(), nbytes);
        encoder->outqueue.Advance(nbytes);
      }
    }
    break;
    case DONE: {
      // just idle here til we are shut down
    }
    break;
  }

  conn->run();
}

void NJCast::AudioProc(float **inbuf, int innch, float **outbuf, int outnch, int len, int srate) {
  if (state != SENDDATA) return;	// not ready

  if (encoder == NULL)
    encoder = new LameEncoder(srate, outnch, g_bitrate);

  if (encoder->Status() > 0) {
printf("LAME ENCODER ERROR\n");
  }

#if 1
  if (outnch == 1) {	// yay mono rules
    encoder->Encode(outbuf[0], len);
  } else if (outnch == 2) {
    // interleave the buffers
    float *tmp = (float*)calloc(len, sizeof(float)*outnch);
    float *f1 = (float*)tmp;
    float *f2 = f1+1;
    float *outbuf0 = outbuf[0];
    float *outbuf1 = outbuf[1];
    for (int i = 0; i < len; i++) {
      *f1 = outbuf0[i];
      f1 += 2;
      *f2 = outbuf1[i];
      f2 += 2;
    }
    encoder->Encode(tmp, len);
    free(tmp);
//printf("encoding %d samples\n", len);
  }
#endif
}
