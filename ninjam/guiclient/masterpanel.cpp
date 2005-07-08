#include "precomp.h"

#include "masterpanel.h"

extern NJClient *g_client;

MasterPanel::MasterPanel(RackWnd *wnd)
  : BasicPanel(wnd, IDD_PANEL_MASTER, "Master")
{
  sortval = 0;
}

void MasterPanel::onPan(float pos) {
  if (g_client == NULL) return;
  g_client->config_masterpan = pos;
}

void MasterPanel::onVol(float vol) {
  if (g_client == NULL) return;
  g_client->config_mastervolume = vol;
}

void MasterPanel::onMute(bool mut) {
  if (g_client == NULL) return;
  g_client->config_mastermute = mut;
}

float MasterPanel::getVU() {
  if (g_client->GetStatus() != NJClient::NJC_STATUS_OK) return -1;
  return g_client->GetOutputPeak();
}
