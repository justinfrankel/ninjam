#ifndef _CHANMIX_H_
#define _CHANMIX_H_


#include "../WDL/string.h"

#define MAX_CHANMIX_CHANS 64

class ChanMixer
{
  public:
    ChanMixer();
    ~ChanMixer();

    void CreateWnd(HINSTANCE hInst, HWND parent);
    HWND GetWnd() { return m_hwnd; }

    void SetNCH(int nch) { if (m_values_used < nch) m_values_used=nch; }
    int GetNCH() { return m_values_used; }

    void LoadConfig(const char *str);
    void SaveConfig(WDL_String *str);

    void MixData(float **inbuf, int in_offset, int innch, int chidx, float *outbuf, int len);


  private:

    static BOOL WINAPI _DlgProc(HWND hwndDlg, UINT uMsg, WPARAM wParam, LPARAM lParam);
    BOOL DlgProc(HWND hwndDlg, UINT uMsg, WPARAM wParam, LPARAM lParam);

    static BOOL WINAPI _DlgProc_scrollchild(HWND hwndDlg, UINT uMsg, WPARAM wParam, LPARAM lParam);
    BOOL DlgProc_scrollchild(HWND hwndDlg, UINT uMsg, WPARAM wParam, LPARAM lParam);

    static BOOL WINAPI _DlgProc_item(HWND hwndDlg, UINT uMsg, WPARAM wParam, LPARAM lParam);

    float m_values[MAX_CHANMIX_CHANS];
    HWND m_sliders[MAX_CHANMIX_CHANS];
    int m_values_used;
    
    HWND m_hwnd;
    HWND m_childwnd;
    int m_nScrollPos_w,m_ww,m_w,m_maxpos_w;
};



#endif