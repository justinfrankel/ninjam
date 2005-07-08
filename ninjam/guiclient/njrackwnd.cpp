#include "precomp.h"

#include <wnd/popup.h>
#include <parse/pathparse.h>

#include "attrs.h"
#include "chanpanel.h"
#include "masterpanel.h"
#include "metropanel.h"
#include "locchanpanel.h"
#include "seppanel.h"
#include "sweeppanel.h"

#include "njrackwnd.h"

#include "../audiostream_asio.h"
extern audioStreamer *g_audio;

extern NJClient *g_client;

enum {
  CMD_MASTER,
  CMD_METRONOME,
  CMD_ADDLOCAL,

  CMD_SORT,
};

const float default_monitor_vol=1.0;

NJRackWnd::NJRackWnd() {
  bgColor = GetSysColor(COLOR_APPWORKSPACE);
}

NJRackWnd::~NJRackWnd() {
  String ch;

  int need_pipe=0;
  int num_added=0;
  for (int i = 0; i < MAX_LOCAL_CHANNELS; i++) {
    int srcch=-1;
    if (g_client->GetLocalChannelInfo(i,&srcch,NULL,NULL)) {
      if (need_pipe) ch += "|";
      ch += StringPrintf("%d,%d", i, srcch);
      need_pipe = 1;
      num_added ++;
    }
  }

  if (num_added == 0)
    channels_had = "*";
  else
    channels_had = ch;
}

int NJRackWnd::onInit() {
  int r = NJRACKWND_PARENT::onInit();

  if (master_was_showing) addRackSlot(new MasterPanel(this));
  if (metro_was_showing) addRackSlot(new MetronomePanel(this));
  if (sweep_was_showing) addRackSlot(new SweepPanel(this));
  /*if (sweep_was_showing)*/ addRackSlot(new LocalSepPanel(this));
  /*if (sweep_was_showing)*/ addRackSlot(new ChanSepPanel(this));

  String str=channels_had;
  int had_none = (str.firstChar() == '*');
  str.trim("*");

  // recreate local channels we had last time
  PathParser pp(channels_had, "|");
  int chans_added=0;
  for (int i = 0; i < pp.getNumStrings(); i++) {
    int id=0, srcch=-1;
    if (SSCANF(pp.enumString(i), "%d,%d", &id, &srcch) == 2) {
      DebugString("prev chan: %d,%d", id, srcch);
      addLocalChannel(id, srcch);
      chans_added++;
    }
  }

// if they didn't get any, make em take one
  if (chans_added == 0 && !had_none) 
    addLocalChannel();

  return r;
}

void NJRackWnd::addChannel(const char *username, int id) {
  // dup check
  foreach(slots)
    RackSlot *sl = slots.getfor();
    if (sl->getRackSlotType() != RS_TYPE_CHANNEL) continue;
    ChannelPanel *cp = static_cast<ChannelPanel *>(sl);
    if (STREQLSAFE(cp->user_name, username) && cp->channel_id == id) return;//dup!
  endfor

  // add it
  addRackSlot(new ChannelPanel(this, username, id));

  // make it fit eh
  onResize();
}

void NJRackWnd::addLocalChannel(int channel_id, int srcchan) {
  if (getNumPanels(RS_TYPE_LOCAL_CHANNEL) >= g_client->GetMaxLocalChannels())
    return;

// find a local chan that's not in use, also scan for what srcchans are in use
  TList<int> srcinuse;

  for (int id = 0; id < MAX_LOCAL_CHANNELS; id++) {
    int srcch=-1;
    if (!g_client->GetLocalChannelInfo(id,&srcch,NULL,NULL)) break;
    if (srcch >= 0) {
      srcinuse.addItem(srcch);
    }
  }

  // allow incoming override of chosen channel #
  if (channel_id >= 0) id = channel_id;

  if (id >= MAX_LOCAL_CHANNELS) return;	// no mas just yet

  int srcch=srcchan;

  if (srcch == -2) {
    // find a source channel that is not in use
    for (int i=0; ; i++) {
      srcch = -1;
      if (!g_audio || !g_audio->GetChannelName(i)) break;	// out of src chans, bail
      srcch = i;	// trying #i
      for (int j = 0; j < srcinuse.getNumItems(); j++) {
        int p = srcinuse[j];
        if (p == srcch) {
          srcch = -1;
          break;	// no good, but don't quitit
        }
      }
      if (srcch >= 0) break;	// found one
    }
  }

  g_client->SetLocalChannelInfo(id,"chan",true,srcch,false,0,true,true);
  g_client->SetLocalChannelMonitoring(id,true,default_monitor_vol,false,0.0,false,false,false,false);
  g_client->NotifyServerOfChannelChange();

  addRackSlot(new LocalChannelPanel(this, id));
}

void NJRackWnd::killDeadChannels() {
  foreach(slots)
    RackSlot *sl = slots.getfor();
    if (sl->getRackSlotType() != RS_TYPE_CHANNEL) continue;
    ChannelPanel *cp = static_cast<ChannelPanel *>(sl);

    if (cp->dead()) {
      killRackSlot(cp);
//CUT      delete cp;
//CUT      slots.removeItem(sl);
    }

  endfor
}

void NJRackWnd::refreshChannels() {
  foreach(slots)
    RackSlot *sl = slots.getfor();
    if (sl->getRackSlotType() != RS_TYPE_CHANNEL) continue;
    ChannelPanel *cp = static_cast<ChannelPanel *>(sl);

    cp->refreshInfo();

  endfor
}

void NJRackWnd::rackwndAppendToPopup(PopupMenu *pop, int ofs) {
  pop->addCommand("Add new local channel...", CMD_ADDLOCAL+ofs);
  pop->addSeparator();
  pop->addCommand("Show Master", CMD_MASTER+ofs, (getNumPanels(RS_TYPE_MASTER) > 0));
  pop->addCommand("Show Metronome", CMD_METRONOME+ofs, (getNumPanels(RS_TYPE_METRONOME) > 0));
//  pop->addCommand("Sort panels", CMD_SORT+ofs);
}

void NJRackWnd::rackwndOnPopupCommand(int cmd) {
  switch (cmd) {
    case CMD_MASTER: {
      PtrList<RackSlot> masters = getPanelList(RS_TYPE_MASTER);
      if (masters.n() > 0) {
        foreach(masters)
          killRackSlot(masters.getfor(), TRUE);
        endfor
        master_was_showing = 0;
      } else {
        addRackSlot(new MasterPanel(this));
        master_was_showing = 1;
      }
    }
    break;
    case CMD_METRONOME: {
      PtrList<RackSlot> metros = getPanelList(RS_TYPE_METRONOME);
      if (metros.n() > 0) {
        foreach(metros)
          killRackSlot(metros.getfor(), TRUE);
        endfor
        metro_was_showing = 0;
      } else {
        addRackSlot(new MetronomePanel(this));
        metro_was_showing = 1;
      }
    }
    break;
    case CMD_ADDLOCAL: {
      addLocalChannel();	// might need to defer
    }
    break;
    case CMD_SORT: {
    }
    break;
  }
}

void NJRackWnd::onDisconnect() {
  killDeadChannels();
}
