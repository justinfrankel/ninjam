#include <windows.h>
#include <stdio.h>
#include <math.h>
#include <signal.h>

#include "../audiostream.h"
#include "../njclient.h"


extern char *get_asio_configstr();
audioStreamer *g_audio;
NJClient *g_client;


void audiostream_onunder() { }
void audiostream_onover() { }

void audiostream_onsamples(float *buf, int len, int nch) 
{ 
  g_client->AudioProc(buf,len,nch,g_audio->m_srate);
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
    "  -nosend\n"
    "  -norecv\n"
    "  -savelocal\n"
    "  -user <username>\n"
    "  -monitor <level in dB>\n"
    "  -metronome <level in dB>\n"
    "  -debuglevel [0..2]\n"
    "  -audiostr 0:0,0:0,0\n");

  exit(1);
}

int main(int argc, char **argv)
{
  signal(SIGINT,sigfunc);

  char *parmuser=NULL;

  printf("Ninjam v0.0 command line client starting up...\n");
  char *audioconfigstr=NULL;
  g_client=new NJClient;

  if (argc < 2) usage();
  {
    int p;
    for (p = 2; p < argc; p++)
    {
      if (!stricmp(argv[p],"-nosend"))
      {
        g_client->config_send=0;
      }
      else if (!stricmp(argv[p],"-norecv"))
      {
        g_client->config_recv=0;
      }
      else if (!stricmp(argv[p],"-savelocal"))
      {
        g_client->config_savelocalaudio=1;
      }
      else if (!stricmp(argv[p],"-monitor"))
      {
        if (++p >= argc) usage();
        g_client->config_monitor=(float)pow(2.0,atof(argv[p])/6.0);
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
      else if (!stricmp(argv[p],"-user"))
      {
        if (++p >= argc) usage();
        parmuser=argv[p];
      }
      else usage();
    }
  }
  char passbuf[512]="gay";
  char userbuf[512];
  if (!parmuser)
  {
    parmuser=userbuf;
    printf("Enter username: ");
    gets(userbuf);
  }

  {
    audioStreamer_ASIO *audio;
    char *dev_name_in;
    
    dev_name_in=audioconfigstr?audioconfigstr:get_asio_configstr();
    audio=new audioStreamer_ASIO;

    int nbufs=2,bufsize=4096;
    if (audio->Open(&dev_name_in,48000,2,16,-1,&nbufs,&bufsize))
    {
      printf("Error opening audio!\n");
      return 0;
    }
    printf("Opened %s (%dHz %dch %dbps)\n",dev_name_in,
      audio->m_srate, audio->m_nch, audio->m_bps);
    g_audio=audio;
  }

  JNL::open_socketlib();

  printf("Connecting to %s...\n",argv[1]);
  g_client->Connect(argv[1],userbuf,passbuf);


  while (g_client->GetStatus() >= 0 && !g_done)
  {
    if (g_client->Run()) Sleep(1);
  }
  printf("exiting on status %d\n",g_client->GetStatus());


  printf("Shutting down\n");

  delete g_client;
  delete g_audio;

  JNL::close_socketlib();
  return 0;
}
