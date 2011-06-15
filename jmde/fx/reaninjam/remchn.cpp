/*
    NINJAM Windows Client - remchn.cpp
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

  Remote channel (sub) dialog code

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

extern void (*format_timestr_pos)(double tpos, char *buf, int buflen, int modeoverride); // actually implemented in tracklist.cpp for now
extern HWND (*GetMainHwnd)();
extern HANDLE * (*GetIconThemePointer)(const char *name);

static WDL_DLGRET RemoteUserItemProc(HWND hwndDlg, UINT uMsg, WPARAM wParam, LPARAM lParam)
{
  switch (uMsg)
  {
    case WM_RCUSER_UPDATE:
      SetWindowLongPtr(hwndDlg,GWLP_USERDATA,-1 - wParam);
    break;
    case WM_INITDIALOG:
      SetWindowLongPtr(hwndDlg,GWLP_USERDATA,0x0fffffff);
    break;
  }
  int m_user=-1-GetWindowLongPtr(hwndDlg,GWLP_USERDATA);
  switch (uMsg)
  {
    case WM_CTLCOLOREDIT:
    case WM_CTLCOLORLISTBOX:
    case WM_CTLCOLORBTN:
    case WM_CTLCOLORDLG:
    case WM_CTLCOLORSTATIC :
    case WM_DRAWITEM:
      return SendMessage(GetMainHwnd(),uMsg,wParam,lParam);;
    case WM_RCUSER_UPDATE: // update the items
      {        
        g_client->m_remotechannel_rd_mutex.Enter();
        bool mute;
        const char *un=g_client->GetUserState(m_user,NULL,NULL,&mute);
        if (!un) un="";
        SetDlgItemText(hwndDlg,IDC_USERNAME,un);
        g_client->m_remotechannel_rd_mutex.Leave();

        ShowWindow(GetDlgItem(hwndDlg,IDC_DIV),m_user ? SW_SHOW : SW_HIDE);
        SendDlgItemMessage(hwndDlg,IDC_MUTE,BM_SETIMAGE,IMAGE_ICON|0x8000,(LPARAM)GetIconThemePointer(mute?"track_mute_on":"track_mute_off"));
      }
    break;
    case WM_LCUSER_VUUPDATE:
      //IDC_SESSIONINFO
      if (format_timestr_pos && !(lParam&31))
      {
        time_t t;
        double mp=-1.0;
        double d=g_client->GetUserSessionPos(m_user,&t,&mp);
        char buf[512];          
        if ((d>-0.5||mp>-0.5))
        {
          if (d>-0.5&&t > time(NULL)-7) format_timestr_pos(d,buf,sizeof(buf),-1);
          else strcpy(buf,"---");

          if (mp > -0.5)
          {
            strcat(buf," / ");
            format_timestr_pos(mp,buf+strlen(buf),sizeof(buf)-strlen(buf)-32,-1);
          }
          SetDlgItemText(hwndDlg,IDC_SESSIONINFO,buf);
        }
        else 
        {
          SetDlgItemText(hwndDlg,IDC_SESSIONINFO,"");
        }
      }

    break;

    case WM_COMMAND:
      switch (LOWORD(wParam))
      {
        case IDC_MUTE:
          {
            g_client->m_remotechannel_rd_mutex.Enter();
            bool mute=false;
            g_client->GetUserState(m_user,NULL,NULL,&mute);
            mute=!mute;
            g_client->SetUserState(m_user,false,0.0,false,0.0,true,mute);
            g_client->m_remotechannel_rd_mutex.Leave();
            SendDlgItemMessage(hwndDlg,IDC_MUTE,BM_SETIMAGE,IMAGE_ICON|0x8000,(LPARAM)GetIconThemePointer(mute?"track_mute_on":"track_mute_off"));
          }
        break;
      }
    break;
  }
  return 0;
}

static WDL_DLGRET RemoteChannelItemProc(HWND hwndDlg, UINT uMsg, WPARAM wParam, LPARAM lParam)
{
  int m_userch=GetWindowLongPtr(hwndDlg,GWLP_USERDATA); // high 16 bits, user, low 16 bits, channel
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
      SetWindowLongPtr(hwndDlg,GWLP_USERDATA,0x0fffffff);

//      SendDlgItemMessage(hwndDlg,IDC_VU,PBM_SETRANGE,0,MAKELPARAM(0,100));

//      SendDlgItemMessage(hwndDlg,IDC_VOL,TBM_SETRANGE,FALSE,MAKELONG(0,100));
      SendDlgItemMessage(hwndDlg,IDC_VOL,TBM_SETTIC,FALSE,-1);       
      SendDlgItemMessage(hwndDlg,IDC_PAN,TBM_SETRANGE,FALSE,MAKELONG(0,100));
      SendDlgItemMessage(hwndDlg,IDC_PAN,TBM_SETTIC,FALSE,50);       
      {
        extern void (*RemoveXPStyle)(HWND hwnd, int rem);
        if (RemoveXPStyle)
        {
          RemoveXPStyle(GetDlgItem(hwndDlg,IDC_RECV),1);
        }
      }

    return 0;
    case WM_RCUSER_UPDATE:
      m_userch=((int)LOWORD(wParam) << 16) | LOWORD(lParam);
      SetWindowLongPtr(hwndDlg,GWLP_USERDATA,m_userch);
    break;
  }
  int user=m_userch>>16;
  int chan=m_userch&0xffff;
  switch (uMsg)
  {
    case WM_RCUSER_UPDATE: // update the items
      {
        g_client->m_remotechannel_rd_mutex.Enter();
        const char *un=g_client->GetUserState(user,NULL,NULL,NULL);
        SetDlgItemText(hwndDlg,IDC_USERNAME,un?un:"");

        bool sub=0,m=0,s=0;
        float v=0,p=0;
        int flags=0;
        char *cn=g_client->GetUserChannelState(user,chan,&sub,&v,&p,&m,&s,NULL,&flags);

        if (flags&2)
        {
          char buf[512];
          sprintf(buf,"[Voice Chat] %.23s",cn?cn:"");
          SetDlgItemText(hwndDlg,IDC_CHANNELNAME,buf);
        }
        else if (flags&4)
        {
          char buf[512];
          sprintf(buf,"[Session] %.23s",cn?cn:"");
          SetDlgItemText(hwndDlg,IDC_CHANNELNAME,buf);
        }
        else SetDlgItemText(hwndDlg,IDC_CHANNELNAME,cn?cn:"");
        g_client->m_remotechannel_rd_mutex.Leave();

        CheckDlgButton(hwndDlg,IDC_RECV,sub?BST_CHECKED:0);
        SendDlgItemMessage(hwndDlg,IDC_MUTE,BM_SETIMAGE,IMAGE_ICON|0x8000,(LPARAM)GetIconThemePointer(m?"track_mute_on":"track_mute_off"));
        SendDlgItemMessage(hwndDlg,IDC_SOLO,BM_SETIMAGE,IMAGE_ICON|0x8000,(LPARAM)GetIconThemePointer(s?"track_solo_on":"track_solo_off"));
        
        SendDlgItemMessage(hwndDlg,IDC_VOL,TBM_SETPOS,TRUE,(LPARAM)DB2SLIDER(VAL2DB(v)));

        int t=(int)(p*50.0) + 50;
        if (t < 0) t=0; else if (t > 100)t=100;
        SendDlgItemMessage(hwndDlg,IDC_PAN,TBM_SETPOS,TRUE,t);

        {
         char tmp[512];
         mkvolstr(tmp,v);
         SetDlgItemText(hwndDlg,IDC_VOLLBL,tmp);
         mkpanstr(tmp,p);
         SetDlgItemText(hwndDlg,IDC_PANLBL,tmp);
        }

      }
    break;
    case WM_LCUSER_VUUPDATE:
      {
        int ival=(int)floor(VAL2DB(g_client->GetUserChannelPeak(user,chan,0))*10.0);
        int ival2=(int)floor(VAL2DB(g_client->GetUserChannelPeak(user,chan,1))*10.0);
        SendDlgItemMessage(hwndDlg,IDC_VU,WM_USER+1010,ival,ival2);
      }
    return 0;
    case WM_HSCROLL:
      {
        double pos=(double)SendMessage((HWND)lParam,TBM_GETPOS,0,0);

        g_client->m_remotechannel_rd_mutex.Enter();
		    if ((HWND) lParam == GetDlgItem(hwndDlg,IDC_VOL))
        {
          pos=SLIDER2DB(pos);
          if (fabs(pos- -6.0) < 0.5) pos=-6.0;
          else if (pos < -115.0) pos=-1000.0;
          pos=DB2VAL(pos);
          g_client->SetUserChannelState(user,chan,false,false,true,(float)pos,false,0.0,false,false,false,false);

          char tmp[512];
          mkvolstr(tmp,pos);
          SetDlgItemText(hwndDlg,IDC_VOLLBL,tmp);
        }
		    else if ((HWND) lParam == GetDlgItem(hwndDlg,IDC_PAN))
        {
          pos=(pos-50.0)/50.0;
          if (fabs(pos) < 0.08) pos=0.0;
          g_client->SetUserChannelState(user,chan,false,false,false,0.0,true,(float)pos,false,false,false,false);
          char tmp[512];
          mkpanstr(tmp,pos);
          SetDlgItemText(hwndDlg,IDC_PANLBL,tmp);
        }
        g_client->m_remotechannel_rd_mutex.Leave();
      }
    return 0;
    case WM_COMMAND:
      switch (LOWORD(wParam))
      {
        case IDC_RECV:
          g_client->m_remotechannel_rd_mutex.Enter();
          g_client->SetUserChannelState(user,chan,true,!!IsDlgButtonChecked(hwndDlg,LOWORD(wParam)),false,0.0,false,0.0,false,false,false,false);
          g_client->m_remotechannel_rd_mutex.Leave();
        break;
        case IDC_SOLO:
          {
            g_client->m_remotechannel_rd_mutex.Enter();
            bool sub=0,m=0,s=0;
            float v=0,p=0;
            int flags=0;
            g_client->GetUserChannelState(user,chan,&sub,&v,&p,&m,&s,NULL,&flags);
            s=!s;
            g_client->SetUserChannelState(user,chan,false,false,false,0.0,false,0.0,false,false,true,s);
            g_client->m_remotechannel_rd_mutex.Leave();
            SendDlgItemMessage(hwndDlg,IDC_SOLO,BM_SETIMAGE,IMAGE_ICON|0x8000,(LPARAM)GetIconThemePointer(s?"track_solo_on":"track_solo_off"));
          }
        break;
        case IDC_MUTE:
          {
            g_client->m_remotechannel_rd_mutex.Enter();
            bool sub=0,m=0,s=0;
            float v=0,p=0;
            int flags=0;
            g_client->GetUserChannelState(user,chan,&sub,&v,&p,&m,&s,NULL,&flags);
            m=!m;
            g_client->SetUserChannelState(user,chan,false,false,false,0.0,false,0.0,true,m,false,false);
            g_client->m_remotechannel_rd_mutex.Leave();
            SendDlgItemMessage(hwndDlg,IDC_MUTE,BM_SETIMAGE,IMAGE_ICON|0x8000,(LPARAM)GetIconThemePointer(m?"track_mute_on":"track_mute_off"));
          }
        break;
        case IDC_VOLLBL:
          if (HIWORD(wParam) == STN_DBLCLK) {
            double vol = 1.0;
            g_client->m_remotechannel_rd_mutex.Enter();
            g_client->SetUserChannelState(user,chan,false,false,true,(float)vol,false,0.0,false,false,false,false);
            g_client->m_remotechannel_rd_mutex.Leave();
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
            g_client->m_remotechannel_rd_mutex.Enter();
            g_client->SetUserChannelState(user,chan,false,false,false,0.0,true,(float)pan,false,false,false,false);
            g_client->m_remotechannel_rd_mutex.Leave();
            SendDlgItemMessage(hwndDlg,IDC_PAN,TBM_SETPOS,TRUE,t);

            char tmp[512];
            mkpanstr(tmp,pan);
            SetDlgItemText(hwndDlg,IDC_PANLBL,tmp);
          }
        break;
      }
    return 0;
  }
  return 0;
}


static WDL_DLGRET RemoteChannelListProc(HWND hwndDlg, UINT uMsg, WPARAM wParam, LPARAM lParam)
{
  static WDL_PtrList<struct HWND__> m_children;
  switch (uMsg)
  {

    case WM_INITDIALOG:
      {
      }
    break;
    case WM_LCUSER_VUUPDATE:
      {
        HWND hwnd=GetWindow(hwndDlg,GW_CHILD);

        static int a;

        while (hwnd)
        {
          SendMessage(hwnd,uMsg,0,a);
          hwnd=GetWindow(hwnd,GW_HWNDNEXT);
        }        
        a++;
      }
    break;
    case WM_RCUSER_UPDATE:
      {
        int pos=0;
        int us;
        int did_sizing=0;
        RECT lastr={0,0,0,0};
        g_client->m_remotechannel_rd_mutex.Enter();
        for (us = 0; us < g_client->GetNumUsers(); us ++)
        {

          // add/update a user divider
          {
            HWND h=NULL;

            if (pos < m_children.GetSize() && GetWindowLongPtr(h=m_children.Get(pos),GWLP_USERDATA) < 0)
            {
              // this is our wnd
            }
            else
            {
              if (h) DestroyWindow(h);
              h=CreateDialog(g_hInst,MAKEINTRESOURCE(IDD_REMOTEUSER),hwndDlg,RemoteUserItemProc);
              if (pos < m_children.GetSize()) m_children.Set(pos,h);
              else m_children.Add(h);

              ShowWindow(h,SW_SHOWNA);
              did_sizing=1;
            }
            SendMessage(h,WM_RCUSER_UPDATE,(WPARAM)us,0);
            RECT r;
            GetWindowRect(h,&r);
            ScreenToClient(hwndDlg,(LPPOINT)&r);
            ScreenToClient(hwndDlg,((LPPOINT)&r) + 1);
            if (r.top != lastr.bottom) 
            {
              SetWindowPos(h,0, 0,lastr.bottom, 0,0, SWP_NOSIZE|SWP_NOZORDER|SWP_NOACTIVATE);
              GetWindowRect(h,&lastr);
              ScreenToClient(hwndDlg,(LPPOINT)&lastr);
              ScreenToClient(hwndDlg,((LPPOINT)&lastr) + 1);
            }
            else lastr=r;

            pos++;
          }

          int ch=0;
          for (;;)
          {
            int i=g_client->EnumUserChannels(us,ch++);
            if (i < 0) break;
            HWND h=NULL;
            if (pos < m_children.GetSize() && GetWindowLongPtr(h=m_children.Get(pos),GWLP_USERDATA) >= 0)
            {
            }
            else
            {
              if (h) DestroyWindow(h);
              h=CreateDialog(g_hInst,MAKEINTRESOURCE(IDD_REMOTECHANNEL),hwndDlg,RemoteChannelItemProc);
              if (pos < m_children.GetSize()) m_children.Set(pos,h);
              else m_children.Add(h);
              did_sizing=1;
              ShowWindow(h,SW_SHOWNA);
            }
            RECT r;
            GetWindowRect(h,&r);
            ScreenToClient(hwndDlg,(LPPOINT)&r);
            ScreenToClient(hwndDlg,((LPPOINT)&r)+1);
            if (r.top != lastr.bottom) 
            {
              SetWindowPos(h,0,0,lastr.bottom,0,0,SWP_NOSIZE|SWP_NOZORDER|SWP_NOACTIVATE);
              GetWindowRect(h,&lastr);
              ScreenToClient(hwndDlg,(LPPOINT)&lastr);
              ScreenToClient(hwndDlg,((LPPOINT)&lastr)+1);
            }
            else lastr=r;
            
            SendMessage(h,WM_RCUSER_UPDATE,(WPARAM)us,(LPARAM)i);
      
            pos++;
          }
        }

        g_client->m_remotechannel_rd_mutex.Leave();

        for (; pos < m_children.GetSize(); )
        {
          DestroyWindow(m_children.Get(pos));
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



WDL_DLGRET RemoteOuterChannelListProc(HWND hwndDlg, UINT uMsg, WPARAM wParam, LPARAM lParam)
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
        ScreenToClient(GetParent(hwndDlg),(LPPOINT)&r);
        ScreenToClient(GetParent(hwndDlg),((LPPOINT)&r)+1);
        m_wh=r.bottom-r.top;
        m_ww=r.right-r.left;


        SetWindowPos(hwndDlg,NULL,r.left,r.top,m_ww,m_wh,SWP_NOZORDER|SWP_NOACTIVATE);

        m_wh -= GetSystemMetrics(SM_CYHSCROLL);

        if (uMsg == WM_INITDIALOG)
        {
          InitializeCoolSB(hwndDlg);
          m_child=CreateDialog(g_hInst,MAKEINTRESOURCE(IDD_EMPTY),hwndDlg,RemoteChannelListProc);
          ShowWindow(m_child,SW_SHOWNA);
          ShowWindow(hwndDlg,SW_SHOWNA);
        }
      }

      {
        SendMessage(m_child,WM_RCUSER_UPDATE,0,0);
        RECT r;
        GetWindowRect(m_child,&r);
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
      #ifndef _WIN32
      InvalidateRect(hwndDlg,NULL,FALSE);
      #endif

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
      UninitializeCoolSB(hwndDlg);
    break;

  }
  return 0;
}


