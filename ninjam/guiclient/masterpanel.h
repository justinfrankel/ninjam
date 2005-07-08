#ifndef _MASTERPANEL_H
#define _MASTERPANEL_H

//CUT #include <commctrl.h>	// for TBM_SETTIC, FUCKO move into OSDialog

#include "../njclient.h"

#include "basicpanel.h"

#include "resource.h"

#define MasterPanel_PARENT BasicPanel
class MasterPanel : public MasterPanel_PARENT {
public:
  MasterPanel(RackWnd *wnd);

  virtual FOURCC getRackSlotType() { return RS_TYPE_MASTER; }

  virtual void onPan(float pos);
  virtual void onVol(float vol);
  virtual void onMute(bool mut);
  virtual float getVU();
};

#endif
