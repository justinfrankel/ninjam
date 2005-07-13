#include <windows.h>

#include "chanmix.h"
#include "resource.h"

#define MIN_VOL 0.000001

ChanMixer::ChanMixer()
{
  m_values_used=0;
  m_hwnd=0;
  memset(m_values,0,sizeof(m_values));
}

ChanMixer::~ChanMixer()
{
  if (m_hwnd && IsWindow(m_hwnd)) DestroyWindow(m_hwnd);
}


void ChanMixer::CreateWnd(HINSTANCE hInst, HWND parent)
{
  if (m_hwnd)
  {
    SetParent(m_hwnd,parent);
  }
  else
  {
    m_hwnd=CreateDialog(hInst,MAKEINTRESOURCE(IDD_MIXERDLG),parent,_DlgProc);
    // create dialog
  }
}

void ChanMixer::LoadConfig(const char *str)
{
}

void ChanMixer::SaveConfig(WDL_String *str)
{
}

void ChanMixer::MixData(float **inbuf, int in_offset, int innch, int chidx, float *outbuf, int len)
{
  memset(outbuf,0,len*sizeof(float));
  if (m_values_used < innch) innch=m_values_used;
  int ch;
  for (ch = 0; ch < innch; ch ++)
  {
    if (m_values[ch] >= MIN_VOL) // -100dB or so
    {
      int l=len;
      float *in=inbuf[ch]+in_offset;
      float *out=outbuf;
      while (l--) *out++ += *in++;
    }
  }
}

BOOL WINAPI ChanMixer::_DlgProc(HWND hwndDlg, UINT uMsg, WPARAM wParam, LPARAM lParam)
{
  ChanMixer *t;
  if (uMsg == WM_INITDIALOG)
  {
    SetWindowLong(hwndDlg,GWL_USERDATA,(LONG)(t=(ChanMixer *)lParam));
  }
  else t=(ChanMixer*)GetWindowLong(hwndDlg,GWL_USERDATA);

  return t?t->DlgProc(hwndDlg,uMsg,wParam,lParam):0;
}

BOOL ChanMixer::DlgProc(HWND hwndDlg, UINT uMsg, WPARAM wParam, LPARAM lParam)
{
  return 0;
}