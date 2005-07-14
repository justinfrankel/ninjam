#include "precomp.h"

#include "audio.h"

#include "masterpanel.h"

extern NJClient *g_client;
int audioConfig();

MasterPanel::MasterPanel(RackWnd *wnd)
  : BasicPanel(wnd, IDD_PANEL_MASTER, "Master")
{
  registerAttribute(sessionpos, IDC_SESSION_POS);
  sortval = 0;
  refresh_count = 0;
  registerAttribute(audioenable, IDC_AUDIOENABLE);
  addSizeBinding(IDC_CONFIGUREAUDIO, OSDialogSizeBind::RIGHT);
  addSizeBinding(IDC_AUDIOENABLE, OSDialogSizeBind::RIGHT);
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

void MasterPanel::onRefresh() {
  MasterPanel_PARENT::onRefresh();

  if (refresh_count++ & 1) return;

#define int64 __int64
  int64 sp = g_client->GetSessionPosition();
//DebugString("sp is %d", sp);
  int ms = sp % 1000;
  sp /= 1000;
  int sec = sp % 60;
  sp /= 60;
  int mins = sp;
  sessionpos = StringPrintf("%01d:%02d.%03d ", mins, sec, ms);
}

void MasterPanel::onUserButton(int id) {
  switch (id) {
    case IDC_CONFIGUREAUDIO: {
      audioConfig();
    }
    break;
  }
}

void MasterPanel::onPostApplyDlgToAttr(Attribute *attr, const char *newval, int dlgid) {
  switch (dlgid) {
    case IDC_AUDIOENABLE: {
       int v = ATOI(newval);
       if (v) {
         audioStart();
          g_audio_enable=1;
       }
       else {
         audioStop();
          g_audio_enable=0;
       }
    }
    break;
    default:
      MasterPanel_PARENT::onPostApplyDlgToAttr(attr, newval, dlgid);
  }
}
