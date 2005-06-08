#ifndef _AUDIOSTREAM_H_
#define _AUDIOSTREAM_H_

extern void audiostream_onunder();
extern void audiostream_onover();
extern void audiostream_onsamples(float *buf, int nsamples, int nch);

class audioStreamer
{
	public:
		audioStreamer() { m_srate=48000; m_nch=2; m_bps=16; }
		virtual ~audioStreamer() { }

		virtual int Read(char *buf, int len)=0; // returns 0 if blocked, < 0 if error, > 0 if data
		virtual int Write(char *buf, int len)=0; // returns 0 on success

		int m_srate, m_nch, m_bps;
};


#include </System/Library/Frameworks/CoreAudio.framework/Headers/AudioHardware.h>
		

class audioStreamer_CoreAudio  : public audioStreamer
{
	public:

		audioStreamer_CoreAudio();
		~audioStreamer_CoreAudio();
		int Open(char **dev, int srate, int nch, int bps, int *nbufs, int *bufsize);
		int Read(char *buf, int len); // returns 0 if blocked, < 0 if error, > 0 if data
		int Write(char *buf, int len); // returns 0 on success

	private:

	        AudioDeviceID m_myDev_i;
	        AudioDeviceID m_myDev_o;
 		int m_started;

};

#endif
