#ifndef _AUDIOSTREAM_H_
#define _AUDIOSTREAM_H_


class audioStreamer
{
	public:
		audioStreamer() { m_srate=48000; m_outnch=m_innch=2; m_bps=16; }
		virtual ~audioStreamer() { }

    virtual const char *GetChannelName(int idx)=0;

		int m_srate, m_innch, m_outnch, m_bps;
};


typedef void (*SPLPROC)(float **inbuf, int innch, float **outbuf, int outnch, int len, int srate);


audioStreamer *create_audioStreamer_ASIO(char **dev, SPLPROC proc);

audioStreamer *create_audioStreamer_KS(int srate, int bps, int *nbufs, int *bufsize, SPLPROC proc);

audioStreamer *create_audioStreamer_WO(int srate, int bps, int devs[2], int *nbufs, int *bufsize, SPLPROC proc);
audioStreamer *create_audioStreamer_DS(int srate, int bps, GUID devs[2], int *nbufs, int *bufsize, SPLPROC proc);
#endif
