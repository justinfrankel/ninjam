
#include <stdio.h>
#include <stdlib.h>
#include <errno.h>

#include <signal.h>
#include <unistd.h>
#include <fcntl.h>

#include "jesusonic.h"


#include <sys/ioctl.h>
#include <sys/soundcard.h>

#include "audiostream.h"

//////////////// OSS driver

audioStreamer_OSS::audioStreamer_OSS() 
{
	audio_fd=-1;
}
audioStreamer_OSS::~audioStreamer_OSS()
{
	if (audio_fd >= 0) close(audio_fd);
}

//#define IGNORE_OSS_ERRORS

int audioStreamer_OSS::Open(char *dev, int perm, int srate, int nch, int bps, int sleep)
{
	m_sleep=sleep;
	int ignore_errors=!strcmp(dev,"/dev/null") || !strcmp(dev,"/dev/zero");
	m_nch = nch;
	m_srate=srate;
	audio_fd = open(dev,perm,0);
	if (audio_fd < 0)
	{
		perror(dev);
		return -1;
	}
#if 0
	int caps=0;
	if (ioctl(audio_fd,SNDCTL_DSP_GETCAPS,&caps)==-1)
	{
		perror("SNDCTL_DSP_GETCAPS");
		return -1;
	}
	if (caps & DSP_CAP_DUPLEX) printf("full duplex!\n");
	if (caps & DSP_CAP_REALTIME) printf("realtime!\n");
	if (caps & DSP_CAP_MMAP) printf("mmap!\n");
#endif
	int format = AFMT_S16_LE;
#ifdef AFMT_S32_LE
	if (bps == 32) format=AFMT_S32_LE;
	else if (bps == 24) format=AFMT_S24_LE;
#endif
	if (ioctl(audio_fd,SNDCTL_DSP_SETFMT, &format)==-1)
	{
#ifndef IGNORE_OSS_ERRORS
		if (!ignore_errors)
		{
			perror("SNDCTL_DSP_SETFMT");
			return -1;
		}
#endif
	}
	switch (format)
	{
		case AFMT_S16_LE: m_bps=16; break;
#ifdef AFMT_S32_LE
		case AFMT_S24_LE: m_bps=24; break;
		case AFMT_S32_LE: m_bps=32; break;
#endif
		default:
			  printf("Error setting sample type, got %d\n",format);
			  return -1;

	}
	int stereo=m_nch;
	if (ioctl(audio_fd, SNDCTL_DSP_CHANNELS, &stereo)==-1)
	{
#ifndef IGNORE_OSS_ERRORS
		if (!ignore_errors)
		{
			perror("SNDCTL_DSP_CHANNELS");
			return -1;
		}
#endif
	}
	if (stereo != m_nch)
	{
		printf("Warning: requested %d channels, got %d instead\n",m_nch,stereo);
		m_nch=stereo;
	}
	int speed=m_srate;
	if (ioctl(audio_fd, SNDCTL_DSP_SPEED, &speed)==-1)
	{
#ifndef IGNORE_OSS_ERRORS
		if (!ignore_errors)
		{
			perror("SNDCTL_DSP_SPEED");
			return -1;
		}
#endif
	}
	if (speed != m_srate)
	{
		printf("Warning: requested %dhz, got %d instead\n",m_srate,speed);
		m_srate=speed;
	}
	int nonblock=1;
	if (ioctl(audio_fd,SNDCTL_DSP_NONBLOCK,&nonblock)==-1)
	{
#ifndef IGNORE_OSS_ERRORS
		if (!ignore_errors)
		{
			perror("SNDCTL_DSP_NONBLOCK");
			return -1;
		}
#endif
	}
	if (!nonblock)
	{
		printf("Warning: device doesn't seem to support nonblocking\n");
	}
	return 0;
}

int audioStreamer_OSS::Read(char *buf, int len) // returns 0 if blocked, < 0 if error, > 0 if data
{
	if (m_sleep>=0)
	{
		struct pollfd pfds[1];
		pfds[0].fd=audio_fd;
		pfds[0].events=POLLIN;
		pfds[0].revents=0;
		poll(pfds,1,m_sleep);
	}

	int r=read(audio_fd,buf,len);
 	//int qs;
	if (r < 0)
	{
		if (errno == EAGAIN) return 0;
		else 
		{
			perror("calling read");
			return -1;
		}
	}
	return r;
}
int audioStreamer_OSS::Write(char *buf, int len) // returns 0 on success
{
	if (len > 0)
	{
		int l=write(audio_fd,buf,len);
		if (l<0)
		{
			if (errno != EAGAIN)
			{
				perror("calling write 2");
				return -1;
			}
		}
	}
	return 0;
}


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

