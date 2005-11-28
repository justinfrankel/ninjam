/*
    NINJAM ASIO driver - audiostream_asio.cpp
    Copyright (C) 2005 Cockos Incorporated

  
    This software is provided 'as-is', without any express or implied
    warranty.  In no event will the authors be held liable for any damages
    arising from the use of this software.

    Permission is granted to anyone to use this software for any purpose,
    including commercial applications, and to alter it and redistribute it
    freely, subject to the following restrictions:

    1. The origin of this software must not be misrepresented; you must not
       claim that you wrote the original software. If you use this software
       in a product, an acknowledgment in the product documentation would be
       appreciated but is not required.
    2. Altered source versions must be plainly marked as such, and must not be
       misrepresented as being the original software.
    3. This notice may not be removed or altered from any source distribution.
    

*/

/*

  This provides ASIO input/output functionality for NINJAM. The reason it is 
  in a seperate DLL file is so that people who want to develop the GPL'd 
  client can do so without having to agree to Steinberg's ASIO SDK license 
  agreement.  This file implements an audioStreamer object that uses ASIO.
*/

#include <windows.h>
#include <mmsystem.h>

#include <math.h>
#include "../audiostream.h"



// this streamer object can only handle one instance
static SPLPROC _sample_proc;

#include "../../WDL/pcmfmtcvt.h"

#include <stdio.h>


#if 1
static void myPrintf(char *s, ... ) { }

#else
//#ifdef 
#include <stdarg.h>
//#include <vargs.h>

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





class audioStreamer_ASIO  : public audioStreamer
{
	public:
		audioStreamer_ASIO();
		~audioStreamer_ASIO();
		int Open(char **dev);

    const char *GetChannelName(int idx);

	private:
    
    int m_latency;
    char *m_chnames[256];
    int m_driver_active;
};


audioStreamer *create_audioStreamer_ASIO(char **dev, SPLPROC proc)
{

  _sample_proc=proc;

  audioStreamer_ASIO *audio=new audioStreamer_ASIO;

  if (!audio->Open(dev)) return audio;
  delete audio;
  
  return 0;
}



//////////////////////////////
/// ASIO
#include <asiosys.h>
#include <asio.h>
#include <asiodrivers.h>

// GCC only: #include "iasiothiscallresolver.h"

enum {
	// number of input and outputs supported by the host application
	// you can change these to higher or lower values
	kMaxInputChannels = 32,
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
	long           inputLatency;
	long           outputLatency;

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
    float *outptrs[kMaxOutputChannels]={0,};
    float *inptrs[kMaxInputChannels]={0,};
    float *ptr=asio_pcmbuf;

    int x;
    for (x = 0; x < myDriverInfo.inputBuffers; x ++)
    {
      inptrs[x]=ptr;
      pcmToFloats(myDriverInfo.bufferInfos[x].buffers[index],buffSize,splsize*8,1,ptr,1);
      ptr+=buffSize;
    }
    for (x = 0; x < myDriverInfo.outputBuffers; x ++)
    {
      outptrs[x]=ptr;
      ptr+=buffSize;
    }

    {
//      EnterCriticalSection(&myDriverInfo.cs);

      if (_sample_proc) _sample_proc(inptrs,myDriverInfo.inputBuffers,outptrs,myDriverInfo.outputBuffers,buffSize,(int)myDriverInfo.sampleRate);

  //    LeaveCriticalSection(&myDriverInfo.cs);
    }

    for (x = 0; x < myDriverInfo.outputBuffers; x ++)
    {
      floatsToPcm(outptrs[x],1,buffSize,myDriverInfo.bufferInfos[myDriverInfo.inputBuffers+x].buffers[index],splsize*8,1);
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
  m_latency=0;
  m_driver_active=0;
  asioDrivers=0;
  memset(m_chnames,0,sizeof(m_chnames));
}

const char *audioStreamer_ASIO::GetChannelName(int idx)
{
  if (idx == 0x80000001) return (const char *)myDriverInfo.outputLatency;
  if (idx == 0x80000000)
  {
    return (const char *)m_latency;
  }
  if (idx < 0 || idx >= m_innch) return NULL;
  return m_chnames[idx];
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
  unsigned int x;
  for (x = 0; x < sizeof(m_chnames)/sizeof(m_chnames[0]); x++)
  {
    free(m_chnames[x]);
    m_chnames[x]=0;
  }

}

int audioStreamer_ASIO::Open(char **dev)
{
	if(!asioDrivers)
		asioDrivers = new AsioDrivers();

  if (!asioDrivers)
  {
    myPrintf("Error initializing ASIO\n");
    return -1;
  }

  int l=asioDrivers->asioGetNumDev();
  myPrintf("ASIO: %d drivers available\n",l);
  if (l < 1) return -1;
  int x;

  char *olddev=*dev;
  int driverindex=*dev ? atoi(*dev) : -1;
  for (x = 0; x < l; x ++)
  {
    char buf[256];
    asioDrivers->asioGetDriverName(x,buf,sizeof(buf));
    if (x == driverindex || (driverindex<0 && !x)) *dev = strdup(buf);
    myPrintf("  #%d: %s\n",x,buf);
  }
  if (driverindex < 0) 
  {
    myPrintf("ASIO Use -in <driverindex> on the command line to specify a driver\n");
    myPrintf("(using -in 1c will show the asio control panel for before launching)\n");
    myPrintf("You can also append :inch1,inch2:outch1,outch2\n");
    return -1;
  }
  else if (driverindex < 0) driverindex=0;

  myPrintf("Loading driver (%s): ",*dev);
  if (!asioDrivers->loadDriver(*dev)) 
  {
    myPrintf("error!\n");
    return -1;
  }
  myPrintf("done\n");

  m_driver_active=1;
  //myDriverInfo.driverInfo.sysRef=(void*)GetDesktopWindow();
  //myDriverInfo.driverInfo.asioVersion=2;

  myPrintf("Initializing driver: "); 
  if (ASIOInit(&myDriverInfo.driverInfo) != ASE_OK)
  {
    myPrintf("error!\n");    
    return -1;
  }
  myPrintf("done\n");

  if (olddev && strstr(olddev,"c")) 
  {
    //myPrintf("Displaying ASIO control panel!\n");
    ASIOControlPanel();
    //myPrintf("Hit any key to continue (MAKE SURE THE PANEL HAS BEEN CLOSED)...\n");
    //getchar();
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
    myPrintf("Error getting ASIO channels!\n");
    return -1;
  }

  myPrintf ("ASIO channel info: %d inputs, %d outputs\n", (int)myDriverInfo.inputChannels, (int)myDriverInfo.outputChannels);

	// get the usable buffer sizes
  if(ASIOGetBufferSize(&myDriverInfo.minSize, &myDriverInfo.maxSize, &myDriverInfo.preferredSize, &myDriverInfo.granularity) != ASE_OK)
  {
    myPrintf("Error getting ASIO buffer sizes\n");
    return -1;
  }

	myPrintf ("ASIO preferred buffer size: %d samples\n", (int)myDriverInfo.preferredSize);

	// get the currently selected sample rate
	if(ASIOGetSampleRate(&myDriverInfo.sampleRate) != ASE_OK)
  {
    myPrintf("Error getting ASIO samplerate\n");
    return -1;
  }

	if (myDriverInfo.sampleRate <= 0.0 || myDriverInfo.sampleRate > 192000.0)
	{
		// Driver does not store it's internal sample rate, so set it to a know one.
		// Usually you should check beforehand, that the selected sample rate is valid
		// with ASIOCanSampleRate().
		if(ASIOSetSampleRate(44100.0) != ASE_OK || ASIOGetSampleRate(&myDriverInfo.sampleRate) != ASE_OK)
		{
      myPrintf("Error trying to set a default (44kHz) samplerate\n");
			return -1;
		}
	}
	myPrintf ("ASIO sample rate: %f\n", myDriverInfo.sampleRate);

	// check wether the driver requires the ASIOOutputReady() optimization
	// (can be used by the driver to reduce output latency by one block)
	if(ASIOOutputReady() == ASE_OK) myDriverInfo.postOutput = true;
	else myDriverInfo.postOutput = false;
	//myPrintf ("ASIOOutputReady(); - %s\n", myDriverInfo.postOutput ? "Supported" : "Not supported");

	asioCallbacks.bufferSwitch = &bufferSwitch;
	asioCallbacks.sampleRateDidChange = &sampleRateChanged;
	asioCallbacks.asioMessage = &asioMessages;
	asioCallbacks.bufferSwitchTimeInfo = &bufferSwitchTimeInfo;


  ASIOBufferInfo *info = myDriverInfo.bufferInfos;

  myDriverInfo.outputBuffers = min(myDriverInfo.outputChannels,kMaxOutputChannels);

  myDriverInfo.inputBuffers=inchoffs[1]-inchoffs[0]+1;
  if (myDriverInfo.inputBuffers > myDriverInfo.inputChannels) myDriverInfo.inputBuffers = myDriverInfo.inputChannels;
  if (myDriverInfo.inputBuffers > kMaxInputChannels) myDriverInfo.inputBuffers = kMaxInputChannels;

  int i;
  for (i = 0; i < myDriverInfo.inputChannels; i ++)
  {
    ASIOChannelInfo c;
    c.channel=i;
    c.isInput=1;
    if (ASIOGetChannelInfo(&c) == ASE_OK) 
    {
      myPrintf("ASIO Input Channel %d: '%s' \n",i,c.name);
    }
    else sprintf(c.name,"Channel %d",i);

    if (i >= inchoffs[0] && i <= inchoffs[1])
    {
      int pos=i-inchoffs[0];
      if (pos >=0 && pos <= 255)
        m_chnames[pos]=strdup(c.name);
    }
  }
  for (i = 0; i < myDriverInfo.outputChannels; i ++)
  {
    ASIOChannelInfo c;
    c.channel=i;
    c.isInput=0;
    if (ASIOGetChannelInfo(&c) == ASE_OK) myPrintf("ASIO Output Channel %d: '%s' \n",i,c.name);
  }

  if (myDriverInfo.outputBuffers == 2 && outchoffs[0] == outchoffs[1])
    myDriverInfo.outputBuffers=1;


	for(i = 0; i < myDriverInfo.inputBuffers; i++, info++)
	{
		info->isInput = ASIOTrue;
		info->channelNum = inchoffs[0]+i;
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

      //myPrintf("Channel %d info: %s\n",i,myDriverInfo.channelInfos[i].name);

      if (last_spltype < 0) last_spltype = myDriverInfo.channelInfos[i].type;
      else if (myDriverInfo.channelInfos[i].type != last_spltype)
      {
        myPrintf("ASIO: Different sample types per stream, can't deal!\n");
        return -1;
      }
			if (result != ASE_OK)
				break;
		}

		//if (result == ASE_OK)
		{
			// get the input and output latencies
			// Latencies often are only valid after ASIOCreateBuffers()
			// (input latency is the age of the first sample in the currently returned audio block)
			// (output latency is the time the first sample in the currently returned audio block requires to get to the output)
			result = ASIOGetLatencies(&myDriverInfo.inputLatency, &myDriverInfo.outputLatency);
			if (result != ASE_OK)
      {
        m_latency = myDriverInfo.preferredSize*2;
        myDriverInfo.inputLatency=myDriverInfo.outputLatency=0;
      }
      else
      {
        m_latency=myDriverInfo.inputLatency+myDriverInfo.outputLatency;
        if (m_latency < myDriverInfo.preferredSize*2) m_latency=myDriverInfo.preferredSize*2;

        char buf[512];
        sprintf(buf,"Got latencies of %d/%d/%d",myDriverInfo.inputLatency,myDriverInfo.outputLatency,myDriverInfo.preferredSize);
        OutputDebugString(buf);
      }
			//	myPrintf ("ASIOGetLatencies (input: %d, output: %d);\n", myDriverInfo.inputLatency, myDriverInfo.outputLatency);
		}
	}

	if (result != ASE_OK)
	{
    myPrintf("ASIO: Error initializing buffers\n");
    return -1;
  }

  m_driver_active=3;

  m_innch = (int)myDriverInfo.inputBuffers;
  m_outnch = (int)myDriverInfo.outputBuffers;
  m_srate=(int)myDriverInfo.sampleRate;
  if (last_spltype == ASIOSTInt16LSB)
  {
    m_bps=16;
  }
  else if (last_spltype == ASIOSTInt24LSB)
  {
    m_bps=24;
  }
  else if (last_spltype == ASIOSTInt32LSB)
  {
    m_bps=32;
  }
  else
  {
    myPrintf("ASIO: unknown sample type '%d'. I currently only support 16 and 24 bit LSB.\n",last_spltype);
    return -1;
  }

  int bsize=(int)myDriverInfo.preferredSize*sizeof(float);
  free(asio_pcmbuf);
  asio_pcmbuf = (float *)malloc(bsize*(m_innch+m_outnch));
  myDriverInfo.bytesProcessed=0;

  //InitializeCriticalSection(&myDriverInfo.cs);

  if (ASIOStart() != ASE_OK)
  {
    myPrintf("ASIO: Error starting\n");
    return -1;
  }

  //EnterCriticalSection(&myDriverInfo.cs);

  m_driver_active=4;

  return 0;
}
