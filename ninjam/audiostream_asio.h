#ifndef _AUDIOSTREAM_H_
#define _AUDIOSTREAM_H_


#define SUPPORT_ASIO

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

#ifdef _MAC

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

#else

#ifdef _WIN32

#include "../WDL/ptrlist.h"

#ifdef SUPPORT_ASIO
class audioStreamer_ASIO  : public audioStreamer
{
	public:
		audioStreamer_ASIO();
		~audioStreamer_ASIO();
		int Open(char **dev, int srate, int nch, int bps, int sleep, int *nbufs, int *bufsize);
		int Read(char *buf, int len); // returns 0 if blocked, < 0 if error, > 0 if data
		int Write(char *buf, int len); // returns 0 on success

	private:

    int m_driver_active;
	
		int m_sleep;
		int m_bufsize;
};

#endif //SUPPORT_ASIO

#ifdef SUPPORT_KS

audioStreamer *create_audioStreamer_KS(int iswrite, char **dev, int srate, int nch, int bps, int sleep, int *nbufs, int *bufsize);

#endif //SUPPORT_KS

class audioStreamer_waveOut  : public audioStreamer
{
	public:
		audioStreamer_waveOut();
		~audioStreamer_waveOut();
		int Open(int iswrite, int srate, int nch, int bps, int sleep, int nbufs, int bufsize, int device=-1);
		int Read(char *buf, int len); // returns 0 if blocked, < 0 if error, > 0 if data
		int Write(char *buf, int len); // returns 0 on success

	private:
	
		int m_sleep;
		int m_bufsize;

		HWAVEOUT m_hwo; 
		HWAVEIN m_hwi;
    
		WDL_PtrList<WDL_HeapBuf> m_bufs; // includes WAVEHDR and buffer each
   
		int m_whichbuf; // used only for read mode
};

#include "dsound.h"

class audioStreamer_ds : public audioStreamer
{
	public:
		audioStreamer_ds();
		~audioStreamer_ds();
		int Open(int iswrite, int srate, int nch, int bps, int sleep, int nbufs, int bufsize, GUID *device=NULL);// guid anyway for device
		int Read(char *buf, int len); // returns 0 if blocked, < 0 if error, > 0 if data
		int Write(char *buf, int len); // returns 0 on success

	private:
	
    LPDIRECTSOUND m_lpds;
    LPDIRECTSOUNDCAPTURE m_lpcap;
    LPDIRECTSOUNDBUFFER m_outbuf;
    LPDIRECTSOUNDCAPTUREBUFFER m_inbuf;

		int m_has_started;
    int m_bufpos;
    int m_last_pos;
    unsigned int m_i_lw, m_i_dw;

    int m_totalbufsize;
		int m_sleep;
		int m_bufsize;

    // fucko: finish dsound implementation
};

#else

#include <alsa/asoundlib.h>


class audioStreamer_ALSA : public audioStreamer
{
	public:
		audioStreamer_ALSA();
		~audioStreamer_ALSA();
		int Open(char *devname, int is_write, int srate, int nch, int bps, int fragsize, int nfrags, int dosleep);

		int Read(char *buf, int len); // returns 0 if blocked, < 0 if error, > 0 if data
		int Write(char *buf, int len); // returns 0 on success
	private:
	snd_pcm_t *pcm_handle;
	int m_sleep;
	int m_bufsize;
	int m_nfrags;
	int m_started;
};

class audioStreamer_OSS  : public audioStreamer
{
	public:
		audioStreamer_OSS();
		~audioStreamer_OSS();
		int Open(char *dev, int perm, int srate, int nch, int bps, int sleep);

		int Read(char *buf, int len); // returns 0 if blocked, < 0 if error, > 0 if data
		int Write(char *buf, int len); // returns 0 on success

	private:
		int m_sleep;
		int audio_fd;

};

#endif // !_WIN32

#endif // !_MAC

#endif
