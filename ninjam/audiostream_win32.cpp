#include <windows.h>
#include <mmsystem.h>

#include <math.h>
#include "audiostream.h"


#include "pcmfmtcvt.h"

#ifdef _MSC_VER 


#if 1
#define printf myPrintf
static void myPrintf(char *s, ... ) { }
#define fflush(x)
#define getchar()


#else
//#ifdef 
#include <stdarg.h>
//#include <vargs.h>

#define printf myPrintf
static void myPrintf(char *s, ...)
{
  char buf[1024];
  va_list ap;
  va_start(ap,s);

  _vsnprintf(buf,1024,s,ap);
  OutputDebugString(buf);

  va_end(ap);
}
#endif
#endif


audioStreamer_ds::audioStreamer_ds()
{
  m_lpds=0;
  m_outbuf=0;
  m_lpcap=0;
  m_has_started=0;
  m_inbuf=0;
  m_last_pos=0;
  m_bufpos=0;
  m_i_lw=0;
  m_i_dw=0;
}

audioStreamer_ds::~audioStreamer_ds()
{
  if (m_lpds) m_lpds->Release();
  if (m_lpcap) m_lpcap->Release();
  m_lpcap=0;
  m_lpds=0;
  m_outbuf=0;
  m_inbuf=0;

}

int audioStreamer_ds::Open(int iswrite, int srate, int nch, int bps, int sleep, int nbufs, int bufsize, GUID *device)
{
  // todo: use device
  m_sleep = sleep >= 0 ? sleep : 0;

  GUID zero={0,};
  if (!memcmp(device,&zero,sizeof(zero))) device=NULL;

  m_nch = nch;
  m_srate=srate;
  m_bps=bps;

  int fmt_align=(bps>>3)*nch;
  int fmt_mul=fmt_align*srate;
  WAVEFORMATEX wfx={
		WAVE_FORMAT_PCM,
		nch,
		srate,
		fmt_mul,
		fmt_align,
		bps,
		0
	};
  m_totalbufsize=nbufs*bufsize;

  if (iswrite)
  {
      DirectSoundCreate(device,&m_lpds,NULL);

      if (m_lpds)
      {
        HWND hWnd = GetForegroundWindow();
        if (hWnd == NULL) hWnd = GetDesktopWindow();
        m_lpds->SetCooperativeLevel(hWnd,DSSCL_PRIORITY);

        // create a secondary buffer for now
        DSBUFFERDESC ds={sizeof(ds),DSBCAPS_GETCURRENTPOSITION2|DSBCAPS_GLOBALFOCUS,m_totalbufsize,0,&wfx, };
        m_lpds->CreateSoundBuffer(&ds,&m_outbuf,NULL);
        
      }

  }
  else
  {
    DirectSoundCaptureCreate(device,&m_lpcap,NULL);
    if (m_lpcap)
    {
      DSCBUFFERDESC ds={sizeof(ds),0,m_totalbufsize,0,&wfx, };
      m_lpcap->CreateCaptureBuffer(&ds,&m_inbuf,NULL);
    }
  }

  m_bufsize=bufsize;


  return 0;
}

int audioStreamer_ds::Read(char *buf, int len) // returns 0 if blocked, < 0 if error, > 0 if data
{
  if (!m_inbuf) return -1;
  if (!m_has_started)
  {
    m_inbuf->Start(DSCBSTART_LOOPING);
    m_has_started=1;
  }

  int cappos;
  m_inbuf->GetCurrentPosition(NULL,(DWORD *)&cappos);
  if (cappos < m_last_pos) m_i_dw++;
  m_last_pos=cappos;

  if ((m_i_dw - m_i_lw) * m_totalbufsize + cappos - m_bufpos >= (unsigned int)m_totalbufsize/2) // detect overrun, set to cappos
  {
    m_i_lw=m_i_dw;
    m_bufpos=0;
    while (m_bufpos < cappos-m_bufsize) m_bufpos += m_bufsize;
    if (m_bufpos >= m_totalbufsize) 
    {
      m_i_lw++;
      m_bufpos -= m_totalbufsize;
    }
    audiostream_onover();
  }

  for (;;)
  {
    if (m_i_lw < m_i_dw || m_bufpos + m_bufsize < cappos) break;

    Sleep(m_sleep);
    m_inbuf->GetCurrentPosition(NULL,(DWORD*)&cappos);

    if (cappos < m_last_pos) m_i_dw++;
    m_last_pos=cappos;
  }

  //audiostream_instance->g_sound_in_overruns = (m_i_lw < m_i_dw ? (m_totalbufsize+cappos) : cappos ) -m_bufpos;

  void *v1=0, *v2=0;
  DWORD lv1=0, lv2=0;
  
  if (m_inbuf->Lock(m_bufpos,len,&v1,&lv1,&v2,&lv2,FALSE) == DS_OK)
  {
    int l1=min((int)lv1,len);
    memcpy(buf,v1,l1);
    if (l1 < len && v2 && lv2) memcpy(buf+l1,v2,min((int)lv2,len-l1));
    m_inbuf->Unlock(v1,lv1,v2,lv2);

    m_bufpos += len;
    if (m_bufpos >= m_totalbufsize) 
    {
      m_i_lw++;
      m_bufpos -= m_totalbufsize;
    }
  }
  else 
  {
    printf("Error locking read buffer @ %d!\n",m_bufpos);
    return -1;
  }

  return len;
}

int audioStreamer_ds::Write(char *buf, int len) // returns 0 on success
{ 
  if (len<1) return 0;
  if (!m_outbuf) return -1;

  if (!m_has_started)
  {
    m_outbuf->Play(0,0,DSBPLAY_LOOPING);
    m_has_started=1;
  }


  int thispos=0,ppos=0;
  m_outbuf->GetCurrentPosition((DWORD*)&ppos,(DWORD*)&thispos);

//  thispos=ppos; // let's use the write cursor, not the play position

  if (thispos < m_last_pos) 
  {
    m_i_dw++;
  }
  m_last_pos = thispos;

  if (m_i_lw < m_i_dw || (m_i_lw == m_i_dw && m_bufpos < thispos )) // detect if we fall too far behind
  {
    audiostream_onunder();
    m_i_lw=m_i_dw;
    m_bufpos=0;
    while (m_bufpos <= thispos+m_bufsize) m_bufpos+=m_bufsize;
    if (m_bufpos >= m_totalbufsize)
    {
      m_bufpos -= m_totalbufsize;
      m_i_lw++;
    }
  }

  if (m_bufpos + len >= ppos && m_i_lw > m_i_dw)
  {
    return 0;
  }

//    printf("%d,%d,%d\n",m_totalbufsize, tp2, m_bufpos+len);

  if (1)//tp2 >= m_bufpos + len)
  {
    void *v1=0, *v2=0;
    DWORD lv1=0, lv2=0;
    if (m_outbuf->Lock(m_bufpos,len,&v1,&lv1,&v2,&lv2,0) == DS_OK)
    {
      memcpy(v1,buf,min((int)lv1,len));
      if ((int)lv1 < len && v2 && lv2) memcpy(v2,buf+lv1,min((int)lv2,(int)(len-lv1)));

      m_outbuf->Unlock(v1,lv1,v2,lv2);

      m_bufpos += len;
      if (m_bufpos >= m_totalbufsize) 
      {
        m_bufpos -= m_totalbufsize;
        m_i_lw++;
      }
    }
    else 
    {
      printf("Error locking write buffer!\n");
      return -1;
    }


  }
  else
  {
  	audiostream_onunder(); // g_sound_in_overruns?
  }

  return 0;
}





//////////////////////////////
/// waveout
audioStreamer_waveOut::audioStreamer_waveOut()
{
	m_hwi=0;
	m_hwo=0;
}

audioStreamer_waveOut::~audioStreamer_waveOut()
{
   int x;
   if (m_hwo) waveOutReset(m_hwo);
   if (m_hwi) waveInReset(m_hwi);
   for (x = 0; x < m_bufs.GetSize(); x ++)
   {
	   WAVEHDR *p = (WAVEHDR *)m_bufs.Get(x)->Get();
	   if (m_hwi && p)
		   if (p->dwFlags & WHDR_PREPARED) waveInUnprepareHeader(m_hwi,p,sizeof(WAVEHDR));

	   if (m_hwo && p)
     {
		   if (p->dwFlags & WHDR_PREPARED) waveOutUnprepareHeader(m_hwo,p,sizeof(WAVEHDR));
     }
	   delete m_bufs.Get(x);
   }
   m_bufs.Empty();

   if (m_hwo)
   {
	   waveOutClose(m_hwo);
	   m_hwo=0;
   }
   if (m_hwi)
   {
	   waveInClose(m_hwi);
	   m_hwi=0;
   }

}

int audioStreamer_waveOut::Open(int iswrite, int srate, int nch, int bps, int sleep, int nbufs, int bufsize, int device)
{
  m_sleep =   sleep >= 0 ? sleep : 0;

  m_nch = nch;
  m_srate=srate;
  m_bps=bps;

  int fmt_align=(bps>>3)*nch;
  int fmt_mul=fmt_align*srate;
  WAVEFORMATEX wfx={
		WAVE_FORMAT_PCM,
		nch,
		srate,
		fmt_mul,
		fmt_align,
		bps,
		0
	};

  m_bufsize=bufsize;
  m_whichbuf=0;

  if (iswrite)
  {
	  if(waveOutOpen(&m_hwo,device,&wfx,(DWORD)0,0,CALLBACK_NULL|WAVE_FORMAT_DIRECT)!=MMSYSERR_NOERROR) return -1; 
  }
  else
  {
  	if(waveInOpen(&m_hwi,WAVE_MAPPER,&wfx,(DWORD)0,0,CALLBACK_NULL|WAVE_FORMAT_DIRECT)!=MMSYSERR_NOERROR) return -1; 
  }


  int x;
  for (x = 0; x < nbufs; x ++)
  {
	  WDL_HeapBuf *p=new WDL_HeapBuf;
	  p->Resize(sizeof(WAVEHDR) + m_bufsize);

	  WAVEHDR *h = (WAVEHDR *)p->Get();

	  memset(h,0,p->GetSize());


	  h->lpData=(char *)h + sizeof(WAVEHDR);
	  h->dwBufferLength = m_bufsize;

	  if (!iswrite)
	  {
		  waveInPrepareHeader(m_hwi,h,sizeof(WAVEHDR));
		  waveInAddBuffer(m_hwi,h,sizeof(WAVEHDR));
	  }
	  else 
	  {
      waveOutPrepareHeader(m_hwo,h,sizeof(WAVEHDR));
	  }

	  m_bufs.Add(p);
  }

  if (iswrite) waveOutRestart(m_hwo);
  else waveInStart(m_hwi);

  return 0;
}

int audioStreamer_waveOut::Read(char *buf, int len) // returns 0 if blocked, < 0 if error, > 0 if data
{
  if (!m_hwi) return -1;

#if 0 // lame, this doesnt really do what we want it to
  // check to see if all are full, and if so, kill a lot of em
  {
    int x;
    int cnt=0;
    for (x = 0; x < m_bufs.GetSize(); x ++)
    {
      WAVEHDR *th = (WAVEHDR *) m_bufs.Get(x)->Get();
      if (th->dwFlags & WHDR_DONE) cnt++;
    }
    if (cnt >= m_bufs.GetSize()-1)
    {
      audiostream_onover();
      for (x = 0; x < m_bufs.GetSize(); x ++)
      {
        if (x != m_whichbuf)
        {
          WAVEHDR *th = (WAVEHDR *) m_bufs.Get(x)->Get();
          if (th->dwFlags & WHDR_DONE) 
          {
            th->dwBytesRecorded=0;
            th->dwFlags = WHDR_PREPARED;
            waveInAddBuffer(m_hwi,th,sizeof(WAVEHDR));
          }
        }
      }
    }
  }
#endif

  WAVEHDR *th = (WAVEHDR *) m_bufs.Get(m_whichbuf)->Get();
  while (!(th->dwFlags & WHDR_DONE)) Sleep(m_sleep); 

  len=min(len,(int)th->dwBytesRecorded);

  memcpy(buf,th->lpData,len);

  th->dwBytesRecorded=0;
  th->dwFlags = WHDR_PREPARED;
  waveInAddBuffer(m_hwi,th,sizeof(WAVEHDR));

  if (++m_whichbuf >= m_bufs.GetSize()) m_whichbuf=0;

  return len;
}

int audioStreamer_waveOut::Write(char *buf, int len) // returns 0 on success
{ 
  if (!m_hwo) return -1;
  if (len<1) return 0;

  int use_addr=-1;

  int cnt;
  do
  {
    int x;
    cnt=0;
    for (x = 0; x < m_bufs.GetSize(); x ++)
    {
        WAVEHDR *h=(WAVEHDR *)m_bufs.Get(x)->Get();
        if (h->dwFlags & WHDR_DONE) h->dwFlags &= ~(WHDR_INQUEUE|WHDR_DONE); // remove done and in queue

        if (!(h->dwFlags & WHDR_INQUEUE)) 
        {
          cnt++;
          use_addr=x;
        }
    }
    if (use_addr < 0)
    {
#if 1
      Sleep(m_sleep);
#else
      audiostream_onover();
      return 0;
#endif
    }
  } while (use_addr < 0);


  WAVEHDR *h=(WAVEHDR *)m_bufs.Get(use_addr)->Get();

  if (len > m_bufsize) len=m_bufsize;

  h->dwBufferLength=len;
  memcpy(h->lpData,buf,len);
  waveOutWrite(m_hwo,h,sizeof(WAVEHDR)); 

  if (!cnt)
  {
    audiostream_onunder();

    int x;
    for (x = 0; x < m_bufs.GetSize(); x ++)
    {
      if (x != use_addr)
      {
        h=(WAVEHDR *) m_bufs.Get(x)->Get();
        h->dwBufferLength=len;      
        waveOutWrite(m_hwo,h,sizeof(WAVEHDR)); 
      }
    }
  }

  return 0;
}



#ifdef SUPPORT_ASIO
//////////////////////////////
/// ASIO
#include "asiosys.h"
#include "asio.h"
#include "asiodrivers.h"

// GCC only: #include "iasiothiscallresolver.h"

enum {
	// number of input and outputs supported by the host application
	// you can change these to higher or lower values
	kMaxInputChannels = 2,
	kMaxOutputChannels = 2
};


// internal data storage
typedef struct DriverInfo
{
	// ASIOInit()
	ASIODriverInfo driverInfo;

	// ASIOGetChannels()
	long           inputChannels;
	long           outputChannels;

	// ASIOGetBufferSize()
	long           minSize;
	long           maxSize;
	long           preferredSize;
	long           granularity;

	// ASIOGetSampleRate()
	ASIOSampleRate sampleRate;

	// ASIOOutputReady()
	bool           postOutput;

	// ASIOGetLatencies ()
	//long           inputLatency;
	//long           outputLatency;

	// ASIOCreateBuffers ()
	long inputBuffers;	// becomes number of actual created input buffers
	long outputBuffers;	// becomes number of actual created output buffers
	ASIOBufferInfo bufferInfos[kMaxInputChannels + kMaxOutputChannels]; // buffer info's

	// ASIOGetChannelInfo()
	ASIOChannelInfo channelInfos[kMaxInputChannels + kMaxOutputChannels]; // channel info's
	// The above two arrays share the same indexing, as the data in them are linked together

	// Information from ASIOGetSamplePosition()
	// data is converted to double floats for easier use, however 64 bit integer can be used, too

  // 

  int bytesProcessed;
  //CRITICAL_SECTION cs;

} DriverInfo;

float *asio_pcmbuf;

static DriverInfo myDriverInfo;
static ASIOCallbacks asioCallbacks;

extern AsioDrivers* asioDrivers;





// callback prototypes
void bufferSwitch(long index, ASIOBool processNow);
ASIOTime *bufferSwitchTimeInfo(ASIOTime *timeInfo, long index, ASIOBool processNow);
void sampleRateChanged(ASIOSampleRate sRate);
long asioMessages(long selector, long value, void* message, double* opt);


ASIOTime *bufferSwitchTimeInfo(ASIOTime *timeInfo, long index, ASIOBool processNow)
{	// the actual processing callback.
	// Beware that this is normally in a seperate thread, hence be sure that you take care
	// about thread synchronization. This is omitted here for simplicity.

	// get the time stamp of the buffer, not necessary if no
	// synchronization to other media is required

	// get the system reference time

	// buffer size in samples
	long buffSize = myDriverInfo.preferredSize;

//  if (myDriverInfo.inputBuffers == 2 )
  {
    int t=myDriverInfo.channelInfos[0].type;
    int splsize=2;
    
    if (t==ASIOSTInt24LSB) splsize=3;
    else if (t==ASIOSTInt32LSB) splsize=4;

    // interleave the first two buffers into the queue
    if (myDriverInfo.inputBuffers==1)
    {
      if (splsize == 3)
      {
        unsigned char *i1=(unsigned char *)myDriverInfo.bufferInfos[0].buffers[index];
        float *o=asio_pcmbuf;
        int l=buffSize;
        while (l--)
        {          
          i24_to_float(i1,o);
          o[1]=o[0];            
          o++;
          i1+=3;
        }
      }
      else if (splsize == 4)
      {
        int *i1=(int *)myDriverInfo.bufferInfos[0].buffers[index];
        float *o=asio_pcmbuf;
        int l=buffSize;
        while (l--)
        {          
          i32_to_float(*i1++,o);
          o[1]=o[0];            
          o++;
        }
      }
      else if (splsize == 2)
      {
        short *i1=(short *)myDriverInfo.bufferInfos[0].buffers[index];
        float *o=asio_pcmbuf;
        int l=buffSize;
        while (l--)
        {          
          INT16_TO_float(*o,*i1);
          i1++;
          o[1]=o[0];            
          o++;
        }
      }
    }
    else
    {
      if (splsize == 3)
      {
        unsigned char *i1=(unsigned char *)myDriverInfo.bufferInfos[0].buffers[index];
        unsigned char *i2=(unsigned char *)myDriverInfo.bufferInfos[1].buffers[index];
        float *o=asio_pcmbuf;
        int l=buffSize;
        while (l--)
        {          
          i24_to_float(i1,o);
          i24_to_float(i2,o+1);
          o+=2;
          i1+=3;
          i2+=3;
        }
      }
      else if (splsize == 4)
      {
        int *i1=(int *)myDriverInfo.bufferInfos[0].buffers[index];
        int *i2=(int *)myDriverInfo.bufferInfos[1].buffers[index];
        float *o=asio_pcmbuf;
        int l=buffSize;
        while (l--)
        {          
          i32_to_float(*i1,o);
          i32_to_float(*i2,o+1);
          i1++;
          i2++;
          o+=2;      
        }
      }
      else if (splsize == 2)
      {
        short *i1=(short *)myDriverInfo.bufferInfos[0].buffers[index];
        short *i2=(short *)myDriverInfo.bufferInfos[1].buffers[index];
        float *o=asio_pcmbuf;
        int l=buffSize;
        while (l--)
        {          
          INT16_TO_float(*o,*i1);
          INT16_TO_float(o[1],*i2);
          i1++;
          i2++;
          o+=2;
        }
      }
    }

    {
      int bytes=buffSize*sizeof(float)*2;
//      EnterCriticalSection(&myDriverInfo.cs);

      audiostream_onsamples(asio_pcmbuf,buffSize,2);

      myDriverInfo.bytesProcessed+=bytes;
  //    LeaveCriticalSection(&myDriverInfo.cs);
    }

    // uninterleave the latest samples into the second two buffers
    if (myDriverInfo.outputBuffers==1)
    {
      if (splsize==4)
      {
        int *o1=(int*)myDriverInfo.bufferInfos[myDriverInfo.inputBuffers].buffers[index];
        float *i=asio_pcmbuf;
        int l=buffSize;
        while (l--)
        {
          float_to_i32(i,o1);
          i+=2;
          o1++;
        }
      }
      else if (splsize==3)
      {
        unsigned char *o1=(unsigned char*)myDriverInfo.bufferInfos[myDriverInfo.inputBuffers].buffers[index];
        float *i=asio_pcmbuf;
        int l=buffSize;
        while (l--)
        {
          float_to_i24(i,o1);
          i+=2;
          o1+=3;
        }
      }
      else if (splsize==2)
      {
        short *o1=(short*)myDriverInfo.bufferInfos[myDriverInfo.inputBuffers].buffers[index];
        float *i=asio_pcmbuf;
        int l=buffSize;
        while (l--)
        {
          float_TO_INT16(*o1,*i);
          i+=2;
          o1++;
        }
      }
    }
    else
    {
      if (splsize==4)
      {
        int *o1=(int*)myDriverInfo.bufferInfos[myDriverInfo.inputBuffers].buffers[index];
        int *o2=(int*)myDriverInfo.bufferInfos[myDriverInfo.inputBuffers+1].buffers[index];
        float *i=asio_pcmbuf;
        int l=buffSize;
        while (l--)
        {
          float_to_i32(i,o1);
          float_to_i32(i+1,o2);
          i+=2;
          o1++;
          o2++;
        }
      }
      else if (splsize==3)
      {
        unsigned char *o1=(unsigned char*)myDriverInfo.bufferInfos[myDriverInfo.inputBuffers].buffers[index];
        unsigned char *o2=(unsigned char*)myDriverInfo.bufferInfos[myDriverInfo.inputBuffers+1].buffers[index];
        float *i=asio_pcmbuf;
        int l=buffSize;
        while (l--)
        {
          float_to_i24(i,o1);
          float_to_i24(i+1,o2);
          i+=2;
          o1+=3;
          o2+=3;
        }
      }
      else if (splsize==2)
      {
        short *o1=(short*)myDriverInfo.bufferInfos[myDriverInfo.inputBuffers].buffers[index];
        short *o2=(short*)myDriverInfo.bufferInfos[myDriverInfo.inputBuffers+1].buffers[index];
        float *i=asio_pcmbuf;
        int l=buffSize;
        while (l--)
        {
          float_TO_INT16(*o1,*i);
          float_TO_INT16(*o2,i[1]);
          i+=2;
          o1++;
          o2++;
        }
      }
    }
  }
  

	// finally if the driver supports the ASIOOutputReady() optimization, do it here, all data are in place
	if (myDriverInfo.postOutput)
		ASIOOutputReady();


	return 0L;
}

//----------------------------------------------------------------------------------
void bufferSwitch(long index, ASIOBool processNow)
{	// the actual processing callback.
	// Beware that this is normally in a seperate thread, hence be sure that you take care
	// about thread synchronization. This is omitted here for simplicity.

	// as this is a "back door" into the bufferSwitchTimeInfo a timeInfo needs to be created
	// though it will only set the timeInfo.samplePosition and timeInfo.systemTime fields and the according flags
	ASIOTime  timeInfo;
	memset (&timeInfo, 0, sizeof (timeInfo));

	// get the time stamp of the buffer, not necessary if no
	// synchronization to other media is required
	if(ASIOGetSamplePosition(&timeInfo.timeInfo.samplePosition, &timeInfo.timeInfo.systemTime) == ASE_OK)
		timeInfo.timeInfo.flags = kSystemTimeValid | kSamplePositionValid;

	bufferSwitchTimeInfo (&timeInfo, index, processNow);
}


//----------------------------------------------------------------------------------
void sampleRateChanged(ASIOSampleRate sRate)
{
	// do whatever you need to do if the sample rate changed
	// usually this only happens during external sync.
	// Audio processing is not stopped by the driver, actual sample rate
	// might not have even changed, maybe only the sample rate status of an
	// AES/EBU or S/PDIF digital input at the audio device.
	// You might have to update time/sample related conversion routines, etc.
}

//----------------------------------------------------------------------------------
long asioMessages(long selector, long value, void* message, double* opt)
{
	// currently the parameters "value", "message" and "opt" are not used.
	long ret = 0;
	switch(selector)
	{
		case kAsioSelectorSupported:
			if(value == kAsioResetRequest
			|| value == kAsioEngineVersion
			|| value == kAsioResyncRequest
			|| value == kAsioLatenciesChanged
			// the following three were added for ASIO 2.0, you don't necessarily have to support them
			|| value == kAsioSupportsTimeInfo
			|| value == kAsioSupportsTimeCode
			|| value == kAsioSupportsInputMonitor)
				ret = 1L;
			break;
		case kAsioResetRequest:
			// defer the task and perform the reset of the driver during the next "safe" situation
			// You cannot reset the driver right now, as this code is called from the driver.
			// Reset the driver is done by completely destruct is. I.e. ASIOStop(), ASIODisposeBuffers(), Destruction
			// Afterwards you initialize the driver again.
//			myDriverInfo.stopped;  // In this sample the processing will just stop
			ret = 1L;
			break;
		case kAsioResyncRequest:
			// This informs the application, that the driver encountered some non fatal data loss.
			// It is used for synchronization purposes of different media.
			// Added mainly to work around the Win16Mutex problems in Windows 95/98 with the
			// Windows Multimedia system, which could loose data because the Mutex was hold too long
			// by another thread.
			// However a driver can issue it in other situations, too.
			ret = 1L;
			break;
		case kAsioLatenciesChanged:
			// This will inform the host application that the drivers were latencies changed.
			// Beware, it this does not mean that the buffer sizes have changed!
			// You might need to update internal delay data.
			ret = 1L;
			break;
		case kAsioEngineVersion:
			// return the supported ASIO version of the host application
			// If a host applications does not implement this selector, ASIO 1.0 is assumed
			// by the driver
			ret = 2L;
			break;
		case kAsioSupportsTimeInfo:
			// informs the driver wether the asioCallbacks.bufferSwitchTimeInfo() callback
			// is supported.
			// For compatibility with ASIO 1.0 drivers the host application should always support
			// the "old" bufferSwitch method, too.
			ret = 1;
			break;
		case kAsioSupportsTimeCode:
			// informs the driver wether application is interested in time code info.
			// If an application does not need to know about time code, the driver has less work
			// to do.
			ret = 0;
			break;
	}
	return ret;
}



audioStreamer_ASIO::audioStreamer_ASIO()
{
  m_driver_active=0;
  asioDrivers=0;
}

audioStreamer_ASIO::~audioStreamer_ASIO()
{
  if (asioDrivers)
  {
    if (m_driver_active>0)
    {
      if (m_driver_active>3)
      {
        //LeaveCriticalSection(&myDriverInfo.cs);
        ASIOStop();
      }
      if (m_driver_active>2)
      {
        //DeleteCriticalSection(&myDriverInfo.cs);
        ASIODisposeBuffers();
      }
      if (m_driver_active>1)
        ASIOExit();

      asioDrivers->removeCurrentDriver();
    }
    delete asioDrivers;
    asioDrivers=0;
  }

}

int audioStreamer_ASIO::Open(char **dev, int srate, int nch, int bps, int sleep, int *nbufs, int *bufsize)
{
	if(!asioDrivers)
		asioDrivers = new AsioDrivers();

  if (nch != 2)
  {
    printf("Error: our ASIO support currently only works with 2 channels!\n");
    return -1;
  }

  if (!asioDrivers)
  {
    printf("Error initializing ASIO\n");
    return -1;
  }

  int l=asioDrivers->asioGetNumDev();
  printf("ASIO: %d drivers available\n",l);
  if (l < 1) return -1;
  int x;

  char *olddev=*dev;
  int driverindex=*dev ? atoi(*dev) : -1;
  for (x = 0; x < l; x ++)
  {
    char buf[256];
    asioDrivers->asioGetDriverName(x,buf,sizeof(buf));
    if (x == driverindex || (driverindex<0 && !x)) *dev = strdup(buf);
    printf("  #%d: %s\n",x,buf);
  }
  if (driverindex < 0) 
  {
    printf("ASIO Use -in <driverindex> on the command line to specify a driver\n");
    printf("(using -in 1c will show the asio control panel for before launching)\n");
    printf("You can also append :inch1,inch2:outch1,outch2\n");
    return -1;
  }
  else if (driverindex < 0) driverindex=0;

  printf("Loading driver (%s): ",*dev);
  fflush(stdout);
  if (!asioDrivers->loadDriver(*dev)) 
  {
    printf("error!\n");
    return -1;
  }
  printf("done\n");

  m_driver_active=1;
  //myDriverInfo.driverInfo.sysRef=(void*)GetDesktopWindow();
  //myDriverInfo.driverInfo.asioVersion=2;

  printf("Initializing driver: "); fflush(stdout);
  if (ASIOInit(&myDriverInfo.driverInfo) != ASE_OK)
  {
    printf("error!\n");    
    return -1;
  }
  printf("done\n");

  if (olddev && strstr(olddev,"c")) 
  {
    printf("Displaying ASIO control panel!\n");
    ASIOControlPanel();
    printf("Hit any key to continue (MAKE SURE THE PANEL HAS BEEN CLOSED)...\n");
    getchar();
  }
  int inchoffs[2]={0};
  int outchoffs[2]={0};
  char *p;
  if (olddev && (p=strstr(olddev,":")))
  {
    p++;
    if (*p)
    {
      inchoffs[0]=atoi(p);
      while (*p && *p != ',' && *p != ':') p++;
      if (*p == ',')
      {
        p++;
        if (*p)
        {
          inchoffs[1]=atoi(p);
          while (*p && *p != ':') p++;
        }
      }
      if (*p == ':')
      {
        p++;
        outchoffs[0]=atoi(p);
        while (*p && *p != ',') p++;
        if (*p == ',')
        {
          p++;
          if (*p)
          {
            outchoffs[1]=atoi(p);
            while (*p && *p != ':') p++;
          }
        }
      }
    }
  }

  m_driver_active=2;

	if (ASIOGetChannels(&myDriverInfo.inputChannels, &myDriverInfo.outputChannels) != ASE_OK) 
  {
    printf("Error getting ASIO channels!\n");
    return -1;
  }

  printf ("ASIO channel info: %d inputs, %d outputs\n", (int)myDriverInfo.inputChannels, (int)myDriverInfo.outputChannels);

	// get the usable buffer sizes
  if(ASIOGetBufferSize(&myDriverInfo.minSize, &myDriverInfo.maxSize, &myDriverInfo.preferredSize, &myDriverInfo.granularity) != ASE_OK)
  {
    printf("Error getting ASIO buffer sizes\n");
    return -1;
  }

	printf ("ASIO preferred buffer size: %d samples\n", (int)myDriverInfo.preferredSize);

	// get the currently selected sample rate
	if(ASIOGetSampleRate(&myDriverInfo.sampleRate) != ASE_OK)
  {
    printf("Error getting ASIO samplerate\n");
    return -1;
  }

	if (myDriverInfo.sampleRate <= 0.0 || myDriverInfo.sampleRate > 192000.0)
	{
		// Driver does not store it's internal sample rate, so set it to a know one.
		// Usually you should check beforehand, that the selected sample rate is valid
		// with ASIOCanSampleRate().
		if(ASIOSetSampleRate(44100.0) != ASE_OK || ASIOGetSampleRate(&myDriverInfo.sampleRate) != ASE_OK)
		{
      printf("Error trying to set a default (44kHz) samplerate\n");
			return -1;
		}
	}
	printf ("ASIO sample rate: %f\n", myDriverInfo.sampleRate);

	// check wether the driver requires the ASIOOutputReady() optimization
	// (can be used by the driver to reduce output latency by one block)
	if(ASIOOutputReady() == ASE_OK) myDriverInfo.postOutput = true;
	else myDriverInfo.postOutput = false;
	//printf ("ASIOOutputReady(); - %s\n", myDriverInfo.postOutput ? "Supported" : "Not supported");

	asioCallbacks.bufferSwitch = &bufferSwitch;
	asioCallbacks.sampleRateDidChange = &sampleRateChanged;
	asioCallbacks.asioMessage = &asioMessages;
	asioCallbacks.bufferSwitchTimeInfo = &bufferSwitchTimeInfo;


  ASIOBufferInfo *info = myDriverInfo.bufferInfos;

  myDriverInfo.outputBuffers = min(myDriverInfo.inputChannels,myDriverInfo.outputChannels);

  myDriverInfo.outputBuffers=myDriverInfo.inputBuffers=min(myDriverInfo.outputBuffers,nch);


  int i;
  for (i = 0; i < myDriverInfo.inputChannels; i ++)
  {
    ASIOChannelInfo c;
    c.channel=i;
    c.isInput=1;
    if (ASIOGetChannelInfo(&c) == ASE_OK) printf("ASIO Input Channel %d: '%s' \n",i,c.name);
  }
  for (i = 0; i < myDriverInfo.outputChannels; i ++)
  {
    ASIOChannelInfo c;
    c.channel=i;
    c.isInput=0;
    if (ASIOGetChannelInfo(&c) == ASE_OK) printf("ASIO Output Channel %d: '%s' \n",i,c.name);
  }

// this should work, but it doesn't seem to :(
//  if (myDriverInfo.inputBuffers == 2 && inchoffs[0] == inchoffs[1])
//    myDriverInfo.inputBuffers=1;

  //{
    //char buf[512];
    //sprintf(buf,"outputting asio to channels %d and %d\n",outchoffs[0],outchoffs[1]);
    //OutputDebugString(buf);
//  }

  if (myDriverInfo.outputBuffers == 2 && outchoffs[0] == outchoffs[1])
    myDriverInfo.outputBuffers=1;


	for(i = 0; i < myDriverInfo.inputBuffers; i++, info++)
	{
		info->isInput = ASIOTrue;
		info->channelNum = inchoffs[i];
		info->buffers[0] = info->buffers[1] = 0;
	}
	for(i = 0; i < myDriverInfo.outputBuffers; i++, info++)
	{
		info->isInput = ASIOFalse;
		info->channelNum = outchoffs[i];
		info->buffers[0] = info->buffers[1] = 0;
	}

	ASIOError result = ASIOCreateBuffers(myDriverInfo.bufferInfos,
		myDriverInfo.inputBuffers + myDriverInfo.outputBuffers,
		myDriverInfo.preferredSize, &asioCallbacks);

  int last_spltype=-1;

	if (result == ASE_OK)
	{
		// now get all the buffer details, sample word length, name, word clock group and activation
		for (i = 0; i < myDriverInfo.inputBuffers + myDriverInfo.outputBuffers; i++)
		{
			myDriverInfo.channelInfos[i].channel = myDriverInfo.bufferInfos[i].channelNum;
			myDriverInfo.channelInfos[i].isInput = myDriverInfo.bufferInfos[i].isInput;
			result = ASIOGetChannelInfo(&myDriverInfo.channelInfos[i]);

      //printf("Channel %d info: %s\n",i,myDriverInfo.channelInfos[i].name);

      if (last_spltype < 0) last_spltype = myDriverInfo.channelInfos[i].type;
      else if (myDriverInfo.channelInfos[i].type != last_spltype)
      {
        printf("ASIO: Different sample types per stream, can't deal!\n");
        return -1;
      }
			if (result != ASE_OK)
				break;
		}

		if (result == ASE_OK)
		{
			// get the input and output latencies
			// Latencies often are only valid after ASIOCreateBuffers()
			// (input latency is the age of the first sample in the currently returned audio block)
			// (output latency is the time the first sample in the currently returned audio block requires to get to the output)
			//result = ASIOGetLatencies(&myDriverInfo.inputLatency, &myDriverInfo.outputLatency);
			//if (result == ASE_OK)
			//	printf ("ASIOGetLatencies (input: %d, output: %d);\n", myDriverInfo.inputLatency, myDriverInfo.outputLatency);
		}
	}

	if (result != ASE_OK)
	{
    printf("ASIO: Error initializing buffers\n");
    return -1;
  }

  m_driver_active=3;

  m_sleep =   sleep >= 0 ? sleep : 0;

  m_nch = (int)2;
  m_srate=(int)myDriverInfo.sampleRate;
  if (last_spltype == ASIOSTInt16LSB)
  {
    bps=16;
  }
  else if (last_spltype == ASIOSTInt24LSB)
  {
    bps=24;
  }
  else if (last_spltype == ASIOSTInt32LSB)
  {
    bps=32;
  }
  else
  {
    printf("ASIO: unknown sample type '%d'. I currently only support 16 and 24 bit LSB.\n",last_spltype);
    return -1;
  }
  m_bps=bps;

  *nbufs=1;
  m_bufsize=*bufsize=(int)myDriverInfo.preferredSize*m_nch*sizeof(float);
  free(asio_pcmbuf);
  asio_pcmbuf = (float *)malloc(m_bufsize);
  myDriverInfo.bytesProcessed=0;

  //InitializeCriticalSection(&myDriverInfo.cs);

  if (ASIOStart() != ASE_OK)
  {
    printf("ASIO: Error starting\n");
    return -1;
  }

  //EnterCriticalSection(&myDriverInfo.cs);

  m_driver_active=4;

  return 0;
}

int audioStreamer_ASIO::Read(char *buf, int len) // returns 0 if blocked, < 0 if error, > 0 if data
{
  //LeaveCriticalSection(&myDriverInfo.cs);
  Sleep(20);
  //EnterCriticalSection(&myDriverInfo.cs);

//  len=myDriverInfo.bytesProcessed;
  //myDriverInfo.bytesProcessed=0;

  return 0;
}

int audioStreamer_ASIO::Write(char *buf, int len) // returns 0 on success
{ 
  return 0;
}

#endif//SUPPORT_ASIO


