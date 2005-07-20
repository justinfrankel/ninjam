#ifdef _WIN32
#include <windows.h>
#include <stdio.h>
#else
#include <stdlib.h>
#include <memory.h>
#endif

#include <time.h>

#include "njcast.h"

#include "../njclient.h"

#include "../../wdl/jnetlib/connection.h"
#include "../../wdl/jnetlib/httpget.h"
#include "../../wdl/lameencdec.h"
#include "../../wdl/string.h"

enum {
  CONNECTING,
  WAITFOROK,
  SENDSTREAMINFO,
  SENDDATA,
  RECONNECT,
};

extern char g_servername[4096];

extern int g_bitrate;
extern char g_sc_pass[4096];
extern char *g_sc_address;
extern int g_sc_port;
extern char *g_sc_servergenre;
extern char *g_sc_serverpub;
extern char *g_sc_serverurl;

#define TITLE_SET_INTERVAL	10
#define TITLE_SET_TIMEOUT	5

#define RECONNECT_INTERVAL	15

NJCast::NJCast(NJClient *_client) {
  client = _client;
  state = -1;
  conn = NULL;
  encoder = NULL;

  reconnect_timer = 0;

  last_titleset = 0;
  titleset = NULL;
  titleset_began = 0;
}

NJCast::~NJCast() {
  Disconnect();
  delete titleset;
}

int NJCast::Connect(char *servername, int port) {
  Disconnect();

  sc_address.Set(servername);
  sc_port = port;

  state = CONNECTING;

  return 1;
}

void NJCast::Disconnect() {
  delete conn; conn = NULL;
  delete encoder; encoder = NULL;
  delete titleset; titleset = NULL;
  state = -1;
}

int NJCast::sending() {
  return (state == SENDDATA);
}

int NJCast::Run() {

  int work_done=0;

  if (conn) {
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
        if (state != RECONNECT) {
          printf("connection fuct\n");

          // reset
          delete conn; conn = NULL;
          delete encoder; encoder = NULL;

          // reconnect after an interval
          state = RECONNECT;
          reconnect_timer = time(NULL);
        }
      break;
    }
  }

  switch (state) {
    case CONNECTING: {
      conn = new JNL_Connection(JNL_CONNECTION_AUTODNS,65536,65536);
      conn->connect(sc_address.Get(), sc_port+1);

      char buf[4096];
      sprintf(buf, "%s\r\n", g_sc_pass);
      // send pw
      if (conn->send_string(buf) < 0) {
printf("send pass fail\n");
        return 0;	// try again
      }
      state = WAITFOROK;
//printf("->WAITFOROK\n");
    }
    break;
    case WAITFOROK: {
      char buf[4096];
//FUCKO timeout
//CUT printf("where's my ok\n");
      int avail = conn->recv_lines_available();
      if (conn->recv_lines_available()<1) return 0;	// try again
      conn->recv_line(buf, 4095);
      buf[4095] = 0;
//CUT printf("got line '%s'\n", buf);
      if (strcmp(buf, "OK2")) {
//FUCKO log error
printf("couldn't log into sc server\n");
        state = RECONNECT;
        return 0;
      }
      state = SENDSTREAMINFO;
//printf("->SENDSTREAMINFO\n");
    }
    break;
    case SENDSTREAMINFO: {
      WDL_String info;
      info.Append("icy-name:"); info.Append(g_servername); info.Append("\r\n");
      info.Append("icy-genre:"); info.Append(g_sc_servergenre); info.Append("\r\n");
      info.Append("icy-pub:"); info.Append(g_sc_serverpub); info.Append("\r\n");
      char b[512];
      sprintf(b, "%d", g_bitrate);
      info.Append("icy-br:"); info.Append(b); info.Append("\r\n");
      info.Append("icy-url:"); info.Append(g_sc_serverurl); info.Append("\r\n");
      info.Append("\r\n");
      if (conn->send_bytes_available() < (int)strlen(info.Get())) return 0;// try again
      conn->send_string(info.Get());
      state = SENDDATA;	// woot
//printf("->SENDDATA\n");
    }
    break;
    case SENDDATA: {
      if (encoder == NULL) return 0;  // not jet

      // push whatever we have
      for (;;) {
        int send_avail = conn->send_bytes_available();
        int avail_to_send = encoder->outqueue.GetSize();
        int nbytes = min(send_avail, avail_to_send);
//if (nbytes > 0) printf("availtosend %d, nbytes %d\n", avail_to_send, nbytes);
        if (nbytes > 0) {
          conn->send_bytes(encoder->outqueue.Get(), nbytes);
          encoder->outqueue.Advance(nbytes);
          encoder->outqueue.Compact();
          work_done=1;
          conn->run();	// flush them bytes ASAP
        } else break;
      }
      handleTitleSetting();
    }
    break;
    case RECONNECT: {
      time_t now = time(NULL);
      if (now - reconnect_timer >= RECONNECT_INTERVAL) {
        state = CONNECTING;	// off we go
printf("time to reconnect...\n");
      }
    }
    break;
  }

  return work_done;
}

void NJCast::AudioProc(float **inbuf, int innch, float **outbuf, int outnch, int len, int srate) {
  if (state != SENDDATA) return;	// not ready

  if (encoder == NULL)
    encoder = new LameEncoder(srate, outnch, g_bitrate);

  if (client->GetLoopCount() <= 0) return;	// not ready

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

  if (encoder->outqueue.GetSize() > 512*1024) // 512KB in buffer, doh!
  {
    encoder->outqueue.Advance(256*1024); // flush out 256kb
    encoder->outqueue.Compact(); 

  }
#endif
}

// handle title setting
void NJCast::handleTitleSetting() {
  int now = time(NULL);

  if (last_titleset == 0) {
    last_titleset = now;
    return;
  }

  if (titleset == NULL) {
    if (now - last_titleset > TITLE_SET_INTERVAL) {
      WDL_String url;
      url.Append("http://");
      url.Append(g_sc_address);
      url.Append(":");
      char portn[512];
      sprintf(portn, "%d", g_sc_port);
      url.Append(portn);
      url.Append("/admin.cgi?pass=");
      url.Append(g_sc_pass);
      url.Append("&mode=updinfo&song=");
      int n = client->GetNumUsers(), needcomma=0;
      for (int i = 0; i < n; i++) {
        char *username = client->GetUserState(i);
        WDL_String un(username);
        char *pt = strchr(un.Get(), '@');
        if (pt) *pt = NULL;	// cut off @ip
        if (needcomma) url.Append(",%20");
        // encode username properly -- turn space into %20
        for (pt = un.Get(); *pt; pt++) {
          if (*pt == ' ') url.Append("%20");
          else {
            char cbuf[512];
            sprintf(cbuf, "%c", *pt);
            url.Append(cbuf);
          }
        }
        needcomma = 1;
      }
      if (n == 0) {
        url.Append("No%20users.");
      }

      if (strcmp(url.Get(), last_title_url.Get())) {
        titleset = new JNL_HTTPGet();
        titleset->addheader("User-Agent:Ninjamcast (Mozilla)");
        titleset->addheader("Accept:*/*");
        titleset->connect(url.Get());
        last_title_url.Set(url.Get());
        titleset_began = now;
      } else {
        last_titleset = now;
      }
    }
  } else {
    int r = titleset->run();
    if (r == -1 || r == 1 || now - titleset_began > TITLE_SET_TIMEOUT) {
      delete titleset; titleset = 0;
      last_titleset = now;
    }
  }
}
