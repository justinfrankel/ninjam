#include <windows.h>


#include "rng.h"
#include "sha.h"

unsigned char state[32];

void RNG_addentropy(void *buf, int buflen)
{
  SHA1 tmp;
  tmp.add(state,sizeof(state));
  tmp.result(state);
  tmp.reset();
  tmp.add((unsigned char *)buf,buflen);
  tmp.result(state+sizeof(state) - SHA1SIZE);
}

static void rngcycle()
{
  int i;
  for (i = 0; i < sizeof(state) && state[i]++; i++);
}

int RNG_int32()
{
  SHA1 tmp;
  tmp.add(state,sizeof(state));
  rngcycle();
  char buf[SHA1SIZE];
  tmp.result(buf);
  return *(int *)buf;

}


void RNG_bytes(void *buf, int buflen)
{
  char *b=(char *)buf;
  while (buflen > 0)
  {
    char tb[SHA1SIZE];
    SHA1 tmp;
    tmp.add(state,sizeof(state));
    rngcycle();

    tmp.result(tb);
    int l=min(buflen,SHA1SIZE);
    memcpy(b,tb,l);
    buflen-=l;
    b+=l;
  }
}

