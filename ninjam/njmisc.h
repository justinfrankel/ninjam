/*
    NINJAM - njmisc.h
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

  Some utility functions common to clients.

*/

#ifndef _NJMISC_H_
#define _NJMISC_H_

// some utility functions
double DB2SLIDER(double x);
double SLIDER2DB(double y);
double VAL2DB(double x);
#define DB2VAL(x) (pow(2.0,(x)/6.0))
void mkvolpanstr(char *str, double vol, double pan);
void mkvolstr(char *str, double vol);
void mkpanstr(char *str, double pan);

#ifdef _WIN32

#include "../WDL/wdlstring.h"
#include "../jesusonic/jesusonic_dll.h"

extern WDL_String jesusdir;
extern jesusonicAPI *JesusonicAPI;  

void *CreateJesusInstance(int a, const char *chdesc, int srate);
void JesusUpdateInfo(void *myInst, const char *chdesc, int srate);
void deleteJesusonicProc(void *i, int chi);
void jesusonic_processor(float *buf, int len, void *inst);


#endif

#endif