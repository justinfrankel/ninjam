#include <windows.h>
#include <commctrl.h>
#include "../audiostream_asio.h"
#define strncasecmp strnicmp

#include <stdio.h>
#include <math.h>
#include <signal.h>
#include <float.h>

#include "resource.h"

#include "../audiostream_asio.h"

#include "../njclient.h"
#include "../../WDL/wingui/wndsize.h"
#include "../../WDL/dirscan.h"
#include "../../WDL/lineparse.h"

#ifdef _WIN32
#include "../../jesusonic/jesusonic_dll.h"
#endif

#define VALIDATE_TEXT_CHAR(thischar) ((isspace(thischar) || isgraph(thischar)) && (thischar) < 256)

int g_done=0;

HICON g_hSmallIcon;
HINSTANCE g_hInst;
HWND g_hwnd;
WDL_String g_topic;
WDL_PtrList<char> g_chat_buffers;

void addChatLine(char *src, char *text)
{
  while (g_chat_buffers.GetSize() > 256)
  {
    free(g_chat_buffers.Get(0));
    g_chat_buffers.Delete(0);
  }
  WDL_String tmp;
  if (src && *src && !strncmp(text,"/me ",4))
  {
    tmp.Set("* ");
    tmp.Append(src);
    tmp.Append(" ");
    char *p=text+3;
    while (*p == ' ') p++;
    tmp.Append(p);
  }
  else
  {
   if (src&&*src)
   {
     tmp.Set("<");
     tmp.Append(src);
     tmp.Append("> ");
   }
   else if (src)
   {
     tmp.Set("*** ");
   }
   tmp.Append(text);
  }
  g_chat_buffers.Add(strdup(tmp.Get()));
}

void chatmsg_cb(int user32, NJClient *inst, char **parms, int nparms)
{
  if (!parms[0]) return;

  if (!strcmp(parms[0],"TOPIC"))
  {
    if (parms[2])
    {
      WDL_String tmp;
      if (parms[1] && *parms[1])
      {
        tmp.Set(parms[1]);
        tmp.Append(" sets topic to: ");
      }
      else tmp.Set("Topic is: ");
      tmp.Append(parms[2]);

      g_topic.Set(parms[2]);
      addChatLine("",tmp.Get());   
    }
  }
  else if (!strcmp(parms[0],"MSG"))
  {
    if (parms[1] && parms[2])
      addChatLine(parms[1],parms[2]);
  } 
  else if (!strcmp(parms[0],"PRIVMSG"))
  {
    if (parms[1] && parms[2])
    {
      WDL_String tmp;
      tmp.Set("*");
      tmp.Append(parms[1]);
      tmp.Append("* ");
      tmp.Append(parms[2]);
      addChatLine(NULL,tmp.Get());
    }
  } 
  else if (!strcmp(parms[0],"JOIN") || !strcmp(parms[0],"PART"))
  {
    if (parms[1] && *parms[1])
    {
      WDL_String tmp(parms[1]);
      tmp.Append(" has ");
      tmp.Append(parms[0][0]=='P' ? "left" : "joined");
      tmp.Append(" the server");
      addChatLine("",tmp.Get());
    }
  } 
}

double g_ilog2x6;
double VAL2DB(double x)
{
  double v=(log10(x)*g_ilog2x6);
  if (v < -120.0) v=-120.0;
  return v;
}
#define DB2VAL(x) (pow(2.0,(x)/6.0))

#ifdef _WIN32
// jesusonic stuff
#define MAX_JESUS_INST 32
jesusonicAPI *JesusonicAPI;  
HINSTANCE hDllInst;
char *jesusdir=NULL;
#endif


#ifdef _WIN32
audioStreamer *CreateConfiguredStreamer(char *inifile, int showcfg, HWND hwndParent);
#endif
audioStreamer *g_audio;
NJClient *g_client;


void audiostream_onunder() { }
void audiostream_onover() { }

int g_audio_enable=0;

void audiostream_onsamples(float **inbuf, int innch, float **outbuf, int outnch, int len, int srate) 
{ 
  if (!g_audio_enable) 
  {
    int x;
    // clear all output buffers
    for (x = 0; x < outnch; x ++) memset(outbuf[x],0,sizeof(float)*len);
    return;
  }
  g_client->AudioProc(inbuf,innch, outbuf, outnch, len,srate);
}

void deleteJesusonicProc(void *i, int chi)
{
#ifdef _WIN32
  if (JesusonicAPI && i)
  {
      char buf[4096];
      sprintf(buf,"%s\\ninjam.p%02d",jesusdir?jesusdir:".",chi);
      JesusonicAPI->preset_save(i,buf);
      JesusonicAPI->ui_wnd_destroy(i);
      JesusonicAPI->set_opts(i,-1,-1,1);
      JesusonicAPI->ui_quit(i);
      JesusonicAPI->destroyInstance(i);
  }
#endif
}


void jesusonic_processor(float *buf, int len, void *inst)
{
#ifdef _WIN32
  if (inst)
  {
    _controlfp(_RC_CHOP,_MCW_RC);
    JesusonicAPI->jesus_process_samples(inst,(char*)buf,len*sizeof(float));
    JesusonicAPI->osc_run(inst,(char*)buf,len);
  }
#endif
}
#ifdef _WIN32
void CreateJesusInstance(int a)
{
  if (JesusonicAPI)
  {
    void *myInst=JesusonicAPI->createInstance();
    JesusonicAPI->set_rootdir(myInst,jesusdir);
    JesusonicAPI->ui_init(myInst);
    JesusonicAPI->set_opts(myInst,1,1,-1);
    JesusonicAPI->set_sample_fmt(myInst,g_audio->m_srate,1,33);
    JesusonicAPI->set_status(myInst,"","NINJAM embedded");
    HWND h=JesusonicAPI->ui_wnd_create(myInst);
    ShowWindow(h,SW_SHOWNA);
    SetTimer(h,1,40,NULL);
    SetForegroundWindow(h);

    char buf[4096];
    sprintf(buf,"%s\\ninjam.p%02d",jesusdir?jesusdir:".",a);

    JesusonicAPI->preset_load(myInst,buf);

    g_client->SetLocalChannelProcessor(a,jesusonic_processor,myInst);
  }
}
#endif


void mkvolpanstr(char *str, double vol, double pan)
{
  double v=VAL2DB(vol);
  if (vol < 0.0000001 || v < -120.0) v=-120.0;
    sprintf(str,"%2.1fdB ",v);   
    if (fabs(pan) < 0.0001) strcat(str,"center");
    else sprintf(str+strlen(str),"%d%%%s", (int)fabs(pan*100.0),(pan>0.0 ? "R" : "L"));
}




int licensecallback(int user32, char *licensetext)
{
  return 1;
}



static BOOL WINAPI MainProc(HWND hwndDlg, UINT uMsg, WPARAM wParam, LPARAM lParam)
{
  static WDL_WndSizer resize;
  switch (uMsg)
  {
    case WM_INITDIALOG:
      {
        g_hwnd=hwndDlg;
        ShowWindow(g_hwnd,SW_SHOWNA);

        resize.init(hwndDlg);

        // top items
        resize.init_item(IDC_MASTERVOL,  0.0,  0.0,  0.7f,  0.0);
        resize.init_item(IDC_METROVOL,   0.0,  0.0,  0.7f,  0.0);
        resize.init_item(IDC_MASTERLBL,  0.7f,  0.0,  0.7f,  0.0);
        resize.init_item(IDC_METROLBL,   0.7f,  0.0,  0.7f,  0.0);

        resize.init_item(IDC_MASTERPAN,  0.7f,  0.0,  0.8f,  0.0);
        resize.init_item(IDC_METROPAN,   0.7f,  0.0,  0.8f,  0.0);
        resize.init_item(IDC_MASTERMUTE, 0.8f,  0.0,  0.8f,  0.0);
        resize.init_item(IDC_METROMUTE,  0.8f,  0.0,  0.8f,  0.0);
        resize.init_item(IDC_MASTERVU,   0.8f,  0.0,  1.0f,  0.0);
        resize.init_item(IDC_MASTERVULBL,0.8f,  0.0,  1.0f,  0.0);
        
               
        resize.init_item(IDC_DIV1,        0.0,  0.0,  1.0f,  0.0);
        resize.init_item(IDC_INTERVALPOS, 0.0,  1.0,  1.0f,  1.0);
        resize.init_item(IDC_STATUS, 0.0,  1.0,  1.0f,  1.0);
        
        resize.init_item(IDC_DIV2,        0.0,  0.5f,  0.7f,  0.5f);
        resize.init_item(IDC_CHATDIV,     0.7f, 0.0f,  0.7f,  1.0f);

        resize.init_item(IDC_CHATDISP,     0.7f, 0.0f,  1.0f,  1.0f);
        resize.init_item(IDC_CHATENT,      0.7f, 1.0f,  1.0f,  1.0f);
        
        resize.init_item(IDC_LOCALSCROLL,  0.7f, 0.0f,  0.7f,  0.5f);
        resize.init_item(IDC_REMOTESCROLL, 0.7f, 0.5f,  0.7f,  1.0f);
        
        resize.init_item(IDC_LOCRECT,     0.0f, 0.0f,  0.7f,  0.5f);
        resize.init_item(IDC_REMOTERECT,  0.0f, 0.5f,  0.7f,  1.0f);

        

      }
    break;
    case WM_GETMINMAXINFO:
      {
        LPMINMAXINFO p=(LPMINMAXINFO)lParam;
        p->ptMinTrackSize.x = 600;
        p->ptMinTrackSize.y = 400;
      }
    return 0;

    case WM_SIZE:
      if (wParam != SIZE_MINIMIZED) 
      {
        resize.onResize();
      }
    break;
    case WM_COMMAND:
      switch (LOWORD(wParam))
      {
        case ID_FILE_QUIT:
          PostMessage(hwndDlg,WM_CLOSE,0,0);
        break;
      }
    break;
    case WM_CLOSE:
      if (1) DestroyWindow(hwndDlg);
    break;
    case WM_DESTROY:
      PostQuitMessage(0);
    return 0;
  }
  return 0;
}

int WINAPI WinMain(HINSTANCE hInstance, HINSTANCE hPrevInstance, LPSTR lpszCmdParam, int nShowCmd)
{
  g_hInst=hInstance;
  InitCommonControls();
  CoInitialize(0);


  { // load richedit DLL
    WNDCLASS wc={0,};
    if (!LoadLibrary("RichEd20.dll")) LoadLibrary("RichEd32.dll");

    // make richedit20a point to RICHEDIT
    if (!GetClassInfo(NULL,"RichEdit20A",&wc))
    {
      GetClassInfo(NULL,"RICHEDIT",&wc);
      wc.lpszClassName = "RichEdit20A";
      RegisterClass(&wc);
    }
  }


  g_hSmallIcon = (HICON)LoadImage(g_hInst,MAKEINTRESOURCE(IDI_ICON1),IMAGE_ICON,16,16,0);

  {
    WNDCLASS wc={0,};
    GetClassInfo(NULL,"#32770",&wc);
    wc.hIcon=LoadIcon(g_hInst,MAKEINTRESOURCE(IDI_ICON1));
    RegisterClass(&wc);
    wc.lpszClassName = "NINJAMwnd";
    RegisterClass(&wc);
  }



  g_ilog2x6 = 6.0/log10(2.0);

  JNL::open_socketlib();

  g_client = new NJClient;


  if (!CreateDialog(hInstance,MAKEINTRESOURCE(IDD_MAIN),GetDesktopWindow(),MainProc))
  {
    char buf[512];
    wsprintf(buf,"Error %08x\n",GetLastError());
    MessageBox(NULL,buf,"a",0);
    return 0;
  }

  MSG msg;
  while (GetMessage(&msg,NULL,0,0) && IsWindow(g_hwnd))
  {
    if (!IsDialogMessage(g_hwnd,&msg))
    {
      TranslateMessage(&msg);
      DispatchMessage(&msg);
    }
  }



  // delete all effects processors in g_client
  {
    int x=0;
    for (x = 0;;x++)
    {
      int a=g_client->EnumLocalChannels(x);
      if (a<0) break;
      void *i=0;
      g_client->GetLocalChannelProcessor(a,NULL,&i);
      if (i) deleteJesusonicProc(i,a);
      g_client->SetLocalChannelProcessor(a,NULL,NULL);
    }
  }


  delete g_client;


#ifdef _WIN32
  ///// jesusonic stuff
  if (hDllInst) FreeLibrary(hDllInst);
  hDllInst=0;
  JesusonicAPI=0;

#endif

  JNL::close_socketlib();
  return 0;
}
