#ifndef _AUDIOSTREAM_H_
#define _AUDIOSTREAM_H_


extern void audiostream_onunder();
extern void audiostream_onover();
extern void audiostream_onsamples(float **inbuf, int innch, float **outbuf, int outnch, int len, int srate);

class audioStreamer
{
	public:
		audioStreamer() { m_srate=48000; m_outnch=m_innch=2; m_bps=16; }
		virtual ~audioStreamer() { }

    virtual const char *GetChannelName(int idx)=0;

		int m_srate, m_innch, m_outnch, m_bps;
};

class audioStreamer_ASIO  : public audioStreamer
{
	public:
		audioStreamer_ASIO();
		~audioStreamer_ASIO();
		int Open(char **dev);

    const char *GetChannelName(int idx);

	private:
    
    char *m_chnames[256];
    int m_driver_active;
};


audioStreamer *create_audioStreamer_KS(int srate, int bps, int *nbufs, int *bufsize);

audioStreamer *create_audioStreamer_WO(int srate, int bps, int devs[2], int *nbufs, int *bufsize);
audioStreamer *create_audioStreamer_DS(int srate, int bps, GUID devs[2], int *nbufs, int *bufsize);
#endif
