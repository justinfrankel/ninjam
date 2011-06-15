/*
    NINJAM - chanmix.h
    Copyright (C) 2004-2005 Cockos Incorporated

    NINJAM is free software; you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation; either version 2 of the License, or
    (at your option) any later version.

    NINJAM is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU General Public License for more details.

    You should have received a copy of the GNU General Public License
    along with NINJAM; if not, write to the Free Software
    Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
*/

/*

  This file defines the interface to a class that the Windows client uses to mixdown
  multiple input channels into one channel for streaming. This class handles the UI 
  for the mixer and stores its state, creating a dialog windows when necessary, as 
  well. It has a method that the windows client will call to actually do the mixdown,
  MixData().

*/


#ifndef _CHANMIX_H_
#define _CHANMIX_H_


#include "../WDL/wdlstring.h"

#define MAX_CHANMIX_CHANS 64

class ChanMixer
{
  public:
    ChanMixer();
    ~ChanMixer();

    void CreateWnd(HINSTANCE hInst, HWND parent);
    HWND GetWnd() { return m_hwnd; }

    void SetDesc(const char *desc)
    {
      m_desc.Set(desc?desc:"");
      UpdateTitle();
    }

    void SetNCH(int nch) 
    { 
      if (nch < 0) nch=0;
      else if (nch > MAX_CHANMIX_CHANS) nch=MAX_CHANMIX_CHANS;
      m_values_used=nch;  
    }
    void SetCHName(int ch, const char *name) 
    { 
      if (ch >= 0 && ch < MAX_CHANMIX_CHANS) 
        m_chname[ch].Set(name);
    }
    void DoWndUpdate()
    {
      if (m_hwnd) PostMessage(m_hwnd,WM_USER+32,0,0);  
    }
    int GetNCH() { return m_values_used; }

    void LoadConfig(const char *str);
    void SaveConfig(WDL_String *str);

    void MixData(float **inbuf, int in_offset, int innch, float *outbuf, int len);


  private:

    void UpdateTitle()
    {
      if (m_hwnd)
      {
        WDL_String a("NINJAM Channel Mixer: ");
        a.Append(m_desc.Get());
        SetWindowText(m_hwnd,a.Get());
      }
      
    }

    WDL_String m_desc;

    static BOOL WINAPI _DlgProc(HWND hwndDlg, UINT uMsg, WPARAM wParam, LPARAM lParam);
    BOOL DlgProc(HWND hwndDlg, UINT uMsg, WPARAM wParam, LPARAM lParam);

    static BOOL WINAPI _DlgProc_scrollchild(HWND hwndDlg, UINT uMsg, WPARAM wParam, LPARAM lParam);
    BOOL DlgProc_scrollchild(HWND hwndDlg, UINT uMsg, WPARAM wParam, LPARAM lParam);

    static BOOL WINAPI _DlgProc_item(HWND hwndDlg, UINT uMsg, WPARAM wParam, LPARAM lParam);

    float m_values[MAX_CHANMIX_CHANS];
    HWND m_sliders[MAX_CHANMIX_CHANS];
    WDL_String m_chname[MAX_CHANMIX_CHANS];
    int m_values_used;
    
    HWND m_hwnd;
    HWND m_childwnd;
    int m_nScrollPos_w,m_ww,m_w,m_maxpos_w;
};



#endif