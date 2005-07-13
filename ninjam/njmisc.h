#ifndef _NJMISC_H_
#define _NJMISC_H_

// some utility functions
double DB2SLIDER(double x);
double SLIDER2DB(double y);
double VAL2DB(double x);
#define DB2VAL(x) (pow(2.0,(x)/6.0))
void mkvolpanstr(char *str, double vol, double pan);

#ifdef _WIN32

#include "../../WDL/string.h"
#include "../../jesusonic/jesusonic_dll.h"

extern WDL_String jesusdir;
extern jesusonicAPI *JesusonicAPI;  

void *CreateJesusInstance(int a, char *chdesc, int srate);
void JesusUpdateInfo(void *myInst, char *chdesc, int srate);
void deleteJesusonicProc(void *i, int chi);
void jesusonic_processor(float *buf, int len, void *inst);


#endif

#endif