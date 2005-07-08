#ifndef _LOCCHANPANEL_H
#define _LOCCHANPANEL_H

#include <bfc/attrib/attribs.h>

#include "basicpanel.h"

#define LOCALCHANNELPANEL_PARENT BasicPanel
class LocalChannelPanel : public LOCALCHANNELPANEL_PARENT {
public:
  LocalChannelPanel(RackWnd *wnd, int id);

  virtual FOURCC getRackSlotType() { return RS_TYPE_LOCAL_CHANNEL; }

  virtual void onPan(float pos);
  virtual void onVol(float vol);
  virtual void onMute(bool mut);
  virtual void onSolo(bool sol);
  virtual void onFx(bool fx);
  virtual float getVU();

  virtual void basicpanelOnApplyPrevAttrs();

  virtual void panelAppendToPopup(PopupMenu *pop, int ofs);
  virtual void panelOnPopupCommand(int cmd);

  virtual void onUserButton(int id);

  void refreshInfo();

  virtual void onPostApplyDlgToAttr(Attribute *attr, const char *newval, int dlgid);

  void refreshSrcChanName();

  int channel_id;
  _string chid;

  _string channel_name;

  _string src_channel_name;

  _bool broadcasting;
};

#endif
