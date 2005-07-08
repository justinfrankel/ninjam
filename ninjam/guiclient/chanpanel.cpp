#include "precomp.h"

#include <wnd/wnds/os/osdialog.h>
#include "../njclient.h"

#include "resource.h"

#include "chanpanel.h"

//CUT enum {
//CUT   TIMER_VU
//CUT };

extern NJClient *g_client;

int getUserIdFromName(const char *user_name) {
  int ret = -1;
  for (int i = 0; i < g_client->GetNumUsers(); i++) {
    if (STREQLSAFE(user_name, g_client->GetUserState(i))) return i;
  }
  return ret;
}

ChannelPanel::ChannelPanel(RackWnd *wnd, const char *username, int id)
  : BasicPanel(wnd, IDD_PANEL_CHANNEL, StringPrintf("remote user settings/%s/channel %02d", username, id)),
  subscribe(StringPrintf("remote user settings/%s/channel %02d/subscribe", username, id))
{
  addSizeBinding(IDC_CHANNEL_NAME, OSDialogSizeBind::RIGHTEDGETORIGHT);

  user_name = username;
  registerAttribute(&user_name, IDC_CHANNEL_USER);

  channel_id = id;
  channel_id_str = StringPrintf("%02d", channel_id+1);
  registerAttribute(&channel_id_str, IDC_CHANNEL_ID);

  channel_name = "honko";
  registerAttribute(&channel_name, IDC_CHANNEL_NAME);

  registerAttribute(&subscribe, IDC_SUBSCRIBE);

//CUT   registerAttribute(&vumeter, IDC_VUMETER);
//CUT   addSizeBinding(IDC_VUMETER, OSDialogSizeBind::RIGHTEDGETORIGHT);
//CUT   vumeter_avg = 0;

  registerBoolDisable(subscribe, IDC_CHANNEL_USER, FALSE);
  registerBoolDisable(subscribe, IDC_CHANNEL_ID, FALSE);
  registerBoolDisable(subscribe, IDC_CHANNEL_NAME, FALSE);

  registerBoolDisable(subscribe, IDC_PAN, FALSE);
  registerBoolDisable(subscribe, IDC_PAN_TEXT, FALSE);
  registerBoolDisable(subscribe, IDC_VOLUME, FALSE);
  registerBoolDisable(subscribe, IDC_VOLUME_TEXT, FALSE);
  registerBoolDisable(subscribe, IDC_MUTE, FALSE);
  registerBoolDisable(subscribe, IDC_SOLO, FALSE);
  registerBoolDisable(subscribe, IDC_VUMETER, FALSE);

  refreshInfo();

//CUT   timerclient_setTimer(TIMER_VU, 50);
}

void ChannelPanel::refreshInfo() {
  if (g_client == NULL) return;

  int userid = getUserIdFromName(user_name);

  bool sub, _mute, _solo;
  float vol, _pan;
  String nm = g_client->GetUserChannelState(userid, channel_id, &sub, &vol, &_pan, &_mute, &_solo);
  channel_name = nm;	// set channel name
  subscribe = sub;	// and subscription
  mute = _mute;
  solo = _solo;
//CUT DebugString("CHAN %s sub %d mute %d", nm.v(), sub, _mute);
//  volume = vol;		// volume
//  pan = _pan;
}

int ChannelPanel::dead() {
  return (getUserIdFromName(user_name) < 0);
}

void ChannelPanel::onPostApplyDlgToAttr(Attribute *attr, const char *newval, int dlgid) {
  if (g_client == NULL) return;

  int userid = getUserIdFromName(user_name);

  if (attr == &subscribe) {
    g_client->SetUserChannelState(userid, channel_id, TRUE, !!ATOI(newval), FALSE, 0, FALSE, 0, FALSE, 0, FALSE, 0);
  } else {
    CHANNELPANEL_PARENT::onPostApplyDlgToAttr(attr, newval, dlgid);
  }
}

void ChannelPanel::onPan(float pos) {
  if (g_client == NULL) return;
  int userid = getUserIdFromName(user_name);
  g_client->SetUserChannelState(userid, channel_id, FALSE, 0, FALSE, 0, TRUE, pos, FALSE, 0, FALSE, 0);
}

void ChannelPanel::onVol(float vol) {
  if (g_client == NULL) return;
  int userid = getUserIdFromName(user_name);
  g_client->SetUserChannelState(userid, channel_id, FALSE, 0, TRUE, vol, FALSE, 0, FALSE, 0, FALSE, 0);
}

void ChannelPanel::onMute(bool mut) {
  if (g_client == NULL) return;
  int userid = getUserIdFromName(user_name);
  g_client->SetUserChannelState(userid, channel_id, FALSE, 0, FALSE, 0, FALSE, 0, TRUE, mut, FALSE, 0);
}

void ChannelPanel::onSolo(bool sol) {
  if (g_client == NULL) return;
  int userid = getUserIdFromName(user_name);
  g_client->SetUserChannelState(userid, channel_id, FALSE, 0, FALSE, 0, FALSE, 0, FALSE, 0, TRUE, sol);
}

float ChannelPanel::getVU() {
  if (g_client->GetStatus() != NJClient::NJC_STATUS_OK) return -1;
  int userid = getUserIdFromName(user_name);
  return g_client->GetUserChannelPeak(userid, channel_id);
}

void ChannelPanel::basicpanelOnApplyPrevAttrs() {
  onPostApplyDlgToAttr(&subscribe, StringPrintf((int)subscribe), IDC_SUBSCRIBE);
}

int ChannelPanel::getSortVal() {
  int userid = getUserIdFromName(user_name);
  return userid*100+channel_id;
}
