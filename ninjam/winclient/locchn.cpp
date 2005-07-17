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


#include <windows.h>
#include <commctrl.h>
#include <math.h>

#include "../chanmix.h"
#include "winclient.h"

#include "resource.h"

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

        if (!JesusonicAPI)
        {
          EnableWindow(GetDlgItem(hwndDlg,IDC_JS),0);
          EnableWindow(GetDlgItem(hwndDlg,IDC_JSCFG),0);
        }
        else if (jesinst)
          EnableWindow(GetDlgItem(hwndDlg,IDC_JSCFG),1);


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
         mkvolstr(tmp,vol);
         SetDlgItemText(hwndDlg,IDC_VOLLBL,tmp);
         mkpanstr(tmp,pan);
         SetDlgItemText(hwndDlg,IDC_PANLBL,tmp);
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
        int sch=0;
        g_client->GetLocalChannelProcessor(m_idx,NULL,&i);
        char *n=g_client->GetLocalChannelInfo(m_idx,&sch,NULL,NULL);
        g_client_mutex.Leave();
        if (IS_CMIX(sch))
        {
          ChanMixer *p=(ChanMixer *)sch;
          p->SetDesc(n?n:"");
        }
        if (i)
        {
          JesusUpdateInfo(i,buf,g_audio?g_audio->m_srate:44100);
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
              g_client_mutex.Enter();
              if (a == SendDlgItemMessage(hwndDlg,IDC_AUDIOIN,CB_GETCOUNT,0,0)-1) 
              {
                char *name=g_client->GetLocalChannelInfo(m_idx,&a,NULL,NULL);
                if (!IS_CMIX(a))
                {
                  ChanMixer *newa=new ChanMixer;
                  newa->SetDesc(name);
                  newa->CreateWnd(g_hInst,hwndDlg);
                  newa->SetNCH(g_audio?g_audio->m_innch:8);
                  if (g_audio)
                  {
                    int i;
                    for (i = 0; i < g_audio->m_innch; i ++)
                    {
                      const char *n=g_audio->GetChannelName(i);
                      if (n) newa->SetCHName(i,n);
                    }
                  }  
                  newa->DoWndUpdate();
                  ShowWindow(newa->GetWnd(),SW_SHOW);
                  a=(int)newa;
                  // set a to a cmix object
                }
                else
                {
                  ChanMixer *t=(ChanMixer *)a;
                  ShowWindow(t->GetWnd(),SW_SHOW);
                }
              }
              else
              {
                if (a == SendDlgItemMessage(hwndDlg,IDC_AUDIOIN,CB_GETCOUNT,0,0)-2) a=-1;

                int olda;
                g_client->GetLocalChannelInfo(m_idx,&olda,NULL,NULL);
                if (IS_CMIX(olda)) delete (ChanMixer *)olda;
              }
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
            void *p=CreateJesusInstance(m_idx,buf,g_audio?g_audio->m_srate:44100);
            if (p)
            {
              g_client->SetLocalChannelProcessor(m_idx,jesusonic_processor,p);
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
          int mch=max(sch,8);
          if (IS_CMIX(sch)) 
          {
            mch=((ChanMixer *)sch)->GetNCH();
            if (mch < 2) mch=2;
          }

          for (chcnt = 0; chcnt < mch; chcnt++)
          {
            char buf[128];
            sprintf(buf,"Input Channel %d",chcnt+1);
            SendDlgItemMessage(hwndDlg,IDC_AUDIOIN,CB_ADDSTRING,0,(LPARAM)buf);         
          }
        }
        SendDlgItemMessage(hwndDlg,IDC_AUDIOIN,CB_ADDSTRING,0,(LPARAM)"Silent (effects only)");         
        SendDlgItemMessage(hwndDlg,IDC_AUDIOIN,CB_ADDSTRING,0,(LPARAM)"Custom Mixer...");         
        SendDlgItemMessage(hwndDlg,IDC_AUDIOIN,CB_SETCURSEL,IS_CMIX(sch) ? chcnt+1 : (sch >= 0 ? sch : chcnt),0);
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
        sprintf(buf,"%s%.2f dB",val>0.0?"+":"",val);
        SetDlgItemText(hwndDlg,IDC_VULBL,buf);      
      }
    return 0;

  };
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



BOOL WINAPI LocalOuterChannelListProc(HWND hwndDlg, UINT uMsg, WPARAM wParam, LPARAM lParam)
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
    case WM_LCUSER_REPOP_CH:
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

