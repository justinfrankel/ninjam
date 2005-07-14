#ifndef _RACKWND_H
#define _RACKWND_H

//#include <wnd/wnds/listwnd.h>
#include <wnd/scbkgwnd.h>

class RackSlot;	// in rackslot.h
class PopupMenu;

#define RACKWND_PARENT ScrlBkgWnd
class RackWnd : public RACKWND_PARENT {
public:
  RackWnd();
  virtual ~RackWnd();

  virtual int onInit();
  virtual int onResize();

  virtual int onContextMenu(int x, int y);

  void addRackSlot(RackSlot *rs);
  void killRackSlot(RackSlot *rs, int immediate=FALSE);

//override
  virtual void rackwndAppendToPopup(PopupMenu *pop, int ofs) { }
  virtual void rackwndOnPopupCommand(int cmd) { }

  int getNumPanels(int type=-1);
  PtrList<RackSlot> getPanelList(int type=-1);

  virtual int wantHScroll() { return 0; }
  virtual int wantVScroll() { return 1; }

  virtual int getContentsHeight();	// not safe to call getclientrect!

protected:
  int yscroll;
  virtual void onScrollY(int y);
  virtual void timerclient_timerCallback(int id);
  virtual int timerclient_onDeferredCallback(int param1, int param2);

  PtrListQuickSorted<RackSlot,RackSlot> slots;
};

#endif
