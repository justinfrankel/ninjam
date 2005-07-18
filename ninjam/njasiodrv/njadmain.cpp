/*
    NINJAM ASIO driver - njadmain.cpp
    Copyright (C) 2005 Cockos Incorporated

  
    This software is provided 'as-is', without any express or implied
    warranty.  In no event will the authors be held liable for any damages
    arising from the use of this software.

    Permission is granted to anyone to use this software for any purpose,
    including commercial applications, and to alter it and redistribute it
    freely, subject to the following restrictions:

    1. The origin of this software must not be misrepresented; you must not
       claim that you wrote the original software. If you use this software
       in a product, an acknowledgment in the product documentation would be
       appreciated but is not required.
    2. Altered source versions must be plainly marked as such, and must not be
       misrepresented as being the original software.
    3. This notice may not be removed or altered from any source distribution.
    

*/

/*

  This provides ASIO input/output functionality for NINJAM. The reason it is 
  in a seperate DLL file is so that people who want to develop the GPL'd 
  client can do so without having to agree to Steinberg's ASIO SDK license 
  agreement.  This file provides the dialog code and exports so that the 
  NINJAM client can load the DLL.

*/

#include <windows.h>

#include "resource.h"
#include "../audiostream.h"

#include "njasiodrv_if.h"


#include <asiosys.h>
#include <asio.h>
#include <asiodrivers.h>

extern AsioDrivers* asioDrivers;


static BOOL CALLBACK cfgproc_asio( HWND hwndDlg, UINT uMsg, WPARAM wParam, LPARAM lParam)
{
  asio_config_type *cfg;
  if (uMsg == WM_INITDIALOG)
  {
    SetWindowLong(hwndDlg,GWL_USERDATA,(long)(cfg = (asio_config_type *)lParam));
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
      int wd=cfg->asio_driver;
      if (wd<0||wd>l) wd=0;
      SendDlgItemMessage(hwndDlg,IDC_COMBO1,CB_SETCURSEL,(WPARAM)wd,0);
      SendMessage(hwndDlg,WM_COMMAND,MAKEWPARAM(IDC_COMBO1,CBN_SELCHANGE),0);
    }   
    //delete asioDrivers; // fucko thsi shouldnt be commented
   // asioDrivers=0;
    
    return 1;
  }
  cfg = (asio_config_type *)GetWindowLong(hwndDlg,GWL_USERDATA);
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
            int wd=cfg->asio_input[0];
            if (wd<0||wd>inch) wd=0;
            SendDlgItemMessage(hwndDlg,IDC_COMBO2,CB_SETCURSEL,(WPARAM)wd,0);
            wd=cfg->asio_input[1];
            if (wd<0||wd>inch) wd=0;
            SendDlgItemMessage(hwndDlg,IDC_COMBO4,CB_SETCURSEL,(WPARAM)wd,0);
            wd=cfg->asio_output[0];
            if (wd<0||wd>outch) wd=0;
            SendDlgItemMessage(hwndDlg,IDC_COMBO3,CB_SETCURSEL,(WPARAM)wd,0);
            wd=cfg->asio_output[1];
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
    if (a != CB_ERR) cfg->asio_driver = a;
    a=SendDlgItemMessage(hwndDlg,IDC_COMBO2,CB_GETCURSEL,0,0);
    if (a != CB_ERR) cfg->asio_input[0] = a;
    a=SendDlgItemMessage(hwndDlg,IDC_COMBO4,CB_GETCURSEL,0,0);
    if (a != CB_ERR) cfg->asio_input[1] = a;
    a=SendDlgItemMessage(hwndDlg,IDC_COMBO3,CB_GETCURSEL,0,0);
    if (a != CB_ERR) cfg->asio_output[0] = a;
    a=SendDlgItemMessage(hwndDlg,IDC_COMBO5,CB_GETCURSEL,0,0);
    if (a != CB_ERR) cfg->asio_output[1] = a;
  }
  return 0;
}


extern audioStreamer *create_audioStreamer_ASIO(char **dev, SPLPROC proc);

extern "C"
{

HINSTANCE m_hInst;

BOOL WINAPI DllMain(
  HINSTANCE hinstDLL,  // handle to DLL module
  DWORD fdwReason,     // reason for calling function
  LPVOID lpvReserved   // reserved
)
{
  if (fdwReason == DLL_PROCESS_ATTACH)
  {
    m_hInst = hinstDLL;
  }

  return TRUE;
}

__declspec(dllexport) audioStreamer *create_asio_streamer(char **dev, SPLPROC proc)
{
  return create_audioStreamer_ASIO(dev,proc);
}



__declspec(dllexport) HWND create_asio_configdlg(HWND parent, asio_config_type *cfg)
{
  return CreateDialogParam(m_hInst,MAKEINTRESOURCE(IDD_CFG_ASIO),parent,cfgproc_asio,(LPARAM)cfg);
}

};