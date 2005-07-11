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


extern void addChatLine(char *src, char *text);
extern void chatInit(HWND hwndDlg);


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

int licensecallback(int user32, char *licensetext)
{
  if (!licensetext || !*licensetext) return 1;

  return DialogBoxParam(g_hInst,MAKEINTRESOURCE(IDD_LICENSE),g_hwnd,LicenseProc,(LPARAM)licensetext);
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
  return 0;
}

static BOOL WINAPI LocalChannelListProc(HWND hwndDlg, UINT uMsg, WPARAM wParam, LPARAM lParam)
{
  static int m_wh, m_ww,m_nScrollPos;
  static int m_num_children, m_h;
  switch (uMsg)
  {
    case WM_INITDIALOG:
    case WM_USER+49:
      {
        RECT r;
        GetWindowRect(GetDlgItem(GetParent(hwndDlg),IDC_LOCRECT),&r);
        ScreenToClient(GetParent(hwndDlg),(LPPOINT)&r);
        ScreenToClient(GetParent(hwndDlg),((LPPOINT)&r) + 1);

        SetWindowPos(hwndDlg,NULL,r.left,r.top,m_ww=r.right-r.left,m_wh=r.bottom-r.top,SWP_NOZORDER|SWP_NOACTIVATE);
        m_h=0;

#if 0
        RECT r2;
        GetClientRect(hwndDlg,&r2);

        DWORD ws=GetWindowLong(hwndDlg,GWL_STYLE);

        if (r2.bottom < m_wh) ws &= ~WS_VSCROLL;
        else ws |= WS_VSCROLL;
        if (r2.right < m_ww) ws &= ~WS_HSCROLL;
        else ws |= WS_HSCROLL;

        SetWindowLong(hwndDlg,GWL_STYLE,ws);
#endif

        if (uMsg == WM_INITDIALOG) ShowWindow(hwndDlg,SW_SHOWNA);
      }
    break;
    case WM_USER+50:
      {
        // add a new child, with wParam as the index
        HWND h=CreateDialogParam(g_hInst,MAKEINTRESOURCE(IDD_LOCALCHANNEL),hwndDlg,LocalChannelItemProc,wParam);
        if (h)
        {
          RECT sz;
          GetClientRect(h,&sz);
          SetWindowPos(h,NULL,0,sz.bottom*m_num_children,0,0,SWP_NOSIZE|SWP_NOZORDER|SWP_NOACTIVATE);
          ShowWindow(h,SW_SHOWNA);
          m_num_children++;

          m_h=sz.right,sz.bottom*m_num_children;
        }
      }
    break;
    case WM_VSCROLL:
      {
        int nSBCode=LOWORD(wParam);
	      int nDelta=0;

	      int nMaxPos = m_h - m_wh;

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
  }
  return 0;
}

static BOOL WINAPI MainProc(HWND hwndDlg, UINT uMsg, WPARAM wParam, LPARAM lParam)
{
  static WDL_WndSizer resize;
  static HWND m_locwnd;
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

        m_locwnd=CreateDialog(g_hInst,MAKEINTRESOURCE(IDD_EMPTY),hwndDlg,LocalChannelListProc);
     
        int x;
        for (x = 0; x < 20; x ++)
        {
          g_client->SetLocalChannelInfo(x,"poo",true,0,false,0,true,false);
          SendMessage(m_locwnd,WM_USER+50,x,0);
        }
        SendDlgItemMessage(hwndDlg,IDC_MASTERVU,PBM_SETRANGE,0,MAKELPARAM(0,100));

        // this will change when the server info changes
        SendDlgItemMessage(hwndDlg,IDC_INTERVALPOS,PBM_SETRANGE,0,MAKELPARAM(0,16));


        g_last_wndpos.left = GetPrivateProfileInt(CONFSEC,"wnd_x",0,g_ini_file.Get());
        g_last_wndpos.top = GetPrivateProfileInt(CONFSEC,"wnd_y",0,g_ini_file.Get());
        g_last_wndpos.right = g_last_wndpos.left+GetPrivateProfileInt(CONFSEC,"wnd_w",0,g_ini_file.Get());
        g_last_wndpos.bottom = g_last_wndpos.top+GetPrivateProfileInt(CONFSEC,"wnd_h",0,g_ini_file.Get());
        
        if (g_last_wndpos.top || g_last_wndpos.left || g_last_wndpos.right || g_last_wndpos.bottom)
        {
          SetWindowPos(hwndDlg,NULL,g_last_wndpos.left,g_last_wndpos.top,g_last_wndpos.right-g_last_wndpos.left,g_last_wndpos.bottom-g_last_wndpos.top,SWP_NOZORDER);
        }
        // get window position from cfg
        else GetWindowRect(hwndDlg,&g_last_wndpos);

        ShowWindow(g_hwnd,SW_SHOW);
     
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
          static int vu_cnt;
          if (vu_cnt++ > 1)
          {
            vu_cnt=0;
            double val=VAL2DB(g_client->GetOutputPeak());
            int ival=(int)((val+100.0)/1.2);
            if (ival < 0) ival=0;
            else if (ival > 100) ival=100;
            SendDlgItemMessage(hwndDlg,IDC_MASTERVU,PBM_SETPOS,ival,0);

            char buf[128];
            sprintf(buf,"%.2f dB",val);
            SetDlgItemText(hwndDlg,IDC_MASTERVULBL,buf);

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

          }

          // update local/remote VU meters

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
          SendMessage(m_locwnd,WM_USER+49,0,0);
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
                    g_client->ChatMessage_Send("MSG",str);
                  }
                  else if (!strncasecmp(str,"/topic ",7)||
                           !strncasecmp(str,"/kick ",6) ||                        
                           !strncasecmp(str,"/bpm ",5) ||
                           !strncasecmp(str,"/bpi ",5)
                    ) // alias to /admin *
                  {
                    g_client->ChatMessage_Send("ADMIN",str+1);
                  }
                  else if (!strncasecmp(str,"/admin ",7))
                  {
                    char *p=str+7;
                    while (*p == ' ') p++;
                    g_client->ChatMessage_Send("ADMIN",p);
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
                      g_client->ChatMessage_Send("PRIVMSG",n,p);
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
                  g_client->ChatMessage_Send("MSG",str);
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



  g_ilog2x6 = 6.0/log10(2.0);

  JNL::open_socketlib();

  g_client = new NJClient;

  g_client->LicenseAgreementCallback = licensecallback;
  g_client->ChatMessage_Callback = chatmsg_cb;


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


#ifdef _WIN32
  ///// jesusonic stuff
  if (hDllInst) FreeLibrary(hDllInst);
  hDllInst=0;
  JesusonicAPI=0;

#endif

  JNL::close_socketlib();
  return 0;
}
