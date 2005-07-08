#ifndef _SETTINGS_H
#define _SETTINGS_H

#include <wnd/wnds/os/osdialog.h>

class Settings : public OSDialog {
public:
  Settings(HWND parwnd);

  virtual void onUserButton(int id);

  virtual void onPostApplyDlgToAttr(Attribute *attr, const char *newval, int dlgid);

  _string exampledir;
};

#endif
