#ifndef _CHANPANEL_H
#define _CHANPANEL_H

#include "basicpanel.h"

#define CHANNELPANEL_PARENT BasicPanel
class ChannelPanel : public CHANNELPANEL_PARENT {
public:
  ChannelPanel(RackWnd *wnd, const char *username, int id);

  virtual FOURCC getRackSlotType() { return RS_TYPE_CHANNEL; }

  void refreshInfo();
  int dead();

  virtual void onPostApplyDlgToAttr(Attribute *attr, const char *newval, int dlgid);

  virtual void onPan(float pos);
  virtual void onVol(float vol);
  virtual void onMute(bool mut);
  virtual void onSolo(bool sol);
  virtual float getVU();

  virtual void basicpanelOnApplyPrevAttrs();

  virtual int getSortVal();

  _string user_name;
  int channel_id;
  _string channel_id_str;	// %02d of channel_id

  _string channel_name;

  _bool subscribe;

  _int vumeter;
  int vumeter_avg;
};

#endif
