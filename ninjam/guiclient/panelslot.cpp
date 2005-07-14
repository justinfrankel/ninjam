#include "precomp.h"

#include <wnd/basewnd.h>
#include <wnd/wnds/os/osdialog.h>
#include <bfc/std_wnd.h>

#include "rackwnd.h"

#include "panelslot.h"

PanelSlot::PanelSlot(RackWnd *wnd, int dlgid)
  : OSDialog(dlgid, wnd->getOsWindowHandle()),
    RackSlot(wnd)
{
  createModeless();
  RECT r = clientRect();
  panelheight = r.bottom - r. top;
  panel_parent = wnd;
  sortval = reinterpret_cast<int>(this);
}

PanelSlot::~PanelSlot() {
}

int PanelSlot::getHeight() {
  return panelheight;
}

void PanelSlot::resizeSlot(RECT *r) {
//  StdWnd::invalidateRect(getOsWindowHandle());
  resizeToRect(r);
}
