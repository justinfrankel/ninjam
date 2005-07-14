//
// This is only an example of a Wasabi app that creates a basic window.
// If you want to build your app from here, do not use this file in
// place. Copy it to your project directory and modify it there.
// Then add it to your project along with main.cpp.
//

#include "precomp.h"

#include <net/jnetlib/util.h>

#ifdef WASABI_PLATFORM_WIN32
#include "resource.h"
#endif

#include "attrs.h"
#include "mainwnd.h"
#include "session.h"

#include "app.h"

//EDITME: replace with your own app info
#define APPNAME "Ninjam"
#define STRFILEVER "1, 0, 0, 1\0"	// autobuild.dll format

const char *appname = APPNAME;
const char *strfilever = STRFILEVER;

//EDITME: replace with your own app guid
// {96AC20A5-2B79-4fb0-81A4-AE1524123F4A}
static const GUID appguid = 
{ 0x96ac20a5, 0x2b79, 0x4fb0, { 0x81, 0xa4, 0xae, 0x15, 0x24, 0x12, 0x3f, 0x4a } };
const GUID *pappguid = &appguid;

extern OSMODULEHANDLE wasabiAppOSModuleHandle; // WinMain fills in

#include "rackwnd.h"

MainWnd *mainw;

void wasabiAppInit(OSWINDOWHANDLE parentwnd) {
  JNL::open_socketlib();

  // wee
  mainw = new MainWnd;
//  t->setVirtual(0);
  mainw->init(wasabiAppOSModuleHandle, parentwnd);
//t->setVisible(TRUE);
  mainw->resize(mainwnd_x,mainwnd_y,mainwnd_w,mainwnd_h);
}

void wasabiAppShutdown() {
  // la
  delete mainw; mainw = NULL;
  Session::endSession();
  JNL::close_socketlib();
}
