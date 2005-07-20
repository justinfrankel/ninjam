/*
    NINJAM ClipLogCvt - cliplogcvt.cpp
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

  This command line tool parses a NINJAM session .log file (clipsort.log), 
  and writes out a EDL text file that Vegas 4.0+ (and maybe others).can read.
  It can optionally do things like concatenate OGGs, too.

  If only some open portable software like Audacity would support multiple 
  files at different positions in a track, or at LEAST support concatenated
  OGGs, then we'd make it write out a nice project for it. Sigh. Feel free...


  
  */

#ifdef _WIN32
#include <windows.h>
#endif

#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <math.h>


#ifndef _WIN32
#include <sys/types.h>
#include <sys/stat.h>
  #ifndef stricmp
    #define stricmp strcasecmp
  #endif
#endif

#include "../../WDL/string.h"
#include "../../WDL/ptrlist.h"
#include "../../WDL/lineparse.h"
#include "../../WDL/vorbisencdec.h"
#include "../../WDL/wavwrite.h"

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

#ifdef _WIN32

const char *realpath(const char *path, char *resolved_path) 
{
  char *p;
  if (GetFullPathName(path,1024,resolved_path,&p))
    return resolved_path;
  return NULL;
}

#define DIRCHAR '\\'
#define DIRCHAR_S "\\"


#else

#define DIRCHAR '/'
#define DIRCHAR_S "/"

#endif



int g_ogg_concatmode=0;
int g_write_wavs=0;
int g_write_wav_bits=16;
int g_maxsilence=0;

int resolveFile(char *name, WDL_String *outpath, char *path)
{
  char *p=name;
  while (*p && *p == '0') p++;
  if (!*p) return 0; // empty name

  char *exts[]={".wav",".ogg"};
  WDL_String fnfind;
  int x;
  for (x = !!g_ogg_concatmode; x < (int)(sizeof(exts)/sizeof(exts[0])); x ++)
  {
    fnfind.Set(path);
    fnfind.Append(DIRCHAR_S);
    fnfind.Append(name);
    fnfind.Append(exts[x]);

    FILE *tmp=fopen(fnfind.Get(),"rb");
    if (!tmp)
    {
      fnfind.Set(path);
      fnfind.Append(DIRCHAR_S);
      // try adding guid subdir
      char t[3]={name[0],DIRCHAR,0};
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
        if (realpath(fnfind.Get(),buf))
        {
          outpath->Set(buf);
        }       
        return 1;
      }
    }
  }
  printf("Error resolving guid %s\n",name);
  return 0;

}

FILE *g_outfile_edl, *g_outfile_lof;

void WriteRec(char *name, int id, int trackid, double position, double len)
{
  if (g_outfile_edl)
  {
               //ID    Track    StartT  Length
    fprintf(g_outfile_edl,"%d;\t" "%d;\t" "%f;\t" "%f;\t",id,trackid,position,len);

    //          PlayRate   Locked  Normali SM Looped OnRuler  MediaType
    fprintf(g_outfile_edl,"1.000000;\tFALSE;\tFALSE;\t0;\tFALSE;\tFALSE;\tAUDIO;\t");

    //FileName
    fprintf(g_outfile_edl,"\"%s\";\t",name);

    //         Stream  StreamStart Len    FadeTimeIn   FadeTimeOut SustainGn  CurveInGainIn  CvO  GainOut  LayerColorCurveInRCurveOutR
    fprintf(g_outfile_edl,"0;\t" "0.0000;\t" "%f;\t" "0.0000;\t" "0.0000;\t" "1.000000;\t0;\t0.000000;\t0;\t0.000000;\t0;\t-1;\t0;\t0\n",
      (double)len);
  }
  if (g_outfile_lof)
  {
    fprintf(g_outfile_lof,"file \"%s\" offset %f\n",name,position/1000.0);
  }
}

void usage()
{
   printf("Usage: \n"
          "  cliplogcvt session_directory [options]\n"
          "\n"
          "Options:\n"
          "  -skip <intervals>\n"
          "  -maxlen <intervals>\n"
          "  -concat\n"
          "  -decode\n"
          "  -decodebits 16|24\n"
          "  -insertsilence maxseconds   -- valid only with -concat -decode\n"

      );
  exit(1);
}


WDL_String g_concatdir;

void WriteOutTrack(char *chname, UserChannelList *list, int *track_id, int *id, char *path)
{
  int y;
  FILE *concatout=NULL;
  WaveWriter *concatout_wav=NULL;
  double last_pos=-1000.0, last_len=0.0;
  WDL_String concat_fn;
  int concat_filen=0;

  const double DELTA=0.0000001;
  for (y = 0; y < list->items.GetSize(); y ++)
  {
    WDL_String op;
    if (!resolveFile(list->items.Get(y)->guidstr.Get(),&op,path)) 
    {
      if (concatout || concatout_wav) 
      {
        WriteRec(concat_fn.Get(), *id, *track_id, last_pos, last_len);
        (*id)++;
        if (concatout) fclose(concatout);
        delete concatout_wav;
      }
      concatout_wav=0;
      concatout=0;
      continue;
    }

    if (g_ogg_concatmode)
    {
      if ((concatout || concatout_wav) && fabs(last_pos+last_len - list->items.Get(y)->position) > DELTA)
      {
        if (concatout_wav && concatout_wav->Status() && list->items.Get(y)->position >= last_pos+last_len && list->items.Get(y)->position <= last_pos+last_len + g_maxsilence*1000.0)
        {
          double silen=list->items.Get(y)->position - (last_pos+last_len);
          last_len += silen;

          // fill in silence
          float buf[4096]={0,};
          int nsamples=(int) ((silen * concatout_wav->get_srate() * concatout_wav->get_nch())/1000.0);
          while (nsamples > 0)
          {
            int l=nsamples;
            if (l > 4096)l=4096;
            concatout_wav->WriteFloats(buf,l);
            nsamples-=l;
          }

        }
        else
        {
          WriteRec(concat_fn.Get(), *id, *track_id, last_pos, last_len);
          (*id)++;

          if (concatout) fclose(concatout);
          delete concatout_wav;
          concatout_wav=0;
          concatout=0;
        }
      }

      if (!concatout && !concatout_wav)
      {
        concat_fn.Set(g_concatdir.Get());
        char buf[4096];
        sprintf(buf,DIRCHAR_S "%s_%03d.%s",chname,concat_filen++,g_write_wavs?"wav":"ogg");
        concat_fn.Append(buf);

        if (realpath(concat_fn.Get(),buf))
        {
          concat_fn.Set(buf);
        }

        if (g_write_wavs)
        {

          concatout_wav = new WaveWriter;
        }
        else
        {

          concatout = fopen(concat_fn.Get(),"wb");
          if (!concatout)
          {
            printf("Warning: error opening %s. RESULTING TXT WILL LACK REFERENCE TO THIS FILE! ACK!\n",concat_fn.Get());
          }
        }
        last_pos = list->items.Get(y)->position;
        last_len = 0.0;
      }

      if (concatout || concatout_wav)
      {
        FILE *fp = fopen(op.Get(),"rb");
        if (fp)
        {
          if (concatout_wav)
          {
            // decode file
            VorbisDecoder decoder;
            int did_write=0;

            for (;;)
            {
              int l=fread(decoder.DecodeGetSrcBuffer(1024),1,1024,fp);
              decoder.DecodeWrote(l);

              if (decoder.m_samples_used>0)
              {

                if (concatout_wav->Status() && (decoder.GetNumChannels() != concatout_wav->get_nch() || decoder.GetSampleRate() != concatout_wav->get_srate()))
                {
                  // output parameter change
  //                printf("foo\n");

                  WriteRec(concat_fn.Get(), *id, *track_id, last_pos, last_len);
                  (*id)++;
                  delete concatout_wav;

                  concat_fn.Set(g_concatdir.Get());
                  char buf[4096];
                  sprintf(buf,DIRCHAR_S "%s_%03d.wav",chname,concat_filen++);
                  concat_fn.Append(buf);

                  if (realpath(concat_fn.Get(),buf))
                  {
                    concat_fn.Set(buf);
                  }

                  concatout_wav = new WaveWriter;
                  last_pos = list->items.Get(y)->position;
                  last_len = 0.0;

                }

                if (!concatout_wav->Status() && decoder.GetNumChannels() && decoder.GetSampleRate())
                {
//                  printf("opening new wav\n");
                  if (!concatout_wav->Open(concat_fn.Get(),g_write_wav_bits, decoder.GetNumChannels(), decoder.GetSampleRate(),0))
                  {
                    printf("Warning: error opening %s to write WAV\n",concat_fn.Get());
                    break;
                  }
                }

                if (concatout_wav->Status())
                {
                  concatout_wav->WriteFloats((float *)decoder.m_samples.Get(),decoder.m_samples_used);
                }
                did_write += decoder.m_samples_used;

                decoder.m_samples_used=0;
              }

              if (!l) break;
            }

            last_len += did_write * 1000.0 / (double)concatout_wav->get_srate() / (double)concatout_wav->get_nch();
            if (!did_write)
            {
              printf("Warning: error decoding %s to convert to WAV\n",op.Get());
            }

          }
          else
          {
            char buf[4096];
            for (;;)
            {
              int a=fread(buf,1,sizeof(buf),fp);
              if (!a) break;
              fwrite(buf,1,a,concatout);
            }
            last_len += list->items.Get(y)->length;
          }
          fclose(fp);
        }

      }
    }
    else
    {

      char *fn=op.Get();
      int fn_l=strlen(fn);
      
      if (g_write_wavs && fn_l > 3 && !stricmp(fn+fn_l-4,".ogg"))
      {
        // decode OGG file to WAV, set the output file name to that
        FILE *fp=fopen(fn,"rb");
        if (fp)
        {
          WDL_String newfn(fn);
          strcpy(newfn.Get()+fn_l-4,".wav");


          VorbisDecoder decoder;
          WaveWriter *wr=NULL;

          for (;;)
          {
            int l=fread(decoder.DecodeGetSrcBuffer(1024),1,1024,fp);
            decoder.DecodeWrote(l);

            if (decoder.m_samples_used>0)
            {
              if (!wr && decoder.GetNumChannels() && decoder.GetSampleRate())
              {
                wr = new WaveWriter(newfn.Get(), g_write_wav_bits, decoder.GetNumChannels(), decoder.GetSampleRate(),0);
              }

              if (wr)
              {
                wr->WriteFloats((float *)decoder.m_samples.Get(),decoder.m_samples_used);
              }

              decoder.m_samples_used=0;
            }

            if (!l) break;
          }

          if (wr)
          {
            if (wr->Status())
            {
              op.Set(newfn.Get());
            }
            else
            {
              printf("Warning: error opening %s to write WAV\n",newfn.Get());
            }
            delete wr;
          }
          else
          {
            printf("Warning: error decoding %s to convert to WAV\n",op.Get());
          }

          fclose(fp);
        }
        else
        {
          printf("Warning: error opening %s to convert to WAV\n",op.Get());
        }
      }

      WriteRec(op.Get(), *id, *track_id, 
                   list->items.Get(y)->position, list->items.Get(y)->length);
      (*id)++;
    }
  }
  if (concatout || concatout_wav)
  {
    WriteRec(concat_fn.Get(), *id, *track_id, last_pos, last_len);
    (*id)++;
    if (concatout) fclose(concatout);
    delete concatout_wav;

    concatout=0;
    concatout_wav=0;
  }
  if (y) (*track_id)++;
}

int main(int argc, char **argv)
{
  printf("ClipLogCvt v0.02 - Copyright (C) 2005, Cockos, Inc.\n"
         "(Converts NINJAM log file to Vegas 4 compatible EDL text file)\n\n");
  if (argc <  2 || argv[1][0] == '-')
  {
    usage();
  }
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
    else if (!stricmp(argv[p],"-concat"))
    {
      g_ogg_concatmode=1;
    }    
    else if (!stricmp(argv[p],"-decodebits"))
    {
      if (++p >= argc) usage();
      g_write_wav_bits=atoi(argv[p]);
      if (g_write_wav_bits != 24 && g_write_wav_bits != 16) usage();
    }   
    else if (!stricmp(argv[p],"-decode"))
    {
      g_write_wavs=1;
    }   
    else if (!stricmp(argv[p],"-insertsilence"))
    {
      if (++p >= argc) usage();
      g_maxsilence=atoi(argv[p]);
    }       
    else usage();
  }
  end_interval += start_interval;

  WDL_String logfn(argv[1]);
  logfn.Append(DIRCHAR_S "clipsort.log");
  FILE *logfile=fopen(logfn.Get(),"rt");
  if (!logfile)
  {
    printf("Error opening logfile\n");
    return -1;
  }

  if (g_ogg_concatmode)
  {
    g_concatdir.Set(argv[1]);
    g_concatdir.Append(DIRCHAR_S "concat");
#ifdef _WIN32
    CreateDirectory(g_concatdir.Get(),NULL);
#else
    mkdir(g_concatdir.Get(),0755);
#endif
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
//                printf("BPI/BPM changed from %d/%.2f to %d/%.2f\n",m_cur_bpi,m_cur_bpm,bpi,bpm);
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
//              char *channelname=lp.gettoken_str(4);

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
              if (curintrecs.Get(x)->items.GetSize())
              {
                UserChannelValueRec *lastitem=curintrecs.Get(x)->items.Get(curintrecs.Get(x)->items.GetSize()-1); // this is for when the server sometimes groups them in the wrong interval
                double last_end=lastitem->position + lastitem->length;
                if (ucvr->position < last_end)
                {
                  ucvr->position = last_end;
                }
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
  outfn.Append(DIRCHAR_S "clipsort.txt");
  g_outfile_edl=fopen(outfn.Get(),"wt");
  if (!g_outfile_edl)
  {
    printf("Error opening EDL outfile\n");
  }
  outfn.Set(argv[1]);
  outfn.Append(DIRCHAR_S "clipsort.lof");
  g_outfile_lof=fopen(outfn.Get(),"wt");
  if (!g_outfile_lof)
  {
    printf("Error opening LOF outfile\n");
  }

  if (g_outfile_edl)
  {
    fprintf(g_outfile_edl,"%s", 
      "\"ID\";\"Track\";\"StartTime\";\"Length\";\"PlayRate\";\"Locked\";\"Normalized\";\"StretchMethod\";\"Looped\";\"OnRuler\";\"MediaType\";\"FileName\";\"Stream\";\"StreamStart\";\"StreamLength\";\"FadeTimeIn\";\"FadeTimeOut\";\"SustainGain\";\"CurveIn\";\"GainIn\";\"CurveOut\";\"GainOut\";\"Layer\";\"Color\";\"CurveInR\";\"CurveOutR\"\n");
  }
  if (g_outfile_lof)
  {
    fprintf(g_outfile_lof,"window\n");
  }

  if (!g_outfile_edl && !g_outfile_lof)
  {
    printf("Was unable to open any outputs\n");
    return  0;
  }

  int id=1;
  int track_id=0;
  int x;
  for (x= 0; x < (int)(sizeof(localrecs)/sizeof(localrecs[0])); x ++)
  {
    char chname[512];
    sprintf(chname,"local_%02d",x);
    WriteOutTrack(chname,localrecs+x, &track_id, &id, argv[1]);

  }
  for (x= 0; x < curintrecs.GetSize(); x ++)
  {
    char chname[4096];
    sprintf(chname,"%s_%02d",curintrecs.Get(x)->user.Get(),x);
    char *p=chname;
    while (*p)
    {
      if (*p == '/'||*p == '\\' || *p == '?' || *p == '*' || *p == ':' || *p == '\'' || *p == '\"' || *p == '|' || *p == '<' || *p == '>') *p='_';
      p++;
    }

    WriteOutTrack(chname, curintrecs.Get(x), &track_id, &id, argv[1]);
  }
  printf("wrote %d records, %d tracks\n",id-1,track_id);




  if (g_outfile_edl) fclose(g_outfile_edl);
  if (g_outfile_lof) fclose(g_outfile_lof);


  return 0;

}
