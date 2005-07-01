#include <windows.h>
#include <stdio.h>
#include "../WDL/string.h"
#include "resource.h"


#include "audiostream_asio.h"

struct
{
   int mode;
   int asio_driver;
   int asio_input[2];
   int asio_output[2];

   int ks_srate;
   int ks_bps;
   int ks_device[2];
   int ks_blocksize;
   int ks_numblocks;
   int ks_high;

} configdata={
  0,
   0, //asio_driver;
   {0,1}, //asio_input;
   {0,1}, //asio_output;

   48000, //ks_srate;
   16, //ks_bps;
   {-1,-1}, //ks_device;
   512, //ks_blocksize;
   8, // ks_numblocks;
   1, // ks_high

};


static BOOL CALLBACK configDlgMainProc( HWND hwndDlg, UINT uMsg, WPARAM wParam, LPARAM lParam);

static WDL_String m_inifile;
static int load_config()
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

static void save_config()
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


audioStreamer *CreateConfiguredStreamer(char *inifile, int showcfg, HWND hwndParent)
{
  m_inifile.Set(inifile);
  load_config();
  if (showcfg)
  {
    DialogBox(GetModuleHandle(NULL),MAKEINTRESOURCE(IDD_CONFIG),hwndParent,configDlgMainProc);
    save_config();
  }

  if (configdata.mode == 0)
  {
      static char tmpbuf[64];
      wsprintf(tmpbuf,"%d:%d,%d:%d,%d",configdata.asio_driver,
      configdata.asio_input[0],
      configdata.asio_input[1],
      configdata.asio_output[0],
      configdata.asio_output[1]
      );

      audioStreamer_ASIO *audio=new audioStreamer_ASIO;

      char *dev_name_in=tmpbuf;
      if (!audio->Open(&dev_name_in)) return audio;
      delete audio;
  }
  else if (configdata.mode == 1)
  {
    int nbufs=configdata.ks_numblocks;
    int bufsize=configdata.ks_blocksize;
    audioStreamer *p=create_audioStreamer_KS(configdata.ks_srate, configdata.ks_bps, &nbufs, &bufsize);

    if (p && configdata.ks_high)
    {
      SetPriorityClass(GetCurrentProcess(),HIGH_PRIORITY_CLASS);
    }

    return p;
  }

  return 0;
}



#include "asio/asiosys.h"
#include "asio/asio.h"
#include "asio/asiodrivers.h"

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

BOOL CALLBACK cfgproc_ks( HWND hwndDlg, UINT uMsg, WPARAM wParam, LPARAM lParam)
{
  if (uMsg == WM_INITDIALOG)
  {
    SendDlgItemMessage(hwndDlg,IDC_COMBO1,CB_ADDSTRING,0,(LPARAM)"(default device (todo))");
    SendDlgItemMessage(hwndDlg,IDC_COMBO2,CB_ADDSTRING,0,(LPARAM)"(default device (todo))");
    SendDlgItemMessage(hwndDlg,IDC_COMBO1,CB_SETCURSEL,(WPARAM)configdata.ks_device[0]+1,0);
    SendDlgItemMessage(hwndDlg,IDC_COMBO2,CB_SETCURSEL,(WPARAM)configdata.ks_device[1]+1,0);

    CheckDlgButton(hwndDlg,configdata.ks_bps==24?IDC_RADIO4:
                           configdata.ks_bps==32?IDC_RADIO5:IDC_RADIO3,BST_CHECKED);
    SetDlgItemInt(hwndDlg,IDC_EDIT1,configdata.ks_srate,FALSE);
    SetDlgItemInt(hwndDlg,IDC_EDIT2,configdata.ks_numblocks,FALSE);
    SetDlgItemInt(hwndDlg,IDC_EDIT3,configdata.ks_blocksize,FALSE);
    CheckDlgButton(hwndDlg,IDC_CHECK1,configdata.ks_high?BST_CHECKED:0);

    SendMessage(hwndDlg,WM_USER+0x1001,0,0);
    return 1;
  }
  if ((uMsg == WM_COMMAND && (
       LOWORD(wParam) == IDC_RADIO3 ||
       LOWORD(wParam) == IDC_RADIO4 ||
       LOWORD(wParam) == IDC_RADIO5 ||
       LOWORD(wParam) == IDC_EDIT1 ||
       LOWORD(wParam) == IDC_EDIT2 ||
       LOWORD(wParam) == IDC_EDIT3)) || uMsg == WM_USER+0x1001)
  {
    char buf[512];
    BOOL t;
    int bps=IsDlgButtonChecked(hwndDlg,IDC_RADIO4)?24:IsDlgButtonChecked(hwndDlg,IDC_RADIO5)?32:16;
    int srate=GetDlgItemInt(hwndDlg,IDC_EDIT1,&t,0);
    int nb=GetDlgItemInt(hwndDlg,IDC_EDIT2,&t,0);
    int bs=GetDlgItemInt(hwndDlg,IDC_EDIT3,&t,0);

    int bytesec=srate*2*(bps/8);
    
    if (bytesec)
      wsprintf(buf,"(latency: %d ms)", (nb*bs*1000)/bytesec);
    else buf[0]=0;

    SetDlgItemText(hwndDlg,IDC_LATENCYINFO,buf);
  }
  //if (uMsg == WM_COMMAND && LOWORD(wParam) == IDC_COMBO1 && HIWORD(wParam) == CBN_SELCHANGE)
  //{
  //}
  if (uMsg == WM_COMMAND && LOWORD(wParam) == IDOK)
  {
    int a=SendDlgItemMessage(hwndDlg,IDC_COMBO1,CB_GETCURSEL,0,0);
    if (a != CB_ERR) configdata.ks_device[0]=a-1;
    a=SendDlgItemMessage(hwndDlg,IDC_COMBO2,CB_GETCURSEL,0,0);
    if (a != CB_ERR) configdata.ks_device[1]=a-1;

    BOOL t;
    configdata.ks_bps=IsDlgButtonChecked(hwndDlg,IDC_RADIO4)?24:IsDlgButtonChecked(hwndDlg,IDC_RADIO5)?32:16;
    a=GetDlgItemInt(hwndDlg,IDC_EDIT1,&t,0);
    if (t) configdata.ks_srate=a;

    a=GetDlgItemInt(hwndDlg,IDC_EDIT2,&t,0);
    if (t) configdata.ks_numblocks=a;

    a=GetDlgItemInt(hwndDlg,IDC_EDIT3,&t,0);
    if (t) configdata.ks_blocksize=a;
  
    configdata.ks_high = !!IsDlgButtonChecked(hwndDlg,IDC_CHECK1);     
  }
  return 0;
}



BOOL CALLBACK configDlgMainProc( HWND hwndDlg, UINT uMsg, WPARAM wParam, LPARAM lParam)
{
  static const int dlgids[2]={IDD_CFG_ASIO,IDD_CFG_KS};
  static const DLGPROC procs[2]={cfgproc_asio,cfgproc_ks};
  static HWND children[2];
  static const char *labels[2] = { "ASIO", "Kernel Streaming" };

  switch (uMsg)
  {
    case WM_INITDIALOG:
      {
        int x;
        for (x = 0; x < 2; x ++)
        {
            SendDlgItemMessage(hwndDlg,IDC_COMBO1,CB_ADDSTRING,0,(LPARAM)labels[x]);
        }
        int cm=configdata.mode;
        if (cm  < 0 || cm > 1) cm=0;
        SendDlgItemMessage(hwndDlg,IDC_COMBO1,CB_SETCURSEL,(WPARAM)cm,0);
        SendMessage(hwndDlg,WM_COMMAND,MAKEWPARAM(IDC_COMBO1,CBN_SELCHANGE),0);
      }
    return TRUE;
    case WM_DESTROY:
      {
        int x;
        for (x = 0; x < 2; x ++) 
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
              for (x = 0; x < 2; x ++) 
              {
                if (children[x]) ShowWindow(children[x],x==y?SW_SHOWNA:SW_HIDE);
              }
            }
          }
        return 0;
        case IDOK:
          {
            int y;
            for (y = 0; y < 2; y ++)
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
