#ifndef _BASICPANEL_H
#define _BASICPANEL_H

#include <api/timer/timerclient.h>
#include <bfc/attrib/attribs.h>
#include <wnd/subclass.h>

#include "panelslot.h"

class PopupMenu;

// this panel type handles the standard pan, volume, and mute buttons
// it only does the UI part, you have to override onVol() etc
class BasicPanel : public PanelSlot, public TimerClientDI, public SubClasser {
public:
  BasicPanel(RackWnd *wnd, int dlgid, const char *cfgname=NULL);
  virtual ~BasicPanel();

  virtual FOURCC getRackSlotType()=0;

protected:
  virtual void onPostApplyDlgToAttr(Attribute *attr, const char *newval, int dlgid);

  virtual void onPan(float pos) {
  }
  virtual void onVol(float vol) {
  }
  virtual void onMute(bool mut) {
  }
  virtual void onSolo(bool sol) {
  }
  virtual void onFx(bool fx) {
  }
  virtual float getVU() { return -1; }

  virtual void basicpanelOnApplyPrevAttrs() { }

  virtual void panelAppendToPopup(PopupMenu *pop, int ofs) { }
  virtual void panelOnPopupCommand(int cmd) { }

  _int pan, volume, mute;
  _bool solo;
  _bool fx;

private:
  virtual int timerclient_onDeferredCallback(int param1, int param2);
  virtual void onDlgContextMenu();
  virtual void onUserDblClick(int id);
  virtual int onSubclassWndProc(HWND hwnd, UINT uMsg, WPARAM wParam, LPARAM lParam, LRESULT &retval);
  virtual void onRefresh();

  _string vol_text, pan_text;

//VU meter
  float last_vu;
  _int vumeter;
  int vumeter_avg;
  _string vutext;
  _int clip;
};

#endif
