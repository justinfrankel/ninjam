#include <windows.h>
#include <stdio.h>
#include <math.h>
#include "../../WDL/string.h"
#include "../../WDL/ptrlist.h"
#include "../../WDL/lineparse.h"

class UserChannelValueRec
{
public:
  double position;
  double length;
  WDL_String guidstr;
  
};

class UserChannelList
{
  public:

   WDL_String user;
   int chidx;

   WDL_PtrList<UserChannelValueRec> items;
};


int g_ogg_concatmode=0;

int resolveFile(char *name, WDL_String *outpath, char *path)
{
  char *p=name;
  while (*p && *p == '0') p++;
  if (!*p) return 0; // empty name

  char *exts[]={".wav",".ogg"};
  WDL_String fnfind;
  int x;
  for (x = !!g_ogg_concatmode; x < sizeof(exts)/sizeof(exts[0]); x ++)
  {
    fnfind.Set(path);
    fnfind.Append("\\");
    fnfind.Append(name);
    fnfind.Append(exts[x]);

    FILE *tmp=fopen(fnfind.Get(),"rb");
    if (!tmp)
    {
      fnfind.Set(path);
      fnfind.Append("\\");
      // try adding guid subdir
      char t[3]={name[0],'\\',0};
      fnfind.Append(t);

      fnfind.Append(name);
      fnfind.Append(exts[x]);
      tmp=fopen(fnfind.Get(),"rb");
    }
    if (tmp) 
    {
      fseek(tmp,0,SEEK_END);
      int l=ftell(tmp);
      fclose(tmp);
      if (l) 
      {
        char buf[4096];
        char *p;
        if (GetFullPathName(fnfind.Get(),sizeof(buf),buf,&p))
        {
          outpath->Set(buf);
          return 1;
        }       
      }
    }
  }
  printf("Error resolving guid %s\n",name);
  return 0;

}

void WriteRec(FILE *fp, char *name, int id, int trackid, double position, double len, char *path)
{
             //ID    Track    StartT  Length
  fprintf(fp,"%d;\t" "%d;\t" "%f;\t" "%f;\t",id,trackid,position,len);

  //          PlayRate   Locked  Normali SM Looped OnRuler  MediaType
  fprintf(fp,"1.000000;\tFALSE;\tFALSE;\t0;\tFALSE;\tFALSE;\tAUDIO;\t");

  //FileName
  fprintf(fp,"\"%s\";\t",name);

  //         Stream  StreamStart Len    FadeTimeIn   FadeTimeOut SustainGn  CurveInGainIn  CvO  GainOut  LayerColorCurveInRCurveOutR
  fprintf(fp,"0;\t" "0.0000;\t" "%f;\t" "0.0000;\t" "0.0000;\t" "1.000000;\t0;\t0.000000;\t0;\t0.000000;\t0;\t-1;\t0;\t0\n",
    (double)len);
}

void usage()
{
   printf("Usage: cliplogcvt session_directory [options]\n"
           "Options:\n"
           "-skip <intervals>\n"
           "-maxlen <intervals>\n"
           "-oggconcat\n"
      );
  exit(1);
}


WDL_String g_concatdir;

void WriteOutTrack(int chidx, FILE *outfile, UserChannelList *list, int *track_id, int *id, char *path)
{
  int y;
  FILE *concatout=NULL;
  double last_pos=-1000.0, last_len=0.0;
  WDL_String concat_fn;
  int concat_filen=0;

  const double DELTA=0.0000001;
  for (y = 0; y < list->items.GetSize(); y ++)
  {
    WDL_String op;
    if (!resolveFile(list->items.Get(y)->guidstr.Get(),&op,path)) 
    {
      if (concatout) fclose(concatout);
      concatout=0;
      continue;
    }

    if (g_ogg_concatmode)
    {
      if (concatout && fabs(last_pos+last_len - list->items.Get(y)->position) > DELTA)
      {
        WriteRec(outfile,concat_fn.Get(), *id, *track_id, last_pos, last_len, path);
        (*id)++;

        fclose(concatout);
        concatout=0;
      }

      if (!concatout)
      {
        concat_fn.Set(g_concatdir.Get());
        char buf[4096];
        sprintf(buf,"\\%03d_%03d.ogg",chidx,concat_filen++);
        concat_fn.Append(buf);

        char *p;
        if (GetFullPathName(concat_fn.Get(),4096,buf,&p))
        {
          concat_fn.Set(buf);
        }

        concatout = fopen(concat_fn.Get(),"wb");
        last_pos = list->items.Get(y)->position;
        last_len = 0.0;
      }

      if (concatout)
      {
        FILE *fp = fopen(op.Get(),"rb");
        if (fp)
        {
          char buf[4096];
          for (;;)
          {
            int a=fread(buf,1,sizeof(buf),fp);
            if (!a) break;
            fwrite(buf,1,a,concatout);
          }
          last_len += list->items.Get(y)->length;
          fclose(fp);
        }

      }
    }
    else
    {
      WriteRec(outfile,op.Get(), *id, *track_id, 
                   list->items.Get(y)->position, list->items.Get(y)->length, path);
      (*id)++;
    }
  }
  if (concatout)
  {
    WriteRec(outfile,concat_fn.Get(), *id, *track_id, last_pos, last_len, path);
    (*id)++;
    fclose(concatout);
  }
  if (y) (*track_id)++;
}

int main(int argc, char **argv)
{
  printf("ClipLogCvt v0.0 -- converts Ninjam log file to Vegas 4 EDL text file\n");
  printf("Copyright (C) 2005, Cockos, Inc.\n");
  if (argc <  2 || argv[1][0] == '-')
  {
    usage();
  }
  int chunk_len=0x40000000;
  int start_interval=1;
  int end_interval=0x40000000;


  int p;
  for (p = 2; p < argc; p++)
  {
    if (!stricmp(argv[p],"-skip"))
    {
      if (++p >= argc) usage();
      start_interval = atoi(argv[p])+1;
    }
    else if (!stricmp(argv[p],"-maxlen"))
    {
      if (++p >= argc) usage();
      end_interval = atoi(argv[p]);
    }
    else if (!stricmp(argv[p],"-oggconcat"))
    {
      g_ogg_concatmode=1;
    }
    else usage();
  }
  end_interval += start_interval;

  WDL_String logfn(argv[1]);
  logfn.Append("\\clipsort.log");
  FILE *logfile=fopen(logfn.Get(),"rt");
  if (!logfile)
  {
    printf("Error opening logfile\n");
    return -1;
  }

  if (g_ogg_concatmode)
  {
    g_concatdir.Set(argv[1]);
    g_concatdir.Append("\\concat");
    CreateDirectory(g_concatdir.Get(),NULL);
  }

  double m_cur_bpm=-1.0;
  int m_cur_bpi=-1;
  int m_interval=0;
  
  double m_cur_position=0.0;
  double m_cur_lenblock=0.0;

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

    LineParser lp(0);

    int res=lp.parse(buf);

    if (res)
    {
      printf("Error parsing log line!\n");
      return -1;
    }
    else
    {
      if (lp.getnumtokens()>0)
      {
        int w=lp.gettoken_enum(0,"interval\0local\0user\0end\0");
        if (w < 0)
        {
          printf("unknown token %s\n",lp.gettoken_str(0));
          return -1;
        }
        switch (w)
        {
          case 0: // interval
            {
              if (lp.getnumtokens() != 4)
              {
                printf("interval line has wrong number of tokens\n");
                return -2;
              }

              m_cur_position+=m_cur_lenblock;

              int idx=0;
              double bpm=0.0;
              int bpi=0;
              idx=lp.gettoken_int(1);
              bpm=lp.gettoken_float(2);
              bpi=lp.gettoken_int(3);

              if ((m_cur_bpi >= 0 && m_cur_bpi != bpi) ||
                  (m_cur_bpm >= 0 && m_cur_bpm != bpm))
              {
                printf("BPI/BPM changed from %d/%.2f to %d/%.2f\n",m_cur_bpi,m_cur_bpm,bpi,bpm);
              }

              m_cur_bpi=bpi;
              m_cur_bpm=bpm;

              m_cur_lenblock=((double)m_cur_bpi * 60000.0 / m_cur_bpm);
              m_interval++;

            }
          break;
          case 1: // local
            if (m_interval >= start_interval && m_interval < end_interval)
            {
              if (lp.getnumtokens() != 3)
              {
                printf("local line has wrong number of tokens\n");
                return -2;
              }
              UserChannelValueRec *p=new UserChannelValueRec;
              p->position=m_cur_position;
              p->length=m_cur_lenblock;
              p->guidstr.Set(lp.gettoken_str(1));
              localrecs[(lp.gettoken_int(2))&31].items.Add(p);
            }
          break;
          case 2: // user
            if (m_interval >= start_interval && m_interval < end_interval)
            {
              if (lp.getnumtokens() != 5)
              {
                printf("user line has wrong number of tokens\n");
                return -2;
              }

              char *guidtmp=lp.gettoken_str(1);
              char *username=lp.gettoken_str(2);
              int chidx=lp.gettoken_int(3);
              char *channelname=lp.gettoken_str(4);

              //printf("Got user '%s' channel %d '%s' guid %s\n",username,chidx,channelname,guidtmp);

              UserChannelValueRec *ucvr=new UserChannelValueRec;
              ucvr->guidstr.Set(guidtmp);
              ucvr->position=m_cur_position;
              ucvr->length=m_cur_lenblock;

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

          break;
          case 3: // end
          break;
        }



      }
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
  for (x= 0; x < sizeof(localrecs)/sizeof(localrecs[0]); x ++)
  {
    WriteOutTrack(x,outfile, localrecs+x, &track_id, &id, argv[1]);

  }
  for (x= 0; x < curintrecs.GetSize(); x ++)
  {
    WriteOutTrack(x+32,outfile, curintrecs.Get(x), &track_id, &id, argv[1]);
  }
  printf("wrote %d records, %d tracks\n",id-1,track_id);




  fclose(outfile);


  return 0;

}