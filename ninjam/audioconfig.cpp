#include <windows.h>
#include <stdio.h>
#include <dsound.h>
#include "../WDL/string.h"
#include "../WDL/ptrlist.h"
#include "resource.h"


#include "audiostream.h"

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

   int waveout_srate; 
   int waveout_bps;
   int waveout_device[2];
   int waveout_blocksize;
   int waveout_numblocks;

   int dsound_srate;
   int dsound_bps;
   int dsound_device[2][4];
   int dsound_blocksize;
   int dsound_numblocks;
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

   44100, //waveout_srate; 
   16, //waveout_bps;
   {-1,-1}, //waveout_device;
   4096, //waveout_blocksize;
   8, //waveout_numblocks;

   44100, //dsound_srate;
   16, //dsound_bps;
   {{0,0,0,0},{0,0,0,0}}, //dsound_device;
   1024, //dsound_blocksize;
   16, //dsound_numblocks;
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
  extern void audiostream_onsamples(float **inbuf, int innch, float **outbuf, int outnch, int len, int srate) ;

  m_inifile.Set(inifile);
  load_config();
  if (showcfg)
  {
    DialogBox(GetModuleHandle(NULL),MAKEINTRESOURCE(IDD_CONFIG),hwndParent,configDlgMainProc);
    save_config();
    if (showcfg == -1) return NULL;
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

      char *dev_name_in=tmpbuf;
      return create_audioStreamer_ASIO(&dev_name_in,audiostream_onsamples);
  }
  else if (configdata.mode == 1)
  {
    int nbufs=configdata.ks_numblocks;
    int bufsize=configdata.ks_blocksize;
    audioStreamer *p=create_audioStreamer_KS(configdata.ks_srate, configdata.ks_bps, &nbufs, &bufsize,audiostream_onsamples);

    return p;
  }
  else if (configdata.mode == 2)
  {
    GUID bla[2];
    int nbufs=configdata.dsound_numblocks;
    int bufsize=configdata.dsound_blocksize;
    memcpy(bla,configdata.dsound_device,sizeof(bla));
    return create_audioStreamer_DS(configdata.dsound_srate,configdata.dsound_bps,bla,&nbufs,&bufsize,audiostream_onsamples);
  }
  else if (configdata.mode == 3)
  {
    int nbufs=configdata.waveout_numblocks;
    int bufsize=configdata.waveout_blocksize;
    return create_audioStreamer_WO(configdata.waveout_srate,configdata.waveout_bps,configdata.waveout_device,&nbufs,&bufsize,audiostream_onsamples);
  }

  return 0;
}




BOOL CALLBACK cfgproc_waveout( HWND hwndDlg, UINT uMsg, WPARAM wParam, LPARAM lParam)
{
  if (uMsg == WM_INITDIALOG)
  {
    int d,n=waveOutGetNumDevs();
    for (d = -1; d < n; d ++)
    {
      WAVEOUTCAPS c;
      MMRESULT r=waveOutGetDevCaps(d,&c,sizeof(c));
      if (r == MMSYSERR_NOERROR )
      {
        SendDlgItemMessage(hwndDlg,IDC_COMBO1,CB_ADDSTRING,0,(LPARAM)c.szPname);
        SendDlgItemMessage(hwndDlg,IDC_COMBO2,CB_ADDSTRING,0,(LPARAM)c.szPname);
      }
      else
      {
        SendDlgItemMessage(hwndDlg,IDC_COMBO1,CB_ADDSTRING,0,(LPARAM)"(error getting info)");
        SendDlgItemMessage(hwndDlg,IDC_COMBO2,CB_ADDSTRING,0,(LPARAM)"(error getting info)");
      }
    }
    SendDlgItemMessage(hwndDlg,IDC_COMBO1,CB_SETCURSEL,(WPARAM)configdata.waveout_device[0]+1,0);
    SendDlgItemMessage(hwndDlg,IDC_COMBO2,CB_SETCURSEL,(WPARAM)configdata.waveout_device[1]+1,0);
//    SendMessage(hwndDlg,WM_COMMAND,MAKEWPARAM(IDC_COMBO1,CBN_SELCHANGE),0); // update caps

    CheckDlgButton(hwndDlg,configdata.waveout_bps==24?IDC_RADIO4:
                           configdata.waveout_bps==32?IDC_RADIO5:IDC_RADIO3,BST_CHECKED);
    SetDlgItemInt(hwndDlg,IDC_EDIT1,configdata.waveout_srate,FALSE);
    SetDlgItemInt(hwndDlg,IDC_EDIT2,configdata.waveout_numblocks,FALSE);
    SetDlgItemInt(hwndDlg,IDC_EDIT3,configdata.waveout_blocksize,FALSE);

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
    if (a != CB_ERR) configdata.waveout_device[0]=a-1;
    a=SendDlgItemMessage(hwndDlg,IDC_COMBO2,CB_GETCURSEL,0,0);
    if (a != CB_ERR) configdata.waveout_device[1]=a-1;

    BOOL t;
    configdata.waveout_bps=IsDlgButtonChecked(hwndDlg,IDC_RADIO4)?24:IsDlgButtonChecked(hwndDlg,IDC_RADIO5)?32:16;
    a=GetDlgItemInt(hwndDlg,IDC_EDIT1,&t,0);
    if (t) configdata.waveout_srate=a;

    a=GetDlgItemInt(hwndDlg,IDC_EDIT2,&t,0);
    if (t) configdata.waveout_numblocks=a;

    a=GetDlgItemInt(hwndDlg,IDC_EDIT3,&t,0);
    if (t) configdata.waveout_blocksize=a;
  
    
  
  }
  return 0;
}



BOOL WINAPI dscb(
  LPGUID lpGuid,            
  LPCSTR lpcstrDescription,  
  LPCSTR lpcstrModule,       
  LPVOID lpContext          
)
{
  SendMessage((HWND)lpContext,WM_USER+0x100,666,(LPARAM)lpGuid);

  SendDlgItemMessage((HWND)lpContext,IDC_COMBO1,CB_ADDSTRING,0,(LPARAM)lpcstrDescription);
  SendDlgItemMessage((HWND)lpContext,IDC_COMBO2,CB_ADDSTRING,0,(LPARAM)lpcstrDescription);
  return TRUE;
}


BOOL CALLBACK cfgproc_dsound( HWND hwndDlg, UINT uMsg, WPARAM wParam, LPARAM lParam)
{
  static WDL_PtrList<GUID> deviceGuids;

  if (uMsg == WM_USER+0x100 && wParam == 666)
  {
    GUID *p=(GUID *)calloc(1,sizeof(GUID));
    if (lParam)
    {
      *p=*(GUID *)lParam;
    }
    deviceGuids.Add(p);
    return 0;
  }
  if (uMsg == WM_INITDIALOG)
  {
    int x;
    for (x = 0; x < deviceGuids.GetSize(); x ++)
    {
      free(deviceGuids.Get(x));
    }
    deviceGuids.Empty();
    DirectSoundEnumerate(dscb,hwndDlg);
    int s1=0,s2=0;
    for (x = 0; x < deviceGuids.GetSize(); x ++)
    {
      if (!s1 && !memcmp(deviceGuids.Get(x),configdata.dsound_device[0],sizeof(GUID)))
        s1++,SendDlgItemMessage(hwndDlg,IDC_COMBO1,CB_SETCURSEL,(WPARAM)x,0);
      if (!s2 && !memcmp(deviceGuids.Get(x),configdata.dsound_device[1],sizeof(GUID)))
        s2++,SendDlgItemMessage(hwndDlg,IDC_COMBO2,CB_SETCURSEL,(WPARAM)x,0);
    }
    if (!s1)
      SendDlgItemMessage(hwndDlg,IDC_COMBO1,CB_SETCURSEL,(WPARAM)0,0);
    if (!s2)
      SendDlgItemMessage(hwndDlg,IDC_COMBO2,CB_SETCURSEL,(WPARAM)0,0);

//    SendMessage(hwndDlg,WM_COMMAND,MAKEWPARAM(IDC_COMBO1,CBN_SELCHANGE),0); // update caps

    CheckDlgButton(hwndDlg,configdata.dsound_bps==24?IDC_RADIO4:
                           configdata.dsound_bps==32?IDC_RADIO5:IDC_RADIO3,BST_CHECKED);
    SetDlgItemInt(hwndDlg,IDC_EDIT1,configdata.dsound_srate,FALSE);
    SetDlgItemInt(hwndDlg,IDC_EDIT2,configdata.dsound_numblocks,FALSE);
    SetDlgItemInt(hwndDlg,IDC_EDIT3,configdata.dsound_blocksize,FALSE);

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
    if (a >= 0 && a < deviceGuids.GetSize()) memcpy(configdata.dsound_device[0],deviceGuids.Get(a),sizeof(GUID));
    a=SendDlgItemMessage(hwndDlg,IDC_COMBO2,CB_GETCURSEL,0,0);
    if (a >= 0 && a < deviceGuids.GetSize()) memcpy(configdata.dsound_device[1],deviceGuids.Get(a),sizeof(GUID));

    BOOL t;
    configdata.dsound_bps=IsDlgButtonChecked(hwndDlg,IDC_RADIO4)?24:IsDlgButtonChecked(hwndDlg,IDC_RADIO5)?32:16;
    a=GetDlgItemInt(hwndDlg,IDC_EDIT1,&t,0);
    if (t) configdata.dsound_srate=a;

    a=GetDlgItemInt(hwndDlg,IDC_EDIT2,&t,0);
    if (t) configdata.dsound_numblocks=a;

    a=GetDlgItemInt(hwndDlg,IDC_EDIT3,&t,0);
    if (t) configdata.dsound_blocksize=a;
      
  
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
  
  }
  return 0;
}


#define NUM_ITEMS 4

BOOL CALLBACK configDlgMainProc( HWND hwndDlg, UINT uMsg, WPARAM wParam, LPARAM lParam)
{
  static const int dlgids[4]={IDD_CFG_ASIO,IDD_CFG_KS, IDD_CFG_WAVEOUT,IDD_CFG_WAVEOUT};
  static const DLGPROC procs[4]={cfgproc_asio,cfgproc_ks,cfgproc_dsound,cfgproc_waveout};
  static HWND children[4];
  static const char *labels[4] = { "ASIO", "Kernel Streaming", "DirectSound", "WaveOut" };

  switch (uMsg)
  {
    case WM_INITDIALOG:
      {
        int x;
        for (x = 0; x < NUM_ITEMS; x ++)
        {
            SendDlgItemMessage(hwndDlg,IDC_COMBO1,CB_ADDSTRING,0,(LPARAM)labels[x]);
        }
        int cm=configdata.mode;
        if (cm  < 0 || cm >= NUM_ITEMS) cm=0;
        SendDlgItemMessage(hwndDlg,IDC_COMBO1,CB_SETCURSEL,(WPARAM)cm,0);
        SendMessage(hwndDlg,WM_COMMAND,MAKEWPARAM(IDC_COMBO1,CBN_SELCHANGE),0);
      }
    return TRUE;
    case WM_DESTROY:
      {
        int x;
        for (x = 0; x < NUM_ITEMS; x ++) 
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
              for (x = 0; x < NUM_ITEMS; x ++) 
              {
                if (children[x]) ShowWindow(children[x],x==y?SW_SHOWNA:SW_HIDE);
              }
            }
          }
        return 0;
        case IDOK:
          {
            int y;
            for (y = 0; y < NUM_ITEMS; y ++)
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
