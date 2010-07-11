#include <stdio.h>
#include <stdlib.h>
#include <time.h>


#include "../../WDL/sha.h"
#include "../../WDL/wdltypes.h"
#include "../../WDL/filewrite.h"

static const unsigned char hist_magic1[10] = "Star" "tofc" "h";
static const unsigned char hist_magic2[16] = "WDLh" "isto" "mati" "c69";

static void WriteLE(WDL_FileWrite *fr, WDL_UINT64 val, int sz)
{
  sz*=8;
  int x;
  for(x=0;x<sz;x+=8)
  {
    unsigned char c=(unsigned char)((val>>x)&0xff);
    fr->Write(&c,1);
  }
}

bool HistoryFile_Add(const char *filenameout, FILE *fnin, time_t timestamp, const char *username, WDL_UINT64 revno)
{
  WDL_FileWrite fr(filenameout,1,65536,4,4,true,true);
  if (!fr.IsOpen()) return false;

  int sz = 0;
  if (fnin)
  {
    fseek(fnin,0,SEEK_END);
    sz=ftell(fnin);
    fseek(fnin,0,SEEK_SET);
  }

  WDL_SHA1 sha;
  fr.Write(hist_magic1,sizeof(hist_magic1));
  WriteLE(&fr,sz,4);
  WriteLE(&fr,128,2);
  unsigned int bytes_out = 16;
  if (fnin)
  {
    char buf[4096];
    while (sz>0)
    {
      int rdamt = sz;
      if (rdamt > sizeof(buf)) rdamt=sizeof(buf);
      int c=fread(buf,1,rdamt,fnin);
      if (c<rdamt) memset(buf+c,0,rdamt-c);
      fr.Write(buf,rdamt);
      sha.add(buf,rdamt);
      sz-=rdamt;
      bytes_out += rdamt;
    }
  }
  fr.Write(hist_magic2,sizeof(hist_magic2));
  WriteLE(&fr,bytes_out,4);
  WriteLE(&fr,bytes_out-16,4);
  unsigned char shabuf[WDL_SHA1SIZE];
  sha.result(shabuf);
  fr.Write(shabuf,sizeof(shabuf));
  WriteLE(&fr,timestamp,4);
  char userstr[32];
  lstrcpyn(userstr,username,sizeof(userstr));
  fr.Write(userstr,sizeof(userstr));
  WriteLE(&fr,revno,8);

  char tmp[40]={0,}; 
  fr.Write(&tmp,sizeof(tmp));

  return true;
}
