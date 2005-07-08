#ifndef _PANELSLOT_H
#define _PANELSLOT_H

#include <wnd/wnds/os/osdialog.h>

#include "rackslot.h"

class RackWnd;

// a RackSlot that holds a dialog box panel

class PanelSlot : public RackSlot, public OSDialog {
public:
  PanelSlot(RackWnd *wnd, int dlgid);
  ~PanelSlot();

  virtual int getHeight();

  virtual void resizeSlot(RECT *r);

  virtual int getSortVal() { return sortval; }

//  virtual void onPostApplyDlgToAttr(Attribute *attr, const char *newval, int dlgid) { }

protected:
  RackWnd *panel_parent;

  int sortval;

private:
  int panelheight;
};

#endif
