#include <windows.h>
#include <commctrl.h>
#include <stdio.h>
#include <math.h>

#include "../../WDL/lineparse.h"
#include "njmisc.h" // for utility functions like DB2VOL, etc
#include "chanmix.h"
#include "resource.h"

#define MIN_VOL 0.00001

ChanMixer::ChanMixer()
{
  m_nScrollPos_w=0;
  m_ww=0;
  m_w=0;
  m_maxpos_w=0;

  m_values_used=0;
  m_hwnd=0;
  memset(m_values,0,sizeof(m_values));
  memset(m_sliders,0,sizeof(m_sliders));
}

ChanMixer::~ChanMixer()
{
  if (m_hwnd && IsWindow(m_hwnd)) DestroyWindow(m_hwnd);
}


void ChanMixer::CreateWnd(HINSTANCE hInst, HWND parent)
{
  if (m_hwnd)
  {
    SetParent(m_hwnd,parent);
  }
  else
  {
    m_hwnd=CreateDialogParam(hInst,MAKEINTRESOURCE(IDD_MIXERDLG),parent,_DlgProc,(LPARAM)this);
    // create dialog
  }
}

void ChanMixer::LoadConfig(const char *str)
{
  LineParser lp(false);

  lp.parse((char *)str);

  m_values_used = lp.getnumtokens();
  if (m_values_used > MAX_CHANMIX_CHANS) m_values_used=MAX_CHANMIX_CHANS;

  int x;
  for (x = 0; x < m_values_used; x ++)
  {
    m_values[x]=(float)lp.gettoken_float(x);
  }

}

void ChanMixer::SaveConfig(WDL_String *str)
{
  int x;
  for (x = 0; x < m_values_used; x ++)
  {
    if (x) str->Append(" ");
    if (fabs(m_values[x]) <= MIN_VOL)
    {
      str->Append("0");
    }
    else
    {
      char buf[128];
      sprintf(buf,"%.4f",m_values[x]);
      if (strstr(buf,"."))
      {
        char *p=buf;
        while (*p) p++;
        p--;
        while (p > buf && *p == '0') p--;
        if (*p == '.') p[0]=0;
        else p[1]=0;
      }
      str->Append(buf);
    }
  }
}

void ChanMixer::MixData(float **inbuf, int in_offset, int innch, float *outbuf, int len)
{
  memset(outbuf,0,len*sizeof(float));
  if (m_values_used < innch) innch=m_values_used;
  int ch;
  for (ch = 0; ch < innch; ch ++)
  {
    float vol=m_values[ch];
    if (vol >= MIN_VOL) // -100dB or so
    {
      int l=len;
      float *in=inbuf[ch]+in_offset;
      float *out=outbuf;
      while (l--) 
      {
        float f=*in++ * vol;
        if (f < -1.0f) f=-1.0f;
        else if (f > 1.0f) f=1.0f;
        *out++ += f;
      }
    }
  }
}



#define MKDLGPROC(name) \
  BOOL WINAPI ChanMixer::_##name(HWND hwndDlg, UINT uMsg, WPARAM wParam, LPARAM lParam) {  \
    ChanMixer *t; \
    if (uMsg == WM_INITDIALOG) SetWindowLong(hwndDlg,GWL_USERDATA,(LONG)(t=(ChanMixer *)lParam)); \
    else t=(ChanMixer*)GetWindowLong(hwndDlg,GWL_USERDATA); \
    return t?t->name(hwndDlg,uMsg,wParam,lParam):0;  \
  }

MKDLGPROC(DlgProc)
MKDLGPROC(DlgProc_scrollchild)


BOOL ChanMixer::DlgProc(HWND hwndDlg, UINT uMsg, WPARAM wParam, LPARAM lParam)
{
  switch (uMsg)
  {
    case WM_TIMER:
      if (wParam == 666)
      {
        KillTimer(hwndDlg,666);
        if (m_w+16 < m_ww)
        {
          RECT r;
          GetWindowRect(hwndDlg,&r);
          m_ww = m_w+16;
          SetWindowPos(hwndDlg,0,0,0,m_ww,r.bottom-r.top,SWP_NOMOVE|SWP_NOZORDER|SWP_NOACTIVATE);
        }
      }
    break;
    case WM_INITDIALOG:
      m_childwnd=CreateDialogParam((HINSTANCE)GetWindowLong(hwndDlg,GWL_HINSTANCE),MAKEINTRESOURCE(IDD_EMPTY),hwndDlg,_DlgProc_scrollchild,(LPARAM)this);
      ShowWindow(m_childwnd,SW_SHOWNA);
      SetTimer(hwndDlg,666,100,NULL);
    case WM_SIZE:
    case WM_USER+1:
      if (uMsg != WM_SIZE || wParam != SIZE_MINIMIZED)
      {     
        RECT r;
        GetWindowRect(m_childwnd,&r);
        m_w=r.right-r.left;

        GetWindowRect(hwndDlg,&r);
        m_ww=r.right-r.left;

        m_maxpos_w=m_w-m_ww;
        if (m_maxpos_w < 0) m_maxpos_w=0;

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
    return 0;

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
    case WM_USER+32:
      SendMessage(m_childwnd,WM_USER+32,0,0);
    return 0;
  
    case WM_GETMINMAXINFO:
      {
        LPMINMAXINFO p=(LPMINMAXINFO)lParam;
        p->ptMinTrackSize.x = 80;
        p->ptMinTrackSize.y = 264;
        p->ptMaxTrackSize.y = 264;
        p->ptMaxTrackSize.x = m_w+16;
      }
    return 0;
    case WM_CLOSE:
      ShowWindow(hwndDlg,SW_HIDE);
    return 0;
  }
  return 0;
}

BOOL ChanMixer::DlgProc_scrollchild(HWND hwndDlg, UINT uMsg, WPARAM wParam, LPARAM lParam)
{
  switch (uMsg)
  {
    case WM_USER+32:
    case WM_INITDIALOG:
      int x;
      {
        int maxy=100,maxx=100;
        for (x = 0; x < m_values_used; x ++)
        {
          if (!m_sliders[x])
          {
            HWND h=CreateDialogParam((HINSTANCE)GetWindowLong(hwndDlg,GWL_HINSTANCE),MAKEINTRESOURCE(IDD_MIXERITEM),hwndDlg,_DlgProc_item,(LPARAM)x);
            m_sliders[x]=h;

            RECT r;
            GetClientRect(h,&r);
            if (r.bottom-r.top > maxy) maxy=r.bottom-r.top;
            SetWindowPos(h,0,x*(r.right-r.left),0,0,0,SWP_NOSIZE|SWP_NOZORDER|SWP_NOACTIVATE);
            ShowWindow(h,SW_SHOWNA);
            maxx=(x+1)*(r.right-r.left);
          }
          else 
          {
            RECT r;
            GetClientRect(m_sliders[x],&r);
            maxx=(x+1)*(r.right-r.left);
            if (r.bottom-r.top > maxy) maxy=r.bottom-r.top;
            SendMessage(m_sliders[x],WM_USER+66,0,0);
          }
        }
        SetWindowPos(hwndDlg,NULL,0,0,maxx,maxy,SWP_NOMOVE|SWP_NOZORDER|SWP_NOACTIVATE);
        for (; x < MAX_CHANMIX_CHANS; x ++)
        {
          if (m_sliders[x])
          {
            DestroyWindow(m_sliders[x]);
            m_sliders[x]=0;
          }
        }
        if (uMsg != WM_INITDIALOG) 
          SendMessage(GetParent(hwndDlg),WM_USER+1,0,0);
      }
    return 0;
  }
  return 0;
}

BOOL WINAPI ChanMixer::_DlgProc_item(HWND hwndDlg, UINT uMsg, WPARAM wParam, LPARAM lParam)
{
  int chid;
  if (uMsg == WM_INITDIALOG) SetWindowLong(hwndDlg,GWL_USERDATA,(LONG)(chid=(int)lParam));
  else chid=(int)GetWindowLong(hwndDlg,GWL_USERDATA);

  ChanMixer *_this=(ChanMixer*)GetWindowLong(GetParent(hwndDlg),GWL_USERDATA);

  if (_this) switch (uMsg)
  {
    case WM_INITDIALOG:
      SendDlgItemMessage(hwndDlg,IDC_VOL,TBM_SETRANGE,FALSE,MAKELONG(0,100));
      SendDlgItemMessage(hwndDlg,IDC_VOL,TBM_SETTIC,FALSE,100-63);       
      SendDlgItemMessage(hwndDlg,IDC_VOL,TBM_SETPOS,TRUE,100-(LPARAM)DB2SLIDER(VAL2DB(_this->m_values[chid])));      

    case WM_USER+66:
      SetDlgItemText(hwndDlg,IDC_LABEL,_this->m_chname[chid].Get());
    break;
    case WM_VSCROLL:
      {
        double pos=(double)(100-SendMessage((HWND)lParam,TBM_GETPOS,0,0));

        _this->m_values[chid] = (float)DB2VAL(SLIDER2DB(pos));
      }
    break;

  }
  if (uMsg == WM_INITDIALOG || uMsg == WM_VSCROLL)
  {
    char buf[128];
    sprintf(buf,"%.2f dB",VAL2DB(_this->m_values[chid]));
    SetDlgItemText(hwndDlg,IDC_LABEL2,buf);      
  }
  return 0;
}
