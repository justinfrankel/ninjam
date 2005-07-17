/*
    NINJAM Windows Client - chat.cpp
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

  Chat dialog related code...

  */

#include <windows.h>
#include <richedit.h>
#include <commctrl.h>
#include "winclient.h"

#include "resource.h"


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
      chat_addline("",tmp.Get());
    
    }
  }
  else if (!strcmp(parms[0],"MSG"))
  {
    if (parms[1] && parms[2])
      chat_addline(parms[1],parms[2]);
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
      chat_addline(NULL,tmp.Get());
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
      chat_addline("",tmp.Get());
    }
  } 
}


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

void chat_addline(char *src, char *text)
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
  if (m_append_text.Get()[0])
    m_append_text.Append("\n");
  m_append_text.Append(tmp.Get());
  g_client_mutex.Leave();

}

void chatRun(HWND hwndDlg)
{
  WDL_String tmp;
  g_client_mutex.Enter();
  tmp.Set(m_append_text.Get());
  m_append_text.Set("");
  g_client_mutex.Leave();

  if (!tmp.Get()[0]) return;
  HWND m_hwnd=GetDlgItem(hwndDlg,IDC_CHATDISP);
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
