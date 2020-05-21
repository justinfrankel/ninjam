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
#include "../../../WDL/wdlcstring.h"
#include "../../../WDL/wdlutf8.h"

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
#ifndef _WIN32
  WDL_TypedQueue<char> g_chat_textappend;
#endif

void chatRun(HWND hwndDlg)
{
  static WDL_FastString tmp;
  m_append_mutex.Enter();
  tmp.Set(m_append_text.Get());
  m_append_text.Set("");
  m_append_mutex.Leave();

  if (!tmp.Get()[0]) return;
  
  HWND m_hwnd=GetDlgItem(hwndDlg,IDC_CHATDISP);
  const bool is_foc = GetFocus() == m_hwnd;
#ifdef _WIN32
  SCROLLINFO si={sizeof(si),SIF_RANGE|SIF_POS|SIF_TRACKPOS,};
  if (is_foc) GetScrollInfo(m_hwnd,SB_VERT,&si);
#endif

  int oldsel_s=0,oldsel_e=0;
  SendMessage(m_hwnd,EM_GETSEL,(WPARAM)&oldsel_s,(LPARAM)&oldsel_e);

#ifdef _WIN32
  // always feed in CRLF pairs to the richedit control to make sel positioning predictable (ugh)
  {
    for (int x=0;x<tmp.GetLength();x++)
    {
      if (tmp.Get()[x] == '\n' && (!x || tmp.Get()[x-1] != '\r'))
        tmp.Insert("\r",x++);
    }
  }
#endif

  const int maxlen = 30000;
  if (tmp.GetLength() > maxlen - 100) tmp.SetLen(maxlen - 100);
#ifdef _WIN32
  int textl = GetWindowTextLengthW(m_hwnd);
#else
  int textl = GetWindowTextLength(m_hwnd);
#endif
  const int dropb = textl + tmp.GetLength() + 2 - maxlen;
  if (dropb > 0)
  {
#ifdef _WIN32
    WCHAR txt[maxlen + 1024];
    GetWindowTextW(m_hwnd,txt,sizeof(txt) / sizeof(WCHAR));
#else
    char txt[maxlen + 1024];
    GetWindowText(m_hwnd,txt,sizeof(txt));
#endif
    int x, cr=0;
    for (x = 0; x < dropb && txt[x]; x ++)
    {
#ifdef _WIN32
      if (txt[x] == '\r') cr++;
#endif
    }
    for (; txt[x] && txt[x] != '\r' && txt[x] != '\n'; x++);
    for (; txt[x] == '\r' || txt[x] == '\n'; x++)
    {
#ifdef _WIN32
      if (txt[x] == '\r') cr++; 
#endif
    }

    textl -= x;
    x -= cr;

#ifndef _WIN32
    // selection range is in characters rather than bytes
    x = WDL_utf8_bytepos_to_charpos(txt,x);
#endif
    // remove x from start
    if (oldsel_s >= x && oldsel_e >= x)
    {
      oldsel_s -= x;
      oldsel_e -= x;
    }
    else
    {
      oldsel_s = oldsel_e = -1; // hmm not sure if this is best but oh well
    }

    SendMessage(m_hwnd,EM_SETSEL,0, x);
    SendMessage(m_hwnd,EM_REPLACESEL,0, (LPARAM)"");
  }

  if (textl) 
  {
#ifdef _WIN32
    tmp.Insert("\r\n",0);
#else
    tmp.Insert("\n",0);
#endif
  }

  SendMessage(m_hwnd,EM_SETSEL,textl, textl);

#ifdef _WIN32
  WCHAR buf[4096];
  if (GetVersion()<0x80000000)
  {
    if (MultiByteToWideChar(CP_UTF8,MB_ERR_INVALID_CHARS,tmp.Get(),-1,buf,4096))
    {
      buf[4095]=0;
      SendMessageW(m_hwnd,EM_REPLACESEL,0, (LPARAM)buf);
    }
    else
    {
      WCHAR *p = WDL_UTF8ToWC(tmp.Get(),NULL,0,NULL);
      if (p)
      {
        SendMessageW(m_hwnd,EM_REPLACESEL,0, (LPARAM)p);
        free(p);
      }
      else
      {
        SendMessage(m_hwnd,EM_REPLACESEL,0, (LPARAM)tmp.Get()); // fallback
      }
    }
  }
  else
#endif
    SendMessage(m_hwnd,EM_REPLACESEL,0, (LPARAM)tmp.Get());

  SendMessage(m_hwnd,EM_SETSEL,oldsel_s,oldsel_e);

#ifdef _WIN32
  if (!is_foc)
  {
    GetScrollInfo(m_hwnd,SB_VERT,&si);
    si.nTrackPos = si.nMax;
  }
  SendMessage(m_hwnd, WM_VSCROLL, MAKEWPARAM(SB_THUMBPOSITION,si.nTrackPos),0);
#else
  if (!is_foc) SendMessage(m_hwnd,EM_SCROLL,SB_BOTTOM,0);
#endif
}
