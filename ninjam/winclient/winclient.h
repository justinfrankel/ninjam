#ifndef _WINCLIENT_H_
#define _WINCLIENT_H_

#define WM_LCUSER_RESIZE WM_USER+49
#define WM_LCUSER_ADDCHILD WM_USER+50
#define WM_LCUSER_REMCHILD WM_USER+51
#define WM_LCUSER_VUUPDATE WM_USER+52
#define WM_LCUSER_REPOP_CH WM_USER+53
#define WM_RCUSER_UPDATE WM_USER+54

#include "../audiostream_asio.h"
#include "../njclient.h"
#include "../../jesusonic/jesusonic_dll.h"

#include "../../WDL/mutex.h"
#include "../../WDL/string.h"


extern WDL_Mutex g_client_mutex;
extern WDL_String g_ini_file;
extern audioStreamer *g_audio;
extern NJClient *g_client;
extern jesusonicAPI *JesusonicAPI;  
extern HINSTANCE g_hInst;
extern int g_done;
extern WDL_String jesusdir;
extern WDL_String g_topic;


// audioconfig.cpp
audioStreamer *CreateConfiguredStreamer(char *inifile, int showcfg, HWND hwndParent);

// misc.cpp
double DB2SLIDER(double x);
double SLIDER2DB(double y);
double VAL2DB(double x);
#define DB2VAL(x) (pow(2.0,(x)/6.0))
void mkvolpanstr(char *str, double vol, double pan);

int CreateJesusInstance(int a, char *chdesc);
void JesusUpdateInfo(void *myInst, char *chdesc);
void deleteJesusonicProc(void *i, int chi);

// locchn.cpp
BOOL WINAPI LocalOuterChannelListProc(HWND hwndDlg, UINT uMsg, WPARAM wParam, LPARAM lParam);


// remchn.cpp
BOOL WINAPI RemoteOuterChannelListProc(HWND hwndDlg, UINT uMsg, WPARAM wParam, LPARAM lParam);

// chat.cpp
void chat_addline(char *src, char *text);
void chatInit(HWND hwndDlg);
void chatRun(HWND hwndDlg);
void chatmsg_cb(int user32, NJClient *inst, char **parms, int nparms);

// license.cpp
int licensecallback(int user32, char *licensetext);
void licenseRun(HWND hwndDlg);

#endif//_WINCLIENT_H_