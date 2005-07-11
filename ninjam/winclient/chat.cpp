#include <windows.h>
#include <richedit.h>
#include <commctrl.h>
#include "../../WDL/string.h"
#include "../../WDL/mutex.h"

#include "resource.h"

extern HINSTANCE g_hInst;
extern HWND g_hwnd;


WNDPROC chatw_oldWndProc,chate_oldWndProc;
BOOL CALLBACK chatw_newWndProc(HWND hwndDlg, UINT uMsg, WPARAM wParam,LPARAM lParam)
{
  if (uMsg == WM_CHAR)
  {
    HWND h=GetDlgItem(GetParent(hwndDlg),IDC_CHATENT);
    SetFocus(h);
    if (wParam == VK_RETURN)
    {
      SendMessage(GetParent(hwndDlg),WM_COMMAND,IDC_CHATOK,0);
      return 0;
    }
    SendMessage(h,uMsg,wParam,lParam);
    return 0;
  }
  return CallWindowProc(chatw_oldWndProc,hwndDlg,uMsg,wParam,lParam);
}
BOOL CALLBACK chate_newWndProc(HWND hwndDlg, UINT uMsg, WPARAM wParam,LPARAM lParam)
{
  if (uMsg == WM_CHAR)
  {
    if (wParam == VK_RETURN)
    {
      SendMessage(GetParent(hwndDlg),WM_COMMAND,IDC_CHATOK,0);
      return 0;
    }
  }
  return CallWindowProc(chatw_oldWndProc,hwndDlg,uMsg,wParam,lParam);
}
void chatInit(HWND hwndDlg)
{
  chatw_oldWndProc=(WNDPROC) SetWindowLong(GetDlgItem(hwndDlg,IDC_CHATDISP),GWL_WNDPROC,(LONG)chatw_newWndProc);
  chate_oldWndProc=(WNDPROC) SetWindowLong(GetDlgItem(hwndDlg,IDC_CHATENT),GWL_WNDPROC,(LONG)chate_newWndProc);
}

WDL_String m_append_text;
extern WDL_Mutex g_client_mutex;

void addChatLine(char *src, char *text)
{
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

  g_client_mutex.Enter();
  m_append_text.Append(tmp.Get());
  g_client_mutex.Leave();

}

void chat_run()
{
  WDL_String tmp;
  g_client_mutex.Enter();
  tmp.Set(m_append_text.Get());
  m_append_text.Set("");
  g_client_mutex.Leave();

  if (!tmp.Get()[0]) return;
  HWND m_hwnd=GetDlgItem(g_hwnd,IDC_CHATDISP);
  SCROLLINFO si={sizeof(si),SIF_RANGE|SIF_POS|SIF_TRACKPOS,};
  GetScrollInfo(m_hwnd,SB_VERT,&si);

  {
    int oldsels,oldsele;
    SendMessage(m_hwnd,EM_GETSEL,(WPARAM)&oldsels,(LPARAM)&oldsele);
	  char txt[32768];
	  if(strlen(tmp.Get())>sizeof(txt)-1) return;

	  GetWindowText(m_hwnd,txt,sizeof(txt)-1);
	  txt[sizeof(txt)-1]=0;

	  while(strlen(txt)+strlen(tmp.Get())+4>sizeof(txt))
	  {
		  char *p=txt;
		  while(*p!=0 && *p!='\n') p++;
		  if(*p==0) return;
		  while (*p=='\n') p++;
		  strcpy(txt,p);
      oldsels -= p-txt;
      oldsele -= p-txt;
	  }
    if (oldsels < 0) oldsels=0;
    if (oldsele < 0) oldsele=0;

	  if(txt[0]) strcat(txt,"\n");
	  strcat(txt,tmp.Get());

    CHARFORMAT2 cf2;
    cf2.cbSize=sizeof(cf2);
    cf2.dwMask=CFM_LINK;
    cf2.dwEffects=0;
    SendMessage(m_hwnd,EM_SETCHARFORMAT,SCF_ALL,(LPARAM)&cf2);
	  SetWindowText(m_hwnd,txt);

    GetWindowText(m_hwnd,txt,sizeof(txt)-1);
    txt[sizeof(txt)-1]=0;

    char *t=txt;
    char lt=' ';
    int sub=0;
    while (*t)
    {
      if (lt == ' ' || lt == '\n' || lt == '\r')
      {
        int isurl=0;
        if (!strnicmp(t,"http:",5)) isurl=5;
        else if (!strnicmp(t,"ftp:",4)) isurl=4;
        else if (!strnicmp(t,"www.",4)) isurl=4;

        if (isurl && t[isurl] != ' ' && t[isurl] != '\n' && t[isurl] != '\r' && t[isurl])
        {
          int spos=t-txt-sub;
          t+=isurl;
          while (*t && *t != ' ' && *t != '\n' && *t != '\r') { t++; }
          SendMessage(m_hwnd,EM_SETSEL,spos,(t-txt)-sub);
          CHARFORMAT2 cf2;
          cf2.cbSize=sizeof(cf2);
          cf2.dwMask=CFM_LINK;
          cf2.dwEffects=CFE_LINK;
          SendMessage(m_hwnd,EM_SETCHARFORMAT,SCF_SELECTION,(LPARAM)&cf2);
        }
      }
      if (*t == '\n') sub++;
      if (*t) lt=*t++;
    }
    SendMessage(m_hwnd,EM_SETSEL,oldsels,oldsele);
  }

  if (GetFocus() == m_hwnd)      
  {
    SendMessage(m_hwnd, WM_VSCROLL, MAKEWPARAM(SB_THUMBPOSITION,si.nTrackPos),0);
  }
  else
  {
    GetScrollInfo(m_hwnd,SB_VERT,&si);
    SendMessage(m_hwnd, WM_VSCROLL, MAKEWPARAM(SB_THUMBPOSITION,si.nMax),0);
  }
}
