#ifndef _NJRACKWND_H
#define _NJRACKWND_H

#include "rackwnd.h"

#define NJRACKWND_PARENT RackWnd
class NJRackWnd : public NJRACKWND_PARENT {
public:
  NJRackWnd();
  ~NJRackWnd();

  virtual int onInit();

  void addChannel(const char *username, int id);

  void addLocalChannel(int channel_id=-1, int srcchan=-2); // -2 means alloc one
  void killDeadChannels();

  void refreshChannels();

  virtual void rackwndAppendToPopup(PopupMenu *pop, int ofs);
  virtual void rackwndOnPopupCommand(int cmd);

  void onDisconnect();

private:
};

#endif
