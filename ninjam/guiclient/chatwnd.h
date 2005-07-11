#ifndef _CHATWND_H
#define _CHATWND_H

#include <wnd/wnds/os/osdialog.h>
#include <wnd/subclass.h>
#include <bfc/attrib/attribs.h>
#include <api/timer/timerclient.h>

#define CHATWND_PARENT OSDialog
class ChatWnd : public CHATWND_PARENT, public TimerClientDI, public SubClasser {
public:
  ChatWnd(OSWINDOWHANDLE parwnd);
  ~ChatWnd();

  void addChatLine(const char *from, const char *txt);

  void processUserEntry(const char *txt);

  void setTopic(const char *topic);

protected:
  void setDisp();

  virtual void onCreate();	// call ancestor
  virtual void onUserClose();
  virtual void onUserButton(int id);
  virtual void onPostApplyDlgToAttr(Attribute *attr, const char *newval, int dlgid);

  virtual int timerclient_onDeferredCallback(int param1, int param2);

  virtual int onSubclassWndProc(HWND hwnd, UINT uMsg, WPARAM wParam, LPARAM lParam, LRESULT &retval);

private:
  HICON m_icon_small, m_icon_big;
  _string chat_disp, chat_entry;
  _string chat_topic;
};

#endif
