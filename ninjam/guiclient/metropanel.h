#ifndef _METROPANEL_H
#define _METROPANEL_H

#include "../njclient.h"

#include "basicpanel.h"

#include "resource.h"

#define METRONOMEPANEL_PARENT BasicPanel
class MetronomePanel : public METRONOMEPANEL_PARENT {
public:
  MetronomePanel(RackWnd *wnd)
    : BasicPanel(wnd, IDD_PANEL_METRONOME, "Metronome")
  {
    sortval = 1;
  }

  virtual FOURCC getRackSlotType() { return RS_TYPE_METRONOME; }

  virtual void onPan(float pos) {
    extern NJClient *g_client;
    if (g_client == NULL) return;
    g_client->config_metronome_pan = pos;
  }
  virtual void onVol(float vol) {
    extern NJClient *g_client;
    if (g_client == NULL) return;
    g_client->config_metronome = vol;
  }
  virtual void onMute(bool mut) {
    extern NJClient *g_client;
ASSERT(g_client != NULL);
    if (g_client == NULL) return;
    g_client->config_metronome_mute = mut;
  }
};

#endif
