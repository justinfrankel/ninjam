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
#include "../../../WDL/swell/swell.h"
#endif

#define SWAP(a,b,t) { t __tmp = (a); (a)=(b); (b)=__tmp; }

extern BOOL	(WINAPI *InitializeCoolSB)(HWND hwnd);
extern HRESULT (WINAPI *UninitializeCoolSB)(HWND hwnd);
extern BOOL (WINAPI *CoolSB_SetVegasStyle)(HWND hwnd, BOOL active);
extern int	 (WINAPI *CoolSB_SetScrollInfo)(HWND hwnd, int fnBar, LPSCROLLINFO lpsi, BOOL fRedraw);
extern BOOL (WINAPI *CoolSB_GetScrollInfo)(HWND hwnd, int fnBar, LPSCROLLINFO lpsi);
extern int (WINAPI *CoolSB_SetScrollPos)(HWND hwnd, int nBar, int nPos, BOOL fRedraw);
extern int (WINAPI *CoolSB_SetScrollRange)(HWND hwnd, int nBar, int nMinPos, int nMaxPos, BOOL fRedraw);
extern BOOL (WINAPI *CoolSB_SetMinThumbSize)(HWND hwnd, UINT wBar, UINT size);
#include <math.h>

#include "winclient.h"

#include "resource.h"
#include "../../../WDL/wingui/wndsize.h"

extern HWND (*GetMainHwnd)();
extern HANDLE * (*GetIconThemePointer)(const char *name);


#define NUM_INPUTS 8

class LocalChannelRec
{
public:
  LocalChannelRec(int idx) { m_idx=idx; } 
  ~LocalChannelRec() {}

  int m_idx;
  WDL_WndSizer wndsizer;
};

static WDL_DLGRET LocalChannelItemProc(HWND hwndDlg, UINT uMsg, WPARAM wParam, LPARAM lParam)
{
  if (uMsg == WM_INITDIALOG) 
  {
    SetWindowLongPtr(hwndDlg,GWLP_USERDATA,(INT_PTR)new LocalChannelRec(lParam));
  }
  LocalChannelRec *_this = (LocalChannelRec*)GetWindowLongPtr(hwndDlg,GWLP_USERDATA);
  int m_idx=_this?_this->m_idx:0;
  switch (uMsg)
  {
    case WM_DESTROY:
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
      return SendMessage(GetMainHwnd(),uMsg,wParam,lParam);
    case WM_INITDIALOG:
      {
        if (_this)
        {
          _this->wndsizer.init(hwndDlg);
          _this->wndsizer.init_item(IDC_VU,0.0f,0.0f,0.0f,0.0f);
          _this->wndsizer.init_item(IDC_VOL,0.0f,0.0f,0.8f,0.0f);
          _this->wndsizer.init_item(IDC_PAN,0.8f,0.0f,1.0f,0.0f);
          _this->wndsizer.init_item(IDC_MUTE,1.0f,0.0f,1.0f,0.0f);
          _this->wndsizer.init_item(IDC_SOLO,1.0f,0.0f,1.0f,0.0f);
          _this->wndsizer.init_item(IDC_VOLLBL,1.0f,0.0f,1.0f,0.0f);
          _this->wndsizer.init_item(IDC_PANLBL,1.0f,0.0f,1.0f,0.0f);
          _this->wndsizer.init_item(IDC_REMOVE,1.0f,0.0f,1.0f,0.0f);
          _this->wndsizer.init_item(IDC_EDGE,0.0f,0.0f,1.0f,0.0f);
        }
        int sch;
        bool bc;

        int f=0;
        char *buf=g_client->GetLocalChannelInfo(m_idx,&sch,NULL,&bc,NULL,&f);
        float vol=0.0,pan=0.0 ;
        bool ismute=0,issolo=0;
        g_client->GetLocalChannelMonitoring(m_idx, &vol, &pan, &ismute, &issolo);
        void *jesinst=0;
        g_client->GetLocalChannelProcessor(m_idx,NULL,&jesinst);

        if (buf) SetDlgItemText(hwndDlg,IDC_NAME,buf);
        if (bc) CheckDlgButton(hwndDlg,IDC_TRANSMIT,BST_CHECKED);
        SendDlgItemMessage(hwndDlg,IDC_MUTE,BM_SETIMAGE,IMAGE_ICON|0x8000,(LPARAM)GetIconThemePointer(ismute?"track_mute_on":"track_mute_off"));
        SendDlgItemMessage(hwndDlg,IDC_MUTE,WM_USER+0x300,0xbeef,(LPARAM)(ismute ? "Unmute local channel" : "Mute local channel"));
        SendDlgItemMessage(hwndDlg,IDC_SOLO,BM_SETIMAGE,IMAGE_ICON|0x8000,(LPARAM)GetIconThemePointer(issolo?"track_solo_on":"track_solo_off"));
        SendDlgItemMessage(hwndDlg,IDC_SOLO,WM_USER+0x300,0xbeef,(LPARAM)(issolo ? "Unsolo local channel" : "Solo local channel"));

        SendDlgItemMessage(hwndDlg,IDC_ASYNCXMIT,CB_ADDSTRING,0,(LPARAM)"Normal NINJAM");
        SendDlgItemMessage(hwndDlg,IDC_ASYNCXMIT,CB_ADDSTRING,0,(LPARAM)"Voice Chat");
        SendDlgItemMessage(hwndDlg,IDC_ASYNCXMIT,CB_ADDSTRING,0,(LPARAM)"Session Mode");
        if (f&2)
          SendDlgItemMessage(hwndDlg,IDC_ASYNCXMIT,CB_SETCURSEL,1,0);
        else if (f&4)
          SendDlgItemMessage(hwndDlg,IDC_ASYNCXMIT,CB_SETCURSEL,2,0);
        else 
          SendDlgItemMessage(hwndDlg,IDC_ASYNCXMIT,CB_SETCURSEL,0,0);

        SendMessage(hwndDlg,WM_LCUSER_REPOP_CH,0,0);        

        //SendDlgItemMessage(hwndDlg,IDC_VOL,TBM_SETRANGE,FALSE,MAKELONG(0,100));
        SendDlgItemMessage(hwndDlg,IDC_VOL,TBM_SETTIC,FALSE,-1);       
        SendDlgItemMessage(hwndDlg,IDC_VOL,TBM_SETPOS,TRUE,(LPARAM)DB2SLIDER(VAL2DB(vol)));
        SendDlgItemMessage(hwndDlg,IDC_VOL,WM_USER+9999,1,(LPARAM)"Local channel volume");

        SendDlgItemMessage(hwndDlg,IDC_PAN,TBM_SETRANGE,FALSE,MAKELONG(0,100));
        SendDlgItemMessage(hwndDlg,IDC_PAN,TBM_SETTIC,FALSE,50);       
        int t=(int)(pan*50.0) + 50;
        if (t < 0) t=0; else if (t > 100)t=100;
        SendDlgItemMessage(hwndDlg,IDC_PAN,TBM_SETPOS,TRUE,t);
        SendDlgItemMessage(hwndDlg,IDC_PAN,WM_USER+9999,1,(LPARAM)"Local channel pan");

        {
         char tmp[512];
         mkvolstr(tmp,vol);
         SetDlgItemText(hwndDlg,IDC_VOLLBL,tmp);
         mkpanstr(tmp,pan);
         SetDlgItemText(hwndDlg,IDC_PANLBL,tmp);
        }

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
        //void *i=0;
        //int sch=0;
        //g_client->GetLocalChannelProcessor(m_idx,NULL,&i);
        //char *n=g_client->GetLocalChannelInfo(m_idx,&sch,NULL,NULL);
        g_client_mutex.Leave();
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
              if (a>=NUM_INPUTS)
              {
                a-=NUM_INPUTS;
                a|=1024;
              }
              g_client_mutex.Enter();
              g_client->SetLocalChannelInfo(m_idx,NULL,true,a,false,0,false,false);
              g_client_mutex.Leave();
            }
          }
        break;
        case IDC_ASYNCXMIT:
          if (HIWORD(wParam) == CBN_SELCHANGE)
          {
            int w=SendDlgItemMessage(hwndDlg,IDC_ASYNCXMIT,CB_GETCURSEL,0,0);
            if (w<0) break;

            if (w==1)
            {
              if (MessageBox(hwndDlg,"Enabling Voice Chat Mode for this local channel will result in other people hearing\r\n"
                                 "this channel's audio as soon as possible, making synchronizing music using the classic\r\n"
                                 "NINJAM technique difficult and/or not possible. \r\n"
                                 "\r\n"
                                 "Normally you enable Voice Chat Mode to do voice chat, or to monitor sessions.\r\n\r\n"
                                 "Enable Voice Chat Mode now?","Voice Chat Mode Confirmation",MB_OKCANCEL)==IDCANCEL)
              {
                SendDlgItemMessage(hwndDlg,IDC_ASYNCXMIT,CB_SETCURSEL,0,0);
                return 0;
              }
            }

            if (w==2)
            {
              if (MessageBox(hwndDlg,"Enabling Session Mode for this local channel will send session-timestamped audio\r\n"
                                 "to other people, whose clients must support session mode. This is _NOT_ for jamming!\r\n"
                                 "\r\n"
                                 "Normally you enable Session Mode to collaborate on a project.\r\n\r\n"
                                 "Enable Session Mode now?","Session Mode Confirmation",MB_OKCANCEL)==IDCANCEL)
              {
                SendDlgItemMessage(hwndDlg,IDC_ASYNCXMIT,CB_SETCURSEL,0,0);
                return 0;
              }
            }

            g_client_mutex.Enter();
            int f=0;
            g_client->GetLocalChannelInfo(m_idx,NULL,NULL,NULL,&f);
            f&=~(2|4);
            if (w==1) f|=2;
            else if (w==2) f|=4;
            g_client->SetLocalChannelInfo(m_idx,NULL,false,0,false,0,false,0,false,0,true,f);
            g_client->NotifyServerOfChannelChange();
            g_client_mutex.Leave();
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
            SendDlgItemMessage(hwndDlg,IDC_SOLO,WM_USER+0x300,0xbeef,(LPARAM)(issolo ? "Unsolo local channel" : "Solo local channel"));
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
            SendDlgItemMessage(hwndDlg,IDC_MUTE,WM_USER+0x300,0xbeef,(LPARAM)(ismute ? "Unmute local channel" : "Mute local channel"));
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
        case IDC_VOLLBL:
          if (HIWORD(wParam) == STN_DBLCLK) {
            double vol = 1.0;
            g_client->SetLocalChannelMonitoring(m_idx,true,(float)vol,false,0.0,false,false,false,false);
            SendDlgItemMessage(hwndDlg,IDC_VOL,TBM_SETPOS,TRUE,(LPARAM)DB2SLIDER(VAL2DB(vol)));
            char tmp[512];
            mkvolstr(tmp,vol);
            SetDlgItemText(hwndDlg,IDC_VOLLBL,tmp);
          }
        break;
        case IDC_PANLBL:
          if (HIWORD(wParam) == STN_DBLCLK) {
            double pan = 0.0;
            int t=(int)(pan*50.0) + 50;
            if (t < 0) t=0; else if (t > 100)t=100;
            g_client->SetLocalChannelMonitoring(m_idx,false,0.0,true,(float)pan,false,false,false,false);
            SendDlgItemMessage(hwndDlg,IDC_PAN,TBM_SETPOS,TRUE,t);

            char tmp[512];
            mkpanstr(tmp,pan);
            SetDlgItemText(hwndDlg,IDC_PANLBL,tmp);
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

          char tmp[512];
          mkvolstr(tmp,pos);
          SetDlgItemText(hwndDlg,IDC_VOLLBL,tmp);

        }
		    else if ((HWND) lParam == GetDlgItem(hwndDlg,IDC_PAN))
        {
          pos=(pos-50.0)/50.0;
          if (fabs(pos) < 0.08) pos=0.0;
          g_client->SetLocalChannelMonitoring(m_idx,false,false,true,(float)pos,false,false,false,false);

          char tmp[512];
          mkpanstr(tmp,pos);
          SetDlgItemText(hwndDlg,IDC_PANLBL,tmp);

        }
      }
    return 0;
    case WM_LCUSER_REPOP_CH:
      {
        int sch;
        SendDlgItemMessage(hwndDlg,IDC_AUDIOIN,CB_RESETCONTENT,0,0);

        g_client->GetLocalChannelInfo(m_idx,&sch,NULL,NULL);
        int chcnt=0;
        {
          int mch=NUM_INPUTS;

          for (chcnt = 0; chcnt < mch; chcnt++)
          {
            char buf[128];
            sprintf(buf,"Mono %d",chcnt+1);
            SendDlgItemMessage(hwndDlg,IDC_AUDIOIN,CB_ADDSTRING,0,(LPARAM)buf);         
          }
          for (chcnt = 0; chcnt < mch-1; chcnt++)
          {
            char buf[128];
            sprintf(buf,"Stereo %d/%d",chcnt+1,chcnt+2);
            SendDlgItemMessage(hwndDlg,IDC_AUDIOIN,CB_ADDSTRING,0,(LPARAM)buf);         
          }
        }
        if (sch & 1024)
          SendDlgItemMessage(hwndDlg,IDC_AUDIOIN,CB_SETCURSEL,NUM_INPUTS+(sch&1023),0);
        else
          SendDlgItemMessage(hwndDlg,IDC_AUDIOIN,CB_SETCURSEL,sch,0);
      }
    return 0;
    case WM_LCUSER_VUUPDATE:
      {
        float pk1 = g_client->GetLocalChannelPeak(m_idx,0), pk2 = g_client->GetLocalChannelPeak(m_idx,1);
        int ival=(int) floor(VAL2DB(pk1)*10.0);
        int ival2=(int) floor(VAL2DB(pk2)*10.0);
        SendDlgItemMessage(hwndDlg,IDC_VU,WM_USER+1010,ival,ival2);
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
          g_client->SetLocalChannelInfo(idx,"new channel",true,1024,false,0,true,true);
          g_client->NotifyServerOfChannelChange();  
          g_client_mutex.Leave();
        }
        

        if (idx >= maxc) return 0;
        wParam = (WPARAM)idx;
      }

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
      return SendMessage(GetMainHwnd(),uMsg,wParam,lParam);;
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
      return SendMessage(GetMainHwnd(),uMsg,wParam,lParam);;
    case WM_DESTROY:
      g_local_channel_wnd=NULL;
      UninitializeCoolSB(hwndDlg);
    return 0;
  }
  return 0;
}

