#include "precomp.h"

#include "rackslot.h"

#include <wnd/popup.h>

#include "rackwnd.h"

//CUT? #include "jesus.h"

enum {
  TIMER_KILLSLOT,
  TIMER_RESIZE,
  TIMER_REFRESH,
};

RackWnd::RackWnd() {
  slots.setAutoSort(FALSE);
}

RackWnd::~RackWnd() {
  slots.deleteAll();
}

int RackWnd::onInit() {
  int r = RACKWND_PARENT::onInit();

  timerclient_setTimer(TIMER_REFRESH, 50);

  return r;
}

int RackWnd::onResize() {
  RACKWND_PARENT::onResize();

  if (slots.isempty()) return 0;

  slots.sort();

  RECT rc = clientRect();

// allocate positions for each panel
#define BORDER 1
  int y = rc.top + BORDER;
  foreach(slots)
    int type = slots.getfor()->getRackSlotType();
    int h = slots.getfor()->getHeight();
    if (type == RS_TYPE_SWEEP) {	// sweep on the bottom
      RECT r = rc;
      r.top = r.bottom - h;
      slots.getfor()->resizeSlot(&r);
      rc.bottom -= h + BORDER;
    } else {
      RECT r = rc;
      r.top = y;
      r.bottom = r.top + h;
      slots.getfor()->resizeSlot(&r);
      y += h + BORDER;
   }
  endfor

  return 1;
}

int RackWnd::onContextMenu(int x, int y) {
  PopupMenu pop;
  rackwndAppendToPopup(&pop, 0);
  int r = pop.popAtMouse();
  rackwndOnPopupCommand(r);
  return 1;
}

void RackWnd::addRackSlot(RackSlot *rs) {
  slots.addItem(rs);
  addItem(NULL);	// keep listwnd synced
  if (isPostOnInit()) onResize();
}

void RackWnd::killRackSlot(RackSlot *rs, int immediate) {
  if (!immediate)
    timerclient_postDeferredCallback(TIMER_KILLSLOT, reinterpret_cast<int>(rs));
  else
    timerclient_onDeferredCallback(TIMER_KILLSLOT, reinterpret_cast<int>(rs));
}

int RackWnd::getNumPanels(int type) {
  PtrList<RackSlot> r = getPanelList(type);
  return r.n();
}

PtrList<RackSlot> RackWnd::getPanelList(int type) {
  PtrList<RackSlot> ret;
  foreach(slots)
    RackSlot *rs = slots.getfor();
    if (type == -1 || rs->getRackSlotType() == type) ret.addItem(rs);
  endfor
  return ret;
}

void RackWnd::timerclient_timerCallback(int id) {
  switch (id) {
    case TIMER_REFRESH:
      foreach(slots)
        slots.getfor()->onRefresh();
      endfor
    break;
  }
}

int RackWnd::timerclient_onDeferredCallback(int param1, int param2) {
  switch (param1) {
    case TIMER_RESIZE: {
      onResize();
    }
    break;
    case TIMER_KILLSLOT: {
      RackSlot *rs = reinterpret_cast<RackSlot *>(param2);
      slots.removeItem(rs);
      delete rs;
      timerclient_postDeferredCallback(TIMER_RESIZE);
    }
    break;
    default: return 0;
  }
  return 1;
}
