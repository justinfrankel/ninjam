
#ifndef _CURSESCLIENTINST_H_
#define _CURSESCLIENTINST_H_



#include "curses.h"


class ninjamCursesClientInstance
{
public:

  void Run();
  win32CursesCtx cursesCtx;

};

#endif