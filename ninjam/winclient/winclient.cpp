#include <windows.h>
#include <commctrl.h>
#include "../audiostream_asio.h"
#define strncasecmp strnicmp

#include <stdio.h>
#include <math.h>
#include <signal.h>
#include <float.h>

#include "resource.h"

#include "../audiostream_asio.h"

#include "../njclient.h"
#include "../../WDL/wingui/wndsize.h"
#include "../../WDL/dirscan.h"
#include "../../WDL/lineparse.h"

#ifdef _WIN32
#include "../../jesusonic/jesusonic_dll.h"
#endif

#define CONFSEC "ninjam"

#define VALIDATE_TEXT_CHAR(thischar) ((isspace(thischar) || isgraph(thischar)) && (thischar) < 256)

int g_done=0;

char g_exepath[1024];
WDL_String g_ini_file;
HICON g_hSmallIcon;
HINSTANCE g_hInst;
HWND g_hwnd;
WDL_String g_topic;
WDL_PtrList<char> g_chat_buffers;

void addChatLine(char *src, char *text)
{
  while (g_chat_buffers.GetSize() > 256)
  {
    free(g_chat_buffers.Get(0));
    g_chat_buffers.Delete(0);
  }
  WDL_String tmp;
  if (src && *src && !strncmp(text,"/me ",4))
  {
    tmp.Set("* ");
    tmp.Append(src);
    tmp.Append(" ");
    char *p=text+3;
    while (*p == ' ') p++;
    tmp.Append(p);
  }
  else
  {
   if (src&&*src)
   {
     tmp.Set("<");
     tmp.Append(src);
     tmp.Append("> ");
   }
   else if (src)
   {
     tmp.Set("*** ");
   }
   tmp.Append(text);
  }
  g_chat_buffers.Add(strdup(tmp.Get()));
}

void chatmsg_cb(int user32, NJClient *inst, char **parms, int nparms)
{
  if (!parms[0]) return;

  if (!strcmp(parms[0],"TOPIC"))
  {
    if (parms[2])
    {
      WDL_String tmp;
      if (parms[1] && *parms[1])
      {
        tmp.Set(parms[1]);
        tmp.Append(" sets topic to: ");
      }
      else tmp.Set("Topic is: ");
      tmp.Append(parms[2]);

      g_topic.Set(parms[2]);
      addChatLine("",tmp.Get());   
    }
  }
  else if (!strcmp(parms[0],"MSG"))
  {
    if (parms[1] && parms[2])
      addChatLine(parms[1],parms[2]);
  } 
  else if (!strcmp(parms[0],"PRIVMSG"))
  {
    if (parms[1] && parms[2])
    {
      WDL_String tmp;
      tmp.Set("*");
      tmp.Append(parms[1]);
      tmp.Append("* ");
      tmp.Append(parms[2]);
      addChatLine(NULL,tmp.Get());
    }
  } 
  else if (!strcmp(parms[0],"JOIN") || !strcmp(parms[0],"PART"))
  {
    if (parms[1] && *parms[1])
    {
      WDL_String tmp(parms[1]);
      tmp.Append(" has ");
      tmp.Append(parms[0][0]=='P' ? "left" : "joined");
      tmp.Append(" the server");
      addChatLine("",tmp.Get());
    }
  } 
}

double g_ilog2x6;
double VAL2DB(double x)
{
  double v=(log10(x)*g_ilog2x6);
  if (v < -120.0) v=-120.0;
  return v;
}
#define DB2VAL(x) (pow(2.0,(x)/6.0))

#ifdef _WIN32
// jesusonic stuff
#define MAX_JESUS_INST 32
jesusonicAPI *JesusonicAPI;  
HINSTANCE hDllInst;
char *jesusdir=NULL;
#endif


#ifdef _WIN32
audioStreamer *CreateConfiguredStreamer(char *inifile, int showcfg, HWND hwndParent);
#endif
audioStreamer *g_audio;
NJClient *g_client;


void audiostream_onunder() { }
void audiostream_onover() { }

int g_audio_enable=0;

void audiostream_onsamples(float **inbuf, int innch, float **outbuf, int outnch, int len, int srate) 
{ 
  if (!g_audio_enable) 
  {
    int x;
    // clear all output buffers
    for (x = 0; x < outnch; x ++) memset(outbuf[x],0,sizeof(float)*len);
    return;
  }
  g_client->AudioProc(inbuf,innch, outbuf, outnch, len,srate);
}

void deleteJesusonicProc(void *i, int chi)
{
#ifdef _WIN32
  if (JesusonicAPI && i)
  {
      char buf[4096];
      sprintf(buf,"%s\\ninjam.p%02d",jesusdir?jesusdir:".",chi);
      JesusonicAPI->preset_save(i,buf);
      JesusonicAPI->ui_wnd_destroy(i);
      JesusonicAPI->set_opts(i,-1,-1,1);
      JesusonicAPI->ui_quit(i);
      JesusonicAPI->destroyInstance(i);
  }
#endif
}


void jesusonic_processor(float *buf, int len, void *inst)
{
#ifdef _WIN32
  if (inst)
  {
    _controlfp(_RC_CHOP,_MCW_RC);
    JesusonicAPI->jesus_process_samples(inst,(char*)buf,len*sizeof(float));
    JesusonicAPI->osc_run(inst,(char*)buf,len);
  }
#endif
}
#ifdef _WIN32
void CreateJesusInstance(int a)
{
  if (JesusonicAPI)
  {
    void *myInst=JesusonicAPI->createInstance();
    JesusonicAPI->set_rootdir(myInst,jesusdir);
    JesusonicAPI->ui_init(myInst);
    JesusonicAPI->set_opts(myInst,1,1,-1);
    JesusonicAPI->set_sample_fmt(myInst,g_audio->m_srate,1,33);
    JesusonicAPI->set_status(myInst,"","NINJAM embedded");
    HWND h=JesusonicAPI->ui_wnd_create(myInst);
    ShowWindow(h,SW_SHOWNA);
    SetTimer(h,1,40,NULL);
    SetForegroundWindow(h);

    char buf[4096];
    sprintf(buf,"%s\\ninjam.p%02d",jesusdir?jesusdir:".",a);

    JesusonicAPI->preset_load(myInst,buf);

    g_client->SetLocalChannelProcessor(a,jesusonic_processor,myInst);
  }
}
#endif


void mkvolpanstr(char *str, double vol, double pan)
{
  double v=VAL2DB(vol);
  if (vol < 0.0000001 || v < -120.0) v=-120.0;
    sprintf(str,"%2.1fdB ",v);   
    if (fabs(pan) < 0.0001) strcat(str,"center");
    else sprintf(str+strlen(str),"%d%%%s", (int)fabs(pan*100.0),(pan>0.0 ? "R" : "L"));
}




int licensecallback(int user32, char *licensetext)
{
  MessageBox(g_hwnd,licensetext,"license agree",MB_OK);
  return 1;
}

WDL_String g_connect_user,g_connect_pass,g_connect_host;
int g_connect_anon;

static BOOL WINAPI ConnectDlgProc(HWND hwndDlg, UINT uMsg, WPARAM wParam, LPARAM lParam)
{
  switch (uMsg)
  {
    case WM_INITDIALOG:
      {
        SetDlgItemText(hwndDlg,IDC_HOST,g_connect_host.Get());
        SetDlgItemText(hwndDlg,IDC_USER,g_connect_user.Get());
        SetDlgItemText(hwndDlg,IDC_PASS,g_connect_pass.Get());
        if (!g_connect_anon)
        {
          ShowWindow(GetDlgItem(hwndDlg,IDC_PASSLBL),SW_SHOWNA);
          ShowWindow(GetDlgItem(hwndDlg,IDC_PASS),SW_SHOWNA);
        }
        else CheckDlgButton(hwndDlg,IDC_ANON,BST_CHECKED);
      }
    return 0;

    case WM_COMMAND:
      switch (LOWORD(wParam))
      {
        case IDC_ANON:
          if (IsDlgButtonChecked(hwndDlg,IDC_ANON))
          {
            ShowWindow(GetDlgItem(hwndDlg,IDC_PASSLBL),SW_HIDE);
            ShowWindow(GetDlgItem(hwndDlg,IDC_PASS),SW_HIDE);
          }
          else
          {
            ShowWindow(GetDlgItem(hwndDlg,IDC_PASSLBL),SW_SHOWNA);
            ShowWindow(GetDlgItem(hwndDlg,IDC_PASS),SW_SHOWNA);
          }
        break;

        case IDOK:
          {
            g_connect_anon=!!IsDlgButtonChecked(hwndDlg,IDC_ANON);
            WritePrivateProfileString(CONFSEC,"anon",g_connect_anon?"1":"0",g_ini_file.Get());
            char buf[512];
            GetDlgItemText(hwndDlg,IDC_HOST,buf,sizeof(buf));
            g_connect_host.Set(buf);
            WritePrivateProfileString(CONFSEC,"host",buf,g_ini_file.Get());
            GetDlgItemText(hwndDlg,IDC_USER,buf,sizeof(buf));
            g_connect_user.Set(buf);
            WritePrivateProfileString(CONFSEC,"user",buf,g_ini_file.Get());
            GetDlgItemText(hwndDlg,IDC_PASS,buf,sizeof(buf));
            g_connect_pass.Set(buf);
            WritePrivateProfileString(CONFSEC,"pass",buf,g_ini_file.Get());
            EndDialog(hwndDlg,1);
          }
        break;
        case IDCANCEL:
          EndDialog(hwndDlg,0);
        break;
      }
    return 0;
  }
  return 0;
}

void do_disconnect()
{
  delete g_audio;
  g_audio=0;
  g_audio_enable=0;


  EnableMenuItem(GetMenu(g_hwnd),ID_OPTIONS_AUDIOCONFIGURATION,MF_BYCOMMAND|MF_ENABLED);
  EnableMenuItem(GetMenu(g_hwnd),ID_FILE_DISCONNECT,MF_BYCOMMAND|MF_GRAYED);

  WDL_String sessiondir(g_client->GetWorkDir()); // save a copy of the work dir before we blow it away
  
  g_client->Disconnect();
  delete g_client->waveWrite;
  g_client->waveWrite=0;
  g_client->SetWorkDir(NULL);
  g_client->SetOggOutFile(NULL,0,0,0);
  g_client->SetLogFile(NULL);
  
  if (sessiondir.Get()[0])
  {
    addChatLine("","Disconnected from server");
    if (!g_client->config_savelocalaudio)
    {
      int n;
      for (n = 0; n < 16; n ++)
      {
        WDL_String s(sessiondir.Get());
        char buf[32];
        sprintf(buf,"%x",n);
        s.Append(buf);
    
        {
          WDL_DirScan ds;
          if (!ds.First(s.Get()))
          {
            do
            {
              if (ds.GetCurrentFN()[0] != '.')
              {
                WDL_String t;
                ds.GetCurrentFullFN(&t);
                unlink(t.Get());          
              }
            }
            while (!ds.Next());
          }
        }
        RemoveDirectory(s.Get());
      }
      RemoveDirectory(sessiondir.Get());
    }
    
  }
}


void do_connect()
{
  WDL_String userstr;
  if (g_connect_anon)
  {
    userstr.Set("anonymous:");
    userstr.Append(g_connect_user.Get());
  }
  else
  {
    userstr.Set(g_connect_user.Get());
  }

  char buf[2048];
  int cnt=0;
  
  while (cnt < 16)
  {
    time_t tv;
    time(&tv);
    struct tm *t=localtime(&tv);
  
    buf[0]=0;
    strcpy(buf,g_exepath);
    sprintf(buf+strlen(buf),"%04d%02d%02d_%02d%02d",
        t->tm_year+1900,t->tm_mon+1,t->tm_mday,t->tm_hour,t->tm_min);

    if (cnt) sprintf(buf+strlen(buf),"_%d",cnt);
    strcat(buf,".ninjam");

    if (CreateDirectory(buf,NULL)) break;

    cnt++;
  }
  if (cnt >= 16)
  {      
    SetDlgItemText(g_hwnd,IDC_STATUS,"Status: ERROR CREATING SESSION DIRECTORY");
    return;
  }

  g_audio=CreateConfiguredStreamer(g_ini_file.Get(), 0, g_hwnd);
  if (!g_audio)
  {
    RemoveDirectory(buf);
    SetDlgItemText(g_hwnd,IDC_STATUS,"Status: ERROR OPENING AUDIO");
    return;
  }
  
  g_client->SetWorkDir(buf);

  g_client->config_savelocalaudio=0;
  if (1) // save local [[NSUserDefaults standardUserDefaults] integerForKey:@"savelocal"])
  {
    g_client->config_savelocalaudio|=1;
    if (0) // save local wav [[NSUserDefaults standardUserDefaults] integerForKey:@"savelocalwav"])
      g_client->config_savelocalaudio|=2;
  }
  if (0) // save wave [[NSUserDefaults standardUserDefaults] integerForKey:@"savewave"])
  {
    WDL_String wf;
    wf.Set(g_client->GetWorkDir());
    wf.Append("output.wav");
    g_client->waveWrite = new WaveWriter(wf.Get(),24,g_audio->m_outnch>1?2:1,g_audio->m_srate);
  }
  
  if (1) // save ogg [[NSUserDefaults standardUserDefaults] integerForKey:@"saveogg"])
  {
    WDL_String wf;
    wf.Set(g_client->GetWorkDir());
    wf.Append("output.ogg");
    int br=128; //[[NSUserDefaults standardUserDefaults] integerForKey:@"saveoggbr"]
    g_client->SetOggOutFile(fopen(wf.Get(),"ab"),g_audio->m_srate,g_audio->m_outnch>1?2:1,br);
  }
  
  if (g_client->config_savelocalaudio)
  {
    WDL_String lf;
    lf.Set(g_client->GetWorkDir());
    lf.Append("clipsort.log");
    g_client->SetLogFile(lf.Get());
  }

  g_client->Connect(g_connect_host.Get(),userstr.Get(),g_connect_pass.Get());
  g_audio_enable=1;
  SetDlgItemText(g_hwnd,IDC_STATUS,"Status: Connecting...");

  EnableMenuItem(GetMenu(g_hwnd),ID_OPTIONS_AUDIOCONFIGURATION,MF_BYCOMMAND|MF_GRAYED);
  EnableMenuItem(GetMenu(g_hwnd),ID_FILE_DISCONNECT,MF_BYCOMMAND|MF_ENABLED);
}

static BOOL WINAPI MainProc(HWND hwndDlg, UINT uMsg, WPARAM wParam, LPARAM lParam)
{
  static WDL_WndSizer resize;
  switch (uMsg)
  {
    case WM_INITDIALOG:
      {
        g_hwnd=hwndDlg;

        resize.init(hwndDlg);

        // top items
        resize.init_item(IDC_MASTERVOL,  0.0,  0.0,  0.7f,  0.0);
        resize.init_item(IDC_METROVOL,   0.0,  0.0,  0.7f,  0.0);
        resize.init_item(IDC_MASTERLBL,  0.7f,  0.0,  0.7f,  0.0);
        resize.init_item(IDC_METROLBL,   0.7f,  0.0,  0.7f,  0.0);

        resize.init_item(IDC_MASTERPAN,  0.7f,  0.0,  0.8f,  0.0);
        resize.init_item(IDC_METROPAN,   0.7f,  0.0,  0.8f,  0.0);
        resize.init_item(IDC_MASTERMUTE, 0.8f,  0.0,  0.8f,  0.0);
        resize.init_item(IDC_METROMUTE,  0.8f,  0.0,  0.8f,  0.0);
        resize.init_item(IDC_MASTERVU,   0.8f,  0.0,  1.0f,  0.0);
        resize.init_item(IDC_MASTERVULBL,0.8f,  0.0,  1.0f,  0.0);
        
               
        resize.init_item(IDC_DIV1,        0.0,  0.0,  1.0f,  0.0);
        resize.init_item(IDC_INTERVALPOS, 0.0,  1.0,  1.0f,  1.0);
        resize.init_item(IDC_STATUS, 0.0,  1.0,  1.0f,  1.0);
        
        resize.init_item(IDC_DIV2,        0.0,  0.5f,  0.7f,  0.5f);
        resize.init_item(IDC_CHATDIV,     0.7f, 0.0f,  0.7f,  1.0f);

        resize.init_item(IDC_CHATDISP,     0.7f, 0.0f,  1.0f,  1.0f);
        resize.init_item(IDC_CHATENT,      0.7f, 1.0f,  1.0f,  1.0f);
        
        resize.init_item(IDC_LOCALSCROLL,  0.7f, 0.0f,  0.7f,  0.5f);
        resize.init_item(IDC_REMOTESCROLL, 0.7f, 0.5f,  0.7f,  1.0f);
        
        resize.init_item(IDC_LOCRECT,     0.0f, 0.0f,  0.7f,  0.5f);
        resize.init_item(IDC_REMOTERECT,  0.0f, 0.5f,  0.7f,  1.0f);      

        ShowWindow(g_hwnd,SW_SHOWNA);
     
        SetTimer(hwndDlg,1,33,NULL);
      }
    break;
    case WM_TIMER:
      if (wParam == 1)
      {
        static int in_t;
        static int m_last_status = 0xdeadbeef;
        if (!in_t)
        {
          in_t=1;

          while (!g_client->Run());

          if (g_client->HasUserInfoChanged())
          {
            // update remote user list
          }

          int ns=g_client->GetStatus();
          if (ns != m_last_status)
          {
            m_last_status=ns;
            if (ns < 0)
            {
              do_disconnect();
            }

            if (ns == NJClient::NJC_STATUS_DISCONNECTED)
              SetDlgItemText(hwndDlg,IDC_STATUS,"Status: disconnected from host.");
            if (ns == NJClient::NJC_STATUS_INVALIDAUTH)
              SetDlgItemText(hwndDlg,IDC_STATUS,"Status: invalid authentication info.");
            if (ns == NJClient::NJC_STATUS_CANTCONNECT)
              SetDlgItemText(hwndDlg,IDC_STATUS,"Status: can't connect to host.");
            if (ns == NJClient::NJC_STATUS_OK)
            {
              WDL_String tmp;
              tmp.Set("Status: Connected to ");
              tmp.Append(g_client->GetHostName());
              tmp.Append(" as ");
              tmp.Append(g_client->GetUserName());

              SetDlgItemText(hwndDlg,IDC_STATUS,tmp.Get());
            }
          }

          // update VU meters

          in_t=0;
        }
      }
    break;
    case WM_GETMINMAXINFO:
      {
        LPMINMAXINFO p=(LPMINMAXINFO)lParam;
        p->ptMinTrackSize.x = 600;
        p->ptMinTrackSize.y = 400;
      }
    return 0;

    case WM_SIZE:
      if (wParam != SIZE_MINIMIZED) 
      {
        resize.onResize();
      }
    break;
    case WM_COMMAND:
      switch (LOWORD(wParam))
      {
        case ID_FILE_DISCONNECT:
          do_disconnect();
        break;
        case ID_FILE_CONNECT:
          if (DialogBox(g_hInst,MAKEINTRESOURCE(IDD_CONNECT),hwndDlg,ConnectDlgProc))
          {
            do_disconnect();

            do_connect();
          }
        break;
        case ID_OPTIONS_AUDIOCONFIGURATION:
          CreateConfiguredStreamer(g_ini_file.Get(),-1,hwndDlg);
        break;
        case ID_FILE_QUIT:
          PostMessage(hwndDlg,WM_CLOSE,0,0);
        break;
      }
    break;
    case WM_CLOSE:
      if (1) DestroyWindow(hwndDlg);
    break;
    case WM_DESTROY:
      PostQuitMessage(0);
    return 0;
  }
  return 0;
}

int WINAPI WinMain(HINSTANCE hInstance, HINSTANCE hPrevInstance, LPSTR lpszCmdParam, int nShowCmd)
{
  g_hInst=hInstance;
  InitCommonControls();
  CoInitialize(0);

  {
    GetModuleFileName(g_hInst,g_exepath,sizeof(g_exepath));
    char *p=g_exepath;
    while (*p) p++;
    while (p >= g_exepath && *p != '\\') p--; *++p=0;
    g_ini_file.Set(g_exepath);
    g_ini_file.Append("ninjam.ini");
  }

  // read config file
  {
    char buf[512];
    GetPrivateProfileString(CONFSEC,"host","",buf,sizeof(buf),g_ini_file.Get());
    g_connect_host.Set(buf);
    GetPrivateProfileString(CONFSEC,"user","",buf,sizeof(buf),g_ini_file.Get());
    g_connect_user.Set(buf);
    GetPrivateProfileString(CONFSEC,"pass","",buf,sizeof(buf),g_ini_file.Get());
    g_connect_pass.Set(buf);
    g_connect_anon=GetPrivateProfileInt(CONFSEC,"anon",0,g_ini_file.Get());
  }


  { // load richedit DLL
    WNDCLASS wc={0,};
    if (!LoadLibrary("RichEd20.dll")) LoadLibrary("RichEd32.dll");

    // make richedit20a point to RICHEDIT
    if (!GetClassInfo(NULL,"RichEdit20A",&wc))
    {
      GetClassInfo(NULL,"RICHEDIT",&wc);
      wc.lpszClassName = "RichEdit20A";
      RegisterClass(&wc);
    }
  }


  g_hSmallIcon = (HICON)LoadImage(g_hInst,MAKEINTRESOURCE(IDI_ICON1),IMAGE_ICON,16,16,0);

  {
    WNDCLASS wc={0,};
    GetClassInfo(NULL,"#32770",&wc);
    wc.hIcon=LoadIcon(g_hInst,MAKEINTRESOURCE(IDI_ICON1));
    RegisterClass(&wc);
    wc.lpszClassName = "NINJAMwnd";
    RegisterClass(&wc);
  }



  g_ilog2x6 = 6.0/log10(2.0);

  JNL::open_socketlib();

  g_client = new NJClient;

  g_client->LicenseAgreementCallback = licensecallback;


  if (!CreateDialog(hInstance,MAKEINTRESOURCE(IDD_MAIN),GetDesktopWindow(),MainProc))
  {
    char buf[512];
    wsprintf(buf,"Error %08x\n",GetLastError());
    MessageBox(NULL,buf,"a",0);
    return 0;
  }

  MSG msg;
  while (GetMessage(&msg,NULL,0,0) && IsWindow(g_hwnd))
  {
    if (!IsDialogMessage(g_hwnd,&msg))
    {
      TranslateMessage(&msg);
      DispatchMessage(&msg);
    }
  }



  do_disconnect();


  // delete all effects processors in g_client
  {
    int x=0;
    for (x = 0;;x++)
    {
      int a=g_client->EnumLocalChannels(x);
      if (a<0) break;
      void *i=0;
      g_client->GetLocalChannelProcessor(a,NULL,&i);
      if (i) deleteJesusonicProc(i,a);
      g_client->SetLocalChannelProcessor(a,NULL,NULL);
    }
  }


  delete g_client;


#ifdef _WIN32
  ///// jesusonic stuff
  if (hDllInst) FreeLibrary(hDllInst);
  hDllInst=0;
  JesusonicAPI=0;

#endif

  JNL::close_socketlib();
  return 0;
}
