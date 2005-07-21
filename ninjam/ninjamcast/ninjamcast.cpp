/*
  ninjamcast! a Ninjam->Shoutcast gateway.
  by Brennan Underwood
  adapted from cmdclient by Justin Frankel
*/
#ifdef _WIN32
#include <windows.h>
#else
#include <stdlib.h>
#include <memory.h>
#endif

#include <stdio.h>
#include <math.h>
#include <signal.h>
#include <float.h>

#include "../../wdl/dirscan.h"
#include "../../WDL/lineparse.h"

#include "../njclient.h"

#include "njcast.h"

NJClient *g_client;
NJCast *g_njcast;

// configurable stuff
int g_srate = 44100;
int g_bitrate = 64;
int g_numchannels = 1;

char g_sc_streamname[4096]="Ninjam";
char g_sc_address[4096]="brainio.badmofo.org";
int g_sc_port = 8000;
char g_sc_pass[4096]="changeme";
char g_sc_genre[4096]="ninjam";
int g_sc_public=1;
char g_sc_url[4096]="http://ninjam.com/";
int g_sc_reconnect_interval=15;

char g_nj_address[4096]="";	//
char g_nj_user[4096]="njcast";	//
char g_nj_pass[4096]="";	//
int g_nj_titlesetinterval=10;
int g_nj_reconnect_interval=20;
float g_nj_mastervolume=0.0;	// in dB

// end configurable

int waiting_to_reconnect = 0;
time_t waiting_to_reconnect_since = 0;

void doSamples() {
  static __int64 samples_out; // where we are, in samples

  static DWORD start_time;
  if (start_time == 0) start_time = GetTickCount();

  // where we should be, in samples
  __int64 sample_pos = ((__int64)(GetTickCount()-start_time) * g_srate) / 1000i64;

  int block_size=1024; // chunks of 1024 samples at a time
//  int block_size=4096; // chunks of 1024 samples at a time


  WDL_HeapBuf tmp1;
  tmp1.Resize(block_size*sizeof(float)*g_numchannels);

  while (sample_pos >= samples_out + block_size)
  {
    float *inbufs[1]={NULL};
    float *outbufs[2]={(float*)tmp1.Get(),((float *)tmp1.Get()) + block_size};

    g_client->AudioProc(inbufs, 0, outbufs, g_numchannels, block_size, g_srate);
    g_njcast->AudioProc(inbufs, 0, outbufs, g_numchannels, block_size, g_srate);

    samples_out += block_size;

    // keep up! (seems to help)
    sample_pos = ((__int64)(GetTickCount()-start_time) * g_srate) / 1000i64;
  }
}




int g_done=0;

void sigfunc(int sig)
{
  printf("Got Ctrl+C\n");
  g_done++;
}


void usage()
{

  printf("Usage: ninjam hostname [options]\n"
    "Options:\n"
    "  -sessiondir <path>\n"
    "  -user <username>\n"
    "  -pass <password>\n"
);

  exit(1);
}

// heh we don't upload anything anyway
int displayLicense(int user32, char *licensetext) {
  return 1;
}

static int ConfigOnToken(LineParser *lp)
{
  const char *t=lp->gettoken_str(0);
  if (!stricmp(t,"Mp3_Samplerate")) {
    if (lp->getnumtokens() != 2) return -1;
    int p=lp->gettoken_int(1);
    if (!p) return -2;
    g_srate = p;
  } else
  if (!stricmp(t,"Mp3_Bitrate")) {
    if (lp->getnumtokens() != 2) return -1;
    int p=lp->gettoken_int(1);
    if (!p) return -2;
    g_bitrate = p;
  } else
  if (!stricmp(t,"Mp3_Channels")) {
    if (lp->getnumtokens() != 2) return -1;
    int p=lp->gettoken_int(1);
    if (!(p == 1 || p == 2)) return -2;
    g_numchannels = p;
  } else

  if (!stricmp(t,"SC_Server_Name")) {
    if (lp->getnumtokens() != 2) return -1;
    char *p=lp->gettoken_str(1);
    if (!p || !*p) return -2;
    strncpy(g_sc_streamname, p, sizeof(g_sc_streamname)-1);
  } else
  if (!stricmp(t,"SC_Server_Address")) {
    if (lp->getnumtokens() != 2) return -1;
    char *p=lp->gettoken_str(1);
    if (!p || !*p) return -2;
    strncpy(g_sc_address, p, sizeof(g_sc_address)-1);
  } else
  if (!stricmp(t,"SC_Server_Port")) {
    if (lp->getnumtokens() != 2) return -1;
    int p=lp->gettoken_int(1);
    if (p <= 0) return -2;
    g_sc_port = p;
  } else
  if (!stricmp(t,"SC_Server_Password")) {
    if (lp->getnumtokens() != 2) return -1;
    char *p=lp->gettoken_str(1);
    if (!p || !*p) return -2;
    strncpy(g_sc_pass, p, sizeof(g_sc_pass)-1);
  } else
  if (!stricmp(t,"SC_Server_Genre")) {
    if (lp->getnumtokens() != 2) return -1;
    char *p=lp->gettoken_str(1);
    if (!p || !*p) return -2;
    strncpy(g_sc_genre, p, sizeof(g_sc_genre)-1);
  } else
  if (!stricmp(t,"SC_Server_Public")) {
    if (lp->getnumtokens() != 2) return -1;
    int p=!!lp->gettoken_int(1);
//    if (p < 0) return -2;
    g_sc_public = p;
  } else
  if (!stricmp(t,"SC_Server_Url")) {
    if (lp->getnumtokens() != 2) return -1;
    char *p=lp->gettoken_str(1);
    if (!p || !*p) return -2;
    strncpy(g_sc_url, p, sizeof(g_sc_url)-1);
  } else
  if (!stricmp(t,"SC_Reconnect_Interval")) {
    if (lp->getnumtokens() != 2) return -1;
    int p=lp->gettoken_int(1);
    if (p <= 0) return -2;
    g_sc_reconnect_interval = p;
  } else
  if (!stricmp(t,"NJ_Reconnect_Interval")) {
    if (lp->getnumtokens() != 2) return -1;
    int p=lp->gettoken_int(1);
    if (p <= 0) return -2;
    g_nj_reconnect_interval = p;
  } else
  if (!stricmp(t,"NJ_Title_Set_Interval")) {
    if (lp->getnumtokens() != 2) return -1;
    int p=lp->gettoken_int(1);
    if (p <= 0) return -2;
    g_nj_titlesetinterval = p;
  } else
  if (!stricmp(t,"NJ_Address")) {
    if (lp->getnumtokens() != 2) return -1;
    char *p=lp->gettoken_str(1);
    if (!p || !*p) return -2;
    strncpy(g_nj_address, p, sizeof(g_nj_address)-1);
  } else
  if (!stricmp(t,"NJ_User")) {
    if (lp->getnumtokens() != 2) return -1;
    char *p=lp->gettoken_str(1);
    if (!p || !*p) return -2;
    strncpy(g_nj_user, p, sizeof(g_nj_user)-1);
  } else
  if (!stricmp(t,"NJ_Password")) {
    if (lp->getnumtokens() != 2) return -1;
    char *p=lp->gettoken_str(1);
    if (!p || !*p) return -2;
    strncpy(g_nj_pass, p, sizeof(g_nj_pass)-1);
  } else
  return -3;

  return 0;
}

static void readConfig() {
const char *configfile="njcast.cfg";
  FILE *fp=strcmp(configfile,"-")?fopen(configfile,"rt"):stdin; 

  if (fp != NULL) {
    int linecnt=0;
    bool comment_state=0;
    char buf[8192];
    WDL_String linebuild;
    while (fgets(buf, sizeof(buf), fp)) {
      linecnt++;
//CUT      if (!buf[0]) break;
      if (buf[strlen(buf)-1]=='\n') buf[strlen(buf)-1]=0;

      LineParser lp(comment_state);

      if (buf[0] && buf[strlen(buf)-1]=='\\')
      {
        buf[strlen(buf)-1]=0;
        linebuild.Append(buf);
        continue;
      }
      linebuild.Append(buf);
      int res=lp.parse(linebuild.Get());
      linebuild.Set("");
      if (res)
      {
        if (res==-2) 
        {
//          if (g_logfp) logText("[config] warning: unterminated string parsing line %d of %s\n",linecnt,configfile);
          printf("[config] warning: unterminated string parsing line %d of %s\n",linecnt,configfile);
        }
        else 
        {
//          if (g_logfp) logText("[config] warning: error parsing line %d of %s\n",linecnt,configfile);
          printf("[config] warning: error parsing line %d of %s\n",linecnt,configfile);
        }
      }
      else
      {
        comment_state = lp.InCommentBlock();

        if (lp.getnumtokens()>0)
        {
          int err=ConfigOnToken(&lp);
          if (err)
          {
            if (err == -1)
            {
//              if (g_logfp) logText("[config] warning: wrong number of tokens on line %d of %s\n",linecnt,configfile);
              printf("[config] warning: wrong number of tokens on line %d of %s\n",linecnt,configfile);
            }
            if (err == -2)
            {
//              if (g_logfp) logText("[config] warning: invalid parameter on line %d of %s\n",linecnt,configfile);
              printf("[config] warning: invalid parameter on line %d of %s\n",linecnt,configfile);
            }
            if (err == -3)
            {
//              if (g_logfp) logText("[config] warning: invalid config command \"%s\" on line %d of %s\n",lp.gettoken_str(0),linecnt,configfile);
              printf("[config] warning: invalid config command \"%s\" on line %d of %s\n",lp.gettoken_str(0),linecnt,configfile);
            }
          }
        }
      }
    }
    
    if (fp != stdin) fclose(fp);
  }
}

int main(int argc, char **argv)
{
  signal(SIGINT,sigfunc);

  char *parmuser=NULL;
  char *parmpass=NULL;
  WDL_String sessiondir;

  float monitor=1.0;
  g_client=new NJClient;

#define DB2VAL(x) (pow(2.0,(x)/6.0))
  g_client->config_mastervolume = (float)DB2VAL(g_nj_mastervolume);

  g_client->LicenseAgreementCallback=displayLicense;

  g_njcast = new NJCast(g_client);

#if 0
  if (argc < 2) usage();
  {
    int p;
    for (p = 2; p < argc; p++)
    {
#if 0//CUT
      if (!stricmp(argv[p],"-localchannels"))
      {
        if (++p >= argc) usage();
        localchannels=atoi(argv[p]);
        if (localchannels < 0 || localchannels > 2)
        {
          printf("Error: -localchannels must have parameter [0..2]\n");
          return 1;
        }
      }
      else if (!stricmp(argv[p],"-savelocalwavs"))
      {
        g_client->config_savelocalaudio=2;     
      }
      else if (!stricmp(argv[p],"-norecv"))
      {
        g_client->config_autosubscribe=0;
      }
      else if (!stricmp(argv[p],"-nosavelocal"))
      {
        g_client->config_savelocalaudio=0;
      }
      else if (!stricmp(argv[p],"-nostatus"))
      {
        nostatus=1;
      }
      else if (!stricmp(argv[p],"-monitor"))
      {
        if (++p >= argc) usage();
        monitor=(float)pow(2.0,atof(argv[p])/6.0);
      }
      else if (!stricmp(argv[p],"-metronome"))
      {
        if (++p >= argc) usage();
        g_client->config_metronome=(float)pow(2.0,atof(argv[p])/6.0);
      }
      else if (!stricmp(argv[p],"-debuglevel"))
      {
        if (++p >= argc) usage();
        g_client->config_debug_level=atoi(argv[p]);
      }
      else if (!stricmp(argv[p],"-audiostr"))
      {
        if (++p >= argc) usage();
        audioconfigstr=argv[p];
      }
#endif
      if (!stricmp(argv[p],"-user"))
      {
        if (++p >= argc) usage();
        parmuser=argv[p];
      }
      else if (!stricmp(argv[p],"-pass"))
      {
        if (++p >= argc) usage();
        parmpass=argv[p];
      }
#if 0//CUT
      else if (!stricmp(argv[p],"-nowritewav"))
      {
        nowav++;
      }
      else if (!stricmp(argv[p],"-nowritelog"))
      {
        nolog++;
      }
#endif
#if 0//CUT
#ifdef _WIN32
      else if (!stricmp(argv[p],"-jesusonic"))
      {
        if (++p >= argc) usage();
        jesusdir=argv[p];
      }
#endif
#endif
      if (!stricmp(argv[p],"-sessiondir"))
      {
        if (++p >= argc) usage();
        sessiondir.Set(argv[p]);
      }
//      else usage();
    }
  }


  char passbuf[512]="";
  char userbuf[512]="";
  if (!parmuser)
  {
    parmuser=userbuf;
    printf("Enter username: ");
    gets(userbuf);
  }
  if (!parmpass)
  {
    parmpass=passbuf;
    if (strncmp(parmuser,"anonymous",9)||(parmuser[9] && parmuser[9] != ':'))
    {
      printf("Enter password: ");
      gets(passbuf);
    }
  }
#endif

  g_client->config_savelocalaudio=-1;	// -1 means clean up after yourself

  // read config file
  readConfig();

  g_client->config_metronome = 0;
  g_client->config_metronome_mute = 1;

// go!

  JNL::open_socketlib();

//  printf("Connecting to Ninjam server %s...\n",argv[1]);
//printf("user: '%s', pass: '%s'\n", parmuser, parmpass);
  g_client->Connect(g_nj_address, g_nj_user, g_nj_pass);

//  printf("Connecting to shoutcast server %s...\n",g_sc_address);
  g_njcast->Connect(g_sc_address, g_sc_port);

  if (!sessiondir.Get()[0])
  {
    char buf[512];
#ifdef _WIN32
    SYSTEMTIME st;
    GetLocalTime(&st);
    
    int cnt=0;
    for (;;)
    {
      wsprintf(buf,"njsession_%02d%02d%02d_%02d%02d%02d_%d",st.wYear%100,st.wMonth,st.wDay,st.wHour,st.wMinute,st.wSecond,cnt);

      if (CreateDirectory(buf,NULL)) break;

      cnt++;
    }
#else
    int cnt=0;
    for (;;)
    {
      sprintf(buf,"njsession_%d_%d",time(NULL),cnt);

      if (!mkdir(buf,0700)) break;

      cnt++;
    }
#endif
    
    if (cnt > 10)
    {
      printf("Error creating session directory\n");
      buf[0]=0;
    }
      
    sessiondir.Set(buf);
  }
  else
#ifdef _WIN32
    CreateDirectory(sessiondir.Get(),NULL);
#else
    mkdir(sessiondir.Get(),0700);
#endif
  if (sessiondir.Get()[0] && sessiondir.Get()[strlen(sessiondir.Get())-1]!='\\' && sessiondir.Get()[strlen(sessiondir.Get())-1]!='/')
#ifdef _WIN32
    sessiondir.Append("\\");
#else
    sessiondir.Append("/");
#endif

  g_client->SetWorkDir(sessiondir.Get());

  while (!g_done) {

    time_t now = time(NULL);
    if (waiting_to_reconnect) {
      if (now - waiting_to_reconnect_since >= g_nj_reconnect_interval) {
        waiting_to_reconnect = 0;
        g_client->Connect(argv[1],parmuser,parmpass);
      }
    } else if (g_client->GetStatus() < 0) {	// krud, conne
      if (!waiting_to_reconnect) {
  printf("NJ connection fuct\n");
        g_client->Disconnect();
        waiting_to_reconnect = 1;
        waiting_to_reconnect_since = now;
      }
    } else {
      while (!g_client->Run());

      // get some more bits
      if (g_njcast->sending()) doSamples();

    }

    // push bits to server!
    if (!g_njcast->Run()) // if no work done, sleep
    {

#ifdef _WIN32
      Sleep(1);
#else
      struct timespec ts={0,1000*1000};
      nanosleep(&ts,NULL);
#endif
    }
  }
  printf("exiting on status %d\n",g_client->GetStatus());


  printf("Shutting down\n");

  delete g_client->waveWrite;
  g_client->waveWrite=0;

  delete g_njcast;

  delete g_client;

  JNL::close_socketlib();

  // delete the sessiondir
  if (sessiondir.Get()) {
//printf("sessiondir: '%s'\n", sessiondir.Get());
    for (int i = 0; i < 16; i++) {
      WDL_String subdir = sessiondir;
      char buf[512];
      sprintf(buf, "%x", i);
      subdir.Append(buf);
//printf("subdir: '%s'\n", subdir.Get());
      {
        WDL_DirScan ds;
        if (!ds.First(subdir.Get())) {
          do {
            if (ds.GetCurrentFN()[0] != '.') {
              WDL_String t;
              ds.GetCurrentFullFN(&t);
//printf("unlink: %s\n", t.Get());
              unlink(t.Get());          
            }
          } while (!ds.Next());
        }
      }//grr... have to destroy the dirscan or it keeps the dir open
#ifdef _WIN32
      int r = RemoveDirectory(subdir.Get());
//printf("Result: %d, %d\n", r, GetLastError());
#else
      rmdir(subdir.Get());
#endif
//printf("rmdir: '%s'\n", subdir.Get());
    }

    for (char *pt = sessiondir.Get(); *pt; pt++) { }	// go to last
    if (pt > sessiondir.Get()) pt[-1] = 0;	// kill last char

#ifdef _WIN32
    RemoveDirectory(sessiondir.Get());
#else
    rmdir(sessiondir.Get());
#endif
//printf("rm top dir %s\n", sessiondir.Get());
  }

  return 0;
}
