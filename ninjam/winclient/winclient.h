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
extern HWND g_hwnd;


// winclient.cpp
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



// chat.cpp
extern void addChatLine(char *src, char *text);
extern void chatInit(HWND hwndDlg);
extern void chat_run();


#endif//_WINCLIENT_H_