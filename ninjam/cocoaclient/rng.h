#ifndef _WDL_RNG_H_
#define _WDL_RNG_H_


void WDL_RNG_addentropy(void *buf, int buflen);

int WDL_RNG_int32();
void WDL_RNG_bytes(void *buf, int buflen);


#endif

