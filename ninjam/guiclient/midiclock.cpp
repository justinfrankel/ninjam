#include <precomp.h>
#include "midiclock.h"
#include "../njclient.h"
#include "resource.h"

#define MIDICLOCK_DEVICE_WAITING -1
#define MIDICLOCK_DEVICE_OK 0
#define MIDICLOCK_DEVICE_ERROR 1

#define MIDICLOCK_STATUS_STOPPED 0
#define MIDICLOCK_STATUS_STARTED 1
#define MIDICLOCK_STATUS_STOPASSOONASPOSSIBLE 2
#define MIDICLOCK_STATUS_STARTATNEXTMEASURE 3

#define MIDI_CLOCK 0xF8
#define MIDI_CONTINUE 0xFB
#define MIDI_START 0xFA
#define MIDI_STOP 0xFC

int MidiClock::s_dlg_device = -1;

MidiClock::MidiClock() : m_parentwnd(NULL), 
                         m_clockthread(NULL), 
                         m_enabled(0), 
                         m_clock_status(MIDICLOCK_STATUS_STOPPED) {
}


MidiClock::~MidiClock() {
  if (m_clockthread) {
    m_clockthread->setKillSwitch();
    m_clockthread->end();
  }
  delete m_clockthread;
  m_clockthread = NULL;
}

void MidiClock::setParentWnd(HWND parent) {
  m_parentwnd = parent;
}

void MidiClock::setEnabled(int enabled) {
  if (m_enabled == enabled) return;
  m_enabled = enabled;
  if (m_enabled) {
    if (m_clockthread) {
      m_clockthread->setKillSwitch();
      m_clockthread->end();
      delete m_clockthread;
      m_clockthread = NULL;
    }
    if (m_device.isempty()) {
      if (!selectDevice()) {
        m_enabled = 0;
        return;
      }
    }
    m_clockthread = new MidiClockThread(this);
    m_errcode = MIDICLOCK_DEVICE_WAITING;
    m_clockthread->start();
    while (m_errcode == MIDICLOCK_DEVICE_WAITING) {
      Sleep(1);
    }
    if (m_errcode != MIDICLOCK_DEVICE_OK) {
      MessageBox(m_parentwnd, StringPrintf("Could not open device %s", m_device.getValue()), "MIDI Error", 0);
      m_enabled = 0;
      // this forces a device selection when midi clock is enabled, remove this line if you are saving the output 
      // midi device from session to session and if you have added some UI to change to a new output device
      m_device = ""; 
      return;
    }
  } else {
    if (m_clockthread) {
      m_clockthread->setKillSwitch();
      m_clockthread->end();
      delete m_clockthread;
      m_clockthread = NULL;
    }
    // this forces a device selection when midi clock is enabled, remove this line if you are saving the output 
    // midi device from session to session and if you have added some UI to change to a new output device
    m_device = "";
  }
}

int MidiClock::isEnabled() {
  return m_enabled;
}

void MidiClock::setDevice(const char *device) {
  if (m_device.iscaseequal(device)) return;
  int wasenabled = m_enabled;
  if (wasenabled) setEnabled(0); // kill thread
  m_device = device;
  if (wasenabled) setEnabled(1); // start thread again
}

const char *MidiClock::getDevice() {
  return m_device;
}

void MidiClock::start() {
  m_clock_status = MIDICLOCK_STATUS_STARTATNEXTMEASURE;
}

void MidiClock::stop() {
  m_clock_status = MIDICLOCK_STATUS_STOPASSOONASPOSSIBLE;
}

void MidiClock::fillDeviceList(HWND dlg) {
  int ndev = midiOutGetNumDevs();
  for (int i=0;i<ndev;i++) {  
    MIDIOUTCAPS moc;
    midiOutGetDevCaps(i, &moc, sizeof(moc));
    SendDlgItemMessage(dlg, IDC_MIDIOUT_LIST, LB_ADDSTRING, NULL, (LPARAM)moc.szPname);
  }
}

BOOL CALLBACK midiclock_device_wndproc(HWND hwndDlg, UINT uMsg, WPARAM wParam, LPARAM lParam) {
  switch(uMsg) {
    case WM_INITDIALOG:
      MidiClock::fillDeviceList(hwndDlg);
      return 1;
      break;
    case WM_COMMAND: {
      switch(LOWORD(wParam)) {
        case IDOK: {
          MidiClock::s_dlg_device = SendDlgItemMessage(hwndDlg, IDC_MIDIOUT_LIST, LB_GETCURSEL, 0, 0);
          EndDialog(hwndDlg, IDOK); 
          break;
        }
        case IDCANCEL: {
          MidiClock::s_dlg_device = -1;
          EndDialog(hwndDlg, IDCANCEL); 
          break;
        }
      }
    }
    break;
  }
  return 0;
}
 


int MidiClock::selectDevice() {
  extern OSMODULEHANDLE wasabiAppOSModuleHandle; // WinMain fills in
  int ret = DialogBox(wasabiAppOSModuleHandle, MAKEINTRESOURCE(IDD_MIDICLOCK_DEVICE), m_parentwnd, midiclock_device_wndproc);
  if (ret == IDOK) {
    MIDIOUTCAPS moc;
    midiOutGetDevCaps(s_dlg_device, &moc, sizeof(moc));
    m_device = moc.szPname;
    return 1;
  }
  return 0;
}

int MidiClock::threadCallback() {

  // open midi output device
  int ndev = midiOutGetNumDevs();
  int dev = -1;
  for (int i=0;i<ndev;i++) {  
    MIDIOUTCAPS moc;
    midiOutGetDevCaps(i, &moc, sizeof(moc));
    if (STRCASEEQL(moc.szPname, m_device.getValue())) { dev = i; break; }
  }

  if (dev == -1) {
    m_errcode = MIDICLOCK_DEVICE_ERROR;
    return 0;
  }

  if (midiOutOpen(&m_midiout, dev, NULL, NULL, CALLBACK_NULL) != MMSYSERR_NOERROR) {
    m_errcode = MIDICLOCK_DEVICE_ERROR;
    return 0;
  }

  m_clockbufferpos = 0;

  m_errcode = MIDICLOCK_DEVICE_OK;

  extern NJClient *g_client;

  while (!m_clockthread->getKillSwitch()) {
    switch(m_clock_status) {
      case MIDICLOCK_STATUS_STOPPED:
        // nothing to do, so sleep more
        Sleep(20);
        break;
      case MIDICLOCK_STATUS_STARTED: {
        // send clock signal if needed, send multiple ones if we missed any
        DWORD now = timeGetTime();
        int n=0;
        while (m_clock + m_clockdelay < now) {
          n++;
          midiOutShortMsg(m_midiout, MIDI_CLOCK);
          m_clock += m_clockdelay;
        }
        break;
      }
      case MIDICLOCK_STATUS_STARTATNEXTMEASURE:
        // if we just passed the beginning of a measure, start the clock, otherwise, do nothing
        // but do not sleep more, as we should try to send the start signal as close to the beginning
        // of the measure as possible
        if (g_client != NULL) {
          int curpos;
          g_client->GetPosition(&curpos, NULL);
          if (curpos < m_clockbufferpos) {
            // new measure!
            midiOutShortMsg(m_midiout, MIDI_START);
            m_clock_status = MIDICLOCK_STATUS_STARTED;
            int clockpermin = 24 * g_client->GetActualBPM(); // there are 24 midi clocks per beat
            m_clockdelay = 60000.0f/(float)clockpermin;
            m_clock = timeGetTime();
          }
        }
        break;
      case MIDICLOCK_STATUS_STOPASSOONASPOSSIBLE:
        // stop right now
        midiOutShortMsg(m_midiout, MIDI_STOP);
        m_clock_status = MIDICLOCK_STATUS_STOPPED;
        break;
    }
    if (g_client != NULL) {
      g_client->GetPosition(&m_clockbufferpos, NULL);
    }
    Sleep(1);
  }

  // stop slave devices if needed
  if (m_clock_status == MIDICLOCK_STATUS_STARTED) {
    midiOutShortMsg(m_midiout, MIDI_STOP);
    m_clock_status = MIDICLOCK_STATUS_STOPPED;
  }

  // close midi output device
  midiOutClose(m_midiout);

  return 1;
}


