#include <stdio.h>
#include <stdlib.h>
#include <errno.h>

#include <signal.h>
#include <unistd.h>
#include <fcntl.h>

#include "audiostream_mac.h"

#include <sys/stat.h>
#include <pthread.h>

#include "../WDL/queue.h"

static int outchtab[2]={0,1};

static int g_srate;

// this is a total hack until I spend the time and make a good multichannel CoreAudio implementation.
static WDL_HeapBuf spltemp;

// this takes the interleaved samples in, and puts them in their own buffers,
// and processes, then reinterleaves
static void onsamples_old(float *inbuf, int innch, float *outbuf, int outnch, int nsamples, int srate)
{
  float **inptrs = (float **)alloca(sizeof(float *)*innch);
  int sz=nsamples*sizeof(float)*(innch+2);
  if (spltemp.GetSize() < sz) spltemp.Resize(sz);
  int x;
  float *t=(float*)spltemp.Get();
  for (x = 0; x < innch; x ++) 
  {
    float *s=inbuf+x;
    inptrs[x]=t;
    int y=nsamples;
    while (y--)
    {
      *t++ = *s;
      s += innch;
    }
  }
  float *outptrs[2]={t,t+nsamples};

  audiostream_onsamples(inptrs,innch,outptrs,2,nsamples,srate);

  float *p1=outptrs[0];
  float *p2=outptrs[1];
  x=nsamples;
  if (outnch > 0)
  {
    while (x--)
    {
      outbuf[outchtab[0]]=*p1++;
      if (outnch > 1) outbuf[outchtab[1]]=*p2++;
      outbuf += outnch;
    }
  } 
  else
  {
    outnch=-outnch;
    while (x--)
    {
      outbuf[0]=*p1++;
      if (outnch > 1) outbuf[1]=*p2++;
      outbuf += outnch;
    }
  }
  
}

static pthread_mutex_t m_mutex;
static WDL_Queue m_splbuf;
static int inchbuf=0;
static int outchbuf=0;
static float *ca_tmpbuf;
static int ca_tmpbuf_size;

OSStatus caIOproc(AudioDeviceID dev, 
			const AudioTimeStamp* inNow, 
			const AudioBufferList* inInputData, 
			const AudioTimeStamp* inInputTime, 
			AudioBufferList* outOutputData,
			const AudioTimeStamp* inOutputTime, 
			void* inClientData)
{
  // process inInputData to outOutputData
  if (inInputData && outOutputData)
  {
     int in_size=inInputData->mBuffers[inchbuf].mDataByteSize;
     char *in=(char *)inInputData->mBuffers[inchbuf].mData;
     int in_nch = inInputData->mBuffers[inchbuf].mNumberChannels;

     int out_size=outOutputData->mBuffers[outchbuf].mDataByteSize;
     char *out=(char *)outOutputData->mBuffers[outchbuf].mData;
     int out_nch = outOutputData->mBuffers[outchbuf].mNumberChannels;

     if (in_size*out_nch == out_size*in_nch) // faster than a divide
     {
	int needsize=((in_size/in_nch) * 2);
        if (!ca_tmpbuf || ca_tmpbuf_size <  needsize) ca_tmpbuf=(float*)realloc(ca_tmpbuf,ca_tmpbuf_size=needsize);
	if (ca_tmpbuf)
	{
        	int c=in_size/(sizeof(float)*in_nch);
        	onsamples_old((float*)in,in_nch,(float *)out,out_nch,c,g_srate);
	}
     }
  }
  return 0;
}

OSStatus caInproc(AudioDeviceID dev, 
			const AudioTimeStamp* inNow, 
			const AudioBufferList* inInputData, 
			const AudioTimeStamp* inInputTime, 
			AudioBufferList* outOutputData,
			const AudioTimeStamp* inOutputTime, 
			void* inClientData)
{
  // process inInputData to outOutputData
  if (inInputData)
  {
     int in_size=inInputData->mBuffers[inchbuf].mDataByteSize;
     char *in=(char *)inInputData->mBuffers[inchbuf].mData;
     int in_nch = inInputData->mBuffers[inchbuf].mNumberChannels;

     {
	int needsize=((in_size/in_nch) * 2);
        if (!ca_tmpbuf || ca_tmpbuf_size <  needsize) ca_tmpbuf=(float*)realloc(ca_tmpbuf,ca_tmpbuf_size=needsize);
	if (ca_tmpbuf)
	{
		if (m_splbuf.GetSize() < 48000*8)
                {
        	int c=in_size/(sizeof(float)*in_nch);
        	onsamples_old((float*)in,in_nch,(float *)ca_tmpbuf,-2,c,g_srate);

		pthread_mutex_lock(&m_mutex);
		
		m_splbuf.Add(ca_tmpbuf,needsize);

		pthread_mutex_unlock(&m_mutex);
		}
	}
     }
  }
  return 0;
}

OSStatus caOutproc(AudioDeviceID dev, 
			const AudioTimeStamp* inNow, 
			const AudioBufferList* inInputData, 
			const AudioTimeStamp* inInputTime, 
			AudioBufferList* outOutputData,
			const AudioTimeStamp* inOutputTime, 
			void* inClientData)
{
  // process inInputData to outOutputData
  if (outOutputData)
  {
     int out_size=outOutputData->mBuffers[outchbuf].mDataByteSize;
     char *out=(char *)outOutputData->mBuffers[outchbuf].mData;
     int out_nch = outOutputData->mBuffers[outchbuf].mNumberChannels;

     pthread_mutex_lock(&m_mutex);
     if (out_size < m_splbuf.Available())
     {
		float *fin=(float *)m_splbuf.Get();
		float *fout=(float *)out;
        	int x,c=out_size/(sizeof(float)*out_nch);
		for (x = 0; x < c; x ++)
		{
			fout[outchtab[0]]=*fin++;
			fout[outchtab[1]]=*fin++;
			fout += out_nch;
		}
		m_splbuf.Advance(out_size);
		m_splbuf.Compact();
     }
     pthread_mutex_unlock(&m_mutex);
  }
  return 0;
}

audioStreamer_CoreAudio::audioStreamer_CoreAudio() 
{
        m_myDev_i=0;
        m_myDev_o=0;
    	m_started=0;
}

audioStreamer_CoreAudio::~audioStreamer_CoreAudio() 
{
 	if (m_started)
        {
		if (m_myDev_o != m_myDev_i)
		{
			AudioDeviceStop(m_myDev_i,caInproc);
	                AudioDeviceRemoveIOProc(m_myDev_i,caInproc);
			AudioDeviceStop(m_myDev_o,caOutproc);
                	AudioDeviceRemoveIOProc(m_myDev_o,caOutproc);
		}
		else
		{
			AudioDeviceStop(m_myDev_i,caIOproc);
	                AudioDeviceRemoveIOProc(m_myDev_i,caIOproc);
		}
        }
         
}


int matchlen(const char *sub, const char *pa)
{
  int l=0;
  while (*sub && *pa && toupper(*sub) == toupper(*pa)) { sub++; pa++; l++; }
  return l;
}

int audioStreamer_CoreAudio::Open(char **dev, int srate, int nch, int bps)
{
  pthread_mutex_init(&m_mutex,NULL);
  char *olddev= *dev;
	m_srate=g_srate=srate;
        m_bps=33;
        m_innch=m_outnch=2;
  char user_buf[512];
 

	UInt32 theSize; 
	int s = AudioHardwareGetPropertyInfo(kAudioHardwarePropertyDevices, &theSize, NULL ); 
        int theNumberDevices = theSize / sizeof(AudioDeviceID); 
        if (!theNumberDevices)
        {
          printf("No CoreAudio devices found!\n");
          return -1;
        }
        AudioDeviceID *list=(AudioDeviceID *)malloc(sizeof(AudioDeviceID)*theNumberDevices);
        AudioHardwareGetProperty(kAudioHardwarePropertyDevices,&theSize,list);


again:

        char *indev_ptr=olddev?olddev:(char *)"";
        char *outdev_ptr=strstr(indev_ptr,",");
        if (outdev_ptr)
        {
          *outdev_ptr++=0;
          while (*outdev_ptr == ' ') outdev_ptr++;
          if (!*outdev_ptr) outdev_ptr=indev_ptr;
        } else outdev_ptr=indev_ptr;
        
        int outm=0,inm=0;
        printf("CoreAudio device list:\n");
        for (s = 0; s < theNumberDevices; s ++)
        {
          AudioDeviceID myDev;
          myDev = list[s];
          UInt32 os=0; 
          Boolean ow;
          AudioDeviceGetPropertyInfo(myDev,0,0,kAudioDevicePropertyDeviceName,&os,&ow);
          if (os > 0)
          {
            char *buf=(char *)malloc(os+1);

            AudioDeviceGetProperty(myDev,0,0,kAudioDevicePropertyDeviceName,&os,buf);
            if (os > 0)
		{
			int flags=0;
	    		int i;
	  
			for (i = 0; i <2; i ++)
			{
  			    UInt32 nos=0; Boolean now;
		            AudioDeviceGetPropertyInfo(myDev,0,i,kAudioDevicePropertyStreamConfiguration,&nos,&now);
		            if (nos>=sizeof(AudioBufferList))
		            {
		               AudioBufferList *buf2=(AudioBufferList *)malloc(nos);
		               AudioDeviceGetProperty(myDev,0,i,kAudioDevicePropertyStreamConfiguration,&nos,buf2);
		               if (nos>=sizeof(AudioBufferList)) 
		               {
		                 flags |= 1<<i;
		 	       }
		             	free(buf2);
		             }
		          }
                          int ml=(flags & 2) ? matchlen(indev_ptr,buf) : 0;
                          if (ml > inm) { inm=ml; m_myDev_i = myDev; }
                          ml=(flags & 1) ? matchlen(outdev_ptr,buf) : 0;
                          if (ml > outm) { outm=ml; m_myDev_o = myDev; }

 			  printf("  '%s' %s%s%s",buf,flags&2?"Input":"",flags==3?"/":"",flags&1?"Output":"");
	
		}
	 
            printf("\n");
            free(buf);
          }
        }

        if (!m_myDev_i || !m_myDev_o) 
        {
    #ifndef AUDIOSTREAMER_NO_CONSOLEUI
        printf("Type in the beginning of the name of your sound hardware now (or leave blank for system defaults)\n");
        printf("Note: to specify different input/output hardware, use device1, device2\n");
        printf("Choice: ");
        fflush(stdout);
        user_buf[0]=0;
        fgets(user_buf,sizeof(user_buf),stdin);
        olddev=user_buf;
        if (user_buf[0] && user_buf[0] != '\r'  && user_buf[0] != '\n') 
        {
		goto again;
        }
    #endif
        UInt32 theSize=sizeof(AudioDeviceID);
        if (!m_myDev_i) AudioHardwareGetProperty(kAudioHardwarePropertyDefaultInputDevice,&theSize,&m_myDev_i);
        theSize=sizeof(AudioDeviceID);
        if (!m_myDev_o) AudioHardwareGetProperty(kAudioHardwarePropertyDefaultOutputDevice,&theSize,&m_myDev_o);
	}
          

        free(list);

        int isinput;
        for (isinput=0;isinput<2;isinput++)
        {
          AudioDeviceID myDev = isinput ? m_myDev_i : m_myDev_o;
	  
          UInt32 os=0; 
          Boolean ow;
	  AudioStreamBasicDescription d={0,};
	  os=sizeof(d);
          AudioDeviceGetProperty(myDev,0,isinput,kAudioDevicePropertyStreamFormat,&os,&d);
	  if (os > 0) 
  	  {  
		d.mSampleRate=srate;
		os=sizeof(d);
//          	AudioDeviceSetProperty(myDev,NULL,0,isinput,kAudioDevicePropertyStreamFormat,os,&d);
          	AudioDeviceGetProperty(myDev,0,isinput,kAudioDevicePropertyStreamFormat,&os,&d);
		if (os>0) g_srate=m_srate=(int)d.mSampleRate; 
 	 }
          AudioDeviceGetPropertyInfo(myDev,0,isinput,kAudioDevicePropertyStreamConfiguration,&os,&ow);
          if (os > 0) 
          {
             AudioBufferList *buf=(AudioBufferList *)malloc(os);
             AudioDeviceGetProperty(myDev,0,isinput,kAudioDevicePropertyStreamConfiguration,&os,buf);
             int x;
             for (x = 0; x < (int)(os/sizeof(AudioBufferList)); x ++)
             {
                printf("  %s Channel %d: %d buffers\n",isinput?"Input":"Output",x,(int) buf[x].mNumberBuffers);
                int y;
                for (y = 0; y < (int)buf[x].mNumberBuffers; y++)
		{
                   if (buf[x].mBuffers[y].mNumberChannels) 
                      printf("    buffer %d: %d channels\n",y,(int)buf[x].mBuffers[y].mNumberChannels);
		   if (y == inchbuf && !x && buf[x].mBuffers[y].mNumberChannels)
			m_innch = buf[x].mBuffers[y].mNumberChannels;
		}
		break;
             }

             free(buf);
          }
	  if (os < sizeof(AudioBufferList))
	  {
		printf("Device has no %s buffers! Invalid device?\n",isinput ? "Input" : "Output");
		return -1;
	  }
       }
      
       m_started=1;
       if (m_myDev_o != m_myDev_i)
 	{
       		AudioDeviceAddIOProc(m_myDev_i,caInproc,(void *)this);
       		AudioDeviceAddIOProc(m_myDev_o,caOutproc,(void *)this);

       		AudioDeviceStart(m_myDev_i,caInproc);
 	        AudioDeviceStart(m_myDev_o,caOutproc);
	}
	else
	{
       		AudioDeviceAddIOProc(m_myDev_i,caIOproc,(void *)this);
       		AudioDeviceStart(m_myDev_i,caIOproc);
	}

       return 0;

}

int audioStreamer_CoreAudio::Read(char *buf, int len) // returns 0 if blocked, < 0 if error, > 0 if data
{
   struct timespec s={0,1000*1000*10}; // sleep 10ms;
   nanosleep(&s,NULL);
//   memset(buf,0,len);
   return 0;//len;
}
int audioStreamer_CoreAudio::Write(char *buf, int len) // returns 0 on success
{
	return 0;
}

