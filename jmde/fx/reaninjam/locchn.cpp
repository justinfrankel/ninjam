/*
    NINJAM Windows Client - locchn.cpp
    Copyright (C) 2005 Cockos Incorporated

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

  Local channel (sub) dialog code

  */


#ifdef _WIN32
#include <windows.h>
#include <commctrl.h>
#else
#include "swell/swell.h"
#endif

#define SWAP(a,b,t) { t __tmp = (a); (a)=(b); (b)=__tmp; }

#include <math.h>

#include "winclient.h"

#include "resource.h"
#include "wingui/wndsize.h"

extern HWND (*GetMainHwnd)();
extern HANDLE * (*GetIconThemePointer)(const char *name);

static INT_PTR ForwardToHostMainWindow(HWND self, UINT uMsg, WPARAM wParam, LPARAM lParam)
{
  HWND h = GetMainHwnd ? GetMainHwnd() : NULL;
  if (h && h != self) return SendMessage(h,uMsg,wParam,lParam);
  return 0;
}


class LocalChannelRec
{
public:
  LocalChannelRec(int idx) { m_idx=idx; }
  ~LocalChannelRec() {}

  int m_idx;
  WDL_WndSizer wndsizer;
};


static void UpdateVolPanLabels(HWND hwndDlg, double vol, double pan)
{
  char tmp[512], vs[128], ps[128];;
  mkvolstr(vs,vol);
  mkpanstr(ps,pan);
  snprintf(tmp,sizeof(tmp),"%s %s %s",__LOCALIZE("Monitor:","reaninjam_DLG_110"),vs,ps);
  SetDlgItemText(hwndDlg,IDC_LABEL1,tmp);
}

static const struct { int br; const char *str; const char *sstr; } brtab[] =
{
  // !WANT_LOCALIZE_STRINGS_BEGIN:reaninjam
  { 32, "Extra Low Quality - approx 32kbps", "LQ" },
  { 64, "Default - approx 64kbps", "" },
  { 96, "Better Quality - approx 96kbps", "BQ" },
  { 128, "High Quality - approx 128kbps", "HQ" },
  { 192, "Very High Quality - approx 200kbps", "VHQ" },
  { 256, "Extra High Quality - approx 300kbps", "XQ" },
  // !WANT_LOCALIZE_STRINGS_END
};

static void UpdateXmitButtonText(HWND hwndDlg, int f, int br)
{
  const char *lbl;
  if (f&2) lbl = __LOCALIZE("Voice Chat","reaninjam");
  else if (f&4) lbl = __LOCALIZE("Session Mode","reaninjam");
  else lbl = __LOCALIZE("Normal NINJAM","reaninjam");
  char tmp[256];
  const char *qstr=NULL;
  const int nbr = (int) (sizeof(brtab)/sizeof(brtab[0]));
  for (int x = 0; x < nbr && !qstr; x++)
    if (br == brtab[x].br) qstr = brtab[x].sstr;
  if (!qstr)
  {
    snprintf(tmp,sizeof(tmp),"%s %dk",lbl,br);
    lbl = tmp;
  }
  else if (*qstr)
  {
    snprintf(tmp,sizeof(tmp),"%s %s",lbl,__localizeFunc(qstr,"reaninjam",0));
    lbl = tmp;
  }
  SetDlgItemText(hwndDlg,IDC_ASYNCXMIT,lbl);
}

static WDL_DLGRET LocalChannelItemProc(HWND hwndDlg, UINT uMsg, WPARAM wParam, LPARAM lParam)
{
  if (uMsg == WM_INITDIALOG)
  {
    SetWindowLongPtr(hwndDlg,GWLP_USERDATA,(INT_PTR)new LocalChannelRec(lParam));
  }
  LocalChannelRec *_this = (LocalChannelRec*)GetWindowLongPtr(hwndDlg,GWLP_USERDATA);
  int m_idx=_this?_this->m_idx:0;
  static const struct { int id; const char *str; } s_accesslist[] = {
    // !WANT_LOCALIZE_STRINGS_BEGIN:reaninjam_access
    { IDC_NAME, "Local channel name" },
    { IDC_AUDIOIN, "Input channel" },
    { IDC_ASYNCXMIT, "Local channel mode" },
    { IDC_LOCALOUT, "Local channel output" },
    { IDC_TRANSMIT, "Transmit enabled" },
    { IDC_REMOVE, "Remove local channel" },
    // !WANT_LOCALIZE_STRINGS_END
  };
  switch (uMsg)
  {
    case WM_DESTROY:
      if (SetWindowAccessibilityString)
        for (int x = 0; x < (int)(sizeof(s_accesslist)/sizeof(s_accesslist[0])); x++)
          SetWindowAccessibilityString(GetDlgItem(hwndDlg,s_accesslist[x].id),NULL,0);

      delete _this;
    return 0;
    case WM_NOTIFY:
#ifdef _WIN32
      {
        extern LRESULT (*handleCheckboxCustomDraw)(HWND, LPARAM, const unsigned short *list, int listsz, bool isdlg);
        const unsigned short list[] = { IDC_TRANSMIT };
        if (handleCheckboxCustomDraw)
          return handleCheckboxCustomDraw(hwndDlg,lParam,list,1,true);
      }
#endif
    break;

    case WM_CTLCOLOREDIT:
    case WM_CTLCOLORLISTBOX:
    case WM_CTLCOLORBTN:
    case WM_CTLCOLORDLG:
    case WM_CTLCOLORSTATIC :
    case WM_DRAWITEM:
      return ForwardToHostMainWindow(hwndDlg,uMsg,wParam,lParam);
    case WM_INITDIALOG:
      {
        if (_this)
        {
          _this->wndsizer.init(hwndDlg);
          _this->wndsizer.init_item(IDC_VU,0.0f,0.0f,0.0f,0.0f);
          _this->wndsizer.init_item(IDC_VOL,0.0f,0.0f,0.8f,0.0f);
          _this->wndsizer.init_item(IDC_PAN,0.8f,0.0f,1.0f,0.0f);
          _this->wndsizer.init_item(IDC_MUTE,1.0f,0.0f,1.0f,0.0f);
          _this->wndsizer.init_item(IDC_LABEL1,0.0f,0.0f,1.0f,0.0f);
          _this->wndsizer.init_item(IDC_SOLO,1.0f,0.0f,1.0f,0.0f);
          _this->wndsizer.init_item(IDC_REMOVE,1.0f,0.0f,1.0f,0.0f);
          _this->wndsizer.init_item(IDC_EDGE,0.0f,0.0f,1.0f,0.0f);
        }
        if (SetWindowAccessibilityString)
          for (int x = 0; x < (int)(sizeof(s_accesslist)/sizeof(s_accesslist[0])); x++)
            SetWindowAccessibilityString(GetDlgItem(hwndDlg,s_accesslist[x].id),__localizeFunc(s_accesslist[x].str,"reaninjam_access",0),0);
        int sch;
        bool bc;
        WDL_UTF8_HookComboBox(GetDlgItem(hwndDlg,IDC_AUDIOIN));

        int f=0, br=0;
        const char *buf=g_client->GetLocalChannelInfo(m_idx,&sch,&br,&bc,NULL,&f);
        float vol=0.0,pan=0.0 ;
        bool ismute=0,issolo=0;
        g_client->GetLocalChannelMonitoring(m_idx, &vol, &pan, &ismute, &issolo);

        if (buf) SetDlgItemText(hwndDlg,IDC_NAME,buf);
        if (bc) CheckDlgButton(hwndDlg,IDC_TRANSMIT,BST_CHECKED);
        SendDlgItemMessage(hwndDlg,IDC_MUTE,BM_SETIMAGE,IMAGE_ICON|0x8000,(LPARAM)GetIconThemePointer(ismute?"track_mute_on":"track_mute_off"));
        SendDlgItemMessage(hwndDlg,IDC_MUTE,WM_USER+0x300,0xbeef,(LPARAM)(ismute ? __LOCALIZE("Unmute local channel","reaninjam") :
              __LOCALIZE("Mute local channel","reaninjam")));
        SendDlgItemMessage(hwndDlg,IDC_SOLO,BM_SETIMAGE,IMAGE_ICON|0x8000,(LPARAM)GetIconThemePointer(issolo?"track_solo_on":"track_solo_off"));
        SendDlgItemMessage(hwndDlg,IDC_SOLO,WM_USER+0x300,0xbeef,(LPARAM)(issolo ? __LOCALIZE("Unsolo local channel","reaninjam") :
              __LOCALIZE("Solo local channel","reaninjam")));

        UpdateXmitButtonText(hwndDlg,f,br);

        SendMessage(hwndDlg,WM_LCUSER_REPOP_CH,0,0);

        //SendDlgItemMessage(hwndDlg,IDC_VOL,TBM_SETRANGE,FALSE,MAKELONG(0,100));
        SendDlgItemMessage(hwndDlg,IDC_VOL,TBM_SETTIC,FALSE,-1);
        SendDlgItemMessage(hwndDlg,IDC_VOL,TBM_SETPOS,TRUE,(LPARAM)DB2SLIDER(VAL2DB(vol)));
        SendDlgItemMessage(hwndDlg,IDC_VOL,WM_USER+9999,1,(LPARAM)__LOCALIZE("Local channel volume","reaninjam_access"));

        SendDlgItemMessage(hwndDlg,IDC_PAN,TBM_SETRANGE,FALSE,MAKELONG(0,100));
        SendDlgItemMessage(hwndDlg,IDC_PAN,TBM_SETTIC,FALSE,50);
        int t=(int)(pan*50.0) + 50;
        if (t < 0) t=0; else if (t > 100)t=100;
        SendDlgItemMessage(hwndDlg,IDC_PAN,TBM_SETPOS,TRUE,t);
        SendDlgItemMessage(hwndDlg,IDC_PAN,WM_USER+9999,1,(LPARAM)__LOCALIZE("Local channel pan","reaninjam_access"));

        UpdateVolPanLabels(hwndDlg,vol,pan);
      }
    return 0;
    case WM_SIZE:
      if (_this) _this->wndsizer.onResize();
    return 0;
    case WM_TIMER:
      if (wParam == 1)
      {
        KillTimer(hwndDlg,1);
        char buf[512];
        GetDlgItemText(hwndDlg,IDC_NAME,buf,sizeof(buf)-64);
        g_client_mutex.Enter();
        g_client->SetLocalChannelInfo(m_idx,buf,false,0,false,0,false,0);
        g_client->NotifyServerOfChannelChange();
        g_client_mutex.Leave();
      }
    return 0;
    case WM_COMMAND:
      switch (LOWORD(wParam))
      {
        case IDC_LOCALOUT:
          if (HIWORD(wParam) == CBN_SELCHANGE)
          {
            int a=(int)SendMessage((HWND)lParam,CB_GETCURSEL,0,0);
            if (a >= 0)
            {
              int idx = (int)SendMessage((HWND)lParam,CB_GETITEMDATA,a,0);
              if (idx>=0 && idx<2048)
              {
                g_client->SetLocalChannelInfo(m_idx,NULL,false,0,false,0,false,false,true,idx,false,0);
              }
            }
          }
        break;

        case IDC_AUDIOIN:
          if (HIWORD(wParam) == CBN_SELCHANGE)
          {
            int a=SendDlgItemMessage(hwndDlg,IDC_AUDIOIN,CB_GETCURSEL,0,0);
            if (a != CB_ERR)
            {
              if (a>=g_config_num_inputs)
              {
                a-=g_config_num_inputs;
                a|=1024;
              }
              g_client_mutex.Enter();
              g_client->SetLocalChannelInfo(m_idx,NULL,true,a,false,0,false,false);
              g_client_mutex.Leave();
            }
          }
        break;
        case IDC_ASYNCXMIT:
          {
            int f=0,br=0;
            g_client->GetLocalChannelInfo(m_idx,NULL,&br,NULL,NULL,&f);

            HMENU menu = CreatePopupMenu();
            int mpos = 0;
            InsertMenu(menu, mpos++, MF_BYPOSITION|MF_STRING|((f&6)==0?MF_CHECKED:0), 1,
                __LOCALIZE("Normal NINJAM channel","reaninjam"));
            InsertMenu(menu, mpos++, MF_BYPOSITION|MF_STRING|((f&2)==2?MF_CHECKED:0), 2,
                __LOCALIZE("Voice Chat channel - others will hear this channel as soon as possible","reaninjam"));
            InsertMenu(menu, mpos++, MF_BYPOSITION|MF_STRING|((f&6)==4?MF_CHECKED:0), 3,
                __LOCALIZE("Session mode channel - playback content will be sent project-synchronized","reaninjam"));
            InsertMenu(menu, mpos++, MF_BYPOSITION|MF_SEPARATOR, 0, NULL);
            const int nbr = (int) (sizeof(brtab)/sizeof(brtab[0]));
            int x;
            for (x = 0; x < nbr && br != brtab[x].br; x ++);
            if (x == nbr)
            {
              char buf[256];
              snprintf(buf,sizeof(buf),__LOCALIZE_VERFMT("Custom: approx %d kbps","reaninjam"), br);
              InsertMenu(menu, mpos++, MF_BYPOSITION|MF_STRING|MF_GRAYED,0,buf);
            }

            for (x = 0; x < nbr; x ++)
            {
              InsertMenu(menu, mpos++, MF_BYPOSITION|MF_STRING|(br==brtab[x].br ? MF_CHECKED:0), 100+x,
                  __localizeFunc(brtab[x].str,"reaninjam",0));
            }

            RECT r;
            GetWindowRect(GetDlgItem(hwndDlg,IDC_ASYNCXMIT),&r);
            int w = TrackPopupMenu(menu, TPM_RETURNCMD|TPM_NONOTIFY, r.left, r.bottom, 0, hwndDlg, NULL);
            DestroyMenu(menu);

            if (w==2)
            {
              if (MessageBox(hwndDlg,__LOCALIZE("Enabling Voice Chat Mode for this local channel will result in other people hearing\r\n"
                                 "this channel's audio as soon as possible, making synchronizing music using the classic\r\n"
                                 "NINJAM technique difficult and/or not possible. \r\n"
                                 "\r\n"
                                 "Normally you enable Voice Chat Mode to do voice chat, or to monitor sessions.\r\n\r\n"
                                 "Enable Voice Chat Mode now?","reaninjam"),
                                 __LOCALIZE("Voice Chat Mode Confirmation","reaninjam"),
                                 MB_OKCANCEL)==IDCANCEL)
              {
                return 0;
              }
            }

            if (w==3)
            {
              if (MessageBox(hwndDlg,__LOCALIZE("Enabling Session Mode for this local channel will send session-timestamped audio\r\n"
                                 "to other people, whose clients must support session mode. This is _NOT_ for jamming!\r\n"
                                 "\r\n"
                                 "Normally you enable Session Mode to collaborate on a project.\r\n\r\n"
                                 "Enable Session Mode now?","reaninjam"),
                    __LOCALIZE("Session Mode Confirmation","reaninjam"),MB_OKCANCEL)==IDCANCEL)
              {
                SendDlgItemMessage(hwndDlg,IDC_ASYNCXMIT,CB_SETCURSEL,0,0);
                return 0;
              }
            }

            if (w >= 1 && w <= 3)
            {
              g_client_mutex.Enter();
              f&=~(2|4);
              if (w==2) f|=2;
              else if (w==3) f|=4;
              g_client->SetLocalChannelInfo(m_idx,NULL,false,0,false,0,false,0,false,0,true,f);
              g_client->NotifyServerOfChannelChange();
              g_client_mutex.Leave();
              UpdateXmitButtonText(hwndDlg,f,br);
            }
            else if (w >= 100 && w < 100+nbr)
            {
              br = brtab[w-100].br;
              g_client_mutex.Enter();
              g_client->SetLocalChannelInfo(m_idx,NULL,false,0,true,br,false,0,false,0,false,0);
              g_client_mutex.Leave();
              UpdateXmitButtonText(hwndDlg,f,br);
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
          {
            g_client_mutex.Enter();
            float vol=0.0,pan=0.0 ;
            bool ismute=0,issolo=0;
            g_client->GetLocalChannelMonitoring(m_idx, &vol, &pan, &ismute, &issolo);
            issolo=!issolo;
            g_client->SetLocalChannelMonitoring(m_idx,false,0.0,false,0.0,false,false,true,issolo);
            g_client->NotifyServerOfChannelChange();
            g_client_mutex.Leave();
            SendDlgItemMessage(hwndDlg,IDC_SOLO,BM_SETIMAGE,IMAGE_ICON|0x8000,(LPARAM)GetIconThemePointer(issolo?"track_solo_on":"track_solo_off"));
            SendDlgItemMessage(hwndDlg,IDC_SOLO,WM_USER+0x300,0xbeef,(LPARAM)(issolo ? __LOCALIZE("Unsolo local channel","reaninjam") :
                  __LOCALIZE("Solo local channel","reaninjam")));
          }
        break;
        case IDC_MUTE:
          {
            g_client_mutex.Enter();
            float vol=0.0,pan=0.0 ;
            bool ismute=0,issolo=0;
            g_client->GetLocalChannelMonitoring(m_idx, &vol, &pan, &ismute, &issolo);
            ismute=!ismute;
            g_client->SetLocalChannelMonitoring(m_idx,false,0.0,false,0.0,true,ismute,false,false);
            g_client->NotifyServerOfChannelChange();
            g_client_mutex.Leave();
            SendDlgItemMessage(hwndDlg,IDC_MUTE,BM_SETIMAGE,IMAGE_ICON|0x8000,(LPARAM)GetIconThemePointer(ismute?"track_mute_on":"track_mute_off"));
            SendDlgItemMessage(hwndDlg,IDC_MUTE,WM_USER+0x300,0xbeef,(LPARAM)(ismute ?
                  __LOCALIZE("Unmute local channel","reaninjam") : __LOCALIZE("Mute local channel","reaninjam")));
          }
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
            g_client_mutex.Enter();
            // remove the channel
            g_client->DeleteLocalChannel(m_idx);
            g_client_mutex.Leave();
            PostMessage(GetParent(hwndDlg),WM_LCUSER_REMCHILD,0,(LPARAM)hwndDlg);
          }
        break;

      }
    return 0;
    case WM_HSCROLL:
      {
        double pos=(double)SendMessage((HWND)lParam,TBM_GETPOS,0,0);

        if ((HWND) lParam == GetDlgItem(hwndDlg,IDC_VOL))
        {
          pos=SLIDER2DB(pos);
          if (fabs(pos- -6.0) < 0.5) pos=-6.0;
          else if (pos < -115.0) pos=-1000.0;
          pos=DB2VAL(pos);
          g_client->SetLocalChannelMonitoring(m_idx,true,(float)pos,false,0.0,false,false,false,false);

          float vol=0.0,pan=0.0 ;
          g_client->GetLocalChannelMonitoring(m_idx, &vol, &pan, NULL,NULL);
          UpdateVolPanLabels(hwndDlg,vol,pan);
        }
        else if ((HWND) lParam == GetDlgItem(hwndDlg,IDC_PAN))
        {
          pos=(pos-50.0)/50.0;
          if (fabs(pos) < 0.08) pos=0.0;
          g_client->SetLocalChannelMonitoring(m_idx,false,false,true,(float)pos,false,false,false,false);

          float vol=0.0,pan=0.0 ;
          g_client->GetLocalChannelMonitoring(m_idx, &vol, &pan, NULL,NULL);
          UpdateVolPanLabels(hwndDlg,vol,pan);
        }
      }
    return 0;
    case WM_LCUSER_REPOP_CH:
      {
        int sch;
        SendDlgItemMessage(hwndDlg,IDC_AUDIOIN,CB_RESETCONTENT,0,0);

        g_client->GetLocalChannelInfo(m_idx,&sch,NULL,NULL);
        for (int chcnt = 0; chcnt < g_config_num_inputs; chcnt++)
        {
          char buf[128];
          snprintf(buf,sizeof(buf),__LOCALIZE_VERFMT("Mono %d","reaninjam"),chcnt+1);
          SendDlgItemMessage(hwndDlg,IDC_AUDIOIN,CB_ADDSTRING,0,(LPARAM)buf);
        }
        for (int chcnt = 0; chcnt < g_config_num_inputs-1; chcnt++)
        {
          char buf[128];
          snprintf(buf,sizeof(buf),__LOCALIZE_VERFMT("Stereo %d/%d","reaninjam"),chcnt+1,chcnt+2);
          SendDlgItemMessage(hwndDlg,IDC_AUDIOIN,CB_ADDSTRING,0,(LPARAM)buf);
        }
        if (sch & 1024)
          SendDlgItemMessage(hwndDlg,IDC_AUDIOIN,CB_SETCURSEL,g_config_num_inputs+(sch&1023),0);
        else
          SendDlgItemMessage(hwndDlg,IDC_AUDIOIN,CB_SETCURSEL,sch,0);

        int chidx=0;
        g_client->GetLocalChannelInfo(m_idx, NULL, NULL, NULL, &chidx, NULL);
        PopulateOutputCombo(GetDlgItem(hwndDlg,IDC_LOCALOUT),chidx,false);
      }
    return 0;
    case WM_LCUSER_VUUPDATE:
      {
        float pk1 = g_client->GetLocalChannelPeak(m_idx,0), pk2 = g_client->GetLocalChannelPeak(m_idx,1);
        double p[2] = { VAL2DB(pk1), VAL2DB(pk2) };
        SendDlgItemMessage(hwndDlg, IDC_VU, WM_USER+1011, 0, (LPARAM)p);
      }
    return 0;

  };
  return 0;
}



static WDL_DLGRET LocalChannelListProc(HWND hwndDlg, UINT uMsg, WPARAM wParam, LPARAM lParam)
{
  static int m_num_children;
  switch (uMsg)
  {
    case WM_INITDIALOG:
      m_num_children=0;
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
        int maxc=g_client->GetMaxLocalChannels();
        for (idx = 0; idx < maxc && g_client->GetLocalChannelInfo(idx,NULL,NULL,NULL); idx++);

        if (idx < maxc)
        {
          g_client_mutex.Enter();
          g_client->SetLocalChannelInfo(idx,__LOCALIZE("new channel","reaninjam"),true,1024,false,0,true,true);
          g_client->NotifyServerOfChannelChange();
          g_client_mutex.Leave();
        }


        if (idx >= maxc) return 0;
        wParam = (WPARAM)idx;
      }

      WDL_FALLTHROUGH;
    case WM_LCUSER_ADDCHILD:
      {
        // add a new child, with wParam as the index
        HWND hwnd=CreateDialogParam(g_hInst,MAKEINTRESOURCE(IDD_LOCALCHANNEL),hwndDlg,LocalChannelItemProc,wParam);
        if (hwnd)
        {
          RECT r;
          GetClientRect(hwndDlg,&r);
          RECT sz;
          GetClientRect(hwnd,&sz);
          SetWindowPos(hwnd,NULL,0,sz.bottom*m_num_children,r.right,sz.bottom,SWP_NOZORDER|SWP_NOACTIVATE);
          ShowWindow(hwnd,SW_SHOWNA);
          m_num_children++;

          int h=sz.bottom*m_num_children;
          int w=sz.right-sz.left;

          SetWindowPos(GetDlgItem(hwndDlg,IDC_ADDCH),NULL,0,h,0,0,SWP_NOSIZE|SWP_NOZORDER|SWP_NOACTIVATE);

          GetWindowRect(GetDlgItem(hwndDlg,IDC_ADDCH),&sz);
          if (sz.bottom < sz.top) SWAP(sz.bottom,sz.top,int);
          h += sz.bottom - sz.top + 3;

          SetWindowPos(hwndDlg,0,0,0,w,h,SWP_NOMOVE|SWP_NOZORDER|SWP_NOACTIVATE);
          SendMessage(GetParent(hwndDlg),WM_LCUSER_RESIZE,0,uMsg == WM_COMMAND);
          if (uMsg == WM_COMMAND)
            SetFocus(GetDlgItem(hwnd,IDC_NAME));
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
        int n=300;
        while (hwnd && n--)
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
    case WM_SIZE:
      {
        HWND hwnd=GetWindow(hwndDlg,GW_CHILD);

        int n=300;
        RECT r,r2;
        GetClientRect(hwndDlg,&r2);
        while (hwnd && n--)
        {
          if (GetWindowLongPtr(hwnd,GWLP_USERDATA))
          {
            GetClientRect(hwnd,&r);
            SetWindowPos(hwnd,0,0,0,r2.right,r.bottom,SWP_NOMOVE|SWP_NOZORDER|SWP_NOACTIVATE);
          }
          hwnd=GetWindow(hwnd,GW_HWNDNEXT);
        }
      }
    break;
    case WM_CTLCOLOREDIT:
    case WM_CTLCOLORLISTBOX:
    case WM_CTLCOLORBTN:
    case WM_CTLCOLORDLG:
    case WM_CTLCOLORSTATIC :
    case WM_DRAWITEM:
      return ForwardToHostMainWindow(hwndDlg,uMsg,wParam,lParam);
  }
  return 0;
}

HWND g_local_channel_wnd;
WDL_DLGRET LocalOuterChannelListProc(HWND hwndDlg, UINT uMsg, WPARAM wParam, LPARAM lParam)
{
  static int m_wh, m_ww,m_nScrollPos,m_nScrollPos_w;
  static int m_h, m_maxpos_h, m_w,m_maxpos_w;
  switch (uMsg)
  {

    case WM_INITDIALOG:
      m_nScrollPos=m_nScrollPos_w=0;
      m_maxpos_h=m_h=m_maxpos_w=m_w=0;
      g_local_channel_wnd=NULL;
      WDL_FALLTHROUGH;

    case WM_RCUSER_UPDATE:
    case WM_LCUSER_RESIZE:
      {
        RECT r;
        GetWindowRect(GetDlgItem(GetParent(hwndDlg),IDC_LOCRECT),&r);
        ScreenToClient(GetParent(hwndDlg),(LPPOINT)&r);
        ScreenToClient(GetParent(hwndDlg),(LPPOINT)&r + 1);
        m_wh=r.bottom-r.top;
        m_ww=r.right-r.left;

        SetWindowPos(hwndDlg,NULL,r.left,r.top,m_ww,m_wh,SWP_NOZORDER|SWP_NOACTIVATE);


        if (uMsg == WM_INITDIALOG)
        {
          InitializeCoolSB(hwndDlg);
          g_local_channel_wnd=CreateDialog(g_hInst,MAKEINTRESOURCE(IDD_LOCALLIST),hwndDlg,LocalChannelListProc);
          ShowWindow(g_local_channel_wnd,SW_SHOWNA);
          ShowWindow(hwndDlg,SW_SHOWNA);
        }
      }

      {
        SendMessage(g_local_channel_wnd,WM_RCUSER_UPDATE,0,0);
        RECT r;
        GetWindowRect(g_local_channel_wnd,&r);
        if (r.bottom < r.top) SWAP(r.bottom,r.top,int);
        m_h=r.bottom-r.top;
        m_w=r.right-r.left;
        m_maxpos_h=m_h-m_wh;
        m_maxpos_w=m_w-m_ww;

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
          CoolSB_SetScrollInfo(hwndDlg,SB_VERT,&si,TRUE);
        }
#if 0
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

          CoolSB_SetScrollInfo(hwndDlg,SB_HORZ,&si,TRUE);
        }
#endif
      }
      {
        RECT r,r2;
        GetClientRect(g_local_channel_wnd,&r);
        GetClientRect(hwndDlg,&r2);
        SetWindowPos(g_local_channel_wnd,0,0,0,r2.right,r.bottom,SWP_NOMOVE|SWP_NOZORDER|SWP_NOACTIVATE);
      }
      if (uMsg == WM_LCUSER_RESIZE && lParam == 1)
      {
        if (m_wh < m_h)
        {
          int npos=m_h-m_wh;
          if (npos >= 0 && npos != m_nScrollPos)
          {
            CoolSB_SetScrollPos(hwndDlg,SB_VERT,npos,TRUE);
            ScrollWindow(hwndDlg,0,m_nScrollPos-npos,NULL,NULL);
            m_nScrollPos=npos;
          }
        }
      }
      #ifndef _WIN32
      InvalidateRect(hwndDlg,NULL,FALSE);
      #endif

      // update scrollbars and shit
    return 0;
    case WM_LCUSER_REPOP_CH:
    case WM_LCUSER_ADDCHILD:
    case WM_LCUSER_VUUPDATE:
      SendMessage(g_local_channel_wnd,uMsg,wParam,lParam);
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
            if (m_nScrollPos < nMaxPos) nDelta = min(4,nMaxPos-m_nScrollPos);
          break;
          case SB_LINEUP:
            if (m_nScrollPos > 0) nDelta = -min(4,m_nScrollPos);
          break;
          case SB_PAGEDOWN:
            if (m_nScrollPos < nMaxPos) nDelta = min(nMaxPos/4,nMaxPos-m_nScrollPos);
          break;
          case SB_THUMBTRACK:
          case SB_THUMBPOSITION:
            nDelta = (int)HIWORD(wParam) - m_nScrollPos;
          break;
          case SB_PAGEUP:
            if (m_nScrollPos > 0) nDelta = -min(nMaxPos/4,m_nScrollPos);
          break;
        }
        if (nDelta)
        {
          m_nScrollPos += nDelta;
          CoolSB_SetScrollPos(hwndDlg,SB_VERT,m_nScrollPos,TRUE);
          ScrollWindow(hwndDlg,0,-nDelta,NULL,NULL);
        }
      }
    break;
#if 0
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
          CoolSB_SetScrollPos(hwndDlg,SB_HORZ,m_nScrollPos_w,TRUE);
          ScrollWindow(hwndDlg,-nDelta,0,NULL,NULL);
        }
      }
    break;
#endif
    case WM_CTLCOLOREDIT:
    case WM_CTLCOLORLISTBOX:
    case WM_CTLCOLORBTN:
    case WM_CTLCOLORDLG:
    case WM_CTLCOLORSTATIC :
    case WM_DRAWITEM:
      return ForwardToHostMainWindow(hwndDlg,uMsg,wParam,lParam);
    case WM_DESTROY:
      g_local_channel_wnd=NULL;
      UninitializeCoolSB(hwndDlg);
    return 0;
  }
  return 0;
}

