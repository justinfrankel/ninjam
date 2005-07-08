#ifndef _SWEEPPANEL_H
#define _SWEEPPANEL_H

#include "../njclient.h"

#include "panelslot.h"

#include "resource.h"

class SweepPanel : public PanelSlot {
public:
  enum { TIMER_REFRESH=1 };
  SweepPanel(RackWnd *wnd) : PanelSlot(wnd, IDD_PANEL_SWEEP) {
    registerAttribute(sweep_pos, IDC_SWEEP);
    addSizeBinding(IDC_SWEEP, OSDialogSizeBind::RIGHTEDGETORIGHT);
    addSizeBinding(IDC_TICKMARK, OSDialogSizeBind::RIGHTEDGETORIGHT);
  }

  virtual FOURCC getRackSlotType() { return RS_TYPE_SWEEP; }

  virtual void onRefresh() {
    extern NJClient *g_client;
    if (g_client == NULL) return;
    if (g_client->GetStatus() != NJClient::NJC_STATUS_OK) {
      sweep_pos = 0;
      return;//only if connected
    }
    int pos, len;
    g_client->GetPosition(&pos, &len);
    if (len <= 0) return;
    sweep_pos = (int)floor(((float)pos / (float)len) * 100.f);
  }

  _int sweep_pos;
};

#endif
