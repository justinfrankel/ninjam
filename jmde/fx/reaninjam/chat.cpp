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

#ifdef _WIN32
#include <windows.h>
#include <richedit.h>
#include <commctrl.h>
#else
#include "../../../WDL/swell/swell.h"
#endif
#include "winclient.h"

#include "resource.h"


void chatmsg_cb(void *userData, NJClient *inst, const char **parms, int nparms)
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


WDL_String m_append_text;
WDL_Mutex m_append_mutex;

void chat_addline(const char *src, const char *text)
{
  WDL_String tmp;
  if (src && *src && !strncmp(text,"/me ",4))
  {
    tmp.Set("* ");
    tmp.Append(src);
    tmp.Append(" ");
    const char *p=text+3;
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

  m_append_mutex.Enter();
  if (m_append_text.Get()[0])
    m_append_text.Append("\n");
  m_append_text.Append(tmp.Get());
  m_append_mutex.Leave();

}

void chatRun(HWND hwndDlg)
{
  WDL_String tmp;
  m_append_mutex.Enter();
  tmp.Set(m_append_text.Get());
  m_append_text.Set("");
  m_append_mutex.Leave();

  if (!tmp.Get()[0]) return;
  
  HWND m_hwnd=GetDlgItem(hwndDlg,IDC_CHATDISP);
#ifdef _WIN32
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
#else
  static WDL_TypedQueue<char> bla;
  int sz=bla.Available();
  char *p=bla.Add(tmp.Get(),strlen(tmp.Get())+1);
  if (sz) p[-1]='\n';
  
  sz=bla.Available();
  if (sz> 4096) 
  {
    bla.Advance(sz-4096);  
    while (bla.Available() > 0 && bla.Get()[0] != '\n') bla.Advance(1);
    bla.Advance(1);
    bla.Compact();
  }
  if (bla.Available() > 0)
    SetWindowText(m_hwnd,bla.Get());
  else SetWindowText(m_hwnd,"");
#endif
}
