#include <windows.h>
#include <stdio.h>
#include "../../WDL/string.h"
#include "../../WDL/ptrlist.h"

class UserChannelValueRec
{
public:
  int isloc;
  WDL_String user;
  int chidx;
  WDL_String guidstr;
  
};

int main(int argc, char **argv)
{
  if (argc != 2 && argc != 3)
  {
    printf("Usage: clipoutcvt session_directory [output_directory]\n");
    return 1;
  }
  WDL_String logfn(argv[1]);
  logfn.Append("\\clipsort.log");
  FILE *logfile=fopen(logfn.Get(),"rt");
  if (!logfile)
  {
    printf("Error opening logfile\n");
    return -1;
  }
  WDL_String outpath(argv[argc>2?2:1]);
  CreateDirectory(outpath.Get(),NULL);

  int m_cur_idx=0;
  double m_cur_bpm=-1.0;
  int m_cur_bpi=-1;

  WDL_PtrList<UserChannelValueRec> curintrecs;

  // go through the log file
  for (;;)
  {
    char buf[4096];
    buf[0]=0;
    fgets(buf,sizeof(buf),logfile);
    if (!buf[0]) break;
    if (buf[strlen(buf)-1]=='\n') buf[strlen(buf)-1]=0;
    if (!buf[0]) continue;
    int finish_interval=0;

    if (!strnicmp(buf,"interval:",9))
    {
      int idx=0;
      double bpm=0.0;
      int bpi=0;
      if (sscanf(buf,"interval: %d %fbpm %dbpi",&idx,&bpm,&bpi) != 3)
      {
        printf("Error parsing interval line\n");
        return -2;
      }
      if (idx != m_cur_idx+1)
      {
        printf("Error: interval %d out of sync, expected %d\n",idx,m_cur_idx);
        return -2;
      }
      if ((m_cur_bpi >= 0 && m_cur_bpi != bpi) ||
          (m_cur_bpm >= 0 && m_cur_bpm != bpm))
      {
        printf("BPI/BPM changed from %d/%.2f to %d/%.2f\n",m_cur_bpi,m_cur_bpm,bpi,bpm);
        return -2;
      }

      m_cur_bpi=bpi;
      m_cur_bpm=bpm;
      m_cur_idx=idx;
      finish_interval=1;
    }
    else if (!strnicmp(buf,"end",3))
      finish_interval=1;
    else if (!strnicmp(buf,"local:",6))
    {
      UserChannelValueRec *p=new UserChannelValueRec;
      p->isloc=1;
      p->chidx=0;
      char guidtmp[64];
      if (sscanf(buf,"local:%32s:%d",guidtmp,&p->chidx) < 1)
      {
        printf("Error parsing local line\n");
        return -2;
      }
      p->guidstr.Set(guidtmp);
      //printf("Got local guid %s\n",guidtmp);
      curintrecs.Add(p);     
    }
    else if (!strnicmp(buf,"user :",6))
    {
      char guidtmp[64];
      char username[64],channelname[64];
      int chidx;
      if (sscanf(buf,"user :%32s",guidtmp) != 1)
      {
        printf("Error parsing user guid\n");
        return -2;
      }
      char *op=username;
      char *p=buf+6+32;
      while (*p && *p != '\'') p++;
      if (*p)
      {
        p++;
        while (*p && *p != '\'') 
        {
          *op++=*p++;
        }
        if (*p)
        {
          *op=0;
          p++;
          if (*p != '/') *p=0;
          else
          {
            chidx=atoi(++p);
            if (*p)
            {
              op=channelname;
              while (*p && *p != '\'') p++;
              if (*p)
              {
                p++;
                while (*p && *p != '\'') 
                {
                  *op++=*p++;
                }
                if (*p)
                {
                  *op=0;
                }
              }
            }
          }
        }
      }
      if (!*p)
      {
        printf("Error parsing username/channel/etc\n");
        return -2;
      }
      printf("Got user '%s' channel %d '%s' guid %s\n",username,chidx,channelname,guidtmp);

      UserChannelValueRec *ucvr=new UserChannelValueRec;
      ucvr->isloc=0;
      ucvr->user.Set(username);
      ucvr->chidx=chidx;
      ucvr->guidstr.Set(guidtmp);
      curintrecs.Add(ucvr);     
    }


    if (finish_interval && curintrecs.GetSize())
    {
      int x;
      for (x = 0; x < curintrecs.GetSize(); x ++)
      {
        delete curintrecs.Get(x);
      }
      
      curintrecs.Empty();
    }

  }


  return 0;

}