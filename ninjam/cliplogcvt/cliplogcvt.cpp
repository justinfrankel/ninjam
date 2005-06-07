#include <windows.h>
#include <stdio.h>
#include "../../WDL/string.h"
#include "../../WDL/ptrlist.h"

class UserChannelValueRec
{
  WDL_String user;
  int chidx;
  WDL_String guid;
  
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
      
      
    }


  }


  return 0;

}