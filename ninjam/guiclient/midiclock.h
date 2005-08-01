#ifndef MIDICLOCK_H
#define MIDICLOCK_H

#include <mmsystem.h>
#include <bfc/thread.h>

class MidiClockThread;

class MidiClock {
public:
	MidiClock();
	virtual ~MidiClock();

	void setParentWnd(HWND parent);
  void setEnabled(int enabled);
	int isEnabled();
	void setDevice(const char *device);
	const char *getDevice();
	void start();
	void stop();

  int threadCallback();
  static void fillDeviceList(HWND dlg);
  
  static int s_dlg_device;

private:
  int selectDevice();
  String m_device;
  int m_enabled;
  MidiClockThread *m_clockthread;  
  int m_clock_status;
  int m_errcode;
  HWND m_parentwnd;
  HMIDIOUT m_midiout;
  int m_clockbufferpos;
  double m_clockdelay;
  double m_clock;
};

class MidiClockThread : public Thread {
public:
  MidiClockThread(MidiClock *clock) : m_clock(clock) {}

  virtual int threadProc() { return m_clock->threadCallback(); }

private:
  MidiClock *m_clock;
};

#endif
