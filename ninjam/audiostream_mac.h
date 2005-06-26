#ifndef _AUDIOSTREAM_H_
#define _AUDIOSTREAM_H_

extern void audiostream_onunder();
extern void audiostream_onover();
extern void audiostream_onsamples(float **inbuf, int innch, float **outbuf, int outnch, int len, int srate);

class audioStreamer
{
	public:
		audioStreamer() { m_srate=48000; m_innch=m_outnch=2; m_bps=16; }
		virtual ~audioStreamer() { }

		virtual int Read(char *buf, int len)=0; // returns 0 if blocked, < 0 if error, > 0 if data
		virtual int Write(char *buf, int len)=0; // returns 0 on success
    virtual const char *GetChannelName(int idx)=0;


		int m_srate, m_innch, m_outnch, m_bps;
};


#include </System/Library/Frameworks/CoreAudio.framework/Headers/AudioHardware.h>
		

class audioStreamer_CoreAudio  : public audioStreamer
{
	public:

		audioStreamer_CoreAudio();
		~audioStreamer_CoreAudio();
		int Open(char **dev, int srate, int nch, int bps);
		int Read(char *buf, int len); // returns 0 if blocked, < 0 if error, > 0 if data
		int Write(char *buf, int len); // returns 0 on success
    const char *GetChannelName(int idx)
	{
		if (idx < 0 || idx >= m_innch) return NULL;
		static char buf[128];
		sprintf(buf,"Channel %d",idx+1);
		return buf;
	}


	private:

	        AudioDeviceID m_myDev_i;
	        AudioDeviceID m_myDev_o;
 		int m_started;

};

#endif
