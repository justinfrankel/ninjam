#ifndef __PROJECTMODE_H_
#define __PROJECTMODE_H_

#include "../../WDL/ptrlist.h"
#include "../../WDL/wdlstring.h"

#include "../netmsg.h"
#include "../mpb.h"


class ProjectInstance
{
public:
  ProjectInstance() { m_refCnt=1; }
  ~ProjectInstance() { }

  void Retain() { m_refCnt++; }
  void Release() { if (--m_refCnt==0) delete this; }

private:
  int m_refCnt;
};


#endif