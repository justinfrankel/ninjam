#include <windows.h>
#include <stdio.h>
#include "../WDL/string.h"
#include "resource.h"




struct
{
   int mode;
   int asio_driver;
   int asio_input[2];
   int asio_output[2];
} configdata={
  0,
   0, //asio_driver;
   {0,1}, //asio_input;
   {0,1}, //asio_output;
};
/*
WDL_String m_inifile;
int load_config()
{ 
  char *fn=m_inifile.Get();
  int *p=(int *)&configdata;
  int s=sizeof(configdata)/sizeof(int);
  int c=0;
  for (c = 0; c < s; c ++)
  {
    char n[64];
    wsprintf(n,"Config%d",c);
    char buf[128];
    GetPrivateProfileString( "ninjam",n,"",buf,sizeof(buf),fn);
    if (buf[0] != '0' && !atoi(buf)) break;
    *p++=atoi(buf);
  }
  return c;
}

void save_config()
{
  char *fn=m_inifile.Get();
  int *p=(int *)&configdata;
  int s=sizeof(configdata)/sizeof(int);
  int c=0;
  for (c = 0; c < s; c ++)
  {
    char n[64];
    wsprintf(n,"Config%d",c);
    char buf[128];
    wsprintf(buf,"%d",*p++);
    WritePrivateProfileString( "ninjam",n,buf,fn);
  }
}

  */


#include "asiosys.h"
#include "asio.h"
#include "asiodrivers.h"

extern AsioDrivers* asioDrivers;


BOOL CALLBACK cfgproc_asio( HWND hwndDlg, UINT uMsg, WPARAM wParam, LPARAM lParam)
{
  if (uMsg == WM_INITDIALOG)
  {
	  if(!asioDrivers)
		  asioDrivers = new AsioDrivers();

    if (asioDrivers)
    {
      int l=asioDrivers->asioGetNumDev();
      int x;
      for (x = 0; x < l; x ++)
      {
        char buf[256];
        asioDrivers->asioGetDriverName(x,buf,sizeof(buf));
        SendDlgItemMessage(hwndDlg,IDC_COMBO1,CB_ADDSTRING,0,(LPARAM)buf);        
      }
      if (!l)
        SendDlgItemMessage(hwndDlg,IDC_COMBO1,CB_ADDSTRING,0,(LPARAM)"No ASIO drivers found!");
      int wd=configdata.asio_driver;
      if (wd<0||wd>l) wd=0;
      SendDlgItemMessage(hwndDlg,IDC_COMBO1,CB_SETCURSEL,(WPARAM)wd,0);
      SendMessage(hwndDlg,WM_COMMAND,MAKEWPARAM(IDC_COMBO1,CBN_SELCHANGE),0);
    }   
    //delete asioDrivers; // fucko thsi shouldnt be commented
   // asioDrivers=0;
    
    return 1;
  }
  if (uMsg == WM_COMMAND && wParam == MAKEWPARAM(IDC_COMBO1,CBN_SELCHANGE))
  {
    int wp=SendDlgItemMessage(hwndDlg,IDC_COMBO1,CB_GETCURSEL,0,0);
    SendDlgItemMessage(hwndDlg,IDC_COMBO2,CB_RESETCONTENT,0,0);
    SendDlgItemMessage(hwndDlg,IDC_COMBO3,CB_RESETCONTENT,0,0);
    SendDlgItemMessage(hwndDlg,IDC_COMBO4,CB_RESETCONTENT,0,0);
    SendDlgItemMessage(hwndDlg,IDC_COMBO5,CB_RESETCONTENT,0,0);
    if (wp != CB_ERR)
    {
      char buf[256];
      SendDlgItemMessage(hwndDlg,IDC_COMBO1,CB_GETLBTEXT,wp,(LPARAM)buf);
	    if(!asioDrivers)
		    asioDrivers = new AsioDrivers();
      if (asioDrivers && asioDrivers->loadDriver(buf))
      {
      	ASIODriverInfo driverInfo;
        driverInfo.sysRef = (void *)hwndDlg;
        if (ASIOInit(&driverInfo) == ASE_OK)
        {
          long inch=0,outch=0;
	        if (ASIOGetChannels(&inch, &outch) == ASE_OK) 
          {
            long i;
            for (i = 0; i < inch; i ++)
            {
              ASIOChannelInfo c;
              c.channel=i;
              c.isInput=1;
              if (ASIOGetChannelInfo(&c) == ASE_OK) 
              {
		char buf[1024];
		wsprintf(buf,"%d: %s",i+1,c.name);
                SendDlgItemMessage(hwndDlg,IDC_COMBO2,CB_ADDSTRING,0,(LPARAM)buf);
                SendDlgItemMessage(hwndDlg,IDC_COMBO4,CB_ADDSTRING,0,(LPARAM)buf);
              }
            }
            for (i = 0; i < outch; i ++)
            {
              ASIOChannelInfo c;
              c.channel=i;
              c.isInput=0;
              if (ASIOGetChannelInfo(&c) == ASE_OK) 
              {
		char buf[1024];
		wsprintf(buf,"%d: %s",i+1,c.name);
                SendDlgItemMessage(hwndDlg,IDC_COMBO3,CB_ADDSTRING,0,(LPARAM)buf);
                SendDlgItemMessage(hwndDlg,IDC_COMBO5,CB_ADDSTRING,0,(LPARAM)buf);
              }
            }
            int wd=configdata.asio_input[0];
            if (wd<0||wd>inch) wd=0;
            SendDlgItemMessage(hwndDlg,IDC_COMBO2,CB_SETCURSEL,(WPARAM)wd,0);
            wd=configdata.asio_input[1];
            if (wd<0||wd>inch) wd=0;
            SendDlgItemMessage(hwndDlg,IDC_COMBO4,CB_SETCURSEL,(WPARAM)wd,0);
            wd=configdata.asio_output[0];
            if (wd<0||wd>outch) wd=0;
            SendDlgItemMessage(hwndDlg,IDC_COMBO3,CB_SETCURSEL,(WPARAM)wd,0);
            wd=configdata.asio_output[1];
            if (wd<0||wd>outch) wd=0;
            SendDlgItemMessage(hwndDlg,IDC_COMBO5,CB_SETCURSEL,(WPARAM)wd,0);
          } 
          ASIOExit();
        }

        asioDrivers->removeCurrentDriver();
      }
      //delete asioDrivers;// fucko thsi shouldnt be commented
     // asioDrivers=0;
    }
    // repopulate channel list
  }

  if (uMsg == WM_COMMAND && LOWORD(wParam) == IDC_BUTTON1)
  {
    char buf[256];
    int wp=SendDlgItemMessage(hwndDlg,IDC_COMBO1,CB_GETCURSEL,0,0);
    if (wp != CB_ERR)
    {
      if (!asioDrivers) asioDrivers = new AsioDrivers();

      SendDlgItemMessage(hwndDlg,IDC_COMBO1,CB_GETLBTEXT,wp,(LPARAM)buf);
      if (asioDrivers && asioDrivers->loadDriver(buf))
      {
        ASIODriverInfo driverInfo;
        driverInfo.sysRef=(HWND)hwndDlg;
        if (ASIOInit(&driverInfo) == ASE_OK)
        {
          ASIOControlPanel();
          ASIOExit();
        }
      }
      asioDrivers->removeCurrentDriver();
     // if (asioDrivers) delete asioDrivers;
   //   asioDrivers=0;// fucko thsi shouldnt be commented
    }
  }

  if (uMsg == WM_COMMAND && LOWORD(wParam) == IDOK)
  {
    int a;
    a=SendDlgItemMessage(hwndDlg,IDC_COMBO1,CB_GETCURSEL,0,0);
    if (a != CB_ERR) configdata.asio_driver = a;
    a=SendDlgItemMessage(hwndDlg,IDC_COMBO2,CB_GETCURSEL,0,0);
    if (a != CB_ERR) configdata.asio_input[0] = a;
    a=SendDlgItemMessage(hwndDlg,IDC_COMBO4,CB_GETCURSEL,0,0);
    if (a != CB_ERR) configdata.asio_input[1] = a;
    a=SendDlgItemMessage(hwndDlg,IDC_COMBO3,CB_GETCURSEL,0,0);
    if (a != CB_ERR) configdata.asio_output[0] = a;
    a=SendDlgItemMessage(hwndDlg,IDC_COMBO5,CB_GETCURSEL,0,0);
    if (a != CB_ERR) configdata.asio_output[1] = a;
  }
  return 0;
}



BOOL CALLBACK configDlgMainProc( HWND hwndDlg, UINT uMsg, WPARAM wParam, LPARAM lParam)
{
  static const int dlgids[1]={IDD_CFG_ASIO};
  static const DLGPROC procs[1]={cfgproc_asio};
  static HWND children[1];
  static const char *labels[1] = { "ASIO" };

  switch (uMsg)
  {
    case WM_INITDIALOG:
      {
        int x;
        for (x = 0; x < 1; x ++)
        {
            SendDlgItemMessage(hwndDlg,IDC_COMBO1,CB_ADDSTRING,0,(LPARAM)labels[x]);
        }
        int cm=configdata.mode;
        if (cm  < 0 || cm > 0) cm=0;
        SendDlgItemMessage(hwndDlg,IDC_COMBO1,CB_SETCURSEL,(WPARAM)cm,0);
        SendMessage(hwndDlg,WM_COMMAND,MAKEWPARAM(IDC_COMBO1,CBN_SELCHANGE),0);
      }
    return TRUE;
    case WM_DESTROY:
      {
        int x;
        for (x = 0; x < 1; x ++) 
        {
          if (children[x] && IsWindow(children[x])) DestroyWindow(children[x]);
          children[x]=0;
        }
      }
    return 0;
    case WM_COMMAND:
      switch (LOWORD(wParam))
      {
        case IDC_COMBO1:
          if (HIWORD(wParam) == CBN_SELCHANGE)
          {
            int x;
            int y=SendDlgItemMessage(hwndDlg,IDC_COMBO1,CB_GETCURSEL,0,0);
            if (y != CB_ERR) 
            {
              if (!children[y])
              {
                children[y]=CreateDialog(GetModuleHandle(NULL),MAKEINTRESOURCE(dlgids[y]),hwndDlg,procs[y]);
                RECT r;
                GetWindowRect(GetDlgItem(hwndDlg,IDC_CRECT),&r);
                ScreenToClient(hwndDlg,(LPPOINT)&r);
                SetWindowPos(children[y],NULL,r.left,r.top,0,0,SWP_NOSIZE|SWP_NOZORDER);
                // position the window
              }
              for (x = 0; x < 1; x ++) 
              {
                if (children[x]) ShowWindow(children[x],x==y?SW_SHOWNA:SW_HIDE);
              }
            }
          }
        return 0;
        case IDOK:
          {
            int y;
            for (y = 0; y < 1; y ++)
              SendMessage(children[y],WM_COMMAND,IDOK,0);
            y=SendDlgItemMessage(hwndDlg,IDC_COMBO1,CB_GETCURSEL,0,0);
            if (y != CB_ERR) configdata.mode=y;
          }
        case IDCANCEL:
          EndDialog(hwndDlg,0);
        return 0;
      }
    return 0;
  }
  return 0;
}




char *get_asio_configstr()
{
  DialogBox(GetModuleHandle(NULL),MAKEINTRESOURCE(IDD_CONFIG),NULL,configDlgMainProc);
  static char tmpbuf[64];
    wsprintf(tmpbuf,"%d:%d,%d:%d,%d",configdata.asio_driver,
      configdata.asio_input[0],
      configdata.asio_input[1],
      configdata.asio_output[0],
      configdata.asio_output[1]
      );

  return tmpbuf;
}
