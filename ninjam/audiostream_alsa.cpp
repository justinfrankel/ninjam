
#include <stdio.h>
#include <stdlib.h>
#include <errno.h>

#include <signal.h>
#include <unistd.h>
#include <fcntl.h>
#include <pthread.h>

#include <alsa/asoundlib.h>
#include <sys/ioctl.h>
#include <sys/soundcard.h>

#include "../WDL/pcmfmtcvt.h"

#include "../WDL/ptrlist.h"
#include "audiostream.h"

static void audiostream_onunder() { }
static void audiostream_onover() { }



class audioStreamer_int
{
	public:
		audioStreamer_int() { m_srate=48000; m_nch=2; m_bps=16; }
		virtual ~audioStreamer_int() { }

		virtual int Read(char *buf, int len)=0; // returns 0 if blocked, < 0 if error, > 0 if data
		virtual int Write(char *buf, int len)=0; // returns 0 on success

		int m_srate, m_nch, m_bps;
};



class audioStreamer_ALSA : public audioStreamer_int
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



//////////////// ALSA driver
audioStreamer_ALSA::audioStreamer_ALSA() 
{ 
	m_started=0;
	pcm_handle=NULL;
	m_bufsize=1000000;
}

audioStreamer_ALSA::~audioStreamer_ALSA() 
{
	if (pcm_handle)
	{
		snd_pcm_drop(pcm_handle);
		snd_pcm_close(pcm_handle);
	}
}

int audioStreamer_ALSA::Open(char *devname, int is_write, int srate, int nch, int bps, int fragsize, int nfrags, int dosleep)
{
	m_sleep=dosleep;

        /* Playback stream */
        snd_pcm_stream_t stream = is_write?SND_PCM_STREAM_PLAYBACK:SND_PCM_STREAM_CAPTURE;

	/* This structure contains information about    */
	/* the hardware and can be used to specify the  */      
	/* configuration to be used for the PCM stream. */ 
	snd_pcm_hw_params_t *hwparams;

	/* Allocate the snd_pcm_hw_params_t structure on the stack. */
	snd_pcm_hw_params_alloca(&hwparams);

    	if (snd_pcm_open(&pcm_handle, devname, stream, 0) < 0) 
	{
 		fprintf(stderr, "Error opening PCM device %s\n", devname);
	        return(-1);
    	}

        /* Init hwparams with full configuration space */
        if (snd_pcm_hw_params_any(pcm_handle, hwparams) < 0) 
	{
		fprintf(stderr, "Can not configure this PCM device.\n");
		return(-1);
        }

	if (snd_pcm_hw_params_set_access(pcm_handle, hwparams, SND_PCM_ACCESS_RW_INTERLEAVED) < 0) 
	{
		fprintf(stderr, "Error setting access.\n");
		return(-1);
        }
	  
    	/* Set sample format */
	m_bps=bps==32?32:bps==24?24:16;
    	if (snd_pcm_hw_params_set_format(pcm_handle, hwparams, 
				bps==32?SND_PCM_FORMAT_S32_LE:bps==24?SND_PCM_FORMAT_S24_3LE:SND_PCM_FORMAT_S16_LE) < 0) {
		fprintf(stderr, "Error setting format.\n");
		fprintf(stderr, "Try -bps 16, -bps 24, or -bps 32\n");
		return(-1);
	}

  int dir=0;          /* exact_rate == rate --> dir = 0 */
                          /* exact_rate < rate  --> dir = -1 */
                          /* exact_rate > rate  --> dir = 1 */
	unsigned int usrate=srate;

        /* Set sample rate. If the exact rate is not supported */
        /* by the hardware, use nearest possible rate.         */ 
	m_srate=srate;
  int exact_rate = snd_pcm_hw_params_set_rate_near(pcm_handle, hwparams, &usrate, &dir);
	if (dir != 0) 
	{
		fprintf(stderr, "The rate %d Hz is not supported by your hardware. Using %d Hz instead.\n", srate, exact_rate);
  		m_srate=exact_rate;
      	}

       	/* Set number of channels */
        if (snd_pcm_hw_params_set_channels(pcm_handle, hwparams, nch) < 0) 
	{
		fprintf(stderr, "Error setting channels.\n");
		fprintf(stderr, "Try -nch 1 or -nch 2\n");
		return(-1);
        }
	m_nch=nch;
	
	int periods=m_nfrags=(is_write?nfrags:nfrags*3);
	int periodsize=fragsize;

	/* Set number of periods. Periods used to be called fragments. */ 
	if (snd_pcm_hw_params_set_periods(pcm_handle, hwparams, periods, 0) < 0) 
	{
		fprintf(stderr, "Error setting periods.\n");
		fprintf(stderr, "Try -nbufs 2 through -nbufs 16\n");
		return(-1);
	}

    	/* Set buffer size (in frames). The resulting latency is given by */
    	/* latency = periodsize * periods / (rate * bytes_per_frame)     */
    	if (snd_pcm_hw_params_set_buffer_size(pcm_handle, hwparams, m_bufsize = (periodsize * periods)/(m_nch * m_bps/8)) < 0) 
	{
		fprintf(stderr, "Error setting buffersize.\n");
		fprintf(stderr, "Try -bufsize 256 through -bufsize 2048\n");
		return(-1);
	}

     	/* Apply HW parameter settings to */
        /* PCM device and prepare device  */
        if (snd_pcm_hw_params(pcm_handle, hwparams) < 0) 
	{
		fprintf(stderr, "Error setting HW params.\n");
		return(-1);
	}

	return 0;
}


int audioStreamer_ALSA::Read(char *buf, int len) // returns 0 if blocked, < 0 if error, > 0 if data
{
	int ret;
	if (m_sleep >= 0)
	{
		struct pollfd pfds[32];
		int cnt=snd_pcm_poll_descriptors(pcm_handle,pfds,32);
		if (cnt>0) poll(pfds,cnt,m_sleep);
	}

	ret=snd_pcm_readi(pcm_handle, buf, len/(m_nch*(m_bps/8)));

	if (ret < 0) 
	{
		if (ret != -EAGAIN) { snd_pcm_prepare(pcm_handle);  }
		return 0;
	}
#if 0
	snd_pcm_sframes_t del=0;
	if (!snd_pcm_delay(pcm_handle,&del) && del > m_bufsize/2 /* JF>used to be /1 */)
	{
		audiostream_onover();
		for (;;) if (snd_pcm_readi(pcm_handle, buf, len/(m_nch*(m_bps/8)))<0) break;
		// we have too many samples, eat some
	}
#endif

	return ret*m_nch*(m_bps/8);
}


int audioStreamer_ALSA::Write(char *buf, int len) // returns 0 on success
{
	snd_pcm_sframes_t del=0;
	if (!len) return 0;

	int cnt=1;
	if (!m_started || !snd_pcm_delay(pcm_handle,&del) && del<1)
	{
		if (m_started) audiostream_onunder();
		else m_started=1;
		cnt=m_nfrags;
    memset(buf,0,len); // reduce noise

	} 

	while (cnt-->0)
	{
		int ret=snd_pcm_writei(pcm_handle, buf, len/(m_nch*(m_bps/8)));
		if (ret < 0)
		{
			if (ret == -EPIPE) snd_pcm_prepare(pcm_handle);
			return 0;
		}
	}

	return 0;
}





//============== asio simulation shit
//
class audioStreamer_asiosim : public audioStreamer
{
	public:
    audioStreamer_asiosim(audioStreamer_int *i, audioStreamer_int *o, int bufsize, int srate, int bps, SPLPROC proc)
    {
      m_splproc=proc;
      in=i;
      out=o;
      m_bps=bps;
      m_innch=m_outnch=2;
      m_bps=bps;
      m_srate=srate;
      m_done=0;
      m_buf=(char *)malloc(bufsize);
      m_bufsize=bufsize;

      m_procbuf=(float *)malloc((bufsize*64)/bps);// allocated 2x, input and output

      
      // create thread
      pthread_create(&hThread,NULL,threadProc,(void *)this);
    }
    ~audioStreamer_asiosim()
    {
      m_done=1;

      // kill thread
      pthread_join(hThread,NULL);

      delete in;
      delete out;
      free(m_buf);
      free(m_procbuf);
    }

    const char *GetChannelName(int idx)
    {
      if (idx == 0) return "Left";
      if (idx == 1) return "Right";
      return NULL;
    }

	private:
    void tp();
    static void *threadProc(void *p)
    {
      audioStreamer_asiosim *t=(audioStreamer_asiosim*)p;
      t->tp();
      return 0;
    }
    audioStreamer_int *in, *out;
    
    pthread_t hThread;
    int m_done,m_bufsize;
    char *m_buf;
    float *m_procbuf;

    SPLPROC m_splproc;
};

void audioStreamer_asiosim::tp()
{
  while (!m_done)
  {
    int a=in->Read(m_buf,m_bufsize);
    if (a>0)
    {
      int spllen=a*4/(m_bps); // a*8/m_bps/nch
      float *inptrs[2], *outptrs[2];
      inptrs[0]=m_procbuf;
      inptrs[1]=m_procbuf+spllen;
      outptrs[0]=m_procbuf+spllen*2;
      outptrs[1]=m_procbuf+spllen*3;

      pcmToFloats(m_buf,spllen,m_bps,2,inptrs[0],1);
      pcmToFloats(m_buf+(m_bps/8),spllen,m_bps,2,inptrs[1],1);

      if (m_splproc) m_splproc(inptrs,2,outptrs,2,spllen,m_srate);

      floatsToPcm(outptrs[0],1,spllen,m_buf,m_bps,2);
      floatsToPcm(outptrs[1],1,spllen,m_buf+(m_bps/8),m_bps,2);
  
      out->Write(m_buf,a);
    }
    else
    {
      struct timespec s={0,1000*1000}; // sleep 1ms;
      nanosleep(&s,NULL);
    }
  }
}

audioStreamer *create_audioStreamer_ALSA(char *cfg, SPLPROC proc)
{
  // todo: parse from cfg
  char *indev="hw:0,0";
  char *outdev="hw:0,0";
  int srate=48000;
  int nch=2;
  int bps=16;
  int fs=1024;
  int nf=16;

  while (cfg && *cfg)
  {
    char *p=cfg;
    while (*p && *p != ' ') p++;
    if (!*p) break;
    *p++=0;
    while (*p == ' ') p++;
    if (!*p)
    {
	    printf("config item '%s' has no parameter\n",cfg);
	    return 0;
    }

    if (!strcasecmp(cfg,"in")) indev=p;
    else if (!strcasecmp(cfg,"out")) outdev=p;
    else if (!strcasecmp(cfg,"srate")) srate=atoi(p);
    else if (!strcasecmp(cfg,"nch")) nch=atoi(p);
    else if (!strcasecmp(cfg,"bps")) bps=atoi(p);
    else if (!strcasecmp(cfg,"bsize")) fs=atoi(p);
    else if (!strcasecmp(cfg,"nblock")) nf=atoi(p);
    else 
    {
	    printf("unknown config item '%s'\n",cfg);
	    return 0;
    }

    while (*p && *p != ' ') p++;
    if (!*p) break;
    *p++=0;
    while (*p == ' ') p++;
    cfg=p;
  }

  audioStreamer_ALSA *in=new audioStreamer_ALSA();
  if (in->Open(indev,0,srate,nch,bps,fs,nf,-1))
  {
    delete in;
    return 0;
  }
  audioStreamer_ALSA *out=new audioStreamer_ALSA();
  if (out->Open(outdev,1,srate,nch,bps,fs,nf,-1))
  {
    delete in;
    delete out;
    return 0;
  }

  return new audioStreamer_asiosim(in,out,fs,srate,bps,proc);
}
