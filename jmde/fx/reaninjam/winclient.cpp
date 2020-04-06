/*
    NINJAM Windows Client - winclient.cpp
    Copyright (C) 2005 Cockos Incorporated
    Portions Copyright (C) 2005 Brennan Underwood

    NINJAM is free software; you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation; either version 2 of the License, or
    (at your option) any later version.

    NINJAM is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU General Public License for more details.

    You should have received a copy of the GNU General Public License
    along with NINJAM; if not, write to the Free Software
    Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
*/

/*

  Windows client code.
  
  */

#ifdef _WIN32
#include <windows.h>
#include <windowsx.h>
//int a=WM_ENDSESSION;
#include <richedit.h>
#include <shlobj.h>
#include <commctrl.h>
#include <process.h>
#define PREF_DIRSTR "\\"
#define strncasecmp strnicmp
#else
#define PREF_DIRSTR "/"
#include "../../../WDL/swell/swell.h"
#define RemoveDirectory(x) (!rmdir(x))
#define GetDesktopWindow() ((HWND)0)
#endif

#include <stdio.h>
#include <math.h>

#include "resource.h"

#include "../../../WDL/wingui/wndsize.h"
#include "../../../WDL/dirscan.h"
#include "../../../WDL/lineparse.h"

#include "../../../WDL/jnetlib/httpget.h"
#include "../../../WDL/wdlcstring.h"

#include "winclient.h"

#ifdef _WIN32
#include "../../../WDL/win32_utf8.c"
#endif

#define CONFSEC "ninjam"

#include "../../../WDL/setthreadname.h"

extern HWND (*GetMainHwnd)();
extern HANDLE * (*GetIconThemePointer)(const char *name);
extern int (*GetWindowDPIScaling)(HWND hwnd);
extern INT_PTR (*autoRepositionWindowOnMessage)(HWND hwnd, int msg, const char *desc_str, int flags); // flags unused currently
extern void *get_parent_project(void);
extern int (*GetPlayStateEx)(void *proj);
extern void (*OnPlayButtonForTime)(void *proj, double forTime);
extern void (*SetEditCurPos2)(void *proj, double time, bool moveview, bool seekplay);
extern void (*SetCurrentBPM)(void *proj, double bpm, bool wantUndo);
extern void (*GetSet_LoopTimeRange2)(void* proj, bool isSet, bool isLoop, double* startOut, double* endOut, bool allowautoseek);
extern int (*GetSetRepeatEx)(void* proj, int val);
extern double (*GetCursorPositionEx)(void *proj);
extern void (*Main_OnCommandEx)(int command, int flag, void *proj);

class VSTEffectClass;
extern VSTEffectClass *g_vst_object;

WDL_FastString g_ini_file;
static char g_inipath[1024]; 

WDL_Mutex g_client_mutex;
NJClient *g_client;
int g_done;
WDL_String g_topic;

HWND g_hwnd;
static HANDLE g_hThread;
static HWND m_locwnd,m_remwnd;
static int g_audio_enable=0;
static WDL_String g_connect_user,g_connect_pass,g_connect_host;
static int g_connect_passremember, g_connect_anon;
static RECT g_last_wndpos;
static int g_last_wndpos_state;
static int g_config_appear=0; // &1=don't flash beat counter on !(beat%16)
// static int g_config_sync=0; // unused
static bool s_want_sync;


#define SWAP(a,b,t) { t __tmp = (a); (a)=(b); (b)=__tmp; }

static void GetDefaultSessionDir(char *str, int strsize)
{
  extern void (*GetProjectPath)(char *buf, int bufsz);

  lstrcpyn(str,g_inipath,strsize-30); // default to wherever the ini file is (appdata\reaper etc)

  if (GetProjectPath)  GetProjectPath(str,strsize-30);
  strcat(str,PREF_DIRSTR "NINJAMsessions");
}

void audiostream_onsamples(float **inbuf, int innch, float **outbuf, int outnch, int len, int srate, bool isPlaying, bool isSeek, double curpos) 
{ 
  if (/*!g_audio_enable||*/!g_client) 
  {
    int x;
    if (outnch <= innch)
    {
      // clear all output buffers
      for (x = 0; x < outnch; x ++) 
      {
        memcpy(outbuf[x],inbuf[x],sizeof(float)*len);
      }
      for (x = 2; x < innch-1; x += 2)
      {
        int a=len;
        float *inptr=inbuf[x];
        float *inptr2=inbuf[x+1];

        float *outptr=outbuf[0];
        float *outptr2=outbuf[1];
        while (a--) 
        {
          *outptr++ += *inptr++;
          *outptr2++ += *inptr2++;
        }
      }
    }
    return;
  }
  g_client->AudioProc(inbuf,innch, outbuf, outnch, len,srate,!g_audio_enable, isPlaying, isSeek,curpos);
}



static WDL_DLGRET PrefsProc(HWND hwndDlg, UINT uMsg, WPARAM wParam, LPARAM lParam)
{
  switch (uMsg)
  {
    case WM_INITDIALOG:
      {
        if (GetPrivateProfileInt(CONFSEC,"savelocal",1,g_ini_file.Get()))
          CheckDlgButton(hwndDlg,IDC_SAVELOCAL,BST_CHECKED);
        if (GetPrivateProfileInt(CONFSEC,"savelocalwav",0,g_ini_file.Get()))
          CheckDlgButton(hwndDlg,IDC_SAVELOCALWAV,BST_CHECKED);

       if (!(g_config_appear&1)) CheckDlgButton(hwndDlg, IDC_FLASH, BST_CHECKED);

        char str[2048];
        GetPrivateProfileString(CONFSEC,"sessiondir","",str,sizeof(str),g_ini_file.Get());
        if (!str[0])
        {
          GetDefaultSessionDir(str,sizeof(str));
          SetDlgItemText(hwndDlg,IDC_SESSIONDIR,str);
        }
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
        #ifdef _WIN32
          {
            BROWSEINFO bi={0,};
			      LPITEMIDLIST idlist;
			      char name[2048];
			      GetDlgItemText(hwndDlg,IDC_SESSIONDIR,name,sizeof(name));
			      bi.hwndOwner = hwndDlg;
			      bi.pszDisplayName = name;
			      bi.lpszTitle = "Select a directory:";
			      bi.ulFlags = BIF_RETURNONLYFSDIRS|0x40;
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
        #else
        // todo: macport
        #endif
        break;
        case IDOK:
          {
            WritePrivateProfileString(CONFSEC,"savelocal",IsDlgButtonChecked(hwndDlg,IDC_SAVELOCAL)?"1":"0",g_ini_file.Get());
            WritePrivateProfileString(CONFSEC,"savelocalwav",IsDlgButtonChecked(hwndDlg,IDC_SAVELOCALWAV)?"1":"0",g_ini_file.Get());

            char buf[2048];
            GetDlgItemText(hwndDlg,IDC_SESSIONDIR,buf,sizeof(buf));
            char str[4096];
            GetDefaultSessionDir(str,sizeof(str));

            if (!strcmp(str,buf))
              buf[0]=0;
            WritePrivateProfileString(CONFSEC,"sessiondir",buf,g_ini_file.Get());

            g_config_appear &= ~1;
            if (!IsDlgButtonChecked(hwndDlg, IDC_FLASH)) g_config_appear |= 1;
            snprintf(buf, sizeof(buf), "%d", g_config_appear);
            WritePrivateProfileString(CONFSEC, "config_appear", buf, g_ini_file.Get());

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

#define MAX_HIST_ENTRIES 8

static JNL_HTTPGet *m_httpget=NULL;
static int m_getServerList_status;
static WDL_String m_listbuf;
static void getServerList_setStatusTxt(HWND hwndDlg, const char *txt)
{
  HWND list = GetDlgItem(hwndDlg, IDC_LIST1);
  ListView_DeleteAllItems(list);
  LVITEM item={0,};
  item.mask = LVIF_TEXT;
  item.iItem = 0;
  item.pszText = (char*)txt;
  ListView_InsertItem(list, &item);
}


static time_t serverlist_last_valid_t;

static int RepopulateServerList(HWND hwnd) // return 1 if END encountered
{
  HWND list = GetDlgItem(hwnd, IDC_LIST1);
  ListView_DeleteAllItems(list);

  LineParser lp(false);
  WDL_String tmp(m_listbuf.Get());
  char *p = tmp.Get();
  int i = 0;
  do 
  {
    char *d = strstr(p,"\n");
    if(!d) break;
    *d = 0;

    lp.parse(p);
    if(lp.getnumtokens()>0)
    {
      if(!stricmp(lp.gettoken_str(0),"end"))
        return 1;
    }
    if(lp.getnumtokens()>3)
    {
      if(!stricmp(lp.gettoken_str(0),"server"))
      {
        LVITEM item={0,};
        item.mask = LVIF_TEXT;
        item.iItem = i++;
        item.pszText = (char *)lp.gettoken_str(1);
        int a = ListView_InsertItem(list, &item);
        ListView_SetItemText(list, a, 1, (char*)lp.gettoken_str(2));
        ListView_SetItemText(list, a, 2, (char*)lp.gettoken_str(3));
      }
    }
  
    p = d+1;
  } while(1);
  return 0;
}


static void getServerList_step(HWND hwnd)
{
  switch(m_getServerList_status)
  {
  case 0:
    getServerList_setStatusTxt(hwnd, "Requesting list...");
    m_listbuf.Set("");
    delete m_httpget;
    m_httpget=new JNL_HTTPGet;
    m_httpget->connect("http://ninjam.com/serverlist.php");
    m_getServerList_status++;
    break;
  case 1:
    if (m_httpget)
    {
      int ret=m_httpget->run();
      while(m_httpget->bytes_available()>0)
      {
        char tmp[4096];
        int l = m_httpget->get_bytes(tmp,4095);
        tmp[l]=0;
        m_listbuf.Append(tmp);
      }
      if(ret==-1)
      {
        delete(m_httpget);
        m_httpget=NULL;
        getServerList_setStatusTxt(hwnd, "Error requesting server list!");
        m_getServerList_status=9999;
      }
      if(ret==1||strlen(m_listbuf.Get()) > 1024*1024*8) // prevent us from eating more than 8mb ram
      {
        delete(m_httpget);
        m_httpget=NULL;
        m_getServerList_status++;
      }
    }
    break;
  case 2:
    {
      delete m_httpget;
      m_httpget=0;

      
      if (RepopulateServerList(hwnd))
        serverlist_last_valid_t=time(NULL);
    }
    m_getServerList_status++;
    break;
  }
}

static WDL_DLGRET ConnectDlgProc(HWND hwndDlg, UINT uMsg, WPARAM wParam, LPARAM lParam)
{
  static WDL_WndSizer wndsize;
  switch (uMsg)
  {
    case WM_DESTROY:
        delete m_httpget;
        m_httpget=0;
        if (autoRepositionWindowOnMessage)
          autoRepositionWindowOnMessage(hwndDlg,uMsg,"reaninjamconnect",0);
        wndsize.init(NULL);
        {
          RECT r;
          GetWindowRect(hwndDlg,&r);
          char buf[32];
          snprintf(buf,sizeof(buf),"%d",r.right-r.left);
          WritePrivateProfileString(CONFSEC,"connectw",buf,g_ini_file.Get());
          snprintf(buf,sizeof(buf),"%d",r.bottom > r.top ? r.bottom-r.top : r.top-r.bottom);
          WritePrivateProfileString(CONFSEC,"connecth",buf,g_ini_file.Get());

        }
     return 0;
    case WM_SIZE:
     if (wParam != SIZE_MINIMIZED)
     {
      wndsize.onResize();
     }
     return 0;
    case WM_GETMINMAXINFO:
      {
        RECT init_r = wndsize.get_orig_rect_dpi();
        LPMINMAXINFO p=(LPMINMAXINFO)lParam;
        p->ptMinTrackSize.x = init_r.right-init_r.left;
        p->ptMinTrackSize.y = init_r.bottom-init_r.top;
      }
    return 0;
    case WM_INITDIALOG:
      {
        wndsize.init(hwndDlg);
        wndsize.init_item(IDC_HOST,0,1,0,1);
        wndsize.init_item(IDC_USER,0,1,0,1);
        wndsize.init_item(IDC_PASS,0,1,0,1);
        wndsize.init_item(IDC_PASSREMEMBER,0,1,0,1);
        wndsize.init_item(IDOK,1,1,1,1);
        wndsize.init_item(IDCANCEL,1,1,1,1);
        wndsize.init_item(IDC_BUTTON1,1,1,1,1);
        wndsize.init_item(IDC_CONNECT_LBL,0,1,0,1);
        wndsize.init_item(IDC_USERLBL,0,1,0,1);
        wndsize.init_item(IDC_PASSLBL,0,1,0,1);
        wndsize.init_item(IDC_INFOTEXT,0,1,0,1);
        wndsize.init_item(IDC_ANON,0,1,0,1);
        wndsize.init_item(IDC_LIST1,0,0,1,1);

        int ww=GetPrivateProfileInt(CONFSEC,"connectw",0,g_ini_file.Get());
        int wh=GetPrivateProfileInt(CONFSEC,"connecth",0,g_ini_file.Get());
        if (ww<1)
        {
          RECT r;
          GetWindowRect(hwndDlg,&r);
          if (ww<1) ww=(r.right-r.left)*3/2;
          if (wh<1) wh=r.bottom-r.top;
        }
        if (ww>0 && wh>0)
          SetWindowPos(hwndDlg,NULL,0,0,ww,wh,SWP_NOZORDER|SWP_NOMOVE|SWP_NOACTIVATE);

        int x;
        for (x = 0; x < MAX_HIST_ENTRIES; x ++)
        {
          char fmtbuf[32];
          sprintf(fmtbuf,"recent%02d",x);
          char hostbuf[512];
          GetPrivateProfileString(CONFSEC,fmtbuf,"",hostbuf,sizeof(hostbuf),g_ini_file.Get());
          if (hostbuf[0])
          {
            SendDlgItemMessage(hwndDlg,IDC_HOST,CB_ADDSTRING,0,(LPARAM)hostbuf);
          }
        }
        SetDlgItemText(hwndDlg,IDC_HOST,g_connect_host.Get());
        SetDlgItemText(hwndDlg,IDC_USER,g_connect_user.Get());
        if (g_connect_passremember)
        {
          SetDlgItemText(hwndDlg,IDC_PASS,g_connect_pass.Get());
          CheckDlgButton(hwndDlg,IDC_PASSREMEMBER,BST_CHECKED);
        }
        if (!g_connect_anon)
        {
          ShowWindow(GetDlgItem(hwndDlg,IDC_PASSLBL),SW_SHOWNA);
          ShowWindow(GetDlgItem(hwndDlg,IDC_PASS),SW_SHOWNA);
          ShowWindow(GetDlgItem(hwndDlg,IDC_PASSREMEMBER),SW_SHOWNA);          
        }
        else CheckDlgButton(hwndDlg,IDC_ANON,BST_CHECKED);

        HWND list = GetDlgItem(hwndDlg, IDC_LIST1);
        {
          LVCOLUMN lvc={LVCF_TEXT|LVCF_WIDTH,0,160,(char*)"Server"};
          ListView_InsertColumn(list,0,&lvc);
        }
        {
          LVCOLUMN lvc={LVCF_TEXT|LVCF_WIDTH,0,130,(char*)"Info"};
          ListView_InsertColumn(list,1,&lvc);
        }
        {
          LVCOLUMN lvc={LVCF_TEXT|LVCF_WIDTH,0,800,(char*)"Users"};
          ListView_InsertColumn(list,2,&lvc);
        }
        ListView_SetExtendedListViewStyleEx(GetDlgItem(hwndDlg,IDC_LIST1),LVS_EX_FULLROWSELECT,LVS_EX_FULLROWSELECT);

        if (!serverlist_last_valid_t || serverlist_last_valid_t < time(NULL)-60*2 || !m_listbuf.Get()[0] || !RepopulateServerList(hwndDlg))
        {
          m_getServerList_status = 0;
        }

        if (autoRepositionWindowOnMessage)
        {
          if (autoRepositionWindowOnMessage(hwndDlg,uMsg,"reaninjamconnect",0) & 1)
          {
#ifdef __APPLE__
            ShowWindow(hwndDlg,SW_SHOWNA);
#endif
          }
        }
          
        SetTimer(hwndDlg, 0x456, 100, 0);
      }
    return 0;
    case WM_NOTIFY:
      if (((LPNMHDR)lParam)->idFrom == IDC_LIST1 && (((LPNMHDR)lParam)->code == NM_DBLCLK || ((LPNMHDR)lParam)->code == LVN_ITEMCHANGED))
      {
        int i = ListView_GetNextItem(GetDlgItem(hwndDlg,IDC_LIST1),-1,LVNI_FOCUSED);
        if (i != -1 && !m_httpget)
        {
            char buf1[512];
            buf1[0]=0;
            LVITEM item={0,};
            item.mask = LVIF_TEXT;
            item.iItem = i;
            item.pszText = (char *)buf1;
            item.cchTextMax=sizeof(buf1);
            ListView_GetItem(GetDlgItem(hwndDlg,IDC_LIST1),&item);
            if (buf1[0])
            {
              SetDlgItemText(hwndDlg,IDC_HOST,buf1);

              // select item
              if (((LPNMHDR)lParam)->code == NM_DBLCLK)
                SendMessage(hwndDlg,WM_COMMAND,IDOK,0);
            }
        }
      }
    return 0;
    case WM_COMMAND:
      switch (LOWORD(wParam))
      {
        case IDC_BUTTON1:
          m_getServerList_status = 0;
        break;
        case IDC_ANON:
          if (IsDlgButtonChecked(hwndDlg,IDC_ANON))
          {
            ShowWindow(GetDlgItem(hwndDlg,IDC_PASSLBL),SW_HIDE);
            ShowWindow(GetDlgItem(hwndDlg,IDC_PASS),SW_HIDE);
            ShowWindow(GetDlgItem(hwndDlg,IDC_PASSREMEMBER),SW_HIDE);          
          }
          else
          {
            ShowWindow(GetDlgItem(hwndDlg,IDC_PASSLBL),SW_SHOWNA);
            ShowWindow(GetDlgItem(hwndDlg,IDC_PASS),SW_SHOWNA);
            ShowWindow(GetDlgItem(hwndDlg,IDC_PASSREMEMBER),SW_SHOWNA);          
          }
        break;

        case IDOK:
          {
            g_connect_passremember=!!IsDlgButtonChecked(hwndDlg,IDC_PASSREMEMBER);
            g_connect_anon=!!IsDlgButtonChecked(hwndDlg,IDC_ANON);
            WritePrivateProfileString(CONFSEC,"anon",g_connect_anon?"1":"0",g_ini_file.Get());
            char buf[512];
            GetDlgItemText(hwndDlg,IDC_HOST,buf,sizeof(buf));
            if(!buf[0])
            {
              MessageBox(hwndDlg,"You must enter a server hostname!","Error",0);
              break;
            }
            g_connect_host.Set(buf);
            WritePrivateProfileString(CONFSEC,"host",buf,g_ini_file.Get());
            GetDlgItemText(hwndDlg,IDC_USER,buf,sizeof(buf));
            if(!buf[0] && !g_connect_anon)
            {
              MessageBox(hwndDlg,"You must enter a username!","Error",0);
              break;
            }
            g_connect_user.Set(buf);
            WritePrivateProfileString(CONFSEC,"user",buf,g_ini_file.Get());
            GetDlgItemText(hwndDlg,IDC_PASS,buf,sizeof(buf));
            g_connect_pass.Set(buf);
            WritePrivateProfileString(CONFSEC,"pass",g_connect_passremember?buf:"",g_ini_file.Get());
            WritePrivateProfileString(CONFSEC,"passrem",g_connect_passremember?"1":"0",g_ini_file.Get());

            int x,len=(int)SendDlgItemMessage(hwndDlg,IDC_HOST,CB_GETCOUNT,0,0);;
            int o=1;
            WritePrivateProfileString(CONFSEC,"recent00",g_connect_host.Get(),g_ini_file.Get());

            if (len > MAX_HIST_ENTRIES-1) len=MAX_HIST_ENTRIES-1;
            for (x = 0; x < len; x ++)
            {
              char hostbuf[1024];
              if (SendDlgItemMessage(hwndDlg,IDC_HOST,CB_GETLBTEXT,x,(LPARAM)hostbuf)== CB_ERR) continue;
              if (!stricmp(hostbuf,g_connect_host.Get())) continue;

              char fmtbuf[32];
              sprintf(fmtbuf,"recent%02d",o++);
              WritePrivateProfileString(CONFSEC,fmtbuf,hostbuf,g_ini_file.Get());
            }

            EndDialog(hwndDlg,1);
          }
        break;
        case IDCANCEL:
          EndDialog(hwndDlg,0);
        break;
      }
    return 0;
    case WM_TIMER:
      if(wParam == 0x456) getServerList_step(hwndDlg);
      break;
    case WM_CLOSE:
      delete m_httpget;
      m_httpget = NULL;
      EndDialog(hwndDlg,0);
    return 0;
  }
  return 0;
}

static void updateConnectButton(HWND hwnd)
{
  if (hwnd && g_client)
  {
    SetDlgItemText(hwnd,IDC_CONNECT,g_client->GetStatus()==0 ? "Disconnect" : "Connect...");
  }
}

static void do_disconnect()
{
  g_audio_enable=0;
  s_want_sync=false;

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
    chat_addline("","Disconnected from server");
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
  updateConnectButton(g_hwnd);
}

WDL_FastString g_last_status("Status: not connected.");

static void do_connect()
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
  {
    GetDefaultSessionDir(sroot,sizeof(sroot));
  }

  WDL_remove_trailing_dirchars(sroot);

  CreateDirectory(sroot,NULL);
  while (cnt < 16)
  {
    time_t tv = time(NULL);
    struct tm *t=tv > 0 ? localtime(&tv) : NULL;
  
    buf[0]=0;

    lstrcpyn_safe(buf,sroot,(int) sizeof(buf));
    if (t)
    {
      snprintf_append(buf,sizeof(buf),PREF_DIRSTR "%04d%02d%02d_%02d%02d",
        t->tm_year+1900,t->tm_mon+1,t->tm_mday,t->tm_hour,t->tm_min);
    }

    if (cnt) snprintf_append(buf,sizeof(buf),"_%d",cnt);
    lstrcatn(buf,".ninjam",(int) sizeof(buf));

    if (CreateDirectory(buf,NULL)) break;

    cnt++;
  }
  if (cnt >= 16)
  {      
    g_last_status.Set("Status: ERROR CREATING SESSION DIRECTORY");
    InvalidateRect(GetDlgItem(g_hwnd,IDC_INTERVALPOS),NULL,FALSE);
    MessageBox(g_hwnd,"Error creating session directory!", "NINJAM error", MB_OK);
    return;
  }

  g_client_mutex.Enter();
  
  SendMessage(m_locwnd,WM_LCUSER_REPOP_CH,0,0);

  g_client->SetWorkDir(buf);

  g_client->config_savelocalaudio=0;
  if (GetPrivateProfileInt(CONFSEC,"savelocal",1,g_ini_file.Get()))
  {
    g_client->config_savelocalaudio|=1;
    if (GetPrivateProfileInt(CONFSEC,"savelocalwav",0,g_ini_file.Get())) 
      g_client->config_savelocalaudio|=2;
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

  g_last_status.Set("Status: Connecting...");
  InvalidateRect(GetDlgItem(g_hwnd,IDC_INTERVALPOS),NULL,FALSE);

  EnableMenuItem(GetMenu(g_hwnd),ID_OPTIONS_AUDIOCONFIGURATION,MF_BYCOMMAND|MF_GRAYED);
  EnableMenuItem(GetMenu(g_hwnd),ID_FILE_DISCONNECT,MF_BYCOMMAND|MF_ENABLED);
  updateConnectButton(g_hwnd);
}

static void updateMasterControlLabels(HWND hwndDlg)
{
   char buf[512];
   mkvolstr(buf,g_client->config_mastervolume);
   SetDlgItemText(hwndDlg,IDC_MASTERVOLLBL,buf);

   mkpanstr(buf,g_client->config_masterpan);
   SetDlgItemText(hwndDlg,IDC_MASTERPANLBL,buf);

   mkvolstr(buf,g_client->config_metronome);
   SetDlgItemText(hwndDlg,IDC_METROVOLLBL,buf);

   mkpanstr(buf,g_client->config_metronome_pan);
   SetDlgItemText(hwndDlg,IDC_METROPANLBL,buf);
}


static unsigned WINAPI ThreadFunc(LPVOID p)
{
  WDL_SetThreadName("reaper/ninjam");
  while (!g_done)
  {
    g_client_mutex.Enter();
    while (!g_client->Run());
    g_client_mutex.Leave();
    Sleep(1);
  }
  return 0;
}


#ifdef _MSC_VER
#include <multimon.h>
#endif
#undef GetSystemMetrics


#ifndef _WIN32

void my_getViewport(RECT *r, const RECT *sr, bool wantWork) {
  return SWELL_GetViewPort(r,sr,wantWork);
}
#else

void my_getViewport(RECT *r, const RECT *sr, bool wantWork) {
  if (sr) 
  {
	  static HINSTANCE hlib;
    static bool haschk;
    
    if (!haschk && !hlib) { hlib=LoadLibrary("user32.dll");haschk=true; }

	  if (hlib) 
    {

      static HMONITOR (WINAPI *Mfr)(LPCRECT lpcr, DWORD dwFlags);
      static BOOL (WINAPI *Gmi)(HMONITOR mon, MONITORINFOEX* lpmi);

      if (!Mfr) Mfr = (HMONITOR (WINAPI *)(LPCRECT, DWORD)) GetProcAddress(hlib, "MonitorFromRect");
      if (!Gmi) Gmi = (BOOL (WINAPI *)(HMONITOR,MONITORINFOEX*)) GetProcAddress(hlib,"GetMonitorInfoA");    

			if (Mfr && Gmi) {
			  HMONITOR hm;
			  hm=Mfr(sr,MONITOR_DEFAULTTONULL);
        if (hm) {
          MONITORINFOEX mi;
          memset(&mi,0,sizeof(mi));
          mi.cbSize=sizeof(mi);

          if (Gmi(hm,&mi)) {
            if (wantWork)
              *r=mi.rcWork;
            else *r=mi.rcMonitor;
            return;
          }          
        }
			}
		}
	}
  if (wantWork)
    SystemParametersInfo(SPI_GETWORKAREA,0,r,0);
  else
    GetWindowRect(GetDesktopWindow(), r);
}
#endif

static void EnsureNotCompletelyOffscreen(RECT *r)
{
  RECT tmp;
  RECT scr;
  my_getViewport(&scr, r,false);
  if (!IntersectRect(&tmp,&scr,r)) 
  {
    r->right -= r->left;
    r->bottom -= r->top;
    r->left = 0;
    r->top = 0;
  }
}

static int last_interval_len=-1;
static int last_interval_pos=-1;
static int last_bpm_i=-1;
LRESULT WINAPI ninjamStatusProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam)
{
  static int s_cap_inf;
  switch (msg)
  {
    case WM_SIZE:
      InvalidateRect(hwnd,NULL,FALSE);
    return 0;
    case WM_SETCURSOR:
      {
        POINT p;
        GetCursorPos(&p);
        ScreenToClient(hwnd,&p);
        if (p.y < 8)
        {
          SetCursor(LoadCursor(NULL,IDC_SIZENS));
        }
        else
        {
          SetCursor(LoadCursor(NULL,IDC_ARROW));
        }
      }
    return 1;
    case WM_LBUTTONDOWN:
      if (GET_Y_LPARAM(lParam) < 8)
      {
        s_cap_inf = GET_Y_LPARAM(lParam);
        SetCapture(hwnd);
      }
    return 0;
    case WM_MOUSEMOVE:
    case WM_LBUTTONUP:
      if (GetCapture()==hwnd)
      {
        if (msg == WM_LBUTTONUP) ReleaseCapture();
        int y = GET_Y_LPARAM(lParam);
        if (y != s_cap_inf || msg == WM_LBUTTONUP)
        {
          SendMessage(GetParent(hwnd),WM_USER+100,0,y-s_cap_inf);
        }
      }
    return 0;
    case WM_ERASEBKGND: return 0;
    case WM_PAINT:
      {
        PAINTSTRUCT ps;
        if (BeginPaint(hwnd,&ps))
        {
          bool want_numbers=g_client && !g_client->is_likely_lobby();

          RECT r;
          GetClientRect(hwnd,&r);
          bool flip = !(g_config_appear&1) && want_numbers &&
            last_bpm_i>0 && (last_interval_pos == 0 || (last_interval_len > 16 && (last_interval_len&15)==0 && !(last_interval_pos&15)));
          int fg = RGB(128,255,128), bg=RGB(0,0,0);
          if (flip) { int tmp=fg; fg=bg; bg=tmp; }

          {
            HBRUSH bgbr = CreateSolidBrush(bg);
            FillRect(ps.hdc,&r,bgbr);
            DeleteObject(bgbr);
          }

          static HFONT font1, font2;
          static int lasth;
          const int fontsz=wdl_max(12,r.bottom/8);
          if (!font1 || lasth != r.bottom)
          {
            lasth = r.bottom;
            if (font1) DeleteObject(font1);
            if (font2) DeleteObject(font2);
            LOGFONT lf={ fontsz, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE, ANSI_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, DEFAULT_QUALITY, DEFAULT_PITCH, "Arial"};
            font1 = CreateFontIndirect(&lf);
            lf.lfHeight = lasth;
            font2 = CreateFontIndirect(&lf);
          }
          SetBkMode(ps.hdc,TRANSPARENT);
          SetTextColor(ps.hdc,RGB(0,128,255));
          HGDIOBJ oldfont = SelectObject(ps.hdc,font1);
          const int pad = fontsz > 12 ? 3 : 1;
          RECT tr = { r.left+pad,r.top+pad,r.right-pad,r.bottom-pad};
          if (last_bpm_i>0)
          {
            char buf[128];
            snprintf(buf,sizeof(buf),"%d BPM %d BPI",last_bpm_i,last_interval_len);
            DrawText(ps.hdc,buf,-1,&tr,(lasth > fontsz*5/2 ?DT_LEFT:DT_RIGHT)|DT_TOP|DT_NOPREFIX|DT_SINGLELINE);
          }
          if (g_last_status.GetLength())
          {
            DrawText(ps.hdc,g_last_status.Get(),-1,&tr,DT_LEFT|DT_BOTTOM|DT_NOPREFIX|DT_SINGLELINE);
          }
          SetTextColor(ps.hdc,fg);
          if (last_bpm_i > 0 && last_interval_len > 1 && want_numbers)
          {
            SelectObject(ps.hdc,font2);
            char buf[128];
            snprintf(buf,sizeof(buf),"%d",last_interval_pos+1);
            RECT sz={0,};
            DrawText(ps.hdc,buf,-1,&sz,DT_NOPREFIX|DT_SINGLELINE|DT_CALCRECT);
            RECT ur = { last_interval_pos * (r.right-sz.right) / (last_interval_len-1), 0, r.right, r.bottom };
            DrawText(ps.hdc,buf,-1,&ur,DT_NOPREFIX|DT_SINGLELINE|DT_LEFT|DT_VCENTER);
          }
          SelectObject(ps.hdc,oldfont);

          EndPaint(hwnd,&ps);
        }
      }
    return 0;
  }
  return DefWindowProc(hwnd,msg,wParam,lParam);
}

static bool str_begins_tok(const char *str, const char *tok)
{
  size_t l = strlen(tok);
  if (strncasecmp(str,tok,l)) return false;

  return tok[l] == ' ' || tok[l] == 0;
}


static int g_lchan_sz;
static int g_min_lchan_sz;
static WDL_DLGRET MainProc(HWND hwndDlg, UINT uMsg, WPARAM wParam, LPARAM lParam)
{
  static RECT s_init_r;
  static int cap_mode;
  static int cap_spos;
  static WDL_WndSizer resize;
  switch (uMsg)
  {
    case WM_CTLCOLOREDIT:
    case WM_CTLCOLORLISTBOX:
    case WM_CTLCOLORBTN:
    case WM_CTLCOLORDLG:
    case WM_CTLCOLORSTATIC :
    case WM_DRAWITEM:
      return SendMessage(GetMainHwnd(),uMsg,wParam,lParam);;
    case WM_INITDIALOG:
      {
        s_want_sync=false;

      #ifdef _WIN32
        {
          HWND h;
          if (GetMainHwnd && (h=GetMainHwnd()))
            SetClassLongPtr(hwndDlg,GCLP_HICON,GetClassLongPtr(h,GCLP_HICON));
        }
      #else
        {
          HMENU menu=LoadMenu(g_hInst,MAKEINTRESOURCE(IDR_MENU1));
          SetMenu(hwndDlg,menu);
#ifdef __APPLE__
          HMENU normalFirst=GetMenu(GetMainHwnd());
          if (normalFirst) normalFirst=GetSubMenu(normalFirst,0);
          HMENU nm=SWELL_DuplicateMenu(normalFirst);
          if (nm)
          {
            MENUITEMINFO mi={sizeof(mi),MIIM_STATE|MIIM_SUBMENU|MIIM_TYPE,MFT_STRING,0,0,nm,NULL,NULL,0,(char*)"REAPER"};
            InsertMenuItem(menu,0,TRUE,&mi);           
          }
          SetMenuItemModifier(menu,ID_FILE_CONNECT,MF_BYCOMMAND,'O',FCONTROL);
          SetMenuItemModifier(menu,ID_FILE_DISCONNECT,MF_BYCOMMAND,'D',FCONTROL);
          SetMenuItemModifier(menu,ID_OPTIONS_PREFERENCES,MF_BYCOMMAND,',',FCONTROL);
          SetMenuItemText(menu,ID_SYNC_MENU_HDR, MF_BYCOMMAND, "This menu can be opened directly via Cmd+Y");
#endif
        } 
      #endif
        GetWindowRect(hwndDlg,&s_init_r);
        if (s_init_r.bottom < s_init_r.top) SWAP(s_init_r.top,s_init_r.bottom,int);
        

        g_hwnd=hwndDlg;

        resize.init(hwndDlg);

        // top items
        resize.init_item(IDC_MASTERVOL,  0.0,  0.0,  0.7f,  0.0);
        resize.init_item(IDC_METROVOL,   0.0,  0.0,  0.7f,  0.0);
        resize.init_item(IDC_MASTERVOLLBL,  0.8f,  0.0,  0.8f,  0.0);
        resize.init_item(IDC_MASTERPANLBL,  0.8f,  0.0,  0.8f,  0.0);
        resize.init_item(IDC_METROVOLLBL,   0.8f,  0.0,  0.8f,  0.0);
        resize.init_item(IDC_METROPANLBL,   0.8f,  0.0,  0.8f,  0.0);

        resize.init_item(IDC_MASTERPAN,  0.7f,  0.0,  0.8f,  0.0);
        resize.init_item(IDC_METROPAN,   0.7f,  0.0,  0.8f,  0.0);
        resize.init_item(IDC_MASTERMUTE, 0.8f,  0.0,  0.8f,  0.0);
        resize.init_item(IDC_METROMUTE,  0.8f,  0.0,  0.8f,  0.0);
        resize.init_item(IDC_MASTERVU,   0.8f,  0.0,  1.0f,  0.0);
        
               
        resize.init_item(IDC_DIV1,        0.0,  0.0,  1.0f,  0.0);
        resize.init_item(IDC_DIV3,        0.0,  0.0,  1.0f,  0.0);
        
        resize.init_item(IDC_INTERVALPOS, 0.0,  1.0,  1.0f,  1.0);
        
        const float loc_ratio = 0.0f;
        resize.init_item(IDC_CHATLBL,     0.0f, loc_ratio,  0.0f,  loc_ratio);

        resize.init_item(IDC_CHATDISP,     0.0f, loc_ratio,  1.0f,  1.0f);
        resize.init_item(IDC_CHATENT,      0.0f, 1.0f,  1.0f,  1.0f);
        resize.init_item(IDC_CHATOK, 1,1,1,1);
        

        resize.init_item(IDC_LOCRECT,     0.0f, 0.0f,  1.0f,  loc_ratio);
        resize.init_item(IDC_LOCGRP,     0.0f, 0.0f,  0.0f,  0.0f);
        
        resize.init_item(IDC_REMOTERECT,  1.0f, 0.0f,  1.0f,  1.0f);      
        resize.init_item(IDC_DIV2,        0.0,  loc_ratio,  1.0f,  loc_ratio);
        resize.init_item(IDC_REMGRP,  1.0f, 0.0f,  1.0f,  0.0f);      

        g_config_appear=GetPrivateProfileInt(CONFSEC, "config_appear", g_config_appear, g_ini_file.Get());
        //g_config_sync=GetPrivateProfileInt(CONFSEC, "config_sync", g_config_sync, g_ini_file.Get());

        char tmp[512];
//        SendDlgItemMessage(hwndDlg,IDC_MASTERVOL,TBM_SETRANGE,FALSE,MAKELONG(0,100));
        SendDlgItemMessage(hwndDlg,IDC_MASTERVOL,TBM_SETTIC,FALSE,-1);       
        GetPrivateProfileString(CONFSEC,"mastervol","1.0",tmp,sizeof(tmp),g_ini_file.Get());
        g_client->config_mastervolume=(float)atof(tmp);
        SendDlgItemMessage(hwndDlg,IDC_MASTERVOL,TBM_SETPOS,TRUE,(LPARAM)DB2SLIDER(VAL2DB(g_client->config_mastervolume)));
        SendDlgItemMessage(hwndDlg,IDC_MASTERVOL,WM_USER+9999,1,(LPARAM)"Master volume");

//        SendDlgItemMessage(hwndDlg,IDC_METROVOL,TBM_SETRANGE,FALSE,MAKELONG(0,100));
        SendDlgItemMessage(hwndDlg,IDC_METROVOL,TBM_SETTIC,FALSE,-1);       
        GetPrivateProfileString(CONFSEC,"metrovol","0.5",tmp,sizeof(tmp),g_ini_file.Get());
        g_client->config_metronome=(float)atof(tmp);
        SendDlgItemMessage(hwndDlg,IDC_METROVOL,TBM_SETPOS,TRUE,(LPARAM)DB2SLIDER(VAL2DB(g_client->config_metronome)));
        SendDlgItemMessage(hwndDlg,IDC_METROVOL,WM_USER+9999,1,(LPARAM)"Metronome volume");

        SendDlgItemMessage(hwndDlg,IDC_MASTERPAN,TBM_SETRANGE,FALSE,MAKELONG(0,100));
        SendDlgItemMessage(hwndDlg,IDC_MASTERPAN,TBM_SETTIC,FALSE,50);       
        GetPrivateProfileString(CONFSEC,"masterpan","0.0",tmp,sizeof(tmp),g_ini_file.Get());
        g_client->config_masterpan=(float)atof(tmp);
        int t=(int)(g_client->config_masterpan*50.0) + 50;
        if (t < 0) t=0; else if (t > 100)t=100;
        SendDlgItemMessage(hwndDlg,IDC_MASTERPAN,TBM_SETPOS,TRUE,t);
        SendDlgItemMessage(hwndDlg,IDC_MASTERPAN,WM_USER+9999,1,(LPARAM)"Master pan");

        SendDlgItemMessage(hwndDlg,IDC_METROPAN,TBM_SETRANGE,FALSE,MAKELONG(0,100));
        SendDlgItemMessage(hwndDlg,IDC_METROPAN,TBM_SETTIC,FALSE,50);       
        GetPrivateProfileString(CONFSEC,"metropan","0.0",tmp,sizeof(tmp),g_ini_file.Get());
        g_client->config_metronome_pan=(float)atof(tmp);
        t=(int)(g_client->config_metronome_pan*50.0) + 50;
        if (t < 0) t=0; else if (t > 100)t=100;
        SendDlgItemMessage(hwndDlg,IDC_METROPAN,TBM_SETPOS,TRUE,t);
        SendDlgItemMessage(hwndDlg,IDC_METROPAN,WM_USER+9999,1,(LPARAM)"Metronome pan");

        if (GetPrivateProfileInt(CONFSEC,"mastermute",0,g_ini_file.Get()))
        {
          g_client->config_mastermute=true;
        }
        if (GetPrivateProfileInt(CONFSEC,"metromute",0,g_ini_file.Get()))
        {
          g_client->config_metronome_mute=true;
        }
        SendDlgItemMessage(hwndDlg,IDC_METROMUTE,BM_SETIMAGE,IMAGE_ICON|0x8000,(LPARAM)GetIconThemePointer(g_client->config_metronome_mute?"track_mute_on":"track_mute_off"));
        SendDlgItemMessage(hwndDlg,IDC_MASTERMUTE,BM_SETIMAGE,IMAGE_ICON|0x8000,(LPARAM)GetIconThemePointer(g_client->config_mastermute?"track_mute_on":"track_mute_off"));

        SendDlgItemMessage(hwndDlg,IDC_MASTERMUTE,WM_USER+0x300,0xbeef,(LPARAM)"Master mute");
        SendDlgItemMessage(hwndDlg,IDC_METROMUTE,WM_USER+0x300,0xbeef,(LPARAM)"Metronome mute");

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
            g_client->SetLocalChannelInfo(0,"default channel",true,1024,false,0,true,true);
            SendMessage(m_locwnd,WM_LCUSER_ADDCHILD,0,0);
          }
          for (x = 0; x < cnt; x ++)
          {
            char buf[4096];
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
              int ok=0;
              const char *name=NULL;
              if (ch >= 0 && ch <= MAX_LOCAL_CHANNELS) for (n = 1; n < lp.getnumtokens()-1; n += 2)
              {
                switch (lp.gettoken_enum(n,"source\0bc\0mute\0solo\0volume\0pan\0jesus\0name\0flag\0"))
                {
                  case 0: // source 
                    {
                      g_client->SetLocalChannelInfo(ch,NULL,true,lp.gettoken_int(n+1),false,0,false,false);
                    }
                  break;
                  case 1: //broadcast
                    g_client->SetLocalChannelInfo(ch,NULL,false,0,false,0,true,!!lp.gettoken_int(n+1));
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
                      //wj=1;
                    }
                  break;
                  case 7: //name
                    g_client->SetLocalChannelInfo(ch,name=lp.gettoken_str(n+1),false,0,false,0,false,false);
                    ok|=1;
                  break;
                  case 8: //flag
                    g_client->SetLocalChannelInfo(ch,NULL,false,0,false,0,false,false,false,0,true,lp.gettoken_int(n+1));
                  break;
                  default:
                  break;
                }
              }
              if (ok)
              {

                SendMessage(m_locwnd,WM_LCUSER_ADDCHILD,ch,0);

              }
            }
          }
        }

        {
          RECT r;
          GetWindowRect(GetDlgItem(hwndDlg,IDC_LOCRECT),&r);
          g_min_lchan_sz = r.bottom-r.top;
          if (g_min_lchan_sz<0) g_min_lchan_sz=-g_min_lchan_sz;
          if (GetWindowDPIScaling)
            g_min_lchan_sz = g_min_lchan_sz * 256 / GetWindowDPIScaling(hwndDlg);

          g_lchan_sz=GetPrivateProfileInt(CONFSEC,"lchansz",g_lchan_sz,g_ini_file.Get());          
          if (g_lchan_sz < g_min_lchan_sz)
            g_lchan_sz = g_min_lchan_sz;
        }

        {
          int sz=GetPrivateProfileInt(CONFSEC,"bpisz",0,g_ini_file.Get());
          if (sz>0) SendMessage(hwndDlg,WM_USER+100,100,sz);
        }

        int ws= g_last_wndpos_state = GetPrivateProfileInt(CONFSEC,"wnd_state",0,g_ini_file.Get());


        g_last_wndpos.left = GetPrivateProfileInt(CONFSEC,"wnd_x",0,g_ini_file.Get());
        g_last_wndpos.top = GetPrivateProfileInt(CONFSEC,"wnd_y",0,g_ini_file.Get());
        g_last_wndpos.right = g_last_wndpos.left+GetPrivateProfileInt(CONFSEC,"wnd_w",640,g_ini_file.Get());
        g_last_wndpos.bottom = g_last_wndpos.top+GetPrivateProfileInt(CONFSEC,"wnd_h",480,g_ini_file.Get());
        
        if (g_last_wndpos.top || g_last_wndpos.left || g_last_wndpos.right || g_last_wndpos.bottom)
        {
          EnsureNotCompletelyOffscreen(&g_last_wndpos);

          SetWindowPos(hwndDlg,NULL,g_last_wndpos.left,g_last_wndpos.top,g_last_wndpos.right-g_last_wndpos.left,g_last_wndpos.bottom-g_last_wndpos.top,SWP_NOZORDER);
        }
        else 
        {
          GetWindowRect(hwndDlg,&g_last_wndpos);
          if (g_last_wndpos.bottom < g_last_wndpos.top) SWAP(g_last_wndpos.bottom,g_last_wndpos.top,int);
        }

        if (ws > 0) ShowWindow(hwndDlg,SW_SHOWMAXIMIZED);
        #ifdef _WIN32
        else if (ws < 0) ShowWindow(hwndDlg,SW_SHOWMINIMIZED);
        #endif
        else ShowWindow(hwndDlg,SW_SHOW);
     
        SetTimer(hwndDlg,1,10,NULL);

        unsigned id;
        g_hThread=(HANDLE)_beginthreadex(NULL,0,ThreadFunc,0,0,&id);

        SendMessage(hwndDlg,WM_SIZE,0,0);
      }
    return 0;
    case WM_TIMER:
      if (wParam == 1)
      {
        static int in_t;
        static int m_last_status = 0xdeadbeef;
        static int cycle_cnt;
        if (!in_t)
        {
          const bool do_slow_things = (cycle_cnt++%5)==0;
          in_t=1;          

          licenseRun(hwndDlg);

          if (g_client->HasUserInfoChanged())
          {
            if (m_remwnd) SendMessage(m_remwnd,WM_RCUSER_UPDATE,0,0);
          }

          int ns=g_client->GetStatus();
          if (ns != m_last_status)
          {
            WDL_String errstr(g_client->GetErrorStr());
            m_last_status=ns;
            if (ns < 0)
            {
              do_disconnect();
            }
            else
            {
              updateConnectButton(hwndDlg);
            }

            if (ns == NJClient::NJC_STATUS_OK)
            {
              g_last_status.Set("Status: Connected to ");
              g_last_status.Append(g_client->GetHostName());
              g_last_status.Append(" as ");
              g_last_status.Append(g_client->GetUser());

              InvalidateRect(GetDlgItem(hwndDlg,IDC_INTERVALPOS),NULL,FALSE);
            }
            else if (errstr.Get()[0])
            {
              g_last_status.Set("Status: ");
              g_last_status.Append(errstr.Get());
              InvalidateRect(GetDlgItem(hwndDlg,IDC_INTERVALPOS),NULL,FALSE);
            }
            else
            {
              if (ns == NJClient::NJC_STATUS_DISCONNECTED)
              {
                g_last_status.Set("Status: disconnected from host.");
                InvalidateRect(GetDlgItem(hwndDlg,IDC_INTERVALPOS),NULL,FALSE);
                MessageBox(g_hwnd,"Disconnected from host!", "NINJAM Notice", MB_OK);
              }
              if (ns == NJClient::NJC_STATUS_INVALIDAUTH)
              {
                g_last_status.Set("Status: invalid authentication info.");
                InvalidateRect(GetDlgItem(hwndDlg,IDC_INTERVALPOS),NULL,FALSE);
                MessageBox(g_hwnd,"Error connecting: invalid authentication information!", "NINJAM error", MB_OK);
              }
              if (ns == NJClient::NJC_STATUS_CANTCONNECT)
              {
                g_last_status.Set("Status: can't connect to host.");
                InvalidateRect(GetDlgItem(hwndDlg,IDC_INTERVALPOS),NULL,FALSE);
                MessageBox(g_hwnd,"Error connecting: can't connect to host!", "NINJAM error", MB_OK);
              }
            }
          }
          {
            int intp, intl;
            int pos, len;
            g_client->GetPosition(&pos,&len);
            if (!len) len=1;
            intl=g_client->GetBPI();
            intp = (pos * intl)/len;

            int bpm=(int)g_client->GetActualBPM();
            if (ns != NJClient::NJC_STATUS_OK)
            {
              bpm=0;
              intp=-1;
            }

            int ival=(int) floor(VAL2DB(g_client->GetOutputPeak(0))*10.0);
            int ival2=(int) floor(VAL2DB(g_client->GetOutputPeak(1))*10.0);
            SendDlgItemMessage(hwndDlg,IDC_MASTERVU,WM_USER+1010,ival,ival2);

            if (intl != last_interval_len || last_bpm_i != bpm || intp != last_interval_pos)
            {
              if (s_want_sync && !intp)
              {
                if (bpm > 0 && intl > 0 && !g_client->is_likely_lobby() &&
                  GetCursorPositionEx && OnPlayButtonForTime)
                {
                  void *__proj = get_parent_project();
                  double npos=GetCursorPositionEx(__proj);
                  int srate=g_client->GetSampleRate();
                  if (srate > 0)
                    npos += (double)pos/(double)srate;
                  OnPlayButtonForTime(__proj,npos);
                }
                s_want_sync=false;
              }

              last_interval_pos = intp;
              last_interval_len=intl;
              last_bpm_i = bpm;
              InvalidateRect(GetDlgItem(hwndDlg,IDC_INTERVALPOS),NULL,FALSE);
            }

            if (do_slow_things)
            {
              SendMessage(m_locwnd,WM_LCUSER_VUUPDATE,0,0);
              SendMessage(m_remwnd,WM_LCUSER_VUUPDATE,0,0);
            }
          }
          if (do_slow_things)
          {
            chatRun(hwndDlg);
          }

          in_t=0;
        }
      }
    break;
    case WM_GETMINMAXINFO:
      {
        RECT init_r = s_init_r;
        resize.sizer_to_dpi_rect(&init_r);
        LPMINMAXINFO p=(LPMINMAXINFO)lParam;
        p->ptMinTrackSize.x = init_r.right-init_r.left;
        p->ptMinTrackSize.y = (init_r.bottom-init_r.top)*2/3;
      }
    return 0;
    case WM_LBUTTONUP:
      if (GetCapture() == hwndDlg)
      {
        cap_mode=0;
        ReleaseCapture();
      }
    return 0;
    case WM_MOUSEMOVE:
      if (GetCapture() == hwndDlg)
      {
        if (cap_mode == 1)
        {
          int y = GET_Y_LPARAM(lParam)-cap_spos;

          RECT r;
          GetWindowRect(GetDlgItem(hwndDlg,IDC_LOCRECT),&r);
          ScreenToClient(hwndDlg,(LPPOINT)&r);
          y -= r.top; // todo mac ugh
          if (GetWindowDPIScaling)
            y = y * 256 / GetWindowDPIScaling(hwndDlg);

          if (y < g_min_lchan_sz) y = g_min_lchan_sz;
          if (g_lchan_sz != y)
          {
            g_lchan_sz = y;
            SendMessage(hwndDlg,WM_SIZE,0,0);
          }
        }
      }
    return 0;
    case WM_SETCURSOR:
      {
        POINT p;
        GetCursorPos(&p);
        RECT r;
        GetWindowRect(GetDlgItem(hwndDlg,IDC_DIV2),&r);
         if (r.bottom < r.top) SWAP(r.bottom,r.top,int);
        if (p.x >= r.left && p.x <= r.right && 
            p.y >= r.top - 4 && p.y <= r.bottom + 4)
        {
          SetCursor(LoadCursor(NULL,IDC_SIZENS));
          return 1;
        } 
      }
    return 0;
    case WM_LBUTTONDOWN:
      {
        POINT p={GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam)};
        ClientToScreen(hwndDlg,&p);
        RECT r;
        GetWindowRect(GetDlgItem(hwndDlg,IDC_DIV2),&r);
        if (r.bottom < r.top) SWAP(r.bottom,r.top,int);

        if (p.x >= r.left && p.x <= r.right && 
            p.y >= r.top - 4 && p.y <= r.bottom + 4)
        {
          SetCapture(hwndDlg);
          cap_mode=1;
          cap_spos=p.y - r.top;
        }
      }
    return 0;

    case WM_USER+100:
    case WM_SIZE:
      {
        WDL_WndSizer__rec *rec = resize.get_item(IDC_INTERVALPOS);
        if (rec)
        {
          const int orig_sz = rec->real_orig.bottom - rec->real_orig.top;
          int maxsz = orig_sz*2;
          const int minsz = orig_sz/8;
          RECT cr;
          GetClientRect(hwndDlg,&cr);
          cr.bottom = resize.dpi_to_sizer(cr.bottom);
          if (maxsz > cr.bottom/5) maxsz=cr.bottom/5;

          int sz;
          if (uMsg != WM_SIZE && wParam == 100)
            sz = resize.dpi_to_sizer((int)lParam,256);
          else
            sz = (rec->orig.bottom - rec->orig.top) - (uMsg == WM_SIZE ? 0 : resize.dpi_to_sizer((int)lParam));

          if (sz < minsz) sz = minsz;
          if (sz > maxsz) sz = maxsz;

          const int marg = sz - orig_sz;
          rec->orig.top = rec->real_orig.top;
          rec->orig.bottom = rec->real_orig.bottom + marg;

          resize.set_margins(0,0,0,marg);
          if (uMsg != WM_SIZE && wParam != 100 && !GetCapture())
          {
            char str[64];
            snprintf(str,sizeof(str),"%d",resize.sizer_to_dpi(sz,256));
            WritePrivateProfileString(CONFSEC,"bpisz",str,g_ini_file.Get());
          }
        }
      }
      if (uMsg != WM_SIZE && wParam == 100) return 0;

      if (wParam != SIZE_MINIMIZED) 
      {
        RECT orig_rect=resize.get_orig_rect();
        int bm=0;
        resize.get_margins(NULL,NULL,NULL,&bm);
        orig_rect.bottom += bm;

        RECT cr;
        GetClientRect(hwndDlg,&cr);
        cr.right = resize.dpi_to_sizer(cr.right);
        cr.bottom = resize.dpi_to_sizer(cr.bottom);

        WDL_WndSizer__rec *loc = resize.get_item(IDC_LOCRECT);
        WDL_WndSizer__rec *chate = resize.get_item(IDC_CHATENT);
        WDL_WndSizer__rec *chatd = resize.get_item(IDC_CHATDISP);
        WDL_WndSizer__rec *chatl = resize.get_item(IDC_CHATLBL);
        WDL_WndSizer__rec *div = resize.get_item(IDC_DIV2);

        int sz = g_lchan_sz;
        int __dpi = 256;
        if (GetWindowDPIScaling) sz = (__dpi=GetWindowDPIScaling(hwndDlg))*sz/256;
        sz = resize.dpi_to_sizer(sz);

        // limit sz by window size
        {
          int bpad = orig_rect.bottom - chate->real_orig.bottom;
          int tpad = loc->real_orig.top;
          int mpad = chatl->real_orig.top - loc->real_orig.bottom;
          int avail = cr.bottom - bpad - tpad - mpad - sz;
          const int min_chat_sz = resize.dpi_to_sizer(60 * __dpi/256);
          if (avail < min_chat_sz) sz -= min_chat_sz-avail;
          if (sz<40) sz=40;
        }

        loc->orig.bottom = loc->orig.top + sz;
        int offs = loc->orig.bottom - loc->real_orig.bottom;
        div->orig = div->real_orig; OffsetRect(&div->orig, 0, offs);
        chatl->orig = chatl->real_orig; OffsetRect(&chatl->orig, 0, offs);
        chatd->orig.top = chatd->real_orig.top + offs;

        resize.onResize();
        if (m_locwnd) SendMessage(m_locwnd,WM_LCUSER_RESIZE,0,0);
        if (m_remwnd) SendMessage(m_remwnd,WM_LCUSER_RESIZE,0,0);
      }
      if (wParam == SIZE_MINIMIZED || wParam == SIZE_MAXIMIZED) 
      {
        if (wParam == SIZE_MINIMIZED) g_last_wndpos_state=-1;
        else if (wParam == SIZE_MAXIMIZED) g_last_wndpos_state=1;
        return 0;
      }
    case WM_MOVE:
      {
      
    #ifdef _WIN32
        WINDOWPLACEMENT wp={sizeof(wp)};
        GetWindowPlacement(hwndDlg,&wp);
        if (wp.showCmd == SW_SHOWMAXIMIZED) g_last_wndpos_state=1;
        else if (wp.showCmd == SW_MINIMIZE || wp.showCmd == SW_SHOWMINIMIZED) g_last_wndpos_state=-1;
        else 
        {
          if (IsWindowVisible(hwndDlg) && !IsIconic(hwndDlg) && !IsZoomed(hwndDlg))
    #endif
            GetWindowRect(hwndDlg,&g_last_wndpos);
          if (g_last_wndpos.bottom < g_last_wndpos.top) SWAP(g_last_wndpos.bottom,g_last_wndpos.top,int);
          g_last_wndpos_state = 0;
    #ifdef _WIN32
        }
    #endif
      }
    break;

    case WM_HSCROLL:
      {
        double pos=(double)SendMessage((HWND)lParam,TBM_GETPOS,0,0);

		    if ((HWND) lParam == GetDlgItem(hwndDlg,IDC_MASTERVOL))
        {
          double v=SLIDER2DB(pos);
          if (fabs(v- -6.0) < 0.5) v=-6.0;
          else if (v < -115.0) v=-1000.0;
          g_client->config_mastervolume=(float)DB2VAL(v);
        }
		    else if ((HWND) lParam == GetDlgItem(hwndDlg,IDC_METROVOL))
        {
          double v=SLIDER2DB(pos);
          if (fabs(v- -6.0) < 0.5) v=-6.0;
          else if (v < -115.0) v=-1000.0;
          g_client->config_metronome=(float)DB2VAL(v);
        }
		    else if ((HWND) lParam == GetDlgItem(hwndDlg,IDC_MASTERPAN))
        {
          pos=(pos-50.0)/50.0;
          if (fabs(pos) < 0.08) pos=0.0;
          g_client->config_masterpan=(float) pos;
        }
		    else if ((HWND) lParam == GetDlgItem(hwndDlg,IDC_METROPAN))
        {
          pos=(pos-50.0)/50.0;
          if (fabs(pos) < 0.08) pos=0.0;
          g_client->config_metronome_pan=(float) pos;
        }
        else return 0;

        updateMasterControlLabels(hwndDlg);
      }
    return 0;

    case WM_COMMAND:
      switch (LOWORD(wParam))
      {
        case IDC_MASTERMUTE:
          g_client->config_mastermute=!g_client->config_mastermute;
          SendDlgItemMessage(hwndDlg,IDC_MASTERMUTE,BM_SETIMAGE,IMAGE_ICON|0x8000,(LPARAM)GetIconThemePointer(g_client->config_mastermute?"track_mute_on":"track_mute_off"));
        break;
        case IDC_MASTERVOLLBL:
          if (HIWORD(wParam) == STN_DBLCLK) {
            g_client->config_mastervolume = 1.0;
            SendDlgItemMessage(hwndDlg,IDC_MASTERVOL,TBM_SETPOS,TRUE,(LPARAM)DB2SLIDER(VAL2DB(g_client->config_mastervolume)));
            updateMasterControlLabels(hwndDlg);
          }
        break;
        case IDC_MASTERPANLBL:
          if (HIWORD(wParam) == STN_DBLCLK) {
            g_client->config_masterpan = 0.0;
            updateMasterControlLabels(hwndDlg);
            int t=(int)(g_client->config_masterpan*50.0) + 50;
            if (t < 0) t=0; else if (t > 100)t=100;
            SendDlgItemMessage(hwndDlg,IDC_MASTERPAN,TBM_SETPOS,TRUE,t);
          }
        break;
        case IDC_METROVOLLBL:
          if (HIWORD(wParam) == STN_DBLCLK) {
            g_client->config_metronome = 1.0;
            SendDlgItemMessage(hwndDlg,IDC_METROVOL,TBM_SETPOS,TRUE,(LPARAM)DB2SLIDER(VAL2DB(g_client->config_metronome)));
            updateMasterControlLabels(hwndDlg);
          }
        break;
        case IDC_METROPANLBL:
          if (HIWORD(wParam) == STN_DBLCLK) {
            g_client->config_metronome_pan = 0.0;
            updateMasterControlLabels(hwndDlg);
            int t=(int)(g_client->config_metronome_pan*50.0) + 50;
            if (t < 0) t=0; else if (t > 100)t=100;
            SendDlgItemMessage(hwndDlg,IDC_METROPAN,TBM_SETPOS,TRUE,t);
          }
        break;
        case IDC_METROMUTE:
          g_client->config_metronome_mute =!g_client->config_metronome_mute;
          SendDlgItemMessage(hwndDlg,IDC_METROMUTE,BM_SETIMAGE,IMAGE_ICON|0x8000,(LPARAM)GetIconThemePointer(g_client->config_metronome_mute?"track_mute_on":"track_mute_off"));
        break;
        case IDC_CONNECT:
          if (g_client->GetStatus() != 0)
          {
            SendMessage(hwndDlg,WM_COMMAND,ID_FILE_CONNECT,0);
            return 0;
          }
            
           // fall through
        case ID_FILE_DISCONNECT:
          do_disconnect();
          g_last_status.Set("Status: disconnected manually");
          InvalidateRect(GetDlgItem(hwndDlg,IDC_INTERVALPOS),NULL,FALSE);
        break;
        case ID_FILE_CONNECT:
          if (DialogBox(g_hInst,MAKEINTRESOURCE(IDD_CONNECT),hwndDlg,ConnectDlgProc))
          {
            do_disconnect();

            do_connect();
            HWND fg = GetForegroundWindow();
            if (fg && (fg == hwndDlg || IsChild(hwndDlg,fg)))
              SetFocus(GetDlgItem(hwndDlg,IDC_CHATENT));
          }
        break;
        case IDC_SYNC:
        {
          HMENU hm=CreatePopupMenu();
          int hpos=0;
          int check=(s_want_sync ? MF_CHECKED : 0);
          int gray=(last_bpm_i > 0 && last_interval_len > 0 ? 0 : MF_GRAYED);
          InsertMenu(hm, hpos++, MF_BYPOSITION|MF_STRING|check|gray, IDC_SYNCATLOOP,
            "Start REAPER playback on next loop");
          InsertMenu(hm, hpos++, MF_BYPOSITION|MF_SEPARATOR, 0, NULL);
          InsertMenu(hm, hpos++, MF_BYPOSITION|MF_STRING|gray, IDC_MATCHBPM_SETLOOP,
            "Set project tempo and loop at project start");
          InsertMenu(hm, hpos++, MF_BYPOSITION|MF_STRING|gray, IDC_SETBPM,
            "Set project tempo");
          InsertMenu(hm, hpos++, MF_BYPOSITION|MF_STRING|gray, IDC_SETLOOP,
            "Set loop at edit cursor");

          RECT r;
          GetWindowRect(GetDlgItem(hwndDlg, IDC_SYNC), &r);
          TrackPopupMenu(hm, 0, r.left, r.bottom, 0, hwndDlg, NULL);
          DestroyMenu(hm);
        }
        break;
        case IDC_SYNCATLOOP:
          if (last_bpm_i > 0 && last_interval_len > 0)
          {
            s_want_sync=!s_want_sync;
          }
        break;
        case IDC_MATCHBPM_SETLOOP:
          if (last_bpm_i > 0 && last_interval_len > 0 &&
            SetEditCurPos2 && SetCurrentBPM && GetSet_LoopTimeRange2 && GetSetRepeatEx)
          {
            void *__proj = get_parent_project();
            double spos=0.0, epos=(double)last_interval_len/(double)last_bpm_i*60.0;
            SetEditCurPos2(__proj, 0.0, false, false);
            SetCurrentBPM(__proj, (double)last_bpm_i, true);
            GetSet_LoopTimeRange2(__proj, true, true, &spos, &epos, false);
            GetSetRepeatEx(__proj, 1);
          }
        break;
        case IDC_SETBPM:
          if (last_bpm_i > 0 && last_interval_len > 0 &&
            Main_OnCommandEx && SetCurrentBPM)
          {
            void *__proj=get_parent_project();
#define ID_CLEAR_TEMPO_ENV 42395
            Main_OnCommandEx(ID_CLEAR_TEMPO_ENV, 0, __proj);
            SetCurrentBPM(__proj, (double)last_bpm_i, true);
          }
        break;
        case IDC_SETLOOP:
          if (last_bpm_i > 0 && last_interval_len > 0 &&
            GetCursorPositionEx && GetSet_LoopTimeRange2 && GetSetRepeatEx)
          {
            void *__proj=get_parent_project();
            double spos=GetCursorPositionEx(__proj);
            double epos=spos+(double)last_interval_len/(double)last_bpm_i*60.0;
            GetSet_LoopTimeRange2(__proj, true, true, &spos, &epos, false);
            GetSetRepeatEx(__proj, 1);
          }
        break;
        case ID_OPTIONS_PREFERENCES:
          DialogBox(g_hInst,MAKEINTRESOURCE(IDD_PREFS),hwndDlg,PrefsProc);
        break;
        case IDC_CHATOK:
          {
            char buf[256];
            GetDlgItemText(hwndDlg,IDC_CHATENT,buf,255);
            if (buf[0])
            {
              char *str = buf;
              while (*str == ' ') str++;
              if (str_begins_tok(str,"/clear"))
              {
                SetDlgItemText(hwndDlg,IDC_CHATDISP,"");
#ifndef _WIN32
                extern WDL_TypedQueue<char> g_chat_textappend;
                g_chat_textappend.Clear();
#endif
              }
              else if (g_client->GetStatus() == NJClient::NJC_STATUS_OK)
              {
                if (str[0] == '/')
                {
                  if (str_begins_tok(str,"/me"))
                  {
                    g_client_mutex.Enter();
                    g_client->ChatMessage_Send("MSG",str);
                    g_client_mutex.Leave();
                  }
                  else if (str_begins_tok(str,"/msg"))
                  {
                    char *p=str+4;
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
                      chat_addline(NULL,tmp.Get());
                    }
                    else
                    {
                      chat_addline("","error: /msg requires a username and a message.");
                    }
                  }
                  else
                  {
                    const char *p = str+1;
                    if (str_begins_tok(p,"admin")) p += 5;
                    while (*p == ' ') p++;
                    g_client_mutex.Enter();
                    g_client->ChatMessage_Send("ADMIN",p);
                    g_client_mutex.Leave();
                  }
                }
                else
                {
                  g_client_mutex.Enter();
                  g_client->ChatMessage_Send("MSG",buf);
                  g_client_mutex.Leave();
                }
              }
              else
              {
                chat_addline("","error: not connected to a server.");
              }
            }
            SetDlgItemText(hwndDlg,IDC_CHATENT,"");
            SetFocus(GetDlgItem(hwndDlg,IDC_CHATENT));
          }
        break;
      }
    break;
    case WM_CLOSE:
      {
        if (g_vst_object)
          ShowWindow(hwndDlg,SW_HIDE);
        else 
          DestroyWindow(hwndDlg);
      }
    break;  
    #ifdef _WIN32
    case WM_ENDSESSION:
    #endif
    case WM_DESTROY:

      resize.init(NULL);

      g_done=1;
      WaitForSingleObject(g_hThread,INFINITE);
      CloseHandle(g_hThread);

      do_disconnect();

      // save config


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
          int flag=0;
      
          lcn=g_client->GetLocalChannelInfo(a,&sch,NULL,&bc,NULL,&flag);
          g_client->GetLocalChannelMonitoring(a,&v,&p,&m,&s);
          g_client->GetLocalChannelProcessor(a,NULL,&has_jesus);

          char *ptr=lcn;
          while (*ptr)
          {
            if (*ptr == '`') *ptr='\'';
            ptr++;
          }
          if (strlen(lcn) > 128) lcn[127]=0;
          char buf[4096];
          WDL_String sstr;
          sprintf(buf,"%d",sch);
          sstr.Set(buf);           
          
          snprintf(buf,sizeof(buf),"%d source '%s' bc %d mute %d solo %d volume %f pan %f jesus %d flag %d name `%s`",a,sstr.Get(),bc,m,s,v,p,!!has_jesus,flag,lcn);
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
        
        sprintf(buf,"%d",g_last_wndpos_state);
        WritePrivateProfileString(CONFSEC,"wnd_state",buf,g_ini_file.Get());
        sprintf(buf,"%d",g_lchan_sz);
        WritePrivateProfileString(CONFSEC,"lchansz",buf,g_ini_file.Get());
        

        sprintf(buf,"%d",(int)g_last_wndpos.left);
        WritePrivateProfileString(CONFSEC,"wnd_x",buf,g_ini_file.Get());
        sprintf(buf,"%d",(int)g_last_wndpos.top);
        WritePrivateProfileString(CONFSEC,"wnd_y",buf,g_ini_file.Get());
        sprintf(buf,"%d",(int)(g_last_wndpos.right - g_last_wndpos.left));
        WritePrivateProfileString(CONFSEC,"wnd_w",buf,g_ini_file.Get());
        sprintf(buf,"%d",(int)(g_last_wndpos.bottom - g_last_wndpos.top));
        WritePrivateProfileString(CONFSEC,"wnd_h",buf,g_ini_file.Get());


        snprintf(buf,sizeof(buf),"%f",g_client->config_mastervolume);
        WritePrivateProfileString(CONFSEC,"mastervol",buf,g_ini_file.Get());
        snprintf(buf,sizeof(buf),"%f",g_client->config_masterpan);
        WritePrivateProfileString(CONFSEC,"masterpan",buf,g_ini_file.Get());
        snprintf(buf,sizeof(buf),"%f",g_client->config_metronome);
        WritePrivateProfileString(CONFSEC,"metrovol",buf,g_ini_file.Get());
        snprintf(buf,sizeof(buf),"%f",g_client->config_metronome_pan);
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
          int ch=0;
          g_client->GetLocalChannelInfo(a,&ch,NULL,NULL);
        }
      }


      delete g_client;
      g_client=0;
      g_done=0;
      m_locwnd=m_remwnd=0;
      g_audio_enable=0;

      JNL::close_socketlib();
      g_hwnd=NULL;
    return 0;
    case WM_INITMENUPOPUP:
      if (wParam)
      {
        HMENU menu = (HMENU)wParam;
        int check=(s_want_sync ? MF_CHECKED : 0);
        int gray=(last_bpm_i > 0 && last_interval_len > 0 ? 0 : MF_GRAYED);
        CheckMenuItem(menu, IDC_SYNCATLOOP, MF_BYCOMMAND|check);
        EnableMenuItem(menu, IDC_SYNCATLOOP, MF_BYCOMMAND|gray);
        EnableMenuItem(menu, IDC_MATCHBPM_SETLOOP, MF_BYCOMMAND|gray);
        EnableMenuItem(menu, IDC_SYNCATLOOP, MF_BYCOMMAND|gray);
        EnableMenuItem(menu, IDC_SETBPM, MF_BYCOMMAND|gray);
        EnableMenuItem(menu, IDC_SETLOOP, MF_BYCOMMAND|gray);
      }
    return 0;
  }
  return 0;
}

#ifndef _WIN32

HWND customControlCreator(HWND parent, const char *cname, int idx, const char *classname, int style, int x, int y, int w, int h)
{
  HWND hw=0;
  if (!strcmp(classname,"ninjamstatus"))
  {
    hw=CreateDialog(g_hInst,0,parent,(DLGPROC)ninjamStatusProc);
  }
  
  if (hw)
  {
    SetWindowLong(hw,GWL_ID,idx);
    SetWindowPos(hw,HWND_TOP,x,y,w,h,SWP_NOZORDER|SWP_NOACTIVATE);
    ShowWindow(hw,SW_SHOWNA);
    return hw;
  }
  
  return 0;
}


#endif

void InitializeInstance()
{
  static int first;
  if (!first)
  {
    first=1;
    #ifdef _WIN32
    InitCommonControls();
    CoInitialize(0);
    #endif

    {
      GetModuleFileName(NULL,g_inipath,sizeof(g_inipath));
      WDL_remove_filepart(g_inipath);
      g_ini_file.Set(g_inipath);
      g_ini_file.Append(PREF_DIRSTR "reaninjam.ini");
      FILE *fp = fopenUTF8(g_ini_file.Get(),"r+");
      if (fp) fclose(fp);
      else
      {
        extern const char *(*get_ini_file)();
        const char *ini_file = get_ini_file ? get_ini_file() : NULL;
        if (ini_file)
        {
          g_ini_file.Set(ini_file);
          g_ini_file.remove_filepart();
          lstrcpyn(g_inipath,g_ini_file.Get(),sizeof(g_inipath));
          g_ini_file.Append(PREF_DIRSTR "reaninjam.ini");
        }
        // use reaper.ini path
      }

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
      g_connect_passremember=GetPrivateProfileInt(CONFSEC,"passrem",1,g_ini_file.Get());
      g_connect_anon=GetPrivateProfileInt(CONFSEC,"anon",0,g_ini_file.Get());
    }


#ifdef _WIN32
    { // load richedit DLL
      WNDCLASS wc={0,};
      if (!LoadLibrary("RichEd20.dll")) LoadLibrary("RichEd32.dll");

      // make richedit20a point to RICHEDIT
      if (!GetClassInfo(NULL,"RichEdit20A",&wc))
      {
        GetClassInfo(NULL,"RICHEDIT",&wc);
      }
//        wc.style &= ~CS_GLOBALCLASS;
      wc.lpszClassName = "RichEditChild";
      RegisterClass(&wc);
    }
    {
      WNDCLASS wc={CS_HREDRAW|CS_VREDRAW|CS_DBLCLKS,ninjamStatusProc,};
      wc.lpszClassName="ninjamstatus";
      wc.hInstance=g_hInst;
      RegisterClass(&wc);        
    }
#else
    SWELL_RegisterCustomControlCreator(customControlCreator);
#endif
  }
  if (!g_client)
  {
    JNL::open_socketlib();
    g_client = new NJClient;

    g_client->LicenseAgreementCallback = licensecallback;
    g_client->ChatMessage_Callback = chatmsg_cb;
  }
  if (g_client)
  {
    if (!g_hwnd) g_hwnd=CreateDialog(g_hInst,MAKEINTRESOURCE(IDD_MAIN),GetMainHwnd?GetMainHwnd() : GetDesktopWindow(), MainProc);
  }

}


void QuitInstance()
{
  if (!g_hwnd || !IsWindowVisible(g_hwnd))
  {
    if (g_client)
    {
      if (g_hwnd) DestroyWindow(g_hwnd);
      g_hwnd=0;
      delete g_client;
      g_client=0;
      JNL::close_socketlib();
    }
    g_done=0;
    m_locwnd=m_remwnd=0;
    g_audio_enable=0;
  }
  //UnregisterClass("RichEditChild",NULL);
}


void mkvolpanstr(char *str, double vol, double pan)
{
  mkvolstr(str,vol);
  char *p=str+strlen(str);
  *p++=' ';
  mkpanstr(p,pan);
}

void mkpanstr(char *str, double pan)
{
  if (fabs(pan) < 0.0001) strcpy(str,"center");
  else sprintf(str,"%d%%%s", (int)fabs(pan*100.0),(pan>0.0 ? "R" : "L"));
}

void mkvolstr(char *str, double vol)
{
  double v=VAL2DB(vol);
  if (vol < 0.0000001 || v < -120.0) v=-120.0;
  snprintf(str,16,"%+2.1fdB",v);   
}


extern HWND g_local_channel_wnd, g_remote_channel_wnd;
// returns: 0 if unknown. Otherwise: &0xff=1-based channel index, &0xff00=0,remote user (1-based, in this case channel index=0 for user but no channel)
int getChannelFromHWND(HWND h, HWND *hwndP)
{
  if (hwndP) *hwndP = NULL;
  if (!h) return 0;

  if (g_local_channel_wnd && IsChild(g_local_channel_wnd,h))
  {
    for (;;)
    {
      HWND h2 = GetParent(h);
      if (!h2) return 0;
      if (h2 == g_local_channel_wnd) break;
      h = h2;
    }
    HWND hc = GetWindow(g_local_channel_wnd,GW_CHILD);
    int cnt;
    for (cnt = 0; cnt < 256 && hc != h; cnt ++)
      hc = GetWindow(hc,GW_HWNDNEXT);
    if (hc == h)
    {
      if (hwndP) *hwndP = h;
      return cnt;
    }
  }
  else if (g_remote_channel_wnd && IsChild(g_remote_channel_wnd,h))
  {
    for (;;)
    {
      HWND h2 = GetParent(h);
      if (!h2) return 0;
      if (h2 == g_remote_channel_wnd) break;
      h = h2;
    }
    int idx = (int)GetWindowLongPtr(h,GWLP_USERDATA);
    if (idx == 0x0fffffff) return 0;

    if (hwndP) *hwndP = h;

    if (idx<0)
      return (-idx)<<8;

    return (((idx>>16)+1)<<8) | ((idx&0xff)+1);
  }
  return 0;
}

HWND getHWNDFromChannel(int chan) // see above
{
  if (chan >= 1 && chan <= 0xff)
  {
    if (!g_local_channel_wnd) return NULL;
    HWND hc = GetWindow(g_local_channel_wnd,GW_CHILD);
    while (chan-- > 0 && hc) hc = GetWindow(hc,GW_HWNDNEXT);
    return hc;
  }
  else if ((chan&0xff00)>=0x100)
  {
    if (!g_remote_channel_wnd) return NULL;
    int scan_id;
    if (!(chan&0xff)) scan_id = -((chan>>8)&0xff);
    else scan_id = (((chan>>8)-1)<<16) | ((chan-1)&0xff);

    int maxc=1000;
    HWND h = GetWindow(g_remote_channel_wnd,GW_CHILD);
    while (h && maxc--)
    {
      if (GetWindowLongPtr(h,GWLP_USERDATA) == scan_id) 
      {
        return h;
      }

      h = GetWindow(h,GW_HWNDNEXT);
    }
  }
  return NULL;
}
