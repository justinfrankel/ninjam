#ifndef _SEPPANEL_H
#define _SEPPANEL_H

#include "panelslot.h"

#include "rackwnd.h"

#include "resource.h"

//FUCKO this should be one class with parameters in the constructors

class LocalSepPanel : public PanelSlot {
public:
  LocalSepPanel(RackWnd *wnd) : PanelSlot(wnd, IDD_PANEL_LOCAL_SEPARATOR) {
    addSizeBinding(IDC_SEPTEXT, OSDialogSizeBind::RIGHTEDGETORIGHT);
  }

  virtual FOURCC getRackSlotType() { return RS_TYPE_LOCAL_SEPARATOR; }

  virtual void onDlgContextMenu() {
    getRackWnd()->onContextMenu(0,0);
  }
};

class ChanSepPanel : public PanelSlot {
public:
  ChanSepPanel(RackWnd *wnd) : PanelSlot(wnd, IDD_PANEL_CHAN_SEPARATOR) {
    addSizeBinding(IDC_SEPTEXT, OSDialogSizeBind::RIGHTEDGETORIGHT);
  }

  virtual FOURCC getRackSlotType() { return RS_TYPE_CHAN_SEPARATOR; }

  virtual void onDlgContextMenu() {
    getRackWnd()->onContextMenu(0,0);
  }
};

#endif
