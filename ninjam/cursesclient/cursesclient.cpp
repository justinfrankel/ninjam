#include <windows.h>
#include <stdio.h>
#include <math.h>
#include <signal.h>
#include <float.h>

#include "../audiostream_asio.h"
#include "../njclient.h"

#include "../../jesusonic/jesusonic_dll.h"

#include "cursesclientinst.h"

void ninjamCursesClientInstance::Run()
{
}


// jesusonic stuff
void *myInst;
jesusonicAPI *JesusonicAPI;  
HINSTANCE hDllInst;


extern char *get_asio_configstr(char *inifile, int wantdlg);
audioStreamer *g_audio;
NJClient *g_client;


void audiostream_onunder() { }
void audiostream_onover() { }

void audiostream_onsamples(float *buf, int len, int nch) 
{ 
  if (g_client->IsAudioRunning())
  {
    if (JesusonicAPI && myInst)
    {
      _controlfp(_RC_CHOP,_MCW_RC);
      JesusonicAPI->jesus_process_samples(myInst,(char*)buf,len*nch*sizeof(float));
      JesusonicAPI->osc_run(myInst,(char*)buf,len*nch*sizeof(float));
    }
  }
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
    "  -localchannels [0..2]\n"
    "  -norecv\n"
    "  -sessiondir <path>\n"
    "  -nosavelocal | -savelocalwavs\n"
    "  -user <username>\n"
    "  -pass <password>\n"
    "  -monitor <level in dB>\n"
    "  -metronome <level in dB>\n"
    "  -debuglevel [0..2]\n"
    "  -nowritewav\n"
    "  -nowritelog\n"
    "  -audiostr 0:0,0:0,0\n"
    "  -jesusonic <path to jesusonic root dir>\n");

  exit(1);
}

int main(int argc, char **argv)
{
  signal(SIGINT,sigfunc);

  char *parmuser=NULL;
  char *parmpass=NULL;
  char *jesusdir=NULL;
  WDL_String sessiondir;
  int localchannels=1,nostatus=0, nowav=0, nolog=0;

  float monitor=1.0;
  printf("Ninjam v0.004 command line test client, Copyright (C) 2004-2005 Cockos, Inc.\n");
  char *audioconfigstr=NULL;
  g_client=new NJClient;
  g_client->config_savelocalaudio=1;

  if (argc < 2) usage();
  {
    int p;
    for (p = 2; p < argc; p++)
    {
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
      else if (!stricmp(argv[p],"-user"))
      {
        if (++p >= argc) usage();
        parmuser=argv[p];
      }
      else if (!stricmp(argv[p],"-pass"))
      {
        if (++p >= argc) usage();
        parmpass=argv[p];
      }
      else if (!stricmp(argv[p],"-nowritewav"))
      {
        nowav++;
      }
      else if (!stricmp(argv[p],"-nowritelog"))
      {
        nolog++;
      }
      else if (!stricmp(argv[p],"-jesusonic"))
      {
        if (++p >= argc) usage();
        jesusdir=argv[p];
      }
      else if (!stricmp(argv[p],"-sessiondir"))
      {
        if (++p >= argc) usage();
        sessiondir.Set(argv[p]);
      }
      else usage();
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
    if (strcmp(parmuser,"anonymous"))
    {
      printf("Enter password: ");
      gets(passbuf);
    }
  }

  {
    audioStreamer_ASIO *audio;
    char *dev_name_in;
    
    dev_name_in=audioconfigstr?audioconfigstr:get_asio_configstr("ninjam.ini",1);
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


  // jesusonic init

  WDL_String jesusonic_configfile;
  if (jesusdir)
  {
    jesusonic_configfile.Set(jesusdir);
    jesusonic_configfile.Append("\\cmdclient.jesusonicpreset");
    WDL_String dll;
    dll.Set(jesusdir);
    dll.Append("\\jesus.dll");

    hDllInst = LoadLibrary(dll.Get());
    if (!hDllInst) hDllInst = LoadLibrary(".\\jesus.dll"); // load from current dir
    if (hDllInst) 
    {
      *(void **)(&JesusonicAPI) = (void *)GetProcAddress(hDllInst,"JesusonicAPI");
      if (JesusonicAPI && JesusonicAPI->ver == JESUSONIC_API_VERSION_CURRENT)
      {
        myInst=JesusonicAPI->createInstance();
        JesusonicAPI->set_rootdir(myInst,jesusdir);
        JesusonicAPI->ui_init(myInst);
        JesusonicAPI->set_opts(myInst,1,1,-1);
        JesusonicAPI->set_sample_fmt(myInst,g_audio->m_srate,g_audio->m_nch,33);
        JesusonicAPI->set_status(myInst,"","ninjam embedded");
        HWND h=JesusonicAPI->ui_wnd_create(myInst);
        ShowWindow(h,SW_SHOWNA);
        SetTimer(h,1,40,NULL);

        JesusonicAPI->preset_load(myInst,jesusonic_configfile.Get());
      }
    }
  }

  // end jesusonic init

  if (localchannels>0)
  {
    g_client->SetLocalChannelInfo(0,"channel0",true,0,false,0,true,true);
    g_client->SetLocalChannelMonitoring(0,true,monitor,false,0.0,false,false);

    if (localchannels>1)
    {
      g_client->SetLocalChannelInfo(1,"channel1",true,1,false,0,true,true);
      g_client->SetLocalChannelMonitoring(1,true,monitor,false,0.0,false,false);
    }

  }




  printf("Connecting to %s...\n",argv[1]);
  g_client->Connect(argv[1],parmuser,parmpass);


  if (!sessiondir.Get()[0])
  {
    SYSTEMTIME st;
    GetLocalTime(&st);
    char buf[512];
    
    int cnt=0;
    for (;;)
    {
      wsprintf(buf,"njsession_%02d%02d%02d_%02d%02d%02d_%d",st.wYear%100,st.wMonth,st.wDay,st.wHour,st.wMinute,st.wSecond,cnt);

      if (CreateDirectory(buf,NULL)) break;

      cnt++;
    }
    
    if (cnt > 10)
    {
      printf("Error creating session directory\n");
      buf[0]=0;
    }
      
    sessiondir.Set(buf);
  }
  else
    CreateDirectory(sessiondir.Get(),NULL);
  if (sessiondir.Get()[0] && sessiondir.Get()[strlen(sessiondir.Get())-1]!='\\' && sessiondir.Get()[strlen(sessiondir.Get())-1]!='/')
    sessiondir.Append("\\");

  g_client->SetWorkDir(sessiondir.Get());

  if (!nowav)
  {
    WDL_String wf;
    wf.Set(sessiondir.Get());
    wf.Append("output.wav");
    g_client->waveWrite = new WaveWriter(wf.Get(),24,g_audio->m_nch,g_audio->m_srate);
  }
  if (!nolog)
  {
    WDL_String lf;
    lf.Set(sessiondir.Get());
    lf.Append("clipsort.log");
    g_client->SetLogFile(lf.Get());
  }

  int lastloopcnt=0;
  int statuspos=0;
  while (g_client->GetStatus() >= 0 && !g_done)
  {
    if (g_client->Run()) 
    {
      MSG msg;
      while (PeekMessage(&msg,NULL,0,0,PM_REMOVE))
      {
        TranslateMessage(&msg);
        DispatchMessage(&msg);
      }
      Sleep(1);
      if (g_client->HasUserInfoChanged())
      {
        printf("\nUser, channel list:\n");
        int us=0;
        for(;;)
        {
          char *un=g_client->GetUserState(us);
          if (!un) break;
          printf(" %s\n",un);
          int i;
          for (i=0;;i++)
          {
            bool sub;
            int ch=g_client->EnumUserChannels(us,i++);
            if (ch<0) break;

            char *cn=g_client->GetUserChannelState(us,ch,&sub);
            if (!cn) break;
            printf("    %d: \"%s\" subscribed=%d\n",ch,cn,sub?1:0);
            ch++;
          }
          us++;
        }
        if (!us) printf("  <no users>\n");
      }
      if (!nostatus)
      {
        int lc=g_client->GetLoopCount();
        if (lastloopcnt != lc)
        {
          int l;
          g_client->GetPosition(NULL,&l);
          lastloopcnt = lc;

          printf("\rEntering interval %d (%d samples, %.2f bpm)%20s\n",lc, l,g_client->GetActualBPM(),"");
          printf("[%28s|%29s]\r[","","");
          statuspos=0;
        }
        else
        {
          int l,p;
          g_client->GetPosition(&p,&l);
          p *= 60;
          p/=l;
          while (statuspos<p)
          {
            statuspos++;
            printf("#");
          }       
        }
      }
    }

  }
  printf("exiting on status %d\n",g_client->GetStatus());


  printf("Shutting down\n");

  delete g_audio;


  delete g_client->waveWrite;
  g_client->waveWrite=0;


  delete g_client;


  ///// jesusonic stuff
  if (myInst) 
  {
    JesusonicAPI->preset_save(myInst,jesusonic_configfile.Get());
    JesusonicAPI->ui_wnd_destroy(myInst);
    JesusonicAPI->set_opts(myInst,-1,-1,1);
    //WaitForSingleObject(hUIThread,1000);
    //CloseHandle(hUIThread);
    JesusonicAPI->ui_quit(myInst);
//    m_hwnd=0;

    JesusonicAPI->destroyInstance(myInst);
  }
  if (hDllInst) FreeLibrary(hDllInst);
  hDllInst=0;
  JesusonicAPI=0;
  myInst=0;


  JNL::close_socketlib();
  return 0;
}
