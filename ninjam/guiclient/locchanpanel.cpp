#include "precomp.h"

#include "resource.h"

#include <wnd/popup.h>
#include <wnd/wnds/os/osdialog.h>

#include "../njclient.h"
#include "../audiostream.h"
extern audioStreamer *g_audio;

#include "jesus.h"
#include "rackwnd.h"

#include "locchanpanel.h"

#define DEFAULT_LOCCHAN_NAME "unnamed"
#define DEFAULT_BROADCASTING TRUE
#define VIRTUAL_CHANNEL_NAME "fx only"
#define MAX_UNK_CHANS 8

//#define SRCCHAN_COMBO

enum {
  CMD_REMOVEME,
  CMD_AUDIOSELECT_VIRTUAL=99,
  CMD_AUDIOSELECT=100,
};

extern NJClient *g_client;

static PopupMenu *subpop;

LocalChannelPanel::LocalChannelPanel(RackWnd *wnd, int id)
  : BasicPanel(wnd, IDD_PANEL_LOCALCHANNEL, StringPrintf("local/channel %02d", id)),
  broadcasting(StringPrintf("user settings/local/channel %02d/broadcasting", id), DEFAULT_BROADCASTING),
  channel_name(StringPrintf("user settings/local/channel %02d/name", id), DEFAULT_LOCCHAN_NAME)
//CUT  src_channel(StringPrintf("%d src_channel", id))
{
  channel_id = id;
  chid = StringPrintf("%02d", id+1).v();
  registerAttribute(&chid, IDC_LOCAL_CHID);

  sortval = 1000+id;

  addSizeBinding(IDC_CHANNEL_NAME, OSDialogSizeBind::RIGHTEDGETORIGHT);
  addSizeBinding(IDC_SRCCHAN, OSDialogSizeBind::RIGHT);
  addSizeBinding(IDC_LOCAL_CHID, OSDialogSizeBind::RIGHT);
  addSizeBinding(IDC_MONITOR, OSDialogSizeBind::RIGHT);

  registerAttribute(&channel_name, IDC_CHANNEL_NAME);

  registerAttribute(&broadcasting, IDC_BROADCAST);

//  registerBoolDisable(broadcasting, IDC_CHANNEL_NAME, FALSE);

//  registerBoolDisable(broadcasting, IDC_LOCAL_TEXT, FALSE);
//  registerBoolDisable(broadcasting, IDC_PAN, FALSE);
//  registerBoolDisable(broadcasting, IDC_PAN_TEXT, FALSE);
//  registerBoolDisable(broadcasting, IDC_VOLUME, FALSE);
//  registerBoolDisable(broadcasting, IDC_VOLUME_TEXT, FALSE);
//  registerBoolDisable(broadcasting, IDC_MUTE, FALSE);
//  registerBoolDisable(broadcasting, IDC_SOLO, FALSE);
//  registerBoolDisable(broadcasting, IDC_VUMETER, FALSE);

  registerBoolDisable(fx, IDC_EDIT_FX, FALSE);

// srcchan combo
  int tchnum=-1;
  g_client->GetLocalChannelInfo(channel_id, &tchnum, NULL, NULL);
#ifdef SRCCHAN_COMBO
  int n = MAX(MAX_UNK_CHANS, tchnum);
  for (int i = 0; i < n; i++) {
    populateCombo(IDC_SRCCHAN, StringPrintf("input #%d", i+1), i);
  }
  populateCombo(IDC_SRCCHAN, VIRTUAL_CHANNEL_NAME, -1);
  if (tchnum < 0) 
    setComboSel(IDC_SRCCHAN, n);
  else
    setComboSel(IDC_SRCCHAN, tchnum);
#else
#if 0//CUT
  registerAttribute(srcchan_text, IDC_SRCCHAN);
  if (tchnum >= 0)
    srcchan_text = StringPrintf("input #%d", tchnum);
  else
    srcchan_text = VIRTUAL_CHANNEL_NAME;
#endif
  registerAttribute(&src_channel_name, IDC_SRCCHAN);
  refreshSrcChanName();
#endif
}

#if 0
int getUserIdFromName(const char *user_name) {
  extern NJClient *g_client;
  int ret = -1;
  for (int i = 0; i < g_client->GetNumUsers(); i++) {
    if (STREQLSAFE(user_name, g_client->GetUserState(i))) return i;
  }
  return ret;
}
#endif

void LocalChannelPanel::refreshInfo() {
#if 0	// hmm
  if (g_client == NULL) return;

  int srcch, bitrate;
  String nm = g_client->GetLocalChannelInfo(channel_id, &srcch, &bitrate, NULL);

//  float vo, pa;
//  bool mu;
//  g_client->GetLocalChannelMonitoring(channel_id, &vo, &pa, *mu);

//CUT?  channel_name = nm;	// set channel name
//CUT  subscribed = sub;	// and subscription
//CUT  mute = _mute;
//  volume = vol;		// volume
//  pan = _pan;
#endif
}

void LocalChannelPanel::onPostApplyDlgToAttr(Attribute *attr, const char *newval, int dlgid) {
  LOCALCHANNELPANEL_PARENT::onPostApplyDlgToAttr(attr, newval, dlgid);

  extern NJClient *g_client;
  if (g_client == NULL) return;

  if (attr == &channel_name) {

    g_client->SetLocalChannelInfo(channel_id, channel_name.ncv(), FALSE, 0, FALSE, 0, FALSE, FALSE);
    g_client->NotifyServerOfChannelChange();

  } else if (attr == &broadcasting) {

    g_client->SetLocalChannelInfo(channel_id, channel_name.ncv(), FALSE, 0, FALSE, 0, TRUE, broadcasting);
    g_client->NotifyServerOfChannelChange();

  } else {
  }
}

float LocalChannelPanel::getVU() {
  if (g_client->GetStatus() != NJClient::NJC_STATUS_OK) return -1;
  return g_client->GetLocalChannelPeak(channel_id);
}

void LocalChannelPanel::onPan(float pos) {
  g_client->SetLocalChannelMonitoring(channel_id, FALSE, 0, TRUE, pos, FALSE, 0, FALSE, 0);
}
void LocalChannelPanel::onVol(float vol) {
  g_client->SetLocalChannelMonitoring(channel_id, TRUE, vol, FALSE, 0, FALSE, 0, FALSE, 0);
}

void LocalChannelPanel::onMute(bool mut) {
  g_client->SetLocalChannelMonitoring(channel_id, FALSE, 0, FALSE, 0, TRUE, mut, FALSE, 0);
}

void LocalChannelPanel::onSolo(bool sol) {
  g_client->SetLocalChannelMonitoring(channel_id, FALSE, 0, FALSE, 0, FALSE, 0, TRUE, sol);
}

void LocalChannelPanel::onFx(bool fx) {
  int a = channel_id;
  void *i=0;
  g_client->GetLocalChannelProcessor(channel_id,NULL,&i);
  if (fx) {
    if (!i) {
      attachInstToLocChan(a);
    }
#if 0
// have effects already
            HWND h=JesusonicAPI->ui_wnd_gethwnd(i);
            if (h && IsWindow(h))
            {
              ShowWindow(h,SW_SHOWNA);
              SetForegroundWindow(h);
            }
            else
            {
              HWND h=JesusonicAPI->ui_wnd_create(i);
              ShowWindow(h,SW_SHOWNA);
              SetTimer(h,1,40,NULL);
              SetForegroundWindow(h);
            }
#endif
  } else {	// want no fx
    if (i != NULL) {
      g_client->SetLocalChannelProcessor(a,NULL,NULL);
      deleteJesusonicProc(i,a);
    }
  }
}

void LocalChannelPanel::basicpanelOnApplyPrevAttrs() {
  onPostApplyDlgToAttr(&broadcasting, StringPrintf((int)broadcasting), IDC_BROADCAST);
//CUT  onPostApplyDlgToAttr(&src_channel, StringPrintf((int)src_channel), -1);
}

void LocalChannelPanel::panelAppendToPopup(PopupMenu *pop, int ofs) {
  pop->addCommand("Remove this channel", CMD_REMOVEME+ofs);
  delete subpop; subpop = new PopupMenu(pop);

  int tchnum=-1;
  g_client->GetLocalChannelInfo(channel_id, &tchnum, NULL, NULL);

  if (g_audio) {
    //enum all channels
    for (int i = 0; i < 256; i++) {
      String chn = g_audio->GetChannelName(i);
      if (chn.isempty()) break;
      int checked = (i == tchnum);
//CUT    int enabled = srcChanTaken(i);
      int enabled = 1;
      subpop->addCommand(StringPrintf("%02d: %s", i, chn.v()), CMD_AUDIOSELECT+i+ofs, checked, !enabled);
    }
  } else {
    for (int i = 0; i < MAX_UNK_CHANS; i++) {
      int enabled = 1;
      int checked = (i == tchnum);
      subpop->addCommand(StringPrintf("input %d", i), CMD_AUDIOSELECT+i+ofs, checked, !enabled);
    }
  }

//virtual/fxonly means srcchan = -1
  subpop->addCommand(VIRTUAL_CHANNEL_NAME, CMD_AUDIOSELECT-1+ofs, tchnum==-1);

  pop->addSubMenu(subpop, "Recording source");
}

void LocalChannelPanel::panelOnPopupCommand(int cmd) {
  if (cmd >= CMD_AUDIOSELECT-1) {
    int chnum = cmd - CMD_AUDIOSELECT;
#if 0
    //verify it's not taken
    for (int i = 0; i < MAX_LOCAL_CHANNELS; i++) {
      if (i == channel_id) continue;	// heh it's us
      int tchnum=0;
      if (!g_client->GetLocalChannelInfo(i,&tchnum,NULL,NULL)) break;
      if (tchnum == chnum) return;	// it's taken
    }
#endif

    //assign it 
    g_client->SetLocalChannelInfo(channel_id,NULL,true,chnum,false,0,false,false);
    g_client->NotifyServerOfChannelChange();
//CUT    src_channel = chnum;	// remember source channel
    refreshSrcChanName();
#ifdef SRCCHAN_COMBO
#error adjust combo box here
#endif

  } else switch (cmd) {
    case CMD_REMOVEME:
      // kill the local chan
      g_client->DeleteLocalChannel(channel_id);
      g_client->NotifyServerOfChannelChange();
      // tell parent to kill us
      panel_parent->killRackSlot(this);
    break;
  }
}

void LocalChannelPanel::onUserButton(int id) {
  switch (id) {
    case IDC_EDIT_FX: {
        void *i=0;
        g_client->GetLocalChannelProcessor(channel_id,NULL,&i);
            HWND h=i ? JesusonicAPI->ui_wnd_gethwnd(i) : NULL;
            if (h && IsWindow(h))
            {
// already have wnd, show it
              ShowWindow(h,SW_SHOWNA);
              SetForegroundWindow(h);
            }
            else
            {
// no wnd, create it
              HWND h=JesusonicAPI->ui_wnd_create(i);
              ShowWindow(h,SW_SHOWNA);
              SetTimer(h,1,40,NULL);
              SetForegroundWindow(h);
            }
    }
    break;
  }
}

void LocalChannelPanel::onCreate() {
  LOCALCHANNELPANEL_PARENT::onCreate();
}

void LocalChannelPanel::onConnect() {
//?  onFx(fx);
   adjustSampleRate(channel_id);

  // fix the srcchan selector now that we have a real audio device
#ifdef SRCCHAN_COMBO
  if (g_audio) {
    int tchnum=-1;
    g_client->GetLocalChannelInfo(channel_id, &tchnum, NULL, NULL);
    resetCombo(IDC_SRCCHAN);
    int n = 0;
    for (int i = 0; i < 256; i++) {
      String chn = g_audio->GetChannelName(i);
      if (chn.isempty()) break;
      populateCombo(IDC_SRCCHAN, chn, i);
      n++;
    }
    populateCombo(IDC_SRCCHAN, VIRTUAL_CHANNEL_NAME, n);
    if (tchnum == -1)
      setComboSel(IDC_SRCCHAN, n);
    else
      setComboSel(IDC_SRCCHAN, tchnum);
  }
#endif
  refreshSrcChanName();
}

double g_ilog2x6;
double VAL2DB(double x)
{
  double v=(log10(x)*g_ilog2x6);
  if (v < -120.0) v=-120.0;
  return v;
}

void LocalChannelPanel::refreshSrcChanName() {
  int tchnum=0;
  g_client->GetLocalChannelInfo(channel_id, &tchnum, NULL, NULL);
  if (tchnum < 0)
    src_channel_name = VIRTUAL_CHANNEL_NAME;
  else {
    if (g_audio == NULL) {
      src_channel_name= StringPrintf("input #%d", tchnum);
    } else {
      src_channel_name = StringPrintf("%02d: %s", tchnum, g_audio->GetChannelName(tchnum));
    }
  }
}
