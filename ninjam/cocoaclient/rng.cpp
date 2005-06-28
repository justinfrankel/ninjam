#ifdef _WIN32
#include <windows.h>
#else
#include <stdlib.h>
#include <memory.h>
#include <string.h>
#ifndef min
#define min(x,y) ((x)<(y)?(x):(y))
#endif
#endif


#include "rng.h"
#include "sha.h"

static unsigned char state[32];

void WDL_RNG_addentropy(void *buf, int buflen)
{
  WDL_SHA1 tmp;
  tmp.add(state,sizeof(state));
  tmp.result(state);
  tmp.reset();
  tmp.add((unsigned char *)buf,buflen);
  tmp.result(state+sizeof(state) - WDL_SHA1SIZE);
}

static void rngcycle()
{
  int i;
  for (i = 0; i < (int)sizeof(state) && state[i]++; i++);
}

int WDL_RNG_int32()
{
  WDL_SHA1 tmp;
  tmp.add(state,sizeof(state));
  rngcycle();
  char buf[WDL_SHA1SIZE];
  tmp.result(buf);
  return *(int *)buf;

}


void WDL_RNG_bytes(void *buf, int buflen)
{
  char *b=(char *)buf;
  while (buflen > 0)
  {
    char tb[WDL_SHA1SIZE];
    WDL_SHA1 tmp;
    tmp.add(state,sizeof(state));
    rngcycle();

    tmp.result(tb);
    int l=min(buflen,WDL_SHA1SIZE);
    memcpy(b,tb,l);
    buflen-=l;
    b+=l;
  }
}

