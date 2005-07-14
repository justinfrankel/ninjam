#ifndef _JESUS_H
#define _JESUS_H

#include "../../jesusonic/jesusonic_dll.h"

extern jesusonicAPI *JesusonicAPI;  
extern HINSTANCE hDllInst;
extern String jesusdir;

int jesusAvailable();

void jesusInit();
void jesusShutdown();

void attachInstToLocChan(int channel_id);	//FUCKO rename to smth sane
void adjustSampleRate(int ch);

void jesusonic_processor(float *buf, int len, void *inst);
void deleteJesusonicProc(void *i, int chi);

#endif
