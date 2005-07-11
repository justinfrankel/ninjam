#include <windows.h>
#include <richedit.h>
#include <shlobj.h>
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
#include "../../WDL/mutex.h"

#ifdef _WIN32
#include "../../jesusonic/jesusonic_dll.h"
#endif

#define WM_LCUSER_RESIZE WM_USER+49
#define WM_LCUSER_ADDCHILD WM_USER+50
#define WM_LCUSER_REMCHILD WM_USER+51
#define WM_LCUSER_VUUPDATE WM_USER+52
#define WM_LCUSER_REPOP_CH WM_USER+53
#define WM_RCUSER_UPDATE WM_USER+54

#define VERSION "0.0a"

#define CONFSEC "ninjam"

#define VALIDATE_TEXT_CHAR(thischar) ((isspace(thischar) || isgraph(thischar)) && (thischar) < 256)

int g_done=0;
HANDLE g_hThread;

WDL_Mutex g_client_mutex;

char g_exepath[1024];
WDL_String g_ini_file;
HICON g_hSmallIcon;
HINSTANCE g_hInst;
HWND g_hwnd;
WDL_String g_topic;

static HWND m_locwnd,m_remwnd;

extern void addChatLine(char *src, char *text);
extern void chatInit(HWND hwndDlg);
extern void chat_run();

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
        if (parms[2][0])
        {
          tmp.Append(" sets topic to: ");
          tmp.Append(parms[2]);
        }
        else
        {
          tmp.Append(" removes topic.");
        }  
      }
      else
      {
        if (parms[2][0])
        {
          tmp.Set("Topic is: ");
          tmp.Append(parms[2]);
        }
        else tmp.Set("No topic is set.");
      }

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


double DB2SLIDER(double x)
{
double d=pow(2110.54*fabs(x),1.0/3.0);
if (x < 0.0) d=-d;
return d + 63.0;
}

double SLIDER2DB(double y)
{
return pow(y-63.0,3.0) * (1.0/2110.54);
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
char *jesusdir="C:\\mhc\\jesusonic";
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

void JesusUpdateInfo(void *myInst, char *chdesc)
{
  if (myInst)
  {
    JesusonicAPI->set_sample_fmt(myInst,g_audio?g_audio->m_srate:44100,1,33);
    WDL_String tmp("NINJAM embedded: ");
    tmp.Append(chdesc);
    JesusonicAPI->set_status(myInst,"",tmp.Get());
  }
}

int CreateJesusInstance(int a, char *chdesc)
{
  if (JesusonicAPI)
  {
    void *myInst=JesusonicAPI->createInstance();
    if (!myInst) return 0;
    JesusonicAPI->set_rootdir(myInst,jesusdir);
    JesusonicAPI->ui_init(myInst);
    JesusonicAPI->set_opts(myInst,1,1,-1);

    JesusUpdateInfo(myInst,chdesc);

    char buf[4096];
    sprintf(buf,"%s\\ninjam.p%02d",jesusdir?jesusdir:".",a);

    JesusonicAPI->preset_load(myInst,buf);

    g_client_mutex.Enter();
    g_client->SetLocalChannelProcessor(a,jesusonic_processor,myInst);
    g_client_mutex.Leave();
    return 1;
  }
  return 0;
}
#endif


void mkvolpanstr(char *str, double vol, double pan)
{
  double v=VAL2DB(vol);
  if (vol < 0.0000001 || v < -120.0) v=-120.0;
    sprintf(str,"%s%2.1fdB ",v>0.0?"+":"",v);   
    if (fabs(pan) < 0.0001) strcat(str,"center");
    else sprintf(str+strlen(str),"%d%%%s", (int)fabs(pan*100.0),(pan>0.0 ? "R" : "L"));
}




static BOOL WINAPI LicenseProc(HWND hwndDlg, UINT uMsg, WPARAM wParam, LPARAM lParam)
{
  switch (uMsg)
  {
    case WM_INITDIALOG:
      SetDlgItemText(hwndDlg,IDC_LICENSETEXT,(char *)lParam);
    return 0;
    case WM_CLOSE:
      EndDialog(hwndDlg,0);
    return 0;
    case WM_COMMAND:
      switch (LOWORD(wParam))
      {
        case IDC_CHECK1:
          EnableWindow(GetDlgItem(hwndDlg,IDOK),!!IsDlgButtonChecked(hwndDlg,LOWORD(wParam)));
        break;
        case IDOK:
          EndDialog(hwndDlg,1);
        break;
        case IDCANCEL:
          EndDialog(hwndDlg,0);
        break;
      }
    return 0;

  }
  return 0;
}

char *g_need_license;
int g_license_result;

int licensecallback(int user32, char *licensetext)
{
  if (!licensetext || !*licensetext) return 1;

  g_need_license=licensetext;
  g_license_result=0;
  g_client_mutex.Leave();
  while (g_need_license && !g_done) Sleep(100);
  g_client_mutex.Enter();
  return g_license_result;
}

static BOOL WINAPI PrefsProc(HWND hwndDlg, UINT uMsg, WPARAM wParam, LPARAM lParam)
{
  switch (uMsg)
  {
    case WM_INITDIALOG:
      {
        if (GetPrivateProfileInt(CONFSEC,"savelocal",1,g_ini_file.Get()))
          CheckDlgButton(hwndDlg,IDC_SAVELOCAL,BST_CHECKED);
        if (GetPrivateProfileInt(CONFSEC,"savelocalwav",0,g_ini_file.Get()))
          CheckDlgButton(hwndDlg,IDC_SAVELOCALWAV,BST_CHECKED);
        if (GetPrivateProfileInt(CONFSEC,"savewave",0,g_ini_file.Get()))
          CheckDlgButton(hwndDlg,IDC_SAVEWAVE,BST_CHECKED);
        if (GetPrivateProfileInt(CONFSEC,"saveogg",0,g_ini_file.Get()))
          CheckDlgButton(hwndDlg,IDC_SAVEOGG,BST_CHECKED);
        SetDlgItemInt(hwndDlg,IDC_SAVEOGGBR,GetPrivateProfileInt(CONFSEC,"saveoggbr",128,g_ini_file.Get()),FALSE);

        char str[2048];
        GetPrivateProfileString(CONFSEC,"sessiondir","",str,sizeof(str),g_ini_file.Get());
        if (!str[0])
          SetDlgItemText(hwndDlg,IDC_SESSIONDIR,g_exepath);
        else 
          SetDlgItemText(hwndDlg,IDC_SESSIONDIR,str);
        
        if (g_client->GetStatus() != NJClient::NJC_STATUS_OK)
          ShowWindow(GetDlgItem(hwndDlg,IDC_CHNOTE),SW_HIDE);
      }
    return 0;

    case WM_COMMAND:
      switch (LOWORD(wParam))
      {
        case IDC_BROWSE:
          {
            BROWSEINFO bi={0,};
			      ITEMIDLIST *idlist;
			      char name[2048];
			      GetDlgItemText(hwndDlg,IDC_SESSIONDIR,name,sizeof(name));
			      bi.hwndOwner = hwndDlg;
			      bi.pszDisplayName = name;
			      bi.lpszTitle = "Select a directory:";
			      bi.ulFlags = BIF_RETURNONLYFSDIRS;
			      idlist = SHBrowseForFolder( &bi );
			      if (idlist) 
            {
				      SHGetPathFromIDList( idlist, name );        
	            IMalloc *m;
	            SHGetMalloc(&m);
	            m->Free(idlist);
				      SetDlgItemText(hwndDlg,IDC_SESSIONDIR,name);
			      }
          }
        break;
        case IDOK:
          {
            WritePrivateProfileString(CONFSEC,"savelocal",IsDlgButtonChecked(hwndDlg,IDC_SAVELOCAL)?"1":"0",g_ini_file.Get());
            WritePrivateProfileString(CONFSEC,"savelocalwav",IsDlgButtonChecked(hwndDlg,IDC_SAVELOCALWAV)?"1":"0",g_ini_file.Get());
            WritePrivateProfileString(CONFSEC,"savewave",IsDlgButtonChecked(hwndDlg,IDC_SAVEWAVE)?"1":"0",g_ini_file.Get());
            WritePrivateProfileString(CONFSEC,"saveogg",IsDlgButtonChecked(hwndDlg,IDC_SAVEOGG)?"1":"0",g_ini_file.Get());

            char buf[2048];
            GetDlgItemText(hwndDlg,IDC_SAVEOGGBR,buf,sizeof(buf));
            if (atoi(buf)) WritePrivateProfileString(CONFSEC,"saveoggbr",buf,g_ini_file.Get());

            GetDlgItemText(hwndDlg,IDC_SESSIONDIR,buf,sizeof(buf));
            if (!strcmp(g_exepath,buf))
              buf[0]=0;
            WritePrivateProfileString(CONFSEC,"sessiondir",buf,g_ini_file.Get());

            EndDialog(hwndDlg,1);
          }
        break;
        case IDCANCEL:
          EndDialog(hwndDlg,0);
        break;
      }
    return 0;
    case WM_CLOSE:
      EndDialog(hwndDlg,0);
    return 0;
  }
  return 0;
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
    case WM_CLOSE:
      EndDialog(hwndDlg,0);
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

  g_client_mutex.Enter();

  WDL_String sessiondir(g_client->GetWorkDir()); // save a copy of the work dir before we blow it away
  
  g_client->Disconnect();
  delete g_client->waveWrite;
  g_client->waveWrite=0;
  g_client->SetWorkDir(NULL);
  g_client->SetOggOutFile(NULL,0,0,0);
  g_client->SetLogFile(NULL);
  
  g_client_mutex.Leave();

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

  char sroot[2048];
  GetPrivateProfileString(CONFSEC,"sessiondir","",sroot,sizeof(sroot),g_ini_file.Get());

  if (!sroot[0])
    strcpy(sroot,g_exepath);

  {
    char *p=sroot;
    while (*p) p++;
    p--;
    while (p >= sroot && (*p == '/' || *p == '\\')) p--;
    p[1]=0;
  }

  CreateDirectory(sroot,NULL);
  while (cnt < 16)
  {
    time_t tv;
    time(&tv);
    struct tm *t=localtime(&tv);
  
    buf[0]=0;

    strcpy(buf,sroot);
    sprintf(buf+strlen(buf),"\\%04d%02d%02d_%02d%02d",
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

  g_client_mutex.Enter();

  // configure any running jesusonic instances with new samplerate
  {
    int x=0;
    for (x = 0;;x++)
    {
      int a=g_client->EnumLocalChannels(x);
      if (a<0) break;
      void *i=0;
      g_client->GetLocalChannelProcessor(a,NULL,&i);
      if (i) JesusUpdateInfo(i,g_client->GetLocalChannelInfo(a,NULL,NULL,NULL));
    }
  }
  
  SendMessage(m_locwnd,WM_LCUSER_REPOP_CH,0,0);

  g_client->SetWorkDir(buf);

  g_client->config_savelocalaudio=0;
  if (GetPrivateProfileInt(CONFSEC,"savelocal",1,g_ini_file.Get()))
  {
    g_client->config_savelocalaudio|=1;
    if (GetPrivateProfileInt(CONFSEC,"savelocalwav",0,g_ini_file.Get())) 
      g_client->config_savelocalaudio|=2;
  }
  if (GetPrivateProfileInt(CONFSEC,"savewave",0,g_ini_file.Get()))
  {
    WDL_String wf;
    wf.Set(g_client->GetWorkDir());
    wf.Append("output.wav");
    g_client->waveWrite = new WaveWriter(wf.Get(),24,g_audio->m_outnch>1?2:1,g_audio->m_srate);
  }
  
  if (GetPrivateProfileInt(CONFSEC,"saveogg",0,g_ini_file.Get()))
  {
    WDL_String wf;
    wf.Set(g_client->GetWorkDir());
    wf.Append("output.ogg");
    int br=GetPrivateProfileInt(CONFSEC,"saveoggbr",128,g_ini_file.Get());
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


  g_client_mutex.Leave();

  SetDlgItemText(g_hwnd,IDC_STATUS,"Status: Connecting...");

  EnableMenuItem(GetMenu(g_hwnd),ID_OPTIONS_AUDIOCONFIGURATION,MF_BYCOMMAND|MF_GRAYED);
  EnableMenuItem(GetMenu(g_hwnd),ID_FILE_DISCONNECT,MF_BYCOMMAND|MF_ENABLED);
}

void updateMasterControlLabels(HWND hwndDlg)
{
   char buf[512];
   mkvolpanstr(buf,g_client->config_mastervolume,g_client->config_masterpan);
   SetDlgItemText(hwndDlg,IDC_MASTERLBL,buf);
   mkvolpanstr(buf,g_client->config_metronome,g_client->config_metronome_pan);
   SetDlgItemText(hwndDlg,IDC_METROLBL,buf);
}


RECT g_last_wndpos;


static BOOL WINAPI LocalChannelItemProc(HWND hwndDlg, UINT uMsg, WPARAM wParam, LPARAM lParam)
{
  int m_idx=GetWindowLong(hwndDlg,GWL_USERDATA);
  switch (uMsg)
  {
    case WM_INITDIALOG:
      m_idx=lParam;
      SetWindowLong(hwndDlg,GWL_USERDATA,lParam);
  
      {
        int sch;
        bool bc;

        g_client_mutex.Enter();

        char *buf=g_client->GetLocalChannelInfo(m_idx,&sch,NULL,&bc);
        float vol=0.0,pan=0.0 ;
        bool ismute=0,issolo=0;
        g_client->GetLocalChannelMonitoring(m_idx, &vol, &pan, &ismute, &issolo);
        void *jesinst=0;
        g_client->GetLocalChannelProcessor(m_idx,NULL,&jesinst);

        g_client_mutex.Leave();

        if (buf) SetDlgItemText(hwndDlg,IDC_NAME,buf);
        if (bc) CheckDlgButton(hwndDlg,IDC_TRANSMIT,BST_CHECKED);
        if (ismute) CheckDlgButton(hwndDlg,IDC_MUTE,BST_CHECKED);
        if (issolo) CheckDlgButton(hwndDlg,IDC_SOLO,BST_CHECKED);
        if (jesinst) CheckDlgButton(hwndDlg,IDC_JS,BST_CHECKED);

        SendMessage(hwndDlg,WM_LCUSER_REPOP_CH,0,0);        

        SendDlgItemMessage(hwndDlg,IDC_VU,PBM_SETRANGE,FALSE,MAKELONG(0,100));
        SendDlgItemMessage(hwndDlg,IDC_VOL,TBM_SETRANGE,FALSE,MAKELONG(0,100));
        SendDlgItemMessage(hwndDlg,IDC_VOL,TBM_SETTIC,FALSE,63);       
        SendDlgItemMessage(hwndDlg,IDC_VOL,TBM_SETPOS,TRUE,(LPARAM)DB2SLIDER(VAL2DB(vol)));

        SendDlgItemMessage(hwndDlg,IDC_PAN,TBM_SETRANGE,FALSE,MAKELONG(0,100));
        SendDlgItemMessage(hwndDlg,IDC_PAN,TBM_SETTIC,FALSE,50);       
        int t=(int)(pan*50.0) + 50;
        if (t < 0) t=0; else if (t > 100)t=100;
        SendDlgItemMessage(hwndDlg,IDC_PAN,TBM_SETPOS,TRUE,t);

        {
         char tmp[512];
         mkvolpanstr(tmp,vol,pan);
         SetDlgItemText(hwndDlg,IDC_VOLLBL,tmp);
        }

      }
    return 0;
    case WM_TIMER:
      if (wParam == 1)
      {
        KillTimer(hwndDlg,1);
        char buf[512];
        GetDlgItemText(hwndDlg,IDC_NAME,buf,sizeof(buf));
        g_client_mutex.Enter();
        g_client->SetLocalChannelInfo(m_idx,buf,false,0,false,0,false,0);
        g_client->NotifyServerOfChannelChange();
        void *i=0;
        g_client->GetLocalChannelProcessor(m_idx,NULL,&i);
        g_client_mutex.Leave();
        if (i)
        {
          JesusUpdateInfo(i,buf);
        }
      }
    return 0;
    case WM_COMMAND:
      switch (LOWORD(wParam))
      {
        case IDC_AUDIOIN:
          if (HIWORD(wParam) == CBN_SELCHANGE)
          {
            int a=SendDlgItemMessage(hwndDlg,IDC_AUDIOIN,CB_GETCURSEL,0,0);
            if (a != CB_ERR)
            {
              if (a == SendDlgItemMessage(hwndDlg,IDC_AUDIOIN,CB_GETCOUNT,0,0)-1) a=-1;
              g_client_mutex.Enter();
              g_client->SetLocalChannelInfo(m_idx,NULL,true,a,false,0,false,false);
              g_client_mutex.Leave();
            }
          }
        break;
        case IDC_TRANSMIT:
          g_client_mutex.Enter();
          g_client->SetLocalChannelInfo(m_idx,NULL,false,0,false,0,true,!!IsDlgButtonChecked(hwndDlg,LOWORD(wParam)));
          g_client->NotifyServerOfChannelChange();
          g_client_mutex.Leave();
        break;
        case IDC_SOLO:
          g_client_mutex.Enter();
          g_client->SetLocalChannelMonitoring(m_idx,false,0.0,false,0.0,false,false,true,!!IsDlgButtonChecked(hwndDlg,LOWORD(wParam)));
          g_client->NotifyServerOfChannelChange();
          g_client_mutex.Leave();
        break;
        case IDC_MUTE:
          g_client_mutex.Enter();
          g_client->SetLocalChannelMonitoring(m_idx,false,0.0,false,0.0,true,!!IsDlgButtonChecked(hwndDlg,LOWORD(wParam)),false,false);
          g_client->NotifyServerOfChannelChange();
          g_client_mutex.Leave();
        break;
        case IDC_NAME:
          if (HIWORD(wParam) == EN_CHANGE)
          {
            KillTimer(hwndDlg,1);
            SetTimer(hwndDlg,1,1000,NULL);
          }
        break;
        case IDC_REMOVE:
          {
            // remove JS for this channel
            void *i=0;
            g_client_mutex.Enter();
            g_client->GetLocalChannelProcessor(m_idx,NULL,&i);
            if (i) deleteJesusonicProc(i,m_idx);
            g_client->SetLocalChannelProcessor(m_idx,NULL,NULL);

            // remove the channel
            g_client->DeleteLocalChannel(m_idx);
            g_client_mutex.Leave();
            PostMessage(GetParent(hwndDlg),WM_LCUSER_REMCHILD,0,(LPARAM)hwndDlg);
          }
        break;
        case IDC_JS:
          if (IsDlgButtonChecked(hwndDlg,IDC_JS))
          {
            char buf[512];
            GetDlgItemText(hwndDlg,IDC_NAME,buf,sizeof(buf));
            if (CreateJesusInstance(m_idx,buf))
            {
              EnableWindow(GetDlgItem(hwndDlg,IDC_JSCFG),1);
            }
          }
          else
          {
            void *i=0;
            g_client_mutex.Enter();
            g_client->GetLocalChannelProcessor(m_idx,NULL,&i);
            if (i)
            {
              deleteJesusonicProc(i,m_idx);
              g_client->SetLocalChannelProcessor(m_idx,NULL,NULL);
            }
            g_client_mutex.Leave();
            EnableWindow(GetDlgItem(hwndDlg,IDC_JSCFG),0);
          }
        break;
        case IDC_JSCFG:
          {
            void *i=0;
            g_client_mutex.Enter();
            g_client->GetLocalChannelProcessor(m_idx,NULL,&i);
            HWND h=JesusonicAPI->ui_wnd_gethwnd(i);
            g_client_mutex.Leave();
            if (h && IsWindow(h))
            {
              ShowWindow(h,SW_SHOWNA);
              SetForegroundWindow(h);
            }
            else
            {
              HWND h=JesusonicAPI->ui_wnd_create(i);
              ShowWindow(h,SW_SHOWNA);
              SetTimer(h,1,40,NULL);
              SetForegroundWindow(h);
            }
          }
        break;

      }
    return 0;
    case WM_HSCROLL:
      {
        double pos=(double)SendMessage((HWND)lParam,TBM_GETPOS,0,0);

        g_client_mutex.Enter();

		    if ((HWND) lParam == GetDlgItem(hwndDlg,IDC_VOL))
          g_client->SetLocalChannelMonitoring(m_idx,true,(float)DB2VAL(SLIDER2DB(pos)),false,0.0,false,false,false,false);
		    else if ((HWND) lParam == GetDlgItem(hwndDlg,IDC_PAN))
        {
          if (fabs(pos) < 0.08) pos=0.0;
          g_client->SetLocalChannelMonitoring(m_idx,false,false,true,((float)pos-50.0f)/50.0f,false,false,false,false);
        }
        else 
        {
          g_client_mutex.Leave();
          return 0;
        }

        float vol,pan;
        g_client->GetLocalChannelMonitoring(m_idx, &vol, &pan, NULL, NULL);

        g_client_mutex.Leave();

        char tmp[512];
        mkvolpanstr(tmp,vol,pan);
        SetDlgItemText(hwndDlg,IDC_VOLLBL,tmp);
      }
    return 0;
    case WM_LCUSER_REPOP_CH:
      {
        int sch;
        SendDlgItemMessage(hwndDlg,IDC_AUDIOIN,CB_RESETCONTENT,0,0);

        g_client_mutex.Enter();
        g_client->GetLocalChannelInfo(m_idx,&sch,NULL,NULL);
        g_client_mutex.Leave();
        int chcnt=0;
        if (g_audio)
        {
          for (;;)
          {
            const char *p=g_audio->GetChannelName(chcnt);
            if (!p) break;
            SendDlgItemMessage(hwndDlg,IDC_AUDIOIN,CB_ADDSTRING,0,(LPARAM)p);         
            chcnt++;
          }
        }
        else
        {
          for (chcnt = 0; chcnt < max(sch,8); chcnt++)
          {
            char buf[128];
            sprintf(buf,"Input Channel %d",chcnt+1);
            SendDlgItemMessage(hwndDlg,IDC_AUDIOIN,CB_ADDSTRING,0,(LPARAM)buf);         
          }
        }
        SendDlgItemMessage(hwndDlg,IDC_AUDIOIN,CB_ADDSTRING,0,(LPARAM)"Silence");         
        SendDlgItemMessage(hwndDlg,IDC_AUDIOIN,CB_SETCURSEL,sch >= 0 ? sch : chcnt,0);
      }
    return 0;
    case WM_LCUSER_VUUPDATE:
      {
        double val=VAL2DB(g_client->GetLocalChannelPeak(m_idx));
        int ival=(int)(val+100.0);
        if (ival < 0) ival=0;
        else if (ival > 100) ival=100;
        SendDlgItemMessage(hwndDlg,IDC_VU,PBM_SETPOS,ival,0);

        char buf[128];
        sprintf(buf,"%.2f dB",val);
        SetDlgItemText(hwndDlg,IDC_VULBL,buf);      
      }
    return 0;

  };
  return 0;
}

static BOOL WINAPI RemoteChannelItemProc(HWND hwndDlg, UINT uMsg, WPARAM wParam, LPARAM lParam)
{
  int m_userch=GetWindowLong(hwndDlg,GWL_USERDATA); // high 16 bits, user, low 16 bits, channel
  switch (uMsg)
  {
    case WM_INITDIALOG:
      SetWindowLong(hwndDlg,GWL_USERDATA,0xffffffff);

      SendDlgItemMessage(hwndDlg,IDC_VU,PBM_SETRANGE,0,MAKELPARAM(0,100));

      SendDlgItemMessage(hwndDlg,IDC_VOL,TBM_SETRANGE,FALSE,MAKELONG(0,100));
      SendDlgItemMessage(hwndDlg,IDC_VOL,TBM_SETTIC,FALSE,63);       
      SendDlgItemMessage(hwndDlg,IDC_PAN,TBM_SETRANGE,FALSE,MAKELONG(0,100));
      SendDlgItemMessage(hwndDlg,IDC_PAN,TBM_SETTIC,FALSE,50);       

    return 0;
    case WM_RCUSER_UPDATE:
      m_userch=((int)LOWORD(wParam) << 16) | LOWORD(lParam);
      SetWindowLong(hwndDlg,GWL_USERDATA,m_userch);
    break;
  }
  int user=m_userch>>16;
  int chan=m_userch&0xffff;
  switch (uMsg)
  {
    case WM_RCUSER_UPDATE: // update the items
      {
        g_client_mutex.Enter();
        char *un=g_client->GetUserState(user,NULL,NULL,NULL);
        SetDlgItemText(hwndDlg,IDC_USERNAME,un?un:"");

        bool sub=0,m=0,s=0;
        float v=0,p=0;
        char *cn=g_client->GetUserChannelState(user,chan,&sub,&v,&p,&m,&s);
        g_client_mutex.Leave();

        SetDlgItemText(hwndDlg,IDC_CHANNELNAME,cn?cn:"");

        CheckDlgButton(hwndDlg,IDC_RECV,sub?BST_CHECKED:0);
        CheckDlgButton(hwndDlg,IDC_MUTE,m?BST_CHECKED:0);
        CheckDlgButton(hwndDlg,IDC_SOLO,s?BST_CHECKED:0);

        SendDlgItemMessage(hwndDlg,IDC_VOL,TBM_SETPOS,TRUE,(LPARAM)DB2SLIDER(VAL2DB(v)));

        int t=(int)(p*50.0) + 50;
        if (t < 0) t=0; else if (t > 100)t=100;
        SendDlgItemMessage(hwndDlg,IDC_PAN,TBM_SETPOS,TRUE,t);

        {
         char tmp[512];
         mkvolpanstr(tmp,v,p);
         SetDlgItemText(hwndDlg,IDC_VOLLBL,tmp);
        }

      }
    break;
    case WM_LCUSER_VUUPDATE:
      {
        double val=VAL2DB(g_client->GetUserChannelPeak(user,chan));
        int ival=(int)((val+100.0));
        if (ival < 0) ival=0;
        else if (ival > 100) ival=100;
        SendDlgItemMessage(hwndDlg,IDC_VU,PBM_SETPOS,ival,0);

        char buf[128];
        sprintf(buf,"%.2f dB",val);
        SetDlgItemText(hwndDlg,IDC_VULBL,buf);      
      }
    return 0;
    case WM_HSCROLL:
      {
        double pos=(double)SendMessage((HWND)lParam,TBM_GETPOS,0,0);

        g_client_mutex.Enter();
		    if ((HWND) lParam == GetDlgItem(hwndDlg,IDC_VOL))
          g_client->SetUserChannelState(user,chan,false,false,true,(float)DB2VAL(SLIDER2DB(pos)),false,0.0,false,false,false,false);
		    else if ((HWND) lParam == GetDlgItem(hwndDlg,IDC_PAN))
        {
          if (fabs(pos) < 0.08) pos=0.0;
          g_client->SetUserChannelState(user,chan,false,false,false,0.0,true,((float)pos-50.0f)/50.0f,false,false,false,false);
        }
        else 
        {
          g_client_mutex.Leave();
          return 0;
        }

        float vol,pan;
        g_client->GetUserChannelState(user,chan,NULL,&vol,&pan,NULL,NULL);

        g_client_mutex.Leave();

        char tmp[512];
        mkvolpanstr(tmp,vol,pan);
        SetDlgItemText(hwndDlg,IDC_VOLLBL,tmp);
      }
    return 0;
    case WM_COMMAND:
      switch (LOWORD(wParam))
      {
        case IDC_RECV:
          g_client_mutex.Enter();
          g_client->SetUserChannelState(user,chan,true,!!IsDlgButtonChecked(hwndDlg,LOWORD(wParam)),false,0.0,false,0.0,false,false,false,false);
          g_client_mutex.Leave();
        break;
        case IDC_SOLO:
          g_client_mutex.Enter();
          g_client->SetUserChannelState(user,chan,false,false,false,0.0,false,0.0,false,false,true,!!IsDlgButtonChecked(hwndDlg,LOWORD(wParam)));
          g_client_mutex.Leave();
        break;
        case IDC_MUTE:
          g_client_mutex.Enter();
          g_client->SetUserChannelState(user,chan,false,false,false,0.0,false,0.0,true,!!IsDlgButtonChecked(hwndDlg,LOWORD(wParam)),false,false);
          g_client_mutex.Leave();
        break;
      }
    return 0;
  }
  return 0;
}


static BOOL WINAPI LocalChannelListProc(HWND hwndDlg, UINT uMsg, WPARAM wParam, LPARAM lParam)
{
  static int m_num_children;
  switch (uMsg)
  {
    case WM_INITDIALOG:
    case WM_LCUSER_RESIZE:
      {
      }
    break;
    case WM_LCUSER_REPOP_CH:
    case WM_LCUSER_VUUPDATE:
      {
        HWND hwnd=GetWindow(hwndDlg,GW_CHILD);

        while (hwnd)
        {
          if (hwnd != GetDlgItem(hwndDlg,IDC_ADDCH)) SendMessage(hwnd,uMsg,0,0);
          hwnd=GetWindow(hwnd,GW_HWNDNEXT);
        }        
      }
    break;
    case WM_COMMAND:
      if (LOWORD(wParam) != IDC_ADDCH) return 0;
      {
        int idx;
        g_client_mutex.Enter();
        int maxc=g_client->GetMaxLocalChannels();
        for (idx = 0; idx < maxc && g_client->GetLocalChannelInfo(idx,NULL,NULL,NULL); idx++);

        if (idx < maxc) 
        {
          g_client->SetLocalChannelInfo(idx,"new channel",true,0,false,0,true,true);
          g_client->NotifyServerOfChannelChange();  
        }
        g_client_mutex.Leave();

        if (idx >= maxc) return 0;
        wParam = (WPARAM)idx;
      }

    case WM_LCUSER_ADDCHILD:
      {
        // add a new child, with wParam as the index
        HWND hwnd=CreateDialogParam(g_hInst,MAKEINTRESOURCE(IDD_LOCALCHANNEL),hwndDlg,LocalChannelItemProc,wParam);
        if (hwnd)
        {
          RECT sz;
          GetClientRect(hwnd,&sz);
          SetWindowPos(hwnd,NULL,0,sz.bottom*m_num_children,0,0,SWP_NOSIZE|SWP_NOZORDER|SWP_NOACTIVATE);
          ShowWindow(hwnd,SW_SHOWNA);
          m_num_children++;

          int h=sz.bottom*m_num_children;
          int w=sz.right-sz.left;

          SetWindowPos(GetDlgItem(hwndDlg,IDC_ADDCH),NULL,0,h,0,0,SWP_NOSIZE|SWP_NOZORDER|SWP_NOACTIVATE);

          GetWindowRect(GetDlgItem(hwndDlg,IDC_ADDCH),&sz);
          h += sz.bottom - sz.top + 3;

          SetWindowPos(hwndDlg,0,0,0,w,h,SWP_NOMOVE|SWP_NOZORDER|SWP_NOACTIVATE);
          SendMessage(GetParent(hwndDlg),WM_LCUSER_RESIZE,0,uMsg == WM_COMMAND);
        }
      }
    break;
    case WM_LCUSER_REMCHILD:
      // remove a child, move everything up
      if (lParam) 
      {
        HWND hwndDel=(HWND)lParam;
        RECT cr;
        GetWindowRect(hwndDel,&cr);
        ScreenToClient(hwndDlg,(LPPOINT)&cr);
        ScreenToClient(hwndDlg,((LPPOINT)&cr) + 1);

        DestroyWindow(hwndDel);

        HWND hwnd=GetWindow(hwndDlg,GW_CHILD);

        int w=0;
        int h=0;
        while (hwnd)
        {
          RECT tr;
          GetWindowRect(hwnd,&tr);
          ScreenToClient(hwndDlg,(LPPOINT)&tr);
          ScreenToClient(hwndDlg,((LPPOINT)&tr) + 1);

          if (tr.top > cr.top)
          {
            tr.top -= cr.bottom-cr.top;
            tr.bottom -= cr.bottom-cr.top;
            SetWindowPos(hwnd,NULL,tr.left,tr.top,0,0,SWP_NOSIZE|SWP_NOZORDER|SWP_NOACTIVATE);
            if (tr.bottom > h) h=tr.bottom;
          }
          if (tr.right > w) w=tr.right;

          hwnd=GetWindow(hwnd,GW_HWNDNEXT);
        }        
        m_num_children--;

        h+=3;

        SetWindowPos(hwndDlg,0,0,0,w,h,SWP_NOMOVE|SWP_NOZORDER|SWP_NOACTIVATE);
        SendMessage(GetParent(hwndDlg),WM_LCUSER_RESIZE,0,0);
      }
    break;
  }
  return 0;
}


static BOOL WINAPI RemoteChannelListProc(HWND hwndDlg, UINT uMsg, WPARAM wParam, LPARAM lParam);


static BOOL WINAPI RemoteOuterChannelListProc(HWND hwndDlg, UINT uMsg, WPARAM wParam, LPARAM lParam)
{
  static int m_wh, m_ww,m_nScrollPos,m_nScrollPos_w;
  static int m_h, m_maxpos_h, m_w,m_maxpos_w; 
  static HWND m_child;
  switch (uMsg)
  {

    case WM_RCUSER_UPDATE:
    case WM_LCUSER_RESIZE:
    case WM_INITDIALOG:
      {
        RECT r;
        GetWindowRect(GetDlgItem(GetParent(hwndDlg),IDC_REMOTERECT),&r);
        m_wh=r.bottom-r.top;
        m_ww=r.right-r.left;

        ScreenToClient(GetParent(hwndDlg),(LPPOINT)&r);

        SetWindowPos(hwndDlg,NULL,r.left,r.top,m_ww,m_wh,SWP_NOZORDER|SWP_NOACTIVATE);

        m_wh -= GetSystemMetrics(SM_CYHSCROLL);

        if (uMsg == WM_INITDIALOG)
        {
          m_child=CreateDialog(g_hInst,MAKEINTRESOURCE(IDD_EMPTY),hwndDlg,RemoteChannelListProc);
          ShowWindow(m_child,SW_SHOWNA);
          ShowWindow(hwndDlg,SW_SHOWNA);
        }
      }

      {
        SendMessage(m_child,WM_RCUSER_UPDATE,0,0);
        RECT r;
        GetWindowRect(m_child,&r);
        m_h=r.bottom-r.top;
        m_w=r.right-r.left;
        m_maxpos_h=m_wh-m_h;
        m_maxpos_w=m_ww-m_w;

        if (m_maxpos_h < 0) m_maxpos_h=0;
        if (m_maxpos_w < 0) m_maxpos_w=0;

        {
          SCROLLINFO si={sizeof(si),SIF_RANGE|SIF_PAGE,0,m_h,};
          si.nPage=m_wh;

          if (m_nScrollPos+m_wh > m_h)
          {
            int np=m_h-m_wh;
            if (np<0)np=0;
            si.nPos=np;
            si.fMask |= SIF_POS;

            ScrollWindow(hwndDlg,0,m_nScrollPos-np,NULL,NULL);
            m_nScrollPos=np;
          }
          SetScrollInfo(hwndDlg,SB_VERT,&si,TRUE);
        }
        {
          SCROLLINFO si={sizeof(si),SIF_RANGE|SIF_PAGE,0,m_w,};
          si.nPage=m_ww;
          if (m_nScrollPos_w+m_ww > m_w)
          {
            int np=m_w-m_ww;
            if (np<0)np=0;
            si.nPos=np;
            si.fMask |= SIF_POS;

            ScrollWindow(hwndDlg,m_nScrollPos_w-np,0,NULL,NULL);
            m_nScrollPos_w=np;
          }

          SetScrollInfo(hwndDlg,SB_HORZ,&si,TRUE);
        }
      }

      // update scrollbars and shit
    return 0;
    case WM_LCUSER_VUUPDATE:
      SendMessage(m_child,uMsg,wParam,lParam);
    break;
    case WM_VSCROLL:
      {
        int nSBCode=LOWORD(wParam);
	      int nDelta=0;

	      int nMaxPos = m_maxpos_h;

	      switch (nSBCode)
	      {
          case SB_TOP:
            nDelta = - m_nScrollPos;
          break;
          case SB_BOTTOM:
            nDelta = nMaxPos - m_nScrollPos;
          break;
	        case SB_LINEDOWN:
		        if (m_nScrollPos < nMaxPos) nDelta = min(nMaxPos/100,nMaxPos-m_nScrollPos);
		      break;
	        case SB_LINEUP:
		        if (m_nScrollPos > 0) nDelta = -min(nMaxPos/100,m_nScrollPos);
          break;
          case SB_PAGEDOWN:
		        if (m_nScrollPos < nMaxPos) nDelta = min(nMaxPos/10,nMaxPos-m_nScrollPos);
		      break;
          case SB_THUMBTRACK:
	        case SB_THUMBPOSITION:
		        nDelta = (int)HIWORD(wParam) - m_nScrollPos;
		      break;
	        case SB_PAGEUP:
		        if (m_nScrollPos > 0) nDelta = -min(nMaxPos/10,m_nScrollPos);
		      break;
	      }
        if (nDelta) 
        {
          m_nScrollPos += nDelta;
	        SetScrollPos(hwndDlg,SB_VERT,m_nScrollPos,TRUE);
	        ScrollWindow(hwndDlg,0,-nDelta,NULL,NULL);
        }
      }
    break;
    case WM_HSCROLL:
      {
        int nSBCode=LOWORD(wParam);
	      int nDelta=0;

	      int nMaxPos = m_maxpos_w;

	      switch (nSBCode)
	      {
          case SB_TOP:
            nDelta = - m_nScrollPos_w;
          break;
          case SB_BOTTOM:
            nDelta = nMaxPos - m_nScrollPos_w;
          break;
	        case SB_LINEDOWN:
		        if (m_nScrollPos_w < nMaxPos) nDelta = min(nMaxPos/100,nMaxPos-m_nScrollPos_w);
		      break;
	        case SB_LINEUP:
		        if (m_nScrollPos_w > 0) nDelta = -min(nMaxPos/100,m_nScrollPos_w);
          break;
          case SB_PAGEDOWN:
		        if (m_nScrollPos_w < nMaxPos) nDelta = min(nMaxPos/10,nMaxPos-m_nScrollPos_w);
		      break;
          case SB_THUMBTRACK:
	        case SB_THUMBPOSITION:
		        nDelta = (int)HIWORD(wParam) - m_nScrollPos_w;
		      break;
	        case SB_PAGEUP:
		        if (m_nScrollPos_w > 0) nDelta = -min(nMaxPos/10,m_nScrollPos_w);
		      break;
	      }
        if (nDelta) 
        {
          m_nScrollPos_w += nDelta;
	        SetScrollPos(hwndDlg,SB_HORZ,m_nScrollPos_w,TRUE);
	        ScrollWindow(hwndDlg,-nDelta,0,NULL,NULL);
        }
      }
    break; 
  }
  return 0;
}


static BOOL WINAPI LocalOuterChannelListProc(HWND hwndDlg, UINT uMsg, WPARAM wParam, LPARAM lParam)
{
  static int m_wh, m_ww,m_nScrollPos,m_nScrollPos_w;
  static int m_h, m_maxpos_h, m_w,m_maxpos_w; 
  static HWND m_child;
  switch (uMsg)
  {

    case WM_RCUSER_UPDATE:
    case WM_LCUSER_RESIZE:
    case WM_INITDIALOG:
      {
        RECT r;
        GetWindowRect(GetDlgItem(GetParent(hwndDlg),IDC_LOCRECT),&r);
        m_wh=r.bottom-r.top;
        m_ww=r.right-r.left;

        ScreenToClient(GetParent(hwndDlg),(LPPOINT)&r);

        SetWindowPos(hwndDlg,NULL,r.left,r.top,m_ww,m_wh,SWP_NOZORDER|SWP_NOACTIVATE);

        m_wh -= GetSystemMetrics(SM_CYHSCROLL);

        if (uMsg == WM_INITDIALOG)
        {
          m_child=CreateDialog(g_hInst,MAKEINTRESOURCE(IDD_LOCALLIST),hwndDlg,LocalChannelListProc);
          ShowWindow(m_child,SW_SHOWNA);
          ShowWindow(hwndDlg,SW_SHOWNA);
        }
      }

      {
        SendMessage(m_child,WM_RCUSER_UPDATE,0,0);
        RECT r;
        GetWindowRect(m_child,&r);
        m_h=r.bottom-r.top;
        m_w=r.right-r.left;
        m_maxpos_h=m_wh-m_h;
        m_maxpos_w=m_ww-m_w;

        if (m_maxpos_h < 0) m_maxpos_h=0;
        if (m_maxpos_w < 0) m_maxpos_w=0;

        {
          SCROLLINFO si={sizeof(si),SIF_RANGE|SIF_PAGE,0,m_h,};
          si.nPage=m_wh;

          if (m_nScrollPos+m_wh > m_h)
          {
            int np=m_h-m_wh;
            if (np<0)np=0;
            si.nPos=np;
            si.fMask |= SIF_POS;

            ScrollWindow(hwndDlg,0,m_nScrollPos-np,NULL,NULL);
            m_nScrollPos=np;
          }
          SetScrollInfo(hwndDlg,SB_VERT,&si,TRUE);
        }
        {
          SCROLLINFO si={sizeof(si),SIF_RANGE|SIF_PAGE,0,m_w,};
          si.nPage=m_ww;
          if (m_nScrollPos_w+m_ww > m_w)
          {
            int np=m_w-m_ww;
            if (np<0)np=0;
            si.nPos=np;
            si.fMask |= SIF_POS;

            ScrollWindow(hwndDlg,m_nScrollPos_w-np,0,NULL,NULL);
            m_nScrollPos_w=np;
          }

          SetScrollInfo(hwndDlg,SB_HORZ,&si,TRUE);
        }
      }
      if (uMsg == WM_LCUSER_RESIZE && lParam == 1)
      {
        if (m_wh < m_h)
        {
          int npos=m_h-m_wh;
          if (npos >= 0 && npos != m_nScrollPos)
          {
            SetScrollPos(hwndDlg,SB_VERT,npos,TRUE);
            ScrollWindow(hwndDlg,0,m_nScrollPos-npos,NULL,NULL);
            m_nScrollPos=npos;
          }
        }
      }

      // update scrollbars and shit
    return 0;
    case WM_LCUSER_ADDCHILD:
    case WM_LCUSER_VUUPDATE:
      SendMessage(m_child,uMsg,wParam,lParam);
    break;
    case WM_VSCROLL:
      {
        int nSBCode=LOWORD(wParam);
	      int nDelta=0;

	      int nMaxPos = m_maxpos_h;

	      switch (nSBCode)
	      {
          case SB_TOP:
            nDelta = - m_nScrollPos;
          break;
          case SB_BOTTOM:
            nDelta = nMaxPos - m_nScrollPos;
          break;
	        case SB_LINEDOWN:
		        if (m_nScrollPos < nMaxPos) nDelta = min(nMaxPos/100,nMaxPos-m_nScrollPos);
		      break;
	        case SB_LINEUP:
		        if (m_nScrollPos > 0) nDelta = -min(nMaxPos/100,m_nScrollPos);
          break;
          case SB_PAGEDOWN:
		        if (m_nScrollPos < nMaxPos) nDelta = min(nMaxPos/10,nMaxPos-m_nScrollPos);
		      break;
          case SB_THUMBTRACK:
	        case SB_THUMBPOSITION:
		        nDelta = (int)HIWORD(wParam) - m_nScrollPos;
		      break;
	        case SB_PAGEUP:
		        if (m_nScrollPos > 0) nDelta = -min(nMaxPos/10,m_nScrollPos);
		      break;
	      }
        if (nDelta) 
        {
          m_nScrollPos += nDelta;
	        SetScrollPos(hwndDlg,SB_VERT,m_nScrollPos,TRUE);
	        ScrollWindow(hwndDlg,0,-nDelta,NULL,NULL);
        }
      }
    break;
    case WM_HSCROLL:
      {
        int nSBCode=LOWORD(wParam);
	      int nDelta=0;

	      int nMaxPos = m_maxpos_w;

	      switch (nSBCode)
	      {
          case SB_TOP:
            nDelta = - m_nScrollPos_w;
          break;
          case SB_BOTTOM:
            nDelta = nMaxPos - m_nScrollPos_w;
          break;
	        case SB_LINEDOWN:
		        if (m_nScrollPos_w < nMaxPos) nDelta = min(nMaxPos/100,nMaxPos-m_nScrollPos_w);
		      break;
	        case SB_LINEUP:
		        if (m_nScrollPos_w > 0) nDelta = -min(nMaxPos/100,m_nScrollPos_w);
          break;
          case SB_PAGEDOWN:
		        if (m_nScrollPos_w < nMaxPos) nDelta = min(nMaxPos/10,nMaxPos-m_nScrollPos_w);
		      break;
          case SB_THUMBTRACK:
	        case SB_THUMBPOSITION:
		        nDelta = (int)HIWORD(wParam) - m_nScrollPos_w;
		      break;
	        case SB_PAGEUP:
		        if (m_nScrollPos_w > 0) nDelta = -min(nMaxPos/10,m_nScrollPos_w);
		      break;
	      }
        if (nDelta) 
        {
          m_nScrollPos_w += nDelta;
	        SetScrollPos(hwndDlg,SB_HORZ,m_nScrollPos_w,TRUE);
	        ScrollWindow(hwndDlg,-nDelta,0,NULL,NULL);
        }
      }
    break; 
  }
  return 0;
}


static BOOL WINAPI RemoteChannelListProc(HWND hwndDlg, UINT uMsg, WPARAM wParam, LPARAM lParam)
{
  static WDL_PtrList<void> m_children;
  switch (uMsg)
  {

    case WM_INITDIALOG:
      {
      }
    break;
    case WM_LCUSER_VUUPDATE:
      {
        HWND hwnd=GetWindow(hwndDlg,GW_CHILD);

        while (hwnd)
        {
          SendMessage(hwnd,uMsg,0,0);
          hwnd=GetWindow(hwnd,GW_HWNDNEXT);
        }        
      }
    break;
    case WM_RCUSER_UPDATE:
      {
        int pos=0;
        int us;
        int did_sizing=0;
        RECT lastr={0,0,0,0};
        g_client_mutex.Enter();
        for (us = 0; us < g_client->GetNumUsers(); us ++)
        {
          int ch=0;
          for (;;)
          {
            int i=g_client->EnumUserChannels(us,ch++);
            if (i < 0) break;
            HWND h=NULL;
            if (pos < m_children.GetSize()) h=(HWND)m_children.Get(pos);
            else
            {
              h=CreateDialog(g_hInst,MAKEINTRESOURCE(IDD_REMOTECHANNEL),hwndDlg,RemoteChannelItemProc);
              SetWindowPos(h,0,0,lastr.bottom,0,0,SWP_NOSIZE|SWP_NOZORDER|SWP_NOACTIVATE);
              did_sizing=1;
              ShowWindow(h,SW_SHOWNA);
              m_children.Add((void *)h);
            }
            GetWindowRect(h,&lastr);
            ScreenToClient(hwndDlg,(LPPOINT)&lastr);
            ScreenToClient(hwndDlg,((LPPOINT)&lastr)+1);
            SendMessage(h,WM_RCUSER_UPDATE,(WPARAM)us,(LPARAM)i);
      
            pos++;
          }
        }

        g_client_mutex.Leave();

        for (; pos < m_children.GetSize(); )
        {
          DestroyWindow((HWND)m_children.Get(pos));
          m_children.Delete(pos);
          did_sizing=1;
        }

        if (did_sizing)
        {
          SetWindowPos(hwndDlg,NULL,0,0,lastr.right,lastr.bottom,SWP_NOMOVE|SWP_NOZORDER|SWP_NOACTIVATE); // size ourself

        }
        
      }
      // update channel list, creating and destroying window as necessary
    break;
  }
  return 0;
}



DWORD WINAPI ThreadFunc(LPVOID p)
{
  while (!g_done)
  {
    g_client_mutex.Enter();
    while (!g_client->Run());
    g_client_mutex.Leave();
    Sleep(20);
  }
  return 0;
}


static BOOL WINAPI MainProc(HWND hwndDlg, UINT uMsg, WPARAM wParam, LPARAM lParam)
{
  static WDL_WndSizer resize;
  switch (uMsg)
  {
    case WM_INITDIALOG:
      {
        SetWindowText(hwndDlg,"NINJAM v" VERSION);
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
        resize.init_item(IDC_STATUS2, 1.0f,  1.0,  1.0f,  1.0);
    
        
        resize.init_item(IDC_DIV2,        0.0,  0.5f,  0.7f,  0.5f);
        resize.init_item(IDC_CHATDIV,     0.7f, 0.0f,  0.7f,  1.0f);

        resize.init_item(IDC_CHATDISP,     0.7f, 0.0f,  1.0f,  1.0f);
        resize.init_item(IDC_CHATENT,      0.7f, 1.0f,  1.0f,  1.0f);
        resize.init_item(IDC_CHATOK,       1.0f, 1.0f,  1.0f,  1.0f);
                
        resize.init_item(IDC_LOCRECT,     0.0f, 0.0f,  0.7f,  0.5f);
        resize.init_item(IDC_REMOTERECT,  0.0f, 0.5f,  0.7f,  1.0f);      

        chatInit(hwndDlg);


        char tmp[512];
        SendDlgItemMessage(hwndDlg,IDC_MASTERVOL,TBM_SETRANGE,FALSE,MAKELONG(0,100));
        SendDlgItemMessage(hwndDlg,IDC_MASTERVOL,TBM_SETTIC,FALSE,63);       
        GetPrivateProfileString(CONFSEC,"mastervol","1.0",tmp,sizeof(tmp),g_ini_file.Get());
        g_client->config_mastervolume=(float)atof(tmp);
        SendDlgItemMessage(hwndDlg,IDC_MASTERVOL,TBM_SETPOS,TRUE,(LPARAM)DB2SLIDER(VAL2DB(g_client->config_mastervolume)));

        SendDlgItemMessage(hwndDlg,IDC_METROVOL,TBM_SETRANGE,FALSE,MAKELONG(0,100));
        SendDlgItemMessage(hwndDlg,IDC_METROVOL,TBM_SETTIC,FALSE,63);       
        GetPrivateProfileString(CONFSEC,"metrovol","0.5",tmp,sizeof(tmp),g_ini_file.Get());
        g_client->config_metronome=(float)atof(tmp);
        SendDlgItemMessage(hwndDlg,IDC_METROVOL,TBM_SETPOS,TRUE,(LPARAM)DB2SLIDER(VAL2DB(g_client->config_metronome)));

        SendDlgItemMessage(hwndDlg,IDC_MASTERPAN,TBM_SETRANGE,FALSE,MAKELONG(0,100));
        SendDlgItemMessage(hwndDlg,IDC_MASTERPAN,TBM_SETTIC,FALSE,50);       
        GetPrivateProfileString(CONFSEC,"masterpan","0.0",tmp,sizeof(tmp),g_ini_file.Get());
        g_client->config_masterpan=(float)atof(tmp);
        int t=(int)(g_client->config_masterpan*50.0) + 50;
        if (t < 0) t=0; else if (t > 100)t=100;
        SendDlgItemMessage(hwndDlg,IDC_MASTERPAN,TBM_SETPOS,TRUE,t);

        SendDlgItemMessage(hwndDlg,IDC_METROPAN,TBM_SETRANGE,FALSE,MAKELONG(0,100));
        SendDlgItemMessage(hwndDlg,IDC_METROPAN,TBM_SETTIC,FALSE,50);       
        GetPrivateProfileString(CONFSEC,"metropan","0.0",tmp,sizeof(tmp),g_ini_file.Get());
        g_client->config_metronome_pan=(float)atof(tmp);
        t=(int)(g_client->config_metronome_pan*50.0) + 50;
        if (t < 0) t=0; else if (t > 100)t=100;
        SendDlgItemMessage(hwndDlg,IDC_METROPAN,TBM_SETPOS,TRUE,t);

        if (GetPrivateProfileInt(CONFSEC,"mastermute",0,g_ini_file.Get()))
        {
          CheckDlgButton(hwndDlg,IDC_MASTERMUTE,BST_CHECKED);
          g_client->config_mastermute=true;
        }
        if (GetPrivateProfileInt(CONFSEC,"metromute",0,g_ini_file.Get()))
        {
          CheckDlgButton(hwndDlg,IDC_METROMUTE,BST_CHECKED);
          g_client->config_metronome_mute=true;
        }

        updateMasterControlLabels(hwndDlg);

        m_locwnd=CreateDialog(g_hInst,MAKEINTRESOURCE(IDD_EMPTY_SCROLL),hwndDlg,LocalOuterChannelListProc);
        m_remwnd=CreateDialog(g_hInst,MAKEINTRESOURCE(IDD_EMPTY_SCROLL),hwndDlg,RemoteOuterChannelListProc);
        
     
        // initialize local channels from config
        {
          int cnt=GetPrivateProfileInt(CONFSEC,"lc_cnt",-1,g_ini_file.Get());
          int x;
          if (cnt < 0)
          {
            // add a default channel
            g_client->SetLocalChannelInfo(0,"default channel",false,0,false,0,true,true);
            SendMessage(m_locwnd,WM_LCUSER_ADDCHILD,0,0);
          }
          for (x = 0; x < cnt; x ++)
          {
            char buf[1024];
            char specbuf[64];
            sprintf(specbuf,"lc_%d",x);

            GetPrivateProfileString(CONFSEC,specbuf,"",buf,sizeof(buf),g_ini_file.Get());

            if (!buf[0]) continue;

            LineParser lp(false);

            lp.parse(buf);

            // process local line
            if (lp.getnumtokens()>1)
            {
              int ch=lp.gettoken_int(0);
              int n;
              int wj=0, ok=0;
              char *name=NULL;
              if (ch >= 0 && ch <= MAX_LOCAL_CHANNELS) for (n = 1; n < lp.getnumtokens()-1; n += 2)
              {
                switch (lp.gettoken_enum(n,"source\0bc\0mute\0solo\0volume\0pan\0jesus\0name\0"))
                {
                  case 0: // source 
                    g_client->SetLocalChannelInfo(ch,NULL,true,lp.gettoken_int(n+1),false,0,false,false);
                  break;
                  case 1: //broadcast
                    g_client->SetLocalChannelInfo(ch,NULL,false,false,false,0,true,!!lp.gettoken_int(n+1));
                  break;
                  case 2: //mute
                    g_client->SetLocalChannelMonitoring(ch,false,false,false,false,true,!!lp.gettoken_int(n+1),false,false);
                  break;
                  case 3: //solo
                    g_client->SetLocalChannelMonitoring(ch,false,false,false,false,false,false,true,!!lp.gettoken_int(n+1));
                  break;
                  case 4: //volume
                    g_client->SetLocalChannelMonitoring(ch,true,(float)lp.gettoken_float(n+1),false,false,false,false,false,false);
                  break;
                  case 5: //pan
                    g_client->SetLocalChannelMonitoring(ch,false,false,true,(float)lp.gettoken_float(n+1),false,false,false,false);
                  break;
                  case 6: //jesus
                    if (lp.gettoken_int(n+1))
                    {
                      wj=1;
                    }
                  break;
                  case 7: //name
                    g_client->SetLocalChannelInfo(ch,name=lp.gettoken_str(n+1),false,false,false,0,false,false);
                    ok|=1;
                  break;
                  default:
                  break;
                }
              }
              if (ok)
              {
                if (wj && name)
                {
                  CreateJesusInstance(ch,name);
                }

                SendMessage(m_locwnd,WM_LCUSER_ADDCHILD,ch,0);

              }
            }
          }
        }

        SendDlgItemMessage(hwndDlg,IDC_MASTERVU,PBM_SETRANGE,0,MAKELPARAM(0,100));

        g_last_wndpos.left = GetPrivateProfileInt(CONFSEC,"wnd_x",0,g_ini_file.Get());
        g_last_wndpos.top = GetPrivateProfileInt(CONFSEC,"wnd_y",0,g_ini_file.Get());
        g_last_wndpos.right = g_last_wndpos.left+GetPrivateProfileInt(CONFSEC,"wnd_w",0,g_ini_file.Get());
        g_last_wndpos.bottom = g_last_wndpos.top+GetPrivateProfileInt(CONFSEC,"wnd_h",0,g_ini_file.Get());
        
        if (g_last_wndpos.top || g_last_wndpos.left || g_last_wndpos.right || g_last_wndpos.bottom)
        {
          SetWindowPos(hwndDlg,NULL,g_last_wndpos.left,g_last_wndpos.top,g_last_wndpos.right-g_last_wndpos.left,g_last_wndpos.bottom-g_last_wndpos.top,SWP_NOZORDER);
        }
        else GetWindowRect(hwndDlg,&g_last_wndpos);

        ShowWindow(g_hwnd,SW_SHOW);
     
        SetTimer(hwndDlg,1,50,NULL);

        DWORD id;
        g_hThread=CreateThread(NULL,0,ThreadFunc,0,0,&id);

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

          g_client_mutex.Enter();

          if (g_need_license)
          {
            g_license_result=DialogBoxParam(g_hInst,MAKEINTRESOURCE(IDD_LICENSE),g_hwnd,LicenseProc,(LPARAM)g_need_license);
            g_need_license=0;
          }


          if (g_client->HasUserInfoChanged())
          {
            if (m_remwnd) SendMessage(m_remwnd,WM_RCUSER_UPDATE,0,0);
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
          {
            int intp, intl;
            int pos, len;
            g_client->GetPosition(&pos,&len);
            if (!len) len=1;
            intl=g_client->GetBPI();
            intp = (pos * intl)/len + 1;

            int bpm=(int)g_client->GetActualBPM();
            if (ns != NJClient::NJC_STATUS_OK)
            {
              bpm=0;
              intp=0;
            }
            g_client_mutex.Leave();

            double val=VAL2DB(g_client->GetOutputPeak());
            int ival=(int)((val+100.0));
            if (ival < 0) ival=0;
            else if (ival > 100) ival=100;
            SendDlgItemMessage(hwndDlg,IDC_MASTERVU,PBM_SETPOS,ival,0);

            char buf[128];
            sprintf(buf,"%.2f dB",val);
            SetDlgItemText(hwndDlg,IDC_MASTERVULBL,buf);


            static int last_interval_len=-1;
            static int last_interval_pos=-1;
            static int last_bpm_i=-1;
            if (intl != last_interval_len)
            {
              last_interval_len=intl;
              SendDlgItemMessage(hwndDlg,IDC_INTERVALPOS,PBM_SETRANGE,0,MAKELPARAM(0,intl));             
            }
            if (intl != last_interval_len || last_bpm_i != bpm || intp != last_interval_pos)
            {
              last_bpm_i = bpm;
              char buf[128];
              if (bpm)
                sprintf(buf,"%d/%d @ %d BPM",intp,intl,bpm);
              else buf[0]=0;
              SetDlgItemText(hwndDlg,IDC_STATUS2,buf);
            }

            if (intp != last_interval_pos)
            {
              last_interval_pos = intp;
              SendDlgItemMessage(hwndDlg,IDC_INTERVALPOS,PBM_SETPOS,intp,0);
            }

            SendMessage(m_locwnd,WM_LCUSER_VUUPDATE,0,0);
            SendMessage(m_remwnd,WM_LCUSER_VUUPDATE,0,0);
          }
          chat_run();

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
        if (m_locwnd)
          SendMessage(m_locwnd,WM_LCUSER_RESIZE,0,0);
        if (m_remwnd)
          SendMessage(m_remwnd,WM_LCUSER_RESIZE,0,0);
      }
      if (wParam == SIZE_MINIMIZED || wParam == SIZE_MAXIMIZED) return 0;
    case WM_MOVE:
      GetWindowRect(hwndDlg,&g_last_wndpos);
    break;

    case WM_HSCROLL:
      {
        double pos=(double)SendMessage((HWND)lParam,TBM_GETPOS,0,0);

		    if ((HWND) lParam == GetDlgItem(hwndDlg,IDC_MASTERVOL))
          g_client->config_mastervolume=(float)DB2VAL(SLIDER2DB(pos));
		    else if ((HWND) lParam == GetDlgItem(hwndDlg,IDC_METROVOL))
          g_client->config_metronome=(float)DB2VAL(SLIDER2DB(pos));
		    else if ((HWND) lParam == GetDlgItem(hwndDlg,IDC_MASTERPAN))
        {
          if (fabs(pos) < 0.08) pos=0.0;
          g_client->config_masterpan=((float)pos-50.0f)/50.0f;
        }
		    else if ((HWND) lParam == GetDlgItem(hwndDlg,IDC_METROPAN))
        {
          if (fabs(pos) < 0.08) pos=0.0;
          g_client->config_metronome_pan=((float)pos-50.0f)/50.0f;
        }
        else return 0;

        updateMasterControlLabels(hwndDlg);
      }
    return 0;

    case WM_COMMAND:
      switch (LOWORD(wParam))
      {
        case ID_HELP_ABOUTNINJAM:
          MessageBox(hwndDlg,"NINJAM v" VERSION "\r\nCopyright (C) 2005, Cockos, Inc.","About NINJAM", MB_OK);
        break;
        case IDC_MASTERMUTE:
          g_client->config_mastermute=!!IsDlgButtonChecked(hwndDlg,LOWORD(wParam));
        break;
        case IDC_METROMUTE:
          g_client->config_metronome_mute =!!IsDlgButtonChecked(hwndDlg,LOWORD(wParam));
        break;
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
        case ID_OPTIONS_PREFERENCES:
          DialogBox(g_hInst,MAKEINTRESOURCE(IDD_PREFS),hwndDlg,PrefsProc);
        break;
        case ID_OPTIONS_AUDIOCONFIGURATION:
          CreateConfiguredStreamer(g_ini_file.Get(),-1,hwndDlg);
        break;
        case ID_FILE_QUIT:
          PostMessage(hwndDlg,WM_CLOSE,0,0);
        break;
        case IDC_CHATOK:
          {
            char str[256];
            GetDlgItemText(hwndDlg,IDC_CHATENT,str,255);
            if (str[0])
            {
              if (!strcasecmp(str,"/clear"))
              {
                    SetDlgItemText(hwndDlg,IDC_CHATDISP,"");
              }
              else if (g_client->GetStatus() == NJClient::NJC_STATUS_OK)
              {
                if (str[0] == '/')
                {
                  if (!strncasecmp(str,"/me ",4))
                  {
                    g_client_mutex.Enter();
                    g_client->ChatMessage_Send("MSG",str);
                    g_client_mutex.Leave();
                  }
                  else if (!strncasecmp(str,"/topic ",7)||
                           !strncasecmp(str,"/kick ",6) ||                        
                           !strncasecmp(str,"/bpm ",5) ||
                           !strncasecmp(str,"/bpi ",5)
                    ) // alias to /admin *
                  {
                    g_client_mutex.Enter();
                    g_client->ChatMessage_Send("ADMIN",str+1);
                    g_client_mutex.Leave();
                  }
                  else if (!strncasecmp(str,"/admin ",7))
                  {
                    char *p=str+7;
                    while (*p == ' ') p++;
                    g_client_mutex.Enter();
                    g_client->ChatMessage_Send("ADMIN",p);
                    g_client_mutex.Leave();
                  }
                  else if (!strncasecmp(str,"/msg ",5))
                  {
                    char *p=str+5;
                    while (*p == ' ') p++;
                    char *n=p;
                    while (*p && *p != ' ') p++;
                    if (*p == ' ') *p++=0;
                    while (*p == ' ') p++;
                    if (*p)
                    {
                      g_client_mutex.Enter();
                      g_client->ChatMessage_Send("PRIVMSG",n,p);
                      g_client_mutex.Leave();
                      WDL_String tmp;
                      tmp.Set("-> *");
                      tmp.Append(n);
                      tmp.Append("* ");
                      tmp.Append(p);
                      addChatLine(NULL,tmp.Get());
                    }
                    else
                    {
                      addChatLine("","error: /msg requires a username and a message.");
                    }
                  }
                  else
                  {
                    addChatLine("","error: unknown command.");
                  }
                }
                else
                {
                  g_client_mutex.Enter();
                  g_client->ChatMessage_Send("MSG",str);
                  g_client_mutex.Leave();
                }
              }
              else
              {
                addChatLine("","error: not connected to a server.");
              }
            }
            SetDlgItemText(hwndDlg,IDC_CHATENT,"");
            SetFocus(GetDlgItem(hwndDlg,IDC_CHATENT));
          }
        break;
      }
    break;
    case WM_CLOSE:
      if (1) DestroyWindow(hwndDlg);
    break;
    case WM_ENDSESSION:
    case WM_DESTROY:

      do_disconnect();

      // save config

      g_done=1;
      WaitForSingleObject(g_hThread,INFINITE);
      CloseHandle(g_hThread);

      {
        int x;
        int cnt=0;
        for (x = 0;;x++)
        {
          int a=g_client->EnumLocalChannels(x);
          if (a<0) break;


          int sch=0;
          bool bc=0;
          void *has_jesus=0;
          char *lcn;
          float v=0.0f,p=0.0f;
          bool m=0,s=0;
      
          lcn=g_client->GetLocalChannelInfo(a,&sch,NULL,&bc);
          g_client->GetLocalChannelMonitoring(a,&v,&p,&m,&s);
          g_client->GetLocalChannelProcessor(a,NULL,&has_jesus);

          char *ptr=lcn;
          while (*ptr)
          {
            if (*ptr == '`') *ptr='\'';
            ptr++;
          }
          if (strlen(lcn) > 128) lcn[127]=0;
          char buf[1024];
          sprintf(buf,"%d source %d bc %d mute %d solo %d volume %f pan %f jesus %d name `%s`",a,sch,bc,m,s,v,p,!!has_jesus,lcn);
          char specbuf[64];
          sprintf(specbuf,"lc_%d",cnt++);
          WritePrivateProfileString(CONFSEC,specbuf,buf,g_ini_file.Get());
        }
        char buf[64];
        sprintf(buf,"%d",cnt);
        WritePrivateProfileString(CONFSEC,"lc_cnt",buf,g_ini_file.Get());
      }

      {
        char buf[256];

        sprintf(buf,"%d",g_last_wndpos.left);
        WritePrivateProfileString(CONFSEC,"wnd_x",buf,g_ini_file.Get());
        sprintf(buf,"%d",g_last_wndpos.top);
        WritePrivateProfileString(CONFSEC,"wnd_y",buf,g_ini_file.Get());
        sprintf(buf,"%d",g_last_wndpos.right - g_last_wndpos.left);
        WritePrivateProfileString(CONFSEC,"wnd_w",buf,g_ini_file.Get());
        sprintf(buf,"%d",g_last_wndpos.bottom - g_last_wndpos.top);
        WritePrivateProfileString(CONFSEC,"wnd_h",buf,g_ini_file.Get());


        sprintf(buf,"%f",g_client->config_mastervolume);
        WritePrivateProfileString(CONFSEC,"mastervol",buf,g_ini_file.Get());
        sprintf(buf,"%f",g_client->config_masterpan);
        WritePrivateProfileString(CONFSEC,"masterpan",buf,g_ini_file.Get());
        sprintf(buf,"%f",g_client->config_metronome);
        WritePrivateProfileString(CONFSEC,"metrovol",buf,g_ini_file.Get());
        sprintf(buf,"%f",g_client->config_metronome_pan);
        WritePrivateProfileString(CONFSEC,"metropan",buf,g_ini_file.Get());

        WritePrivateProfileString(CONFSEC,"mastermute",g_client->config_mastermute?"1":"0",g_ini_file.Get());
        WritePrivateProfileString(CONFSEC,"metromute",g_client->config_metronome_mute?"1":"0",g_ini_file.Get());

      }

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
    while (p > g_exepath && *p != '\\') p--; *p=0;
    g_ini_file.Set(g_exepath);
    g_ini_file.Append("\\ninjam.ini");
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

  // todo: jesusdir config
#ifdef _WIN32
  WDL_String jesusonic_configfile;
  if (jesusdir)
  {
    jesusonic_configfile.Set(jesusdir);
    jesusonic_configfile.Append("\\cmdclient.jesusonicpreset");
    WDL_String dll;
    dll.Set(jesusdir);
    dll.Append("\\jesus.dll");

    hDllInst = LoadLibrary(".\\jesus.dll"); // load from current dir
    if (!hDllInst) hDllInst = LoadLibrary(dll.Get());
    if (hDllInst) 
    {
      *(void **)(&JesusonicAPI) = (void *)GetProcAddress(hDllInst,"JesusonicAPI");
      if (JesusonicAPI && JesusonicAPI->ver == JESUSONIC_API_VERSION_CURRENT)
      {
      }
      else JesusonicAPI = 0;
    }
  }

#endif



  g_ilog2x6 = 6.0/log10(2.0);

  JNL::open_socketlib();

  g_client = new NJClient;

  g_client->LicenseAgreementCallback = licensecallback;
  g_client->ChatMessage_Callback = chatmsg_cb;


  if (!CreateDialog(hInstance,MAKEINTRESOURCE(IDD_MAIN),GetDesktopWindow(),MainProc))
  {
    MessageBox(NULL,"Error creating dialog","NINJAM Error",0);
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


#ifdef _WIN32
  ///// jesusonic stuff
  if (hDllInst) FreeLibrary(hDllInst);
  hDllInst=0;
  JesusonicAPI=0;

#endif

  JNL::close_socketlib();
  return 0;
}
