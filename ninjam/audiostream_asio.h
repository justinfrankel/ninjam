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


#endif
