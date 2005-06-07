
#ifndef _CURSESCLIENTINST_H_
#define _CURSESCLIENTINST_H_



#include "curses.h"


class ninjamCursesClientInstance
{
public:
  ninjamCursesClientInstance() { memset(&cursesCtx,0,sizeof(cursesCtx)); }

  void Run();
  win32CursesCtx cursesCtx;

};

#endif