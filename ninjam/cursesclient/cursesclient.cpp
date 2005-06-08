#define CURSES_INSTANCE (&m_cursinst)

#include <windows.h>
#include <stdio.h>
#include <math.h>
#include <signal.h>
#include <float.h>

#include "../audiostream_asio.h"
#include "../njclient.h"

#include "../../jesusonic/jesusonic_dll.h"

#include "curses.h"
#include "cursesclientinst.h"



#define VALIDATE_TEXT_CHAR(thischar) ((isspace(thischar) || isgraph(thischar)) && (thischar) < 256)
#ifdef _WIN32
#define getch() curses_getch(CURSES_INSTANCE)
#define erase() curses_erase(CURSES_INSTANCE)
#endif

void ninjamCursesClientInstance::Run()
{
}


ninjamCursesClientInstance m_cursinst;
int color_map[8];
int g_done=0;
int g_ui_state=0;
int g_ui_locrename_ch;
int g_ui_voltweakstate_channel;
char m_lineinput_str[120];

double g_ilog2x6;
double VAL2DB(double x)
{
  double v=(log10(x)*g_ilog2x6);
  if (v < -120.0) v=-120.0;
  return v;
}
#define DB2VAL(x) (pow(2.0,(x)/6.0))
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


int g_sel_x, g_sel_y;

#define COLORMAP(x) color_map[x]

void mkvolpanstr(char *str, double vol, double pan)
{
  double v=VAL2DB(vol);
  if (vol < 0.0000001 || v < -120.0) v=-120.0;
    sprintf(str,"%2.1fdB ",v);   
    if (fabs(pan) < 0.0001) strcat(str,"center");
    else sprintf(str+strlen(str),"%d%%%s", (int)fabs(pan*100.0),(pan>0.0 ? "R" : "L"));
}


// highlights shit in []
void highlightoutline(int line, char *str, int attrnorm, int bknorm, int attrhi, int bkhi, int attrsel, int bksel, int whl)
{
  int state=0;
  int l=COLS-1;
  int lcol=0;
  move(line,0);
	attrset(attrnorm);
	bkgdset(bknorm);

  while (*str && l-- > 0)
  {
    if (*str == ']')
    {
      if (state)
      {
	      attrset(attrnorm);
    	  bkgdset(bknorm);
        state=0;
      }
    }
    addch(*str);
    if (*str == '[')
    {
      if (whl > 0)
      {
        char *tmp=strstr(str,"]");
        if (tmp && !strstr(tmp,"[")) 
        {
          whl=0;
          g_sel_x=lcol;
        }
      }
      if (!state)
      {
        lcol++;
        if (!whl--)
        {
          attrset(attrsel);
          bkgdset(bksel);
        }
        else
        {
          attrset(attrhi);
          bkgdset(bkhi);
        }
        state=1;
      }
    }
    str++;

  }
  if (state)
  {
	  attrset(attrnorm);
	  bkgdset(bknorm);
  }

}


void drawstatusbar()
{
  if (g_ui_state) return;
  int l,p;
  g_client->GetPosition(&p,&l);
  if (!l) return;

	bkgdset(COLORMAP(6));
	attrset(COLORMAP(6));

  move(LINES-2,0);
  p*=(COLS);
  p/=l;
  int x;
  for (x = 0; x < COLS; x ++) addch(x <= p ? '#' : ' ');

	bkgdset(COLORMAP(0));
	attrset(COLORMAP(0));

  move(LINES-1,0);

}

void showmainview(bool action=false)
{
  int selpos=0;
  erase();
	bkgdset(COLORMAP(1));
	attrset(COLORMAP(1));
	mvaddstr(0,0,"LOCAL");
  clrtoeol();
	bkgdset(COLORMAP(0));
	attrset(COLORMAP(0));
  char linebuf[1024];
  int linemax=LINES-2;
  {
    if (action && g_sel_y == selpos)
    {
        g_ui_state=1;        
        g_ui_voltweakstate_channel=g_sel_x == 0 ? -2 : -1;
    }
    sprintf(linebuf,"  master[");
    mkvolpanstr(linebuf+strlen(linebuf),g_client->config_mastervolume,g_client->config_masterpan);

    strcat(linebuf,"] metronome[");
    mkvolpanstr(linebuf+strlen(linebuf),g_client->config_metronome,g_client->config_metronome_pan);
    strcat(linebuf,"]");

    highlightoutline(1,linebuf,COLORMAP(0),COLORMAP(0),
                               COLORMAP(0)|A_BOLD,COLORMAP(0),
                               COLORMAP(5),COLORMAP(5),g_sel_y != selpos++ ? -1 : g_sel_x);
//	  mvaddnstr(1,0,linebuf,COLS-1);
  }
  int x;
  int ypos=2;
  for (x=0;ypos < linemax;x++)
  {
    int a=g_client->EnumLocalChannels(x);
    if (a<0) break;
    int sch;
    bool bc,mute;
    float vol,pan;
    char *name=g_client->GetLocalChannelInfo(a,&sch,NULL,&bc);
    g_client->GetLocalChannelMonitoring(a,&vol,&pan,&mute);

    if (action && g_sel_y == selpos)
    {
      if (g_sel_x == 0)
      {
        g_ui_state=2;
        g_ui_locrename_ch=a;
        strncpy(m_lineinput_str,name,sizeof(m_lineinput_str)-1);
      }
      else if (g_sel_x == 1)
      {
        // toggle active
        g_client->SetLocalChannelInfo(a,NULL,false,0,false,0,true,bc=!bc);
        g_client->NotifyServerOfChannelChange();
      }
      else if (g_sel_x == 2)
      {
        g_ui_state=3;
        g_ui_locrename_ch=a;
        m_lineinput_str[0]  =0;
      }
      else if (g_sel_x == 3)
      {
        // mute
        g_client->SetLocalChannelMonitoring(a,false,0.0f,false,0.0f,true,mute=!mute);
      }
      else if (g_sel_x == 4)
      {
        //volume
        g_ui_state=1;        
        g_ui_voltweakstate_channel=a;
      }
      else if (g_sel_x >= 5)
      {
        g_client->DeleteLocalChannel(a);
        g_client->NotifyServerOfChannelChange();
        x--;
        action=0;
        continue;
        // delete
      }
    }


    char volstr[256];
    mkvolpanstr(volstr,vol,pan);
    sprintf(linebuf,"  [%s] [%c]active [%d]source [%c]mute [%s] [delete] <%2.1fdB>",name,bc?'X':' ',sch,mute?'X':' ',volstr,VAL2DB(g_client->GetLocalChannelPeak(a)));

    highlightoutline(ypos++,linebuf,COLORMAP(0),COLORMAP(0),
                               COLORMAP(0)|A_BOLD,COLORMAP(0),
                               COLORMAP(5),COLORMAP(5),g_sel_y != selpos++ ? -1 : g_sel_x);
  }
  if (ypos < LINES-3)
  {
    if (action && g_sel_y == selpos)
    {
      int x;
      for (x = 0; x < MAX_LOCAL_CHANNELS; x ++)
      {
        if (!g_client->GetLocalChannelInfo(x,NULL,NULL,NULL)) break;
      }
      if (x < MAX_LOCAL_CHANNELS)
      {
        g_client->SetLocalChannelInfo(x,"channel",true,0,false,0,true,false);
        g_client->NotifyServerOfChannelChange();

        char volstr[256];
        mkvolpanstr(volstr,0.0f,0.0f);
        sprintf(linebuf,"  [channel] [ ]active [0]source [ ]mute [%s] [delete]",volstr);

        action=false;
        selpos++;

        highlightoutline(ypos++,linebuf,COLORMAP(0),COLORMAP(0),
                                   COLORMAP(0)|A_BOLD,COLORMAP(0),
                                   COLORMAP(5),COLORMAP(5),g_sel_x);

      }
    }
    if (ypos < LINES-3)
    {
      highlightoutline(ypos++,"  [new channel]",COLORMAP(0),COLORMAP(0),
                                 COLORMAP(0)|A_BOLD,COLORMAP(0),
                                 COLORMAP(5),COLORMAP(5),g_sel_y != selpos++ ? -1 : g_sel_x);
    }
  }
  if (ypos < LINES-3)
  {
	  bkgdset(COLORMAP(6));
	  attrset(COLORMAP(6));
	  mvaddnstr(ypos++,0,"REMOTE",COLS-1);
    clrtoeol();
	  bkgdset(COLORMAP(0));
	  attrset(COLORMAP(0));
  }
  int user=0;
  x=0;
  while (ypos < linemax)
  {
    if (!x) // show user info
    {
      char *name=g_client->GetUserState(user);
      if (!name) break;

	    bkgdset(COLORMAP(4));
	    attrset(COLORMAP(4));
	    mvaddnstr(ypos++,0,name,COLS-1);

      clrtoeol();
	    bkgdset(COLORMAP(0));
	    attrset(COLORMAP(0));
      
    }

    if (ypos >= linemax) break;

    int a=g_client->EnumUserChannels(user,x);
    if (a < 0)
    {
      x=0;
      user++;
      continue;
    }

    float vol,pan;
    bool sub,mute;
    char *name=g_client->GetUserChannelState(user,a,&sub,&vol,&pan,&mute);
    // show channel info


    if (action && g_sel_y == selpos)
    {
      if (g_sel_x == 0)
      {
        // toggle subscribe
        g_client->SetUserChannelState(user,a,true,sub=!sub,false,0.0f,false,0.0f,false,false);
      }
      else if (g_sel_x == 1)
      {
        // toggle mute
        g_client->SetUserChannelState(user,a,false,false,false,0.0f,false,0.0f,true,mute=!mute);
      }
      else if (g_sel_x >= 2)
      {
        // volume
        g_ui_state=1;
        g_ui_voltweakstate_channel=1024+64*user+a;
      }
    }

    char volstr[256];
    mkvolpanstr(volstr,vol,pan);
    sprintf(linebuf,"  \"%s\" [%c]subscribe [%c]mute [%s] <%2.1fdB>",name,sub?'X':' ',mute?'X':' ',volstr,VAL2DB(g_client->GetUserChannelPeak(user,a)));

    highlightoutline(ypos++,linebuf,COLORMAP(0),COLORMAP(0),
                               COLORMAP(0)|A_BOLD,COLORMAP(0),
                               COLORMAP(5),COLORMAP(5),g_sel_y != selpos++ ? -1 : g_sel_x);

    x++;

    
  }
  if (g_sel_y > selpos) g_sel_y=selpos;


  if (g_ui_state==1)
  {
	  bkgdset(COLORMAP(2));
	  attrset(COLORMAP(2));
	  mvaddnstr(LINES-2,0,"USE UP AND DOWN FOR VOLUME, LEFT AND RIGHT FOR PANNING, ENTER WHEN DONE",COLS-1);
    clrtoeol();
	  bkgdset(COLORMAP(0));
	  attrset(COLORMAP(0));
  }
  else drawstatusbar();


  ypos=LINES-1;
  sprintf(linebuf,"[quit ninjam] : %s : %.1fBPM %dBPI : %dHz %dch %dbps%s",
    g_client->GetHostName(),g_client->GetActualBPM(),g_client->GetBPI(),g_audio->m_srate,g_audio->m_nch,g_audio->m_bps&~7,g_audio->m_bps&1 ? "(f)":"");
  highlightoutline(ypos++,linebuf,COLORMAP(1),COLORMAP(1),COLORMAP(1),COLORMAP(1),COLORMAP(5),COLORMAP(5),g_sel_y != selpos ? -1 : g_sel_x);
  attrset(COLORMAP(1));
  bkgdset(COLORMAP(1));
  clrtoeol();
  if (action && g_sel_y == selpos)
  {
    g_done++;
  }

  if (g_ui_state == 2 || g_ui_state == 3)
  {
	  bkgdset(COLORMAP(2));
	  attrset(COLORMAP(2));
    char *p1=g_ui_state == 2 ? "RENAME CHANNEL:" : "SELECT SOURCE CHANNEL (0 or 1): ";
	  mvaddnstr(LINES-2,0,p1,COLS-1);
	  bkgdset(COLORMAP(0));
	  attrset(COLORMAP(0));

    if ((int)strlen(p1) < COLS-2) { addch(' '); addnstr(m_lineinput_str,COLS-2-strlen(p1)); }

    clrtoeol();
  }
  else
    move(LINES-1,0);

}




void sigfunc(int sig)
{
  printf("Got Ctrl+C\n");
  g_done++;
}


void usage()
{

  printf("Usage: ninjam hostname [options]\n"
    "Options:\n"
    "  -norecv\n"
    "  -sessiondir <path>\n"
    "  -nosavelocal | -savelocalwavs\n"
    "  -user <username>\n"
    "  -pass <password>\n"
    "  -debuglevel [0..2]\n"
    "  -nowritewav\n"
    "  -nowritelog\n"
    "  -audiostr 0:0,0:0,0\n"
    "  -jesusonic <path to jesusonic root dir>\n");

  exit(1);
}

int main(int argc, char **argv)
{
  g_ilog2x6 = 6.0/log10(2.0);
  signal(SIGINT,sigfunc);

  char *parmuser=NULL;
  char *parmpass=NULL;
  char *jesusdir=NULL;
  WDL_String sessiondir;
  int nolog=0,nowav=0;

  printf("Ninjam v0.005 curses client, Copyright (C) 2004-2005 Cockos, Inc.\n");
  char *audioconfigstr=NULL;
  g_client=new NJClient;
  g_client->config_savelocalaudio=1;

  if (argc < 2) usage();
  {
    int p;
    for (p = 2; p < argc; p++)
    {
      if (!stricmp(argv[p],"-savelocalwavs"))
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

  g_client->SetLocalChannelInfo(0,"channel0",true,0,false,0,true,true);
  g_client->SetLocalChannelMonitoring(0,false,0.0f,false,0.0f,false,false);

  



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



	// go into leet curses mode now
#ifdef _WIN32
	initscr(0);
#else
	initscr();
#endif
	cbreak();
	noecho();
	nonl();
	intrflush(stdscr,FALSE);
	keypad(stdscr,TRUE);
	nodelay(stdscr,TRUE);
	raw(); // disable ctrl+C etc. no way to kill if allow quit isn't defined, yay.

#ifndef _WIN32
	ESCDELAY=0; // dont wait--at least on the console this seems to work.
#endif

	if (has_colors()) // we don't use color yet, but we could
	{
		start_color();
		init_pair(1, COLOR_WHITE, COLOR_BLUE); // normal status lines
		init_pair(2, COLOR_BLACK, COLOR_CYAN); // value

#ifdef COLOR_BLUE_DIM
		init_pair(3, COLOR_WHITE, COLOR_BLUE_DIM); // alternating shit for the effect view
		init_pair(4, COLOR_WHITE, COLOR_RED_DIM);
#else

#if 0 // ok this aint gonna do shit for us :(
		if (can_change_color() && init_color(COLOR_YELLOW,0,0,150) && init_color(COLOR_MAGENTA,150,0,0))
		{
			init_pair(3, COLOR_WHITE, COLOR_YELLOW); // alternating shit for the effect view
			init_pair(4, COLOR_WHITE, COLOR_MAGENTA);
		}
		else
#endif


#ifdef VGA_CONSOLE
		char *term=getenv("TERM");
		if (term && !strcmp(term,"linux") && !ioperm(0x3C8,2,1))
		{
			init_pair(3, COLOR_WHITE, COLOR_YELLOW); // alternating shit for the effect view
			init_pair(4, COLOR_WHITE, COLOR_MAGENTA);
			set_pal(6,0,0,15);
			set_pal(5,15,0,0);
		}
		else
#endif
		{
			init_pair(3, COLOR_WHITE, COLOR_BLUE); // alternating shit for the effect view
			init_pair(4, COLOR_WHITE, COLOR_RED);
		}
#endif
		init_pair(5, COLOR_BLACK, COLOR_WHITE);
		init_pair(6, COLOR_WHITE, COLOR_RED);
		int x;
		for (x = 1; x < 8; x ++)
			color_map[x]=COLOR_PAIR(x);

	}
#ifndef _WIN32
	else
	{
//		color_map[1]=A_BOLD;
		color_map[2]=A_STANDOUT;
		color_map[5]=A_STANDOUT;
	}
#endif

  showmainview();
	refresh();

  DWORD nextupd=GetTickCount()+250;
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

      int a=getch();
      if (a!=ERR)
      {
        if (!g_ui_state) switch (a)
        {
          case KEY_LEFT:
            if (g_sel_x > 0)
            {
              g_sel_x--;
              showmainview();
            }
          break;
          case KEY_RIGHT:
            {
              g_sel_x++;
              showmainview();
            }
          break;
          case KEY_UP:
            if (g_sel_y > 0) 
            {
              g_sel_y--;
              showmainview();
            }
          break;
          case KEY_DOWN:
            {
              g_sel_y++;
              showmainview();
            }
          break;
          case '\r': case ' ':
            {
              showmainview(true);
            }
          break;
        }
        else if (g_ui_state == 1)
        {
          switch (a)
          {
            case KEY_LEFT:
            case KEY_RIGHT:
              {
                float pan;
                int ok=0;
                if (g_ui_voltweakstate_channel == -2) { ok=1; pan=(float)g_client->config_masterpan; }
                else if (g_ui_voltweakstate_channel == -1) { pan=(float)g_client->config_metronome_pan; ok=1; }
                else if (g_ui_voltweakstate_channel >= 1024) 
                  ok=!!g_client->GetUserChannelState((g_ui_voltweakstate_channel-1024)/64,g_ui_voltweakstate_channel%64, NULL,NULL,&pan,NULL);
                else ok=!g_client->GetLocalChannelMonitoring(g_ui_voltweakstate_channel,NULL,&pan,NULL);

                if (ok)
                {
                  pan += a == KEY_LEFT ? -0.01f : 0.01f;
                  if (pan > 1.0f) pan=1.0f;
                  if (g_ui_voltweakstate_channel == -2) g_client->config_masterpan=pan;
                  else if (g_ui_voltweakstate_channel == -1) g_client->config_metronome_pan=pan;
                  else if (g_ui_voltweakstate_channel>=1024)
                    g_client->SetUserChannelState((g_ui_voltweakstate_channel-1024)/64,g_ui_voltweakstate_channel%64, false,false,false,0.0f,true,pan,false,false);
                  else
                    g_client->SetLocalChannelMonitoring(g_ui_voltweakstate_channel,false,0.0f,true,pan,false,false);
                  showmainview();
                }
              }
            break;
            case KEY_PPAGE:
            case KEY_UP:
            case KEY_NPAGE:
            case KEY_DOWN:
              {
                float vol;
                int ok=0;
                if (g_ui_voltweakstate_channel == -2) { ok=1; vol=(float)g_client->config_mastervolume; }
                else if (g_ui_voltweakstate_channel == -1) { vol=(float)g_client->config_metronome; ok=1; }
                else if (g_ui_voltweakstate_channel >= 1024) 
                  ok=!!g_client->GetUserChannelState((g_ui_voltweakstate_channel-1024)/64,g_ui_voltweakstate_channel%64, NULL,&vol,NULL,NULL);
                else ok=!g_client->GetLocalChannelMonitoring(g_ui_voltweakstate_channel,&vol,NULL,NULL);

                if (ok)
                {
                  vol=(float) VAL2DB(vol);
                  float sc=a == KEY_PPAGE || a == KEY_NPAGE? 4.0f : 0.5f;
                  if (a == KEY_DOWN || a == KEY_NPAGE) sc=-sc;
                  vol += sc;
                  if (vol > 20.0f) vol=20.0f;
                  vol=(float) DB2VAL(vol);
                  if (g_ui_voltweakstate_channel == -2) g_client->config_mastervolume=vol;
                  else if (g_ui_voltweakstate_channel == -1) g_client->config_metronome=vol;
                  else if (g_ui_voltweakstate_channel>=1024)
                    g_client->SetUserChannelState((g_ui_voltweakstate_channel-1024)/64,g_ui_voltweakstate_channel%64, false,false,true,vol,false,0.0f,false,false);
                  else
                    g_client->SetLocalChannelMonitoring(g_ui_voltweakstate_channel,true,vol,false,0.0f,false,false);
                  showmainview();
                }
              }
            break;
            case 27: case '\r':
              {
                g_ui_state=0;
                showmainview();
              }
            break;
          }
        }
        else if (g_ui_state == 2 || g_ui_state == 3)
        {
          switch (a)
          {
            case '\r':
              if (m_lineinput_str[0])
              {
                if (g_ui_state == 2)
                {
                  g_client->SetLocalChannelInfo(g_ui_locrename_ch,m_lineinput_str,false,0,false,0,false,false);
                }
                else if (g_ui_state == 3)
                {
                  int ch= atoi(m_lineinput_str);
                  if (ch < 0) ch=0;
                  else if (ch > 1) ch=1;
                  g_client->SetLocalChannelInfo(g_ui_locrename_ch,NULL,true,ch,false,0,false,false);
                }
                g_client->NotifyServerOfChannelChange();
              }
              g_ui_state=0;
              showmainview();
            break;
            case 27:
              {
                g_ui_state=0;
                showmainview();
              }
            break;
					  case KEY_BACKSPACE: 
              if (m_lineinput_str[0]) m_lineinput_str[strlen(m_lineinput_str)-1]=0; 
              showmainview();
					  break;
            default:
              if (VALIDATE_TEXT_CHAR(a) && (g_ui_state == 2 || (a >= '0' && a <= '1'))) //fucko: 9 once we have > 2ch
						  { 
							  int l=strlen(m_lineinput_str); 
							  if (l < (int)sizeof(m_lineinput_str)-1) { m_lineinput_str[l]=a; m_lineinput_str[l+1]=0; }
                showmainview();
						  } 
            break;
          }
        }
      }

      if (g_ui_state < 2 && (g_client->HasUserInfoChanged()||GetTickCount()>=nextupd ))
      {
        nextupd=GetTickCount()+1000;
        showmainview();
      }
      else drawstatusbar();
    }

  }

	erase();
	refresh();

	// shut down curses
	endwin();

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
