#ifndef _SESSION_H
#define _SESSION_H

#include <bfc/string/string.h>

String makeSessionDirName(int count=0, time_t t=0);

class Session {
public:
  static int newSession();
  static void endSession();
  static void killIntervalsDir(const char *dir);
};

#endif
