#ifndef _RNG_H_
#define _RNG_H_


void RNG_addentropy(void *buf, int buflen);

int RNG_int32();
void RNG_bytes(void *buf, int buflen);


#endif// _RNG_H_