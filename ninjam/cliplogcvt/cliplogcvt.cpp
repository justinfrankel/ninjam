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


int WriteRec(FILE *fp, char *name, int id, int trackid, int position, int len, char *path)
{
  char *p=name;
  while (*p && *p == '0') p++;
  if (!*p) return 0; // empty name

  char *exts[]={".wav",".ogg"};
  WDL_String fnfind;
  int x;
  for (x = 0; x < sizeof(exts)/sizeof(exts[0]); x ++)
  {
    fnfind.Set(path);
    fnfind.Append("\\");
    fnfind.Append(name);
    fnfind.Append(exts[x]);

    FILE *tmp=fopen(fnfind.Get(),"rb");
    if (tmp) 
    {
      fclose(tmp);
      break;
    }
  }
  if (x == sizeof(exts)/sizeof(exts[0]))
  {
    printf("Error resolving guid %s\n",name);
    return 0;
  }

  fprintf(fp,"%d;\t" "%d;\t" "%f;\t" "%f;\t",id,trackid,(double)position,(double)len);
    
  fprintf(fp,"1.000000;\tFALSE;\tFALSE;\t0;\tFALSE;\tFALSE;\tAUDIO;\t");

  fprintf(fp,"\"%s.ogg\";\t",name);

  fprintf(fp,"0;\t" "0.0000;\t" "%f;\t" "0.0000;\t" "0.0000;\t" "1.000000;\t0;\t0.000000;\t-2;\t0.000000;\t0;\t-1;\t-2;\t2\n",
    (double)len);

  return 1;
}

int main(int argc, char **argv)
{
  printf("ClipLogCvt v0.0 -- converts Ninjam log file to Vegas 4 EDL text file\n");
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

    if (!strnicmp(buf,"interval: ",10))
    {
      int idx=0;
      double bpm=0.0;
      int bpi=0;
      char *p=buf+10;
      idx=atoi(p);
      while (*p && *p != ' ') p++;
      while (*p == ' ') p++;
      bpm=atof(p);
      while (*p && *p != ' ') p++;
      while (*p == ' ') p++;
      bpi=atoi(p);

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


  int id=1;
  int track_id=0;
  int x;
  int len=(int) ((double)m_cur_bpi * 60000.0 / m_cur_bpm);
  for (x= 0; x < sizeof(localrecs)/sizeof(localrecs[0]); x ++)
  {
    int y;

    for (y = 0; y < localrecs[x].items.GetSize(); y ++)
    {
      int pos=(localrecs[x].items.Get(y)->position-1) * len;
      if (WriteRec(outfile, localrecs[x].items.Get(y)->guidstr.Get(), id, track_id, pos, len,argv[1])) id++;
    }
    if (y)  track_id++;

  }
  for (x= 0; x < curintrecs.GetSize(); x ++)
  {
    int y;

    for (y = 0; y < curintrecs.Get(x)->items.GetSize(); y ++)
    {
      int pos=(curintrecs.Get(x)->items.Get(y)->position-1) * len;
      if (WriteRec(outfile, curintrecs.Get(x)->items.Get(y)->guidstr.Get(), id, track_id, pos, len,argv[1])) id++;
    }
    if (y)  track_id++;

  }
  printf("wrote %d records, %d tracks\n",id-1,track_id);




  fclose(outfile);


  return 0;

}