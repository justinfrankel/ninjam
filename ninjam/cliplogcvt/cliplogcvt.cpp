#include <windows.h>
#include <stdio.h>
#include "../../WDL/string.h"
#include "../../WDL/ptrlist.h"

class UserChannelValueRec
{
public:
  int position;
  WDL_String guidstr;
  
};

class UserChannelList
{
  public:

   WDL_String user;
   int chidx;

   WDL_PtrList<UserChannelValueRec> items;
};


void WriteRec(FILE *fp, char *name, int id, int trackid, int position, int len)
{
  fprintf(fp,"%d;\t" "%d;\t" "%f;\t" "%f;\t",id,trackid,(double)position,(double)len);
    
  fprintf(fp,"1.000000;\tFALSE;\tFALSE;\t0;\tTRUE;\tFALSE;\tAUDIO;\t");

  fprintf(fp,"\"%s.ogg\";\t",name);

  fprintf(fp,"0;\t" "0.0000;\t" "%f;\t" "0.0000;\t" "0.0000;\t" "1.000000;\t2;\t0.000000;\t-2;\t0.000000;\t0;\t-1;\t-2;\t2\n",
    (double)len);
}

int main(int argc, char **argv)
{
  if (argc != 2)
  {
    printf("Usage: clipoutcvt session_directory\n");
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

  int m_cur_idx=0;
  double m_cur_bpm=-1.0;
  int m_cur_bpi=-1;

  UserChannelList localrecs[32];
  WDL_PtrList<UserChannelList> curintrecs;
  

  // go through the log file
  for (;;)
  {
    char buf[4096];
    buf[0]=0;
    fgets(buf,sizeof(buf),logfile);
    if (!buf[0]) break;
    if (buf[strlen(buf)-1]=='\n') buf[strlen(buf)-1]=0;
    if (!buf[0]) continue;

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
    }
    else if (!strnicmp(buf,"local:",6))
    {
      UserChannelValueRec *p=new UserChannelValueRec;
      int chidx=0;
      p->position=m_cur_idx;
      char guidtmp[64];
      if (sscanf(buf,"local:%32s:%d",guidtmp,&chidx) < 1)
      {
        printf("Error parsing local line\n");
        return -2;
      }
      p->guidstr.Set(guidtmp);
      //printf("Got local guid %s\n",guidtmp);
      localrecs[chidx&31].items.Add(p);
      
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
      ucvr->guidstr.Set(guidtmp);
      ucvr->position=m_cur_idx;

      int x;
      for (x = 0; x < curintrecs.GetSize(); x ++)
      {
        if (!stricmp(curintrecs.Get(x)->user.Get(),username) && curintrecs.Get(x)->chidx == chidx)
        {
          break;
        }
      }
      if (x == curintrecs.GetSize())
      {
        // add the rec
        UserChannelList *t=new UserChannelList;
        t->user.Set(username);
        t->chidx=chidx;

        curintrecs.Add(t);
      }
      curintrecs.Get(x)->items.Add(ucvr);
      // add this record to it
    }

  }
  fclose(logfile);

  printf("Done analyzing log, building output...\n");
  WDL_String outfn(argv[1]);
  outfn.Append("\\clipsort.txt");
  FILE *outfile=fopen(outfn.Get(),"wt");
  if (!outfile)
  {
    printf("Error opening outfile\n");
    return -1;
  }
  fprintf(outfile,"%s", 
    "\"ID\";\"Track\";\"StartTime\";\"Length\";\"PlayRate\";\"Locked\";\"Normalized\";\"StretchMethod\";\"Looped\";\"OnRuler\";\"MediaType\";\"FileName\";\"Stream\";\"StreamStart\";\"StreamLength\";\"FadeTimeIn\";\"FadeTimeOut\";\"SustainGain\";\"CurveIn\";\"GainIn\";\"CurveOut\";\"GainOut\";\"Layer\";\"Color\";\"CurveInR\";\"CurveOutR\"\n");


  int id=0;
  int track_id=0'
  int x;
  for (x= 0; x < sizeof(localrecs)/sizeof(localrecs[0]); x ++)
  {


  }




  fclose(outfile);


  return 0;

}