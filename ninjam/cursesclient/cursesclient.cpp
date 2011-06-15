/*
    NINJAM Curses Client - cursesclient.cpp
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

  Curses (text mode) client code. On Windows this requires the (included)
  win32 curses emulation layer.
  
  */

#ifdef _WIN32
#define CURSES_INSTANCE (&m_cursinst)
#include <windows.h>
#include "curses.h"
#include "cursesclientinst.h"
#define strncasecmp strnicmp
#else
#include <stdlib.h>
#include <memory.h>
#include <curses.h>
#endif

#include <stdio.h>
#include <ctype.h>
#include <math.h>
#include <signal.h>
#include <float.h>

#include "../audiostream.h"
#include "../njclient.h"
#include "../../WDL/dirscan.h"
#include "../../WDL/lineparse.h"

#include "../njmisc.h"


#define VALIDATE_TEXT_CHAR(thischar) ((isspace(thischar) || isgraph(thischar)) && (thischar) < 256)
#ifdef _WIN32
#define getch() curses_getch(CURSES_INSTANCE)
#define erase() curses_erase(CURSES_INSTANCE)

void ninjamCursesClientInstance::Run()
{
}


ninjamCursesClientInstance m_cursinst;

jesusonicAPI *JesusonicAPI;
WDL_String jesusdir;
#endif

int g_chat_scroll=0;
int curs_ypos,curs_xpos;
int color_map[8];
int g_ui_inchat=0;
int g_done=0;
int g_ui_state=0;
int g_ui_locrename_ch;
int g_ui_voltweakstate_channel;
int g_need_disp_update;
char m_lineinput_str[120];
char m_chatinput_str[120];

WDL_PtrList<char> g_chat_buffers;

void addChatLine(const char *src, const char *text)
{
  while (g_chat_buffers.GetSize() > 256)
  {
    free(g_chat_buffers.Get(0));
    g_chat_buffers.Delete(0);
  }
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
  g_chat_buffers.Add(strdup(tmp.Get()));
  g_chat_scroll=0;
}

WDL_String g_topic;

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
        tmp.Append(" sets topic to: ");
      }
      else tmp.Set("Topic is: ");
      tmp.Append(parms[2]);

      g_topic.Set(parms[2]);
      addChatLine("",tmp.Get());
    
      g_need_disp_update=1;
    }
  }
  else if (!strcmp(parms[0],"MSG"))
  {
    if (parms[1] && parms[2])
      addChatLine(parms[1],parms[2]);
    g_need_disp_update=1;
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
      addChatLine(NULL,tmp.Get());
    }
    g_need_disp_update=1;
  } 
  else if (!strcmp(parms[0],"JOIN") || !strcmp(parms[0],"PART"))
  {
    if (parms[1] && *parms[1])
    {
      WDL_String tmp(parms[1]);
      tmp.Append(" has ");
      tmp.Append(parms[0][0]=='P' ? "left" : "joined");
      tmp.Append(" the server");
      addChatLine("",tmp.Get());
    }
    g_need_disp_update=1;
  } 
}


#ifdef _WIN32
audioStreamer *CreateConfiguredStreamer(char *inifile, int showcfg, HWND hwndParent);
#endif
audioStreamer *g_audio;
NJClient *g_client;


void audiostream_onunder() { }
void audiostream_onover() { }

int g_audio_enable=0;

void audiostream_onsamples(float **inbuf, int innch, float **outbuf, int outnch, int len, int srate) 
{ 
  if (!g_audio_enable) 
  {
    int x;
    // clear all output buffers
    for (x = 0; x < outnch; x ++) memset(outbuf[x],0,sizeof(float)*len);
    return;
  }
  g_client->AudioProc(inbuf,innch, outbuf, outnch, len,srate);
}


int g_sel_x, g_sel_ypos,g_sel_ycat;

#define COLORMAP(x) color_map[x]



// highlights shit in []
void highlightoutline(int line, const char *str, int attrnorm, int bknorm, int attrhi, int bkhi, int attrsel, int bksel, int whl)
{
  int state=0;
  int l=COLS-1;
  int lcol=0;
  move(line,0);
	attrset(attrnorm);
	bkgdset(bknorm);

  while (*str && l-- > 0)
  {
    if (*str == ']')
    {
      if (state)
      {
	      attrset(attrnorm);
    	  bkgdset(bknorm);
        state=0;
      }
    }
    addch(*str);
    if (*str == '[')
    {
      if (whl > 0)
      {
        char *tmp=strstr(str,"]");
        if (tmp && !strstr(tmp,"[")) 
        {
          whl=0;
          g_sel_x=lcol;
        }
      }
      if (!state)
      {
        lcol++;
        if (!whl--)
        {
          attrset(attrsel);
          bkgdset(bksel);
        }
        else
        {
          attrset(attrhi);
          bkgdset(bkhi);
        }
        state=1;
      }
    }
    str++;

  }
  if (state)
  {
	  attrset(attrnorm);
	  bkgdset(bknorm);
  }

}


void drawstatusbar()
{
  if (g_ui_state) return;
  int l,p;
  g_client->GetPosition(&p,&l);
  if (!l) return;

	bkgdset(COLORMAP(6));
	attrset(COLORMAP(6));

  move(LINES-2,0);
  p*=(COLS);
  p/=l;
  int x;
  for (x = 0; x < COLS; x ++) addch(x <= p ? '#' : ' ');

	bkgdset(COLORMAP(0));
	attrset(COLORMAP(0));

  move(curs_ypos,curs_xpos); 
}

void showmainview(bool action=false, int ymove=0)
{
  int chat_lines=LINES/4;
  if (chat_lines<4) chat_lines=4;

  int sec1lines=2;
  int x;
  for (x=0;x<MAX_LOCAL_CHANNELS;x++)
  {
    if (g_client->EnumLocalChannels(x)<0) break;
    sec1lines++;
  }

  int sec2lines=0;

  x=0;
  for (;;)
  {
    if (!g_client->GetUserState(x)) break;

    int y=0;
    for (;;)
    {
      if (g_client->EnumUserChannels(x,y) < 0) break;
      sec2lines++;
      y++;
    }
    x++;
  }
  if (!sec2lines) sec2lines=1;

  if (ymove < 0)
  {
    if (g_sel_ypos-- <= 0)
    {
      if (g_sel_ycat == 1) g_sel_ypos=sec1lines-1;
      else if (g_sel_ycat == 2) g_sel_ypos=sec2lines-1; 
      else g_sel_ypos=0;

      if (g_sel_ycat>0) g_sel_ycat--;
    }
  }
  else if (ymove > 0)
  {
    g_sel_ypos++;
  }

  if (!ymove && g_sel_ycat == 1 && g_sel_ypos >= sec2lines)
  {
    g_sel_ypos=sec2lines-1;
  }

  int selpos=0;
  int selcat=0;
	bkgdset(COLORMAP(0));
	attrset(COLORMAP(0));

  erase();
	bkgdset(COLORMAP(1));
	attrset(COLORMAP(1));
	mvaddstr(0,0,"LOCAL");
  clrtoeol();
	bkgdset(COLORMAP(0));
	attrset(COLORMAP(0));
  char linebuf[1024];
  int linemax=LINES-2-chat_lines;
  {
    if (action && g_sel_ycat == selcat && g_sel_ypos == selpos)
    {
      if (g_sel_x == 1 || g_sel_x == 3)
      {
        g_ui_state=1;        
        g_ui_voltweakstate_channel=g_sel_x == 1 ? -2 : -1;
      }
      else
      {
        if (g_sel_x == 0)
        {
          g_client->config_mastermute=!g_client->config_mastermute;
        }
        else
        {
          g_client->config_metronome_mute=!g_client->config_metronome_mute;
        }
      }
    }
    sprintf(linebuf,"  master: [%c]mute [",g_client->config_mastermute?'X':' ');
    mkvolpanstr(linebuf+strlen(linebuf),g_client->config_mastervolume,g_client->config_masterpan);

    sprintf(linebuf+strlen(linebuf),"]    |    metronome: [%c]mute [",g_client->config_metronome_mute?'X':' ');
    mkvolpanstr(linebuf+strlen(linebuf),g_client->config_metronome,g_client->config_metronome_pan);
    sprintf(linebuf+strlen(linebuf),"]");

    highlightoutline(1,linebuf,COLORMAP(0),COLORMAP(0),
                               COLORMAP(0)|A_BOLD,COLORMAP(0),
                               COLORMAP(5),COLORMAP(5),(g_sel_ypos != selpos++ || g_sel_ycat != selcat) ? -1 : g_sel_x);
//	  mvaddnstr(1,0,linebuf,COLS-1);
  }
  int ypos=2;
  for (x=0;ypos < linemax;x++)
  {
    int a=g_client->EnumLocalChannels(x);
    if (a<0) break;
    int sch;
    bool bc,mute;
    float vol,pan;
    char *name=g_client->GetLocalChannelInfo(a,&sch,NULL,&bc);
    g_client->GetLocalChannelMonitoring(a,&vol,&pan,&mute,NULL);

    if (action && g_sel_ycat == selcat && g_sel_ypos == selpos)
    {
      if (g_sel_x == 0)
      {
        g_ui_state=2;
        g_ui_locrename_ch=a;
        strncpy(m_lineinput_str,name,sizeof(m_lineinput_str)-1);
      }
      else if (g_sel_x == 1)
      {
        // toggle active
        g_client->SetLocalChannelInfo(a,NULL,false,0,false,0,true,bc=!bc);
        g_client->NotifyServerOfChannelChange();
      }
      else if (g_sel_x == 2)
      {
        g_ui_state=3;
        g_ui_locrename_ch=a;
      }
      else if (g_sel_x == 3)
      {
        // mute
        g_client->SetLocalChannelMonitoring(a,false,0.0f,false,0.0f,true,mute=!mute,false,false);
      }
      else if (g_sel_x == 4)
      {
        //volume
        g_ui_state=1;        
        g_ui_voltweakstate_channel=a;
      }
#ifdef _WIN32
      else if (g_sel_x >= 6 && JesusonicAPI)
      {
        void *i=0;
        g_client->GetLocalChannelProcessor(a,NULL,&i);
        if (!i)
        {
          // start it up
          void *p=CreateJesusInstance(a,"",g_audio->m_srate);
          if (p) g_client->SetLocalChannelProcessor(a,jesusonic_processor,p);
        }
        else 
        {
          if (g_sel_x >= 7)  // kill
          {
            g_client->SetLocalChannelProcessor(a,NULL,NULL);
            deleteJesusonicProc(i,a);
          }
          else
          {
           // JesusonicAPI->ui_wnd_destroy(i);
            HWND h=JesusonicAPI->ui_wnd_gethwnd(i);
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

            // show
          }
        }
      }
#endif
      else if (g_sel_x >= 5)
      {
#ifdef _WIN32
        void *i=0;
        g_client->GetLocalChannelProcessor(a,NULL,&i);
        if (i) deleteJesusonicProc(i,a);
#endif
        g_client->DeleteLocalChannel(a);
        g_client->NotifyServerOfChannelChange();
        x--;
        action=0;
        continue;
        // delete
      }
    }


#ifdef _WIN32
    void *tmp;
    g_client->GetLocalChannelProcessor(a,NULL,&tmp); 
#endif
    char volstr[256];
    mkvolpanstr(volstr,vol,pan);
    const char *sname=g_audio->GetChannelName(sch);
    if (!sname) sname="Silence";

    char snamebuf[32];
    if (strlen(sname)>16)
    {
      strcpy(snamebuf,"...");
      strcat(snamebuf,sname+strlen(sname)-13);
      sname=snamebuf;
    }
    sprintf(linebuf,"  [%s] [%c]xmit [%s] [%c]mute [%s] [del] ",name,bc?'X':' ',sname,mute?'X':' ',volstr);
    

#ifdef _WIN32
    if (JesusonicAPI)
    {
      sprintf(linebuf+strlen(linebuf),"[js][%c] ", tmp?'x': ' ');
    }
#endif

    sprintf(linebuf+strlen(linebuf),
      "<%2.1fdB>",
      VAL2DB(g_client->GetLocalChannelPeak(a)));

    highlightoutline(ypos++,linebuf,COLORMAP(0),COLORMAP(0),
                               COLORMAP(0)|A_BOLD,COLORMAP(0),
                               COLORMAP(5),COLORMAP(5),(g_sel_ypos != selpos++ || g_sel_ycat != selcat) ? -1 : g_sel_x);
  }
  if (ypos < LINES-3)
  {
    if (action && g_sel_ycat == selcat && g_sel_ypos == selpos)
    {
      int x;
      for (x = 0; x < g_client->GetMaxLocalChannels(); x ++)
      {
        if (!g_client->GetLocalChannelInfo(x,NULL,NULL,NULL)) break;
      }
      if (x < g_client->GetMaxLocalChannels())
      {
        g_client->SetLocalChannelInfo(x,"channel",true,0,false,0,true,false);
        g_client->NotifyServerOfChannelChange();

        const char *sname=g_audio->GetChannelName(0);
        if (!sname) sname="Silence";        

        char snamebuf[32];
        if (strlen(sname)>16)
        {
          strcpy(snamebuf,"...");
          strcat(snamebuf,sname+strlen(sname)-13);
          sname=snamebuf;
        }
        
        char volstr[256];
        mkvolpanstr(volstr,1.0f,0.0f);
        sprintf(linebuf,"  [channel] [ ]xmit [%s] [ ]mute [%s] [del] %s<-120dB>",
          sname,
          volstr,
#ifdef _WIN32
          JesusonicAPI?"[js][ ] " : 
#endif
           ""
          
          );

        action=false;
        selpos++;

        highlightoutline(ypos++,linebuf,COLORMAP(0),COLORMAP(0),
                                   COLORMAP(0)|A_BOLD,COLORMAP(0),
                                   COLORMAP(5),COLORMAP(5),g_sel_x);

      }
    }
    if (ypos < LINES-3)
    {
      highlightoutline(ypos++,"  [new channel]",COLORMAP(0),COLORMAP(0),
                                 COLORMAP(0)|A_BOLD,COLORMAP(0),
                                 COLORMAP(5),COLORMAP(5),(g_sel_ypos != selpos++ || g_sel_ycat != selcat) ? -1 : g_sel_x);
    }
  }

  int wasadv=0;
  if (g_sel_ycat == selcat && g_sel_ypos >= selpos)
  {
    wasadv=1;
    g_sel_ycat++;
    g_sel_ypos=0;
  }

  selpos=0;
  selcat=1;

  if (ypos < LINES-3)
  {
	  bkgdset(COLORMAP(6));
	  attrset(COLORMAP(6));
	  mvaddnstr(ypos++,0,"REMOTE",COLS-1);
    clrtoeol();
	  bkgdset(COLORMAP(0));
	  attrset(COLORMAP(0));
  }
  int user=0;
  x=0;
  while (ypos < linemax)
  {
    if (!x) // show user info
    {
      char *name=g_client->GetUserState(user);
      if (!name) break;

	    bkgdset(COLORMAP(4));
	    attrset(COLORMAP(4));
	    mvaddnstr(ypos++,0,name,COLS-1);

      clrtoeol();
	    bkgdset(COLORMAP(0));
	    attrset(COLORMAP(0));
      
    }

    if (ypos >= linemax) break;

    int a=g_client->EnumUserChannels(user,x);
    if (a < 0)
    {
      x=0;
      user++;
      continue;
    }

    float vol,pan;
    bool sub,mute;
    char *name=g_client->GetUserChannelState(user,a,&sub,&vol,&pan,&mute);
    // show channel info


    if (action && g_sel_ycat == selcat && g_sel_ypos == selpos)
    {
      if (g_sel_x == 0)
      {
        // toggle subscribe
        g_client->SetUserChannelState(user,a,true,sub=!sub,false,0.0f,false,0.0f,false,false,false,false);
      }
      else if (g_sel_x == 1)
      {
        // toggle mute
        g_client->SetUserChannelState(user,a,false,false,false,0.0f,false,0.0f,true,mute=!mute,false,false);
      }
      else if (g_sel_x >= 2)
      {
        // volume
        g_ui_state=1;
        g_ui_voltweakstate_channel=1024+64*user+a;
      }
    }

    char volstr[256];
    mkvolpanstr(volstr,vol,pan);
    sprintf(linebuf,"  \"%s\" [%c]recv [%c]mute [%s] <%2.1fdB>",name,sub?'X':' ',mute?'X':' ',volstr,VAL2DB(g_client->GetUserChannelPeak(user,a)));

    highlightoutline(ypos++,linebuf,COLORMAP(0),COLORMAP(0),
                               COLORMAP(0)|A_BOLD,COLORMAP(0),
                               COLORMAP(5),COLORMAP(5),(g_sel_ypos != selpos++ || g_sel_ycat != selcat) ? -1 : g_sel_x);

    x++;

    
  }

  if (!selpos && ypos < linemax)
  {
    highlightoutline(ypos++,"[no remote users]",COLORMAP(0),COLORMAP(0),
                               COLORMAP(0)|A_BOLD,COLORMAP(0),
                               COLORMAP(5),COLORMAP(5),(g_sel_ypos != selpos++ || g_sel_ycat != selcat) ? -1 : g_sel_x);
  }

  curs_ypos=LINES-1;
  curs_xpos=0;

  if (!selpos && wasadv) g_sel_ycat++;
  if (selpos > 0 && g_sel_ycat == selcat && g_sel_ypos >= selpos)
  {
    g_sel_ycat++;
    g_sel_ypos=0;
  }

  selcat=2;
  selpos=0;

  g_ui_inchat=0;
  if (chat_lines>=4)
  {
	  bkgdset(COLORMAP(1));
	  attrset(COLORMAP(1));
    mvaddnstr(LINES-2-chat_lines,0,g_topic.Get()[0]?g_topic.Get():"CHAT",COLS-1);
    clrtoeol();
	  bkgdset(COLORMAP(0));
	  attrset(COLORMAP(0));

    int x;
    if (g_chat_scroll > g_chat_buffers.GetSize()-(chat_lines-2)) g_chat_scroll=g_chat_buffers.GetSize()-(chat_lines-2);
    int pos=g_chat_buffers.GetSize()-g_chat_scroll;
    if (pos < 0) pos=0;
    else if (pos > g_chat_buffers.GetSize()) pos=g_chat_buffers.GetSize();

    for (x = 0; x < chat_lines-2; )
    {
      char *p;
      if (--pos < 0 || !(p=g_chat_buffers.Get(pos))) break;

      char *np=p;
      int maxw=COLS-1;
      while ((int)strlen(np) > maxw) np+=maxw;

      while (np >= p && x < chat_lines-2)
      {
        mvaddnstr(LINES-2-2-x,0,np,maxw);
        x++;
        np-=maxw;
      }

    }

    if (g_sel_ycat == selcat && g_sel_ypos == selpos++)
    {
      g_sel_x=0;
      g_ui_inchat=1;
	    bkgdset(COLORMAP(2));
	    attrset(COLORMAP(2));
      curs_ypos=LINES-2-1;
      curs_xpos=strlen(m_chatinput_str);
    }
    else
    {
	    bkgdset(COLORMAP(3));
	    attrset(COLORMAP(3));
    }
	  mvaddstr(LINES-2-1,0,m_chatinput_str);
    clrtoeol();
	  bkgdset(COLORMAP(0));
	  attrset(COLORMAP(0));
  }


  if (g_sel_ycat == selcat && g_sel_ypos > selpos) g_sel_ypos=selpos;

  if (g_ui_state==1)
  {
	  bkgdset(COLORMAP(2));
	  attrset(COLORMAP(2));
	  mvaddnstr(LINES-2,0,"USE UP AND DOWN FOR VOLUME, LEFT AND RIGHT FOR PANNING, ENTER WHEN DONE",COLS-1);
    clrtoeol();
	  bkgdset(COLORMAP(0));
	  attrset(COLORMAP(0));
  }
  else if (g_ui_state == 3)
  {
	  bkgdset(COLORMAP(2));
	  attrset(COLORMAP(2));
	  mvaddnstr(LINES-2,0,"USE ARROW KEYS TO SELECT THE INPUT CHANNEL, ENTER WHEN DONE",COLS-1);
    clrtoeol();
	  bkgdset(COLORMAP(0));
	  attrset(COLORMAP(0));
  }
  else drawstatusbar();


  ypos=LINES-1;
  sprintf(linebuf,"[QUIT NINJAM] : %s : %.1fBPM %dBPI : %dHz %dch->%dch %dbps%s",
    g_client->GetHostName(),g_client->GetActualBPM(),g_client->GetBPI(),g_audio->m_srate,g_audio->m_innch,g_audio->m_outnch,g_audio->m_bps&~7,g_audio->m_bps&1 ? "(f)":"");
  highlightoutline(ypos++,linebuf,COLORMAP(1),COLORMAP(1),COLORMAP(1),COLORMAP(1),COLORMAP(5),COLORMAP(5),(g_sel_ypos != selpos || g_sel_ycat != selcat) ? -1 : g_sel_x);
  attrset(COLORMAP(1));
  bkgdset(COLORMAP(1));
  clrtoeol();
  if (action && g_sel_ycat == selcat && g_sel_ypos == selpos)
  {
    g_done++;
  }

  if (g_ui_state == 2 || g_ui_state == 4)
  {
	  bkgdset(COLORMAP(2));
	  attrset(COLORMAP(2));
    const char *p1="RENAME CHANNEL:";
	  mvaddnstr(LINES-2,0,p1,COLS-1);
	  bkgdset(COLORMAP(0));
	  attrset(COLORMAP(0));

    if ((int)strlen(p1) < COLS-2) { addch(' '); addnstr(m_lineinput_str,COLS-2-strlen(p1)); }

    clrtoeol();
  }
  else
  {
    move(curs_ypos,curs_xpos);
  }

}




void sigfunc(int sig)
{
  printf("Got Ctrl+C\n");
  g_done++;
}


void usage(int noexit=0)
{

  printf("Usage: NINJAM hostname [options]\n"
    "Options:\n"
    "  -user <username> (use anonymous:name for anonymous login)\n"
    "  -pass <password> (omit if anonymous)\n"
#ifdef _WIN32
    "  -noaudiocfg\n"
    "  -jesusonic <path to jesusonic root dir>\n"
#else
#ifdef __APPLE__
    "  -audiostr device_name[,output_device_name]\n"
#else
    "  -jack (to use JACK)\n"
    "  -alsaconfig \"option value [option value ...]\"\n"
    "     ALSA audio options are:\n"
    "       in hw:0,0    -- set input device\n"
    "       out hw:0,0   -- set output device\n"
    "       srate 48000  -- set samplerate\n"
    "       nch 2        -- set channels\n"
    "       bps 16       -- set bits/sample\n"
    "       bsize 2048   -- set blocksize (bytes)\n"
    "       nblock 16    -- set number of blocks\n"
#endif
#endif

    "  -sessiondir <path>   -- sets the session directory (default: auto)\n"
    "  -savelocalwavs       -- save full quality copies of recorded files\n"
    "  -nosavesourcefiles   -- don't save source files for remixing\n"

    "  -writewav            -- writes a .wav of the jam in the session directory\n"
    "  -writeogg <bitrate>  -- writes a .ogg of the jam (bitrate 64-256)..\n");

  if (!noexit) exit(1);
}

int licensecallback(void *userData, const char *licensetext)
{
  /* todo, curses shit */

  int isscrolled=0;
  int linepos=0;
  int needref=1;
  int retval=0;
  while (!retval)
  {
    if (needref)
    {
	    bkgdset(COLORMAP(0));
	    attrset(COLORMAP(0));

      erase();
      const char *tp=licensetext;
      needref=0;
	    bkgdset(COLORMAP(6));
	    attrset(COLORMAP(6));
      mvaddnstr(0,0,"You must agree to this license by scrolling down",COLS-1); clrtoeol();
      mvaddnstr(1,0,"and hitting Y to connect to this server:",COLS-1); clrtoeol();
	    bkgdset(COLORMAP(0));
	    attrset(COLORMAP(0));

      int x;
      for (x = 0; x < linepos; x ++)
      {
        int yp=0;
        while (*tp && *tp != '\n' && x < linepos) 
        {
          if (yp++ >= COLS-1)
          {
            x++;
            yp=0;
          }
          tp++;
        }
        if (*tp) tp++;
      }
      for (x = 2; x < LINES-1 && *tp; x ++)
      {
        move(x,0);
        int yp=0;
        while (*tp && *tp != '\n' && x < LINES-1) 
        {
          if (yp++ >= COLS-1)
          {
            x++;
            yp=0;
            move(x,0);
          }
          addch(*tp);
          tp++;
        }
        if (*tp) tp++;
      }
	    bkgdset(COLORMAP(5));
	    attrset(COLORMAP(5));

      if (*tp)
      {
        mvaddstr(LINES-1,0,"Use the arrows or pagedown to scroll down");
        clrtoeol();
        isscrolled=0;
      }
      else
      {
        mvaddstr(LINES-1,0,"Hit Y to agree");
        clrtoeol();
        isscrolled=1;
      }
	    bkgdset(COLORMAP(0));
	    attrset(COLORMAP(0));
    }
    
    int a=getch();
    switch (a)
    {
      case KEY_UP:
      case KEY_PPAGE:
        needref=1;
        linepos -= a == KEY_UP ? 1 : 10;
        if (linepos <0) linepos=0;
      break;
      case KEY_DOWN:
      case KEY_NPAGE:
        if (!isscrolled) linepos += a == KEY_DOWN ? 1 : 10;
        needref=1;
      break;
      case 'y':
      case 'Y':
        if (isscrolled) retval=1;
      break;
      case 27:
        retval=-1;
      break;
    };


#ifdef _WIN32
      MSG msg;
      while (PeekMessage(&msg,NULL,0,0,PM_REMOVE))
      {
        TranslateMessage(&msg);
        DispatchMessage(&msg);
      }
      Sleep(1);
#else
	    struct timespec ts={0,1000*1000};
	    nanosleep(&ts,NULL);
#endif
    
  }

  showmainview();
  return retval>0;
}

int main(int argc, char **argv)
{
  char *parmuser=NULL;
  char *parmpass=NULL;
  WDL_String sessiondir;
  int sessionspec=0;
  int nolog=0,nowav=1,writeogg=0,g_nssf=0;

  printf("NINJAM v0.01a ALPHA curses client, compiled " __DATE__ " at " __TIME__ "\nCopyright (C) 2004-2005 Cockos, Inc.\n\n");
  const char *audioconfigstr=NULL;
  g_client=new NJClient;
  g_client->config_savelocalaudio=1;
  g_client->LicenseAgreementCallback=licensecallback;
  g_client->ChatMessage_Callback=chatmsg_cb;

  char *hostname;

#if 1//def __APPLE__
  char hostbuf[512];
  if (argc < 2)
  {
    usage(1);
    printf("(no command line options specified, using interactive mode!)\n\n\nHost to connect to: ");
    fgets(hostbuf,sizeof(hostbuf),stdin);
    if (hostbuf[0] && hostbuf[strlen(hostbuf)-1] == '\n') hostbuf[strlen(hostbuf)-1]=0;
    hostname=hostbuf;
    if (!hostbuf[0]) return 0;
  }
#else
  if (argc < 2) usage();
#endif
  else hostname=argv[1];


  

  {
    int p;
    for (p = 2; p < argc; p++)
    {
      if (!stricmp(argv[p],"-savelocalwavs"))
      {
        g_client->config_savelocalaudio=2;     
      }
      else if (!stricmp(argv[p],"-nosavelocal"))
      {
        g_client->config_savelocalaudio=0;
      }
      else if (!stricmp(argv[p],"-debuglevel"))
      {
        if (++p >= argc) usage();
        g_client->config_debug_level=atoi(argv[p]);
      }
      else if (!stricmp(argv[p],"-noaudiocfg"))
      {
        audioconfigstr="";
      }
      else if (!stricmp(argv[p],"-jack")) audioconfigstr="JACK";
      else if (!stricmp(argv[p],"-alsaconfig"))
      {
        if (++p >= argc) usage();
        audioconfigstr=argv[p];
      }
      else if (!stricmp(argv[p],"-user"))
      {
        if (++p >= argc) usage();
        parmuser=argv[p];
      }
      else if (!stricmp(argv[p],"-pass"))
      {
        if (++p >= argc) usage();
        parmpass=argv[p];
      }
      else if (!stricmp(argv[p],"-writewav"))
      {
        nowav=0;
      }
      else if (!stricmp(argv[p],"-writeogg"))
      {
        if (++p >= argc) usage();
        writeogg=atoi(argv[p]);
      }
      else if (!stricmp(argv[p],"-nowritelog"))
      {
        nolog++;
      }
      else if (!stricmp(argv[p],"-nosavesourcefiles"))
      {
        g_nssf++;
      }
#ifdef _WIN32
      else if (!stricmp(argv[p],"-jesusonic"))
      {
        if (++p >= argc) usage();
        jesusdir.Set(argv[p]);
      }
#endif
      else if (!stricmp(argv[p],"-sessiondir"))
      {
        if (++p >= argc) usage();
        sessiondir.Set(argv[p]);
        sessionspec=1;
      }
      else usage();
    }
  }

  if (g_nssf)
  {
    g_client->config_savelocalaudio=0;
    nolog++;
  }

  char passbuf[512]="";
  char userbuf[512]="";
  if (!parmuser)
  {
    parmuser=userbuf;
    printf("Enter username: ");
    fgets(userbuf,sizeof(userbuf),stdin);
    if (userbuf[0] && userbuf[strlen(userbuf)-1] == '\n') userbuf[strlen(userbuf)-1]=0;
    if (!userbuf[0]) return 0;
  }
  if (!parmpass)
  {
    parmpass=passbuf;
    if (strncmp(parmuser,"anonymous",9) || (parmuser[9] && parmuser[9] != ':'))
    {
      printf("Enter password: ");
      fgets(passbuf,sizeof(passbuf),stdin);
      if (passbuf[0] && passbuf[strlen(passbuf)-1] == '\n') passbuf[strlen(passbuf)-1]=0;
    }
  }

#ifdef _WIN32
  g_audio=CreateConfiguredStreamer("ninjam.ini", !audioconfigstr, NULL);

#else
  {
    const char *dev_name_in=audioconfigstr;
#ifdef __APPLE__
    g_audio=create_audioStreamer_CoreAudio(&dev_name_in,48000,2,16,audiostream_onsamples);
#else
    if (dev_name_in && !strcmp(dev_name_in,"JACK"))
	g_audio=create_audioStreamer_JACK(dev_name_in,audiostream_onsamples);
    else g_audio=create_audioStreamer_ALSA(dev_name_in,audiostream_onsamples);
#endif
  }
#endif
  if (!g_audio)
  {
    printf("Error opening audio!\n");
    return 0;
  }
  printf("Opened at %dHz %d->%dch %dbps\n",
    g_audio->m_srate, g_audio->m_innch, g_audio->m_outnch, g_audio->m_bps);

  signal(SIGINT,sigfunc);

  JNL::open_socketlib();


  // jesusonic init

#ifdef _WIN32
  HINSTANCE jesus_hDllInst;
  WDL_String jesusonic_configfile;
  if (jesusdir.Get()[0])
  {
    jesusonic_configfile.Set(jesusdir.Get());
    jesusonic_configfile.Append("\\cmdclient.jesusonicpreset");
    WDL_String dll;
    dll.Set(jesusdir.Get());
    dll.Append("\\jesus.dll");

    jesus_hDllInst = LoadLibrary(".\\jesus.dll"); // load from current dir
    if (!jesus_hDllInst) jesus_hDllInst = LoadLibrary(dll.Get());
    if (jesus_hDllInst) 
    {
      *(void **)(&JesusonicAPI) = (void *)GetProcAddress(jesus_hDllInst,"JesusonicAPI");
      if (JesusonicAPI && JesusonicAPI->ver == JESUSONIC_API_VERSION_CURRENT)
      {
      }
      else JesusonicAPI = 0;
    }
  }

#endif
  // end jesusonic init

  {
    FILE *fp=fopen("ninjam.config","rt");
    int x=0;
    if (fp) 
    {
      bool comment_state=false;
      while (!feof(fp))
      {
        char buf[4096];
        buf[0]=0;
        fgets(buf,sizeof(buf),fp);
        if (!buf[0]) continue;
        if (buf[strlen(buf)-1] == '\n')
          buf[strlen(buf)-1]=0;
        if (!buf[0]) continue;

        LineParser lp(comment_state);

        lp.parse(buf);

        switch (lp.gettoken_enum(0,"local\0master\0"))
        {
          case 0:
            // process local line
            if (lp.getnumtokens()>2)
            {
              int ch=lp.gettoken_int(1);
              int n;
              for (n = 2; n < lp.getnumtokens()-1; n += 2)
              {
                switch (lp.gettoken_enum(n,"source\0bc\0mute\0solo\0volume\0pan\0jesus\0name\0"))
                {
                  case 0: // source 
                    g_client->SetLocalChannelInfo(ch,NULL,true,lp.gettoken_int(n+1),false,0,false,false);
                  break;
                  case 1: //broadcast
                    g_client->SetLocalChannelInfo(ch,NULL,false,false,false,0,true,!!lp.gettoken_int(n+1));
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
#ifdef _WIN32
                      void *p=CreateJesusInstance(ch,"",g_audio->m_srate);
                      if (p) g_client->SetLocalChannelProcessor(ch,jesusonic_processor,p);
#endif
                    }
                  break;
                  case 7: //name
                    g_client->SetLocalChannelInfo(ch,lp.gettoken_str(n+1),false,false,false,0,false,false);
                  break;
                  default:
                  break;
                }
              }
            }

          break;
          case 1:
            if (lp.getnumtokens()>2)
            {
              int n;
              for (n = 1; n < lp.getnumtokens()-1; n += 2)
              {
                switch (lp.gettoken_enum(n,"mastervol\0masterpan\0metrovol\0metropan\0mastermute\0metromute\0"))
                {
                  case 0: // mastervol
                    g_client->config_mastervolume = (float)lp.gettoken_float(n+1);
                  break;
                  case 1: // masterpan
                    g_client->config_masterpan = (float)lp.gettoken_float(n+1);
                  break;
                  case 2:
                    g_client->config_metronome = (float)lp.gettoken_float(n+1);
                  break;
                  case 3:
                    g_client->config_metronome_pan = (float)lp.gettoken_float(n+1);
                  break;
                  case 4:
                    g_client->config_mastermute = !!lp.gettoken_int(n+1);
                  break;
                  case 5:
                    g_client->config_metronome_mute = !!lp.gettoken_int(n+1);
                  break;
                  default:
                  break;
                }
              }
            }
          break;
          default:
          break;
        }       


      }
      fclose(fp);
    }    
    else // set up defaults
    {
      g_client->SetLocalChannelInfo(0,"channel0",true,0,false,0,true,true);
      g_client->SetLocalChannelMonitoring(0,false,0.0f,false,0.0f,false,false,false,false);
    }
  } 

  if (!sessiondir.Get()[0])
  {
    char buf[512];
    
    int cnt=0;
    while (cnt < 16)
    {
#if 0 // _WIN32
      SYSTEMTIME st;
      GetLocalTime(&st);
      wsprintf(buf,"%04d%02d%02d_%02d%02d",st.wYear,st.wMonth,st.wDay,st.wHour,st.wMinute);
#else
      time_t tv;
      time(&tv);
      struct tm *t=localtime(&tv);
      sprintf(buf,"%04d%02d%02d_%02d%02d",t->tm_year+1900,t->tm_mon+1,t->tm_mday,t->tm_hour,t->tm_min);
#endif
      if (cnt)
        wsprintf(buf+strlen(buf),"_%d",cnt);
      strcat(buf,".ninjam");

#ifdef _WIN32
      if (CreateDirectory(buf,NULL)) break;
#else
      if (!mkdir(buf,0700)) break;
#endif

      cnt++;
    }
    
    if (cnt >= 16)
    {
      printf("Error creating session directory\n");
      buf[0]=0;
      return 0;
    }
      
    sessiondir.Set(buf);
  }
  else
#ifdef _WIN32
    CreateDirectory(sessiondir.Get(),NULL);
#else
    mkdir(sessiondir.Get(),0700);
#endif
  if (sessiondir.Get()[0] && sessiondir.Get()[strlen(sessiondir.Get())-1]!='\\' && sessiondir.Get()[strlen(sessiondir.Get())-1]!='/')
#ifdef _WIN32
    sessiondir.Append("\\");
#else
    sessiondir.Append("/");
#endif

  g_client->SetWorkDir(sessiondir.Get());


  if (!nowav)
  {
    WDL_String wf;
    wf.Set(sessiondir.Get());
    wf.Append("output.wav");
    g_client->waveWrite = new WaveWriter(wf.Get(),24,g_audio->m_outnch>1?2:1,g_audio->m_srate);
  }
  if (writeogg)
  {
    WDL_String wf;
    wf.Set(sessiondir.Get());
    wf.Append("output.ogg");
    g_client->SetOggOutFile(fopen(wf.Get(),"ab"),g_audio->m_srate,g_audio->m_outnch>1?2:1,writeogg);
  }
  if (!nolog)
  {
    WDL_String lf;
    lf.Set(sessiondir.Get());
    lf.Append("clipsort.log");
    g_client->SetLogFile(lf.Get());
  }
 
  printf("Connecting to %s...\n",hostname);
  g_client->Connect(hostname,parmuser,parmpass);
  g_audio_enable=1;



	// go into leet curses mode now
#ifdef _WIN32
	initscr(0);
#else
	initscr();
#endif
	cbreak();
	noecho();
	nonl();
	intrflush(stdscr,FALSE);
	keypad(stdscr,TRUE);
	nodelay(stdscr,TRUE);
	raw(); // disable ctrl+C etc. no way to kill if allow quit isn't defined, yay.

#ifndef _WIN32
	ESCDELAY=0; // dont wait--at least on the console this seems to work.
#endif

	if (has_colors()) // we don't use color yet, but we could
	{
		start_color();
		init_pair(1, COLOR_WHITE, COLOR_BLUE); // normal status lines
		init_pair(2, COLOR_BLACK, COLOR_CYAN); // value

#ifdef COLOR_BLUE_DIM
		init_pair(3, COLOR_WHITE, COLOR_BLUE_DIM); // alternating shit for the effect view
		init_pair(4, COLOR_WHITE, COLOR_RED_DIM);
#else

#if 0 // ok this aint gonna do shit for us :(
		if (can_change_color() && init_color(COLOR_YELLOW,0,0,150) && init_color(COLOR_MAGENTA,150,0,0))
		{
			init_pair(3, COLOR_WHITE, COLOR_YELLOW); // alternating shit for the effect view
			init_pair(4, COLOR_WHITE, COLOR_MAGENTA);
		}
		else
#endif


#ifdef VGA_CONSOLE
		char *term=getenv("TERM");
		if (term && !strcmp(term,"linux") && !ioperm(0x3C8,2,1))
		{
			init_pair(3, COLOR_WHITE, COLOR_YELLOW); // alternating shit for the effect view
			init_pair(4, COLOR_WHITE, COLOR_MAGENTA);
			set_pal(6,0,0,15);
			set_pal(5,15,0,0);
		}
		else
#endif
		{
			init_pair(3, COLOR_WHITE, COLOR_BLUE); // alternating shit for the effect view
			init_pair(4, COLOR_WHITE, COLOR_RED);
		}
#endif
		init_pair(5, COLOR_BLACK, COLOR_WHITE);
		init_pair(6, COLOR_WHITE, COLOR_RED);
		int x;
		for (x = 1; x < 8; x ++)
			color_map[x]=COLOR_PAIR(x);

	}
#ifndef _WIN32
	else
	{
//		color_map[1]=A_BOLD;
		color_map[2]=A_STANDOUT;
		color_map[5]=A_STANDOUT;
	}
#endif

  showmainview();
	refresh();

#ifdef _WIN32
  DWORD nextupd=GetTickCount()+250;
#else
  time_t nextupd=time(NULL)+1;
#endif

  while (g_client->GetStatus() >= 0 && !g_done
#ifdef _WIN32
  && IsWindow(CURSES_INSTANCE->cursesCtx.m_hwnd)
#endif
    
    )
  {
    if (g_client->Run()) 
    {
#ifdef _WIN32
      MSG msg;
      while (PeekMessage(&msg,NULL,0,0,PM_REMOVE))
      {
        TranslateMessage(&msg);
        DispatchMessage(&msg);
      }
      Sleep(1);
#else
	struct timespec ts={0,1000*1000};
	nanosleep(&ts,NULL);
#endif

      int a=getch();
#ifdef __APPLE__
		{
			static timeval last_t;
			static int stage;
			timeval now;
			gettimeofday(&now,NULL);
			if (a != ERR || (stage && 
				  ((long long) (((now.tv_sec-last_t.tv_sec) * 1000) + ((now.tv_usec-last_t.tv_usec)/1000)))>333

				))
			{
				last_t = now;
				if (!stage && a == 27) { a=ERR; stage++; }
				else if (stage==1 && a == 79) { a=ERR; stage++; }
				else if (stage==2 && a >= 80 && a <= 91)
				{
					a = KEY_F(a-79);
					stage=0;
				} 
				else if (stage) { a = 27; stage=0; }
			}
		}
		if (a == 127) a = KEY_BACKSPACE;
		if (a == KEY_F(7)) a = KEY_F(11);
		if (a == KEY_F(8)) a = KEY_F(12);
#endif
      if (a!=ERR)
      {
        if (!g_ui_state) switch (a)
        {
          case KEY_LEFT:
            if (g_sel_x > 0)
            {
              g_sel_x--;
              showmainview();
            }
          break;
          case KEY_RIGHT:
            {
              g_sel_x++;
              showmainview();
            }
          break;
          case KEY_UP:
            showmainview(false,-1);
          break;
          case KEY_DOWN:
            showmainview(false,1);
          break;
          case '\r': case ' ':
            if (!g_ui_inchat)
            {
              showmainview(true);
              break;
            }
          default:
            if (g_ui_inchat)
            {
              switch (a)
              {
                case KEY_PPAGE:
                  g_chat_scroll+=LINES/4-2;
                  showmainview();
                break;
                case KEY_NPAGE:
                  g_chat_scroll-=LINES/4-2;
                  if (g_chat_scroll<0)g_chat_scroll=0;
                  showmainview();
                break;
                case '\r':
                  if (m_chatinput_str[0])
                  {
                    if (m_chatinput_str[0] == '/')
                    {
                      if (!strncasecmp(m_chatinput_str,"/me ",4))
                      {
                        g_client->ChatMessage_Send("MSG",m_chatinput_str);
                      }
                      else if (!strncasecmp(m_chatinput_str,"/topic ",7)||
                               !strncasecmp(m_chatinput_str,"/kick ",6) ||                        
                               !strncasecmp(m_chatinput_str,"/bpm ",5) ||
                               !strncasecmp(m_chatinput_str,"/bpi ",5)
                        ) // alias to /admin *
                      {
                        g_client->ChatMessage_Send("ADMIN",m_chatinput_str+1);
                      }
                      else if (!strncasecmp(m_chatinput_str,"/admin ",7))
                      {
                        char *p=m_chatinput_str+7;
                        while (*p == ' ') p++;
                        g_client->ChatMessage_Send("ADMIN",p);
                      }
                      else if (!strncasecmp(m_chatinput_str,"/msg ",5))
                      {
                        char *p=m_chatinput_str+5;
                        while (*p == ' ') p++;
                        char *n=p;
                        while (*p && *p != ' ') p++;
                        if (*p == ' ') *p++=0;
                        while (*p == ' ') p++;
                        if (*p)
                        {
                          g_client->ChatMessage_Send("PRIVMSG",n,p);
                          WDL_String tmp;
                          tmp.Set("-> *");
                          tmp.Append(n);
                          tmp.Append("* ");
                          tmp.Append(p);
                          addChatLine(NULL,tmp.Get());
                        }
                        else
                        {
                          addChatLine("","error: /msg requires a username and a message.");
                        }
                      }
                      else
                      {
                        addChatLine("","error: unknown command.");
                      }
                    }
                    else
                    {
                      g_client->ChatMessage_Send("MSG",m_chatinput_str);
                    }


                    m_chatinput_str[0]=0;                    
                    showmainview();
                  }
                break;
                case 27:
                  {
                    m_chatinput_str[0]=0;
                    showmainview();
                  }
                break;
					      case KEY_BACKSPACE: 
                  if (m_chatinput_str[0]) m_chatinput_str[strlen(m_chatinput_str)-1]=0; 
                  showmainview();
					      break;
                default:
                  if (VALIDATE_TEXT_CHAR(a))
						      { 
							      int l=strlen(m_chatinput_str); 
							      if (l < (int)sizeof(m_chatinput_str)-1) { m_chatinput_str[l]=a; m_chatinput_str[l+1]=0; }
                    showmainview();
						      } 
                break;
              }
            }
          break;
        }
        else if (g_ui_state == 1)
        {
          switch (a)
          {
            case KEY_LEFT:
            case KEY_RIGHT:
              {
                float pan;
                int ok=0;
                if (g_ui_voltweakstate_channel == -2) { ok=1; pan=(float)g_client->config_masterpan; }
                else if (g_ui_voltweakstate_channel == -1) { pan=(float)g_client->config_metronome_pan; ok=1; }
                else if (g_ui_voltweakstate_channel >= 1024) 
                  ok=!!g_client->GetUserChannelState((g_ui_voltweakstate_channel-1024)/64,g_ui_voltweakstate_channel%64, NULL,NULL,&pan,NULL);
                else ok=!g_client->GetLocalChannelMonitoring(g_ui_voltweakstate_channel,NULL,&pan,NULL,NULL);

                if (ok)
                {
                  pan += a == KEY_LEFT ? -0.01f : 0.01f;
                  if (pan > 1.0f) pan=1.0f;
                  else if (pan < -1.0f) pan=-1.0f;
                  if (g_ui_voltweakstate_channel == -2) g_client->config_masterpan=pan;
                  else if (g_ui_voltweakstate_channel == -1) g_client->config_metronome_pan=pan;
                  else if (g_ui_voltweakstate_channel>=1024)
                    g_client->SetUserChannelState((g_ui_voltweakstate_channel-1024)/64,g_ui_voltweakstate_channel%64, false,false,false,0.0f,true,pan,false,false,false,false);
                  else
                    g_client->SetLocalChannelMonitoring(g_ui_voltweakstate_channel,false,0.0f,true,pan,false,false,false,false);
                  showmainview();
                }
              }
            break;
            case KEY_PPAGE:
            case KEY_UP:
            case KEY_NPAGE:
            case KEY_DOWN:
              {
                float vol;
                int ok=0;
                if (g_ui_voltweakstate_channel == -2) { ok=1; vol=(float)g_client->config_mastervolume; }
                else if (g_ui_voltweakstate_channel == -1) { vol=(float)g_client->config_metronome; ok=1; }
                else if (g_ui_voltweakstate_channel >= 1024) 
                  ok=!!g_client->GetUserChannelState((g_ui_voltweakstate_channel-1024)/64,g_ui_voltweakstate_channel%64, NULL,&vol,NULL,NULL,NULL);
                else ok=!g_client->GetLocalChannelMonitoring(g_ui_voltweakstate_channel,&vol,NULL,NULL,NULL);

                if (ok)
                {
                  vol=(float) VAL2DB(vol);
                  float sc=a == KEY_PPAGE || a == KEY_NPAGE? 4.0f : 0.5f;
                  if (a == KEY_DOWN || a == KEY_NPAGE) sc=-sc;
                  vol += sc;
                  if (vol > 20.0f) vol=20.0f;
                  else if (vol < -120.0f) vol=-120.0f;
                  vol=(float) DB2VAL(vol);
                  if (g_ui_voltweakstate_channel == -2) g_client->config_mastervolume=vol;
                  else if (g_ui_voltweakstate_channel == -1) g_client->config_metronome=vol;
                  else if (g_ui_voltweakstate_channel>=1024)
                    g_client->SetUserChannelState((g_ui_voltweakstate_channel-1024)/64,g_ui_voltweakstate_channel%64, false,false,true,vol,false,0.0f,false,false,false,false);
                  else
                    g_client->SetLocalChannelMonitoring(g_ui_voltweakstate_channel,true,vol,false,0.0f,false,false,false,false);
                  showmainview();
                }
              }
            break;
            case 27: case '\r':
              {
                g_ui_state=0;
                showmainview();
              }
            break;
          }
        }
        else if (g_ui_state == 3)
        {
          switch (a)
          {
            case KEY_PPAGE:
            case KEY_UP:
            case KEY_LEFT:
              {
                int ch=0;
                g_client->GetLocalChannelInfo(g_ui_locrename_ch,&ch,NULL,NULL);
                if (ch > 0) 
                {
                  ch--;
                  g_client->SetLocalChannelInfo(g_ui_locrename_ch,NULL,true,ch,false,0,false,false);
                  g_client->NotifyServerOfChannelChange();
                  showmainview();
                }
              }
            break;
            case KEY_NPAGE:
            case KEY_DOWN:
            case KEY_RIGHT:
              {
                int ch=0;
                g_client->GetLocalChannelInfo(g_ui_locrename_ch,&ch,NULL,NULL);
                if (ch < g_audio->m_innch) 
                {
                  ch++;
                  g_client->SetLocalChannelInfo(g_ui_locrename_ch,NULL,true,ch,false,0,false,false);
                  g_client->NotifyServerOfChannelChange();
                  showmainview();
                }
              }
            break;
            
            case 27: case '\r':
              {
                g_ui_state=0;
                showmainview();
              }
            break;
          }
        }
        else if (g_ui_state == 2 || g_ui_state == 4)
        {
          switch (a)
          {
            case '\r':
              if (m_lineinput_str[0])
              {
                if (g_ui_state == 4)
                {
                  g_client->SetLocalChannelInfo(g_ui_locrename_ch,m_lineinput_str,false,0,false,0,false,false);
                  g_client->NotifyServerOfChannelChange();
                }
              }
              g_ui_state=0;
              showmainview();
            break;
            case 27:
              {
                g_ui_state=0;
                showmainview();
              }
            break;
					  case KEY_BACKSPACE: 
              if (m_lineinput_str[0]) m_lineinput_str[strlen(m_lineinput_str)-1]=0; 
              showmainview();
              g_ui_state=4;
					  break;
            default:
              if (VALIDATE_TEXT_CHAR(a) && (g_ui_state != 3 || (a >= '0' && a <= '9'))) //fucko: 9 once we have > 2ch
						  { 
							  int l=strlen(m_lineinput_str); 
                if (g_ui_state == 2)
                {
                  l=0;
                  g_ui_state=4;
                }

							  if (l < (int)sizeof(m_lineinput_str)-1) { m_lineinput_str[l]=a; m_lineinput_str[l+1]=0; }
                showmainview();
						  } 
            break;
          }
        }
      }

      if (g_ui_state < 2 && (g_need_disp_update||g_client->HasUserInfoChanged()||
#ifdef _WIN32
GetTickCount()>=nextupd 
#else
time(NULL) >= nextupd
#endif

))
      {
#ifdef _WIN32
        nextupd=GetTickCount()+1000;
#else
        nextupd=time(NULL)+1;
#endif
        g_need_disp_update=0;
        showmainview();
      }
      else drawstatusbar();
    }

  }

	erase();
	refresh();

	// shut down curses
	endwin();

  switch (g_client->GetStatus())
  {
    case NJClient::NJC_STATUS_OK:
    break;
    case NJClient::NJC_STATUS_INVALIDAUTH:
      printf("ERROR: invalid login/password\n");
    break;
    case NJClient::NJC_STATUS_CANTCONNECT:
      printf("ERROR: failed connecting to host\n");
    break;
    case NJClient::NJC_STATUS_PRECONNECT:
      printf("ERROR: failed connect\n");
    break;
    case NJClient::NJC_STATUS_DISCONNECTED:
      printf("ERROR: disconnected from host\n");
    break;

    default:
      printf("exiting on status %d\n",g_client->GetStatus());
    break;
  }
  if (g_client->GetErrorStr()[0])
  {
    printf("Server gave explanation: %s\n",g_client->GetErrorStr());
  }


  printf("Shutting down\n");

  delete g_audio;


  delete g_client->waveWrite;
  g_client->waveWrite=0;


  // save local channel state
  {
    FILE *fp=fopen("ninjam.config","wt");
    int x=0;
    if (fp) 
    {
      fprintf(fp,"master mastervol %f masterpan %f metrovol %f metropan %f mastermute %d metromute %d\n",
        g_client->config_mastervolume,g_client->config_masterpan,g_client->config_metronome,g_client->config_metronome_pan,
        g_client->config_mastermute,g_client->config_metronome_mute);



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
      
        lcn=g_client->GetLocalChannelInfo(a,&sch,NULL,&bc);
        g_client->GetLocalChannelMonitoring(a,&v,&p,&m,&s);
        g_client->GetLocalChannelProcessor(a,NULL,&has_jesus);

        char *ptr=lcn;
        while (*ptr)
        {
          if (*ptr == '`') *ptr='\'';
          ptr++;
        }
        fprintf(fp,"local %d source %d bc %d mute %d solo %d volume %f pan %f jesus %d name `%s`\n",a,sch,bc,m,s,v,p,!!has_jesus,lcn);
      }
      fclose(fp);
    }    
  }


  // delete all effects processors in g_client
  {
    int x=0;
    for (x = 0;;x++)
    {
      int a=g_client->EnumLocalChannels(x);
      if (a<0) break;
#ifdef _WIN32
      void *i=0;
      g_client->GetLocalChannelProcessor(a,NULL,&i);
      if (i) deleteJesusonicProc(i,a);
      g_client->SetLocalChannelProcessor(a,NULL,NULL);
#endif
    }
  }


  delete g_client;


#ifdef _WIN32
  ///// jesusonic stuff
  if (jesus_hDllInst) FreeLibrary(jesus_hDllInst);
  jesus_hDllInst=0;
  JesusonicAPI=0;

#endif

  if (g_nssf)
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
#ifdef _WIN32
      RemoveDirectory(s.Get());
#else
      rmdir(s.Get());
#endif
    }
  }
  if (!sessionspec)
  {
#ifdef _WIN32
      RemoveDirectory(sessiondir.Get());
#else
      rmdir(sessiondir.Get());
#endif
   
  }

  JNL::close_socketlib();
  return 0;
}
