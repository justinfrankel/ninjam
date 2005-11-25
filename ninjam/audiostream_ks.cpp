/*
    NINJAM - audiostream_ks.cpp
    Copyright (C) 2004-2005 Cockos Incorporated

    NINJAM is free software; you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation; either version 2 of the License, or
    (at your option) any later version.

    NINJAM is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU General Public License for more details.

    You should have received a copy of the GNU General Public License
    along with NINJAM; if not, write to the Free Software
    Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
*/

/*

  This file implements an audioStreamer that uses Kernel Streaming.  
  It only exposes the following function:

  audioStreamer *create_audioStreamer_KS(int srate, int bps, int *nbufs, int *bufsize, SPLPROC proc);

  (everything else in this file is used internally)

*/

#include <windows.h>
#include <mmsystem.h>

#include "audiostream.h"

#include "../WDL/pcmfmtcvt.h"

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



#ifndef NO_SUPPORT_KS
//////////////////////////////
/// Kernel Streaming


#include "ks/kssample.h"

class audioStreamer_KS
{
	public:
		audioStreamer_KS();
		~audioStreamer_KS();
		int Open(int iswrite, int srate, int bps, int *nbufs, int *bufsize);
		int Read(char *buf, int len); // returns 0 if blocked, < 0 if error, > 0 if data
		int Write(char *buf, int len); // returns 0 on success


    int GetLatency() { return m_inqueue; }

  private:

    int m_inqueue;
    int m_nbufs;
    int m_srate, m_bps;
    int read_pos;
    void StartRead(int idx, int len);

    KSSTATE m_state;
    CKsAudRenFilter *m_pFilter;
    CKsAudRenPin *m_pPin;

    CKsAudCapFilter *m_pFilter_cap;
    CKsAudCapPin *m_pPin_cap;

    // data packet stuff

    struct DATA_PACKET
    {
        KSSTREAM_HEADER Header;
        OVERLAPPED      Signal;
    };
    ULONG cbBuffer;
    ULONG       cbPartialBuffer;
    PBYTE   pBuffer;
    HANDLE      *hEventPool;
    DATA_PACKET *Packets;

};


class audioStreamer_KS_asiosim : public audioStreamer
{
	public:
		audioStreamer_KS_asiosim(audioStreamer_KS *i, audioStreamer_KS *o, int bufsize, int srate, int bps, SPLPROC proc)
    {
      m_splproc=proc;
      in=i;
      out=o;
      DWORD id;
      m_bps=bps;
      m_innch=m_outnch=2;
      m_bps=bps;
      m_srate=srate;
      m_done=0;
      m_buf=(char *)malloc(bufsize);
      m_bufsize=bufsize;

      m_procbuf=(float *)malloc((bufsize*64)/bps);// allocated 2x, input and output
      hThread=CreateThread(NULL,0,threadProc,(LPVOID)this,0,&id);
      SetThreadPriority(hThread,THREAD_PRIORITY_HIGHEST);
    }

		~audioStreamer_KS_asiosim()
    {
      m_done=1;
      WaitForSingleObject(hThread,INFINITE);
      CloseHandle(hThread);
      delete in;
      delete out;
      free(m_buf);
      free(m_procbuf);
    }

    const char *GetChannelName(int idx)
    {
      if (idx == 0x80000000) return (const char *)(2);
      if (idx == 0) return "KS Left";
      if (idx == 1) return "KS Right";
      return NULL;
    }

	private:
    void tp();
    static DWORD WINAPI threadProc(LPVOID p)
    {
      audioStreamer_KS_asiosim *t=(audioStreamer_KS_asiosim*)p;
      t->tp();
      return 0;
    }
    audioStreamer_KS *in, *out;
    
    HANDLE hThread;
    int m_done,m_bufsize;
    char *m_buf;
    float *m_procbuf;

    SPLPROC m_splproc;
};


void audioStreamer_KS_asiosim::tp()
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
    else Sleep(1);
  }
}


audioStreamer *create_audioStreamer_KS(int srate, int bps, int *nbufs, int *bufsize, SPLPROC proc)
{
  audioStreamer_KS *in=new audioStreamer_KS();
  if (in->Open(0,srate,bps,nbufs,bufsize))
  {
    delete in;
    return 0;
  }
  audioStreamer_KS *out=new audioStreamer_KS();
  if (out->Open(1,srate,bps,nbufs,bufsize))
  {
    delete in;
    delete out;
    return 0;
  }

  return new audioStreamer_KS_asiosim(in,out,*bufsize,srate,bps, proc);
}


audioStreamer_KS::audioStreamer_KS()
{
  m_inqueue=0;
  m_state=KSSTATE_STOP;
  m_pFilter=NULL;
  m_pFilter_cap=NULL;
  m_pPin=NULL;
  m_pPin_cap=NULL;
  pBuffer=NULL;
  read_pos=-1;
  hEventPool=0;
  Packets=0;
  m_nbufs=1;
}

audioStreamer_KS::~audioStreamer_KS()
{
  // stop the pin
  if(m_pPin)
  {
    m_pPin->SetState(KSSTATE_PAUSE);
    m_pPin->SetState(KSSTATE_STOP);
    m_pPin->ClosePin();
  }
  if(m_pPin_cap)
  {
    m_pPin_cap->SetState(KSSTATE_PAUSE);
    m_pPin_cap->SetState(KSSTATE_STOP);
    m_pPin_cap->ClosePin();
  }

	m_state = KSSTATE_STOP;

  if (m_pFilter) m_pFilter->Close();
  if (m_pFilter_cap) m_pFilter_cap->Close();
//  delete m_pFilter;
  //delete m_pFilter_cap;

	free(pBuffer);

  if (hEventPool) for(int i = 0; i < m_nbufs; i++)
  {
    CloseHandle(hEventPool[i]);
	}
  free(hEventPool);
  free(Packets);
}

int audioStreamer_KS::Open(int iswrite, int srate, int bps, int *nbufs, int *bufsize)
{
  if (*nbufs < 1) *nbufs=8;
  if (*nbufs > 128) *nbufs=128;
  m_nbufs=*nbufs;
  if (iswrite) m_inqueue=m_nbufs;
  else m_inqueue=0;

  if (*bufsize > 8192) *bufsize=8192;
  else if (*bufsize < 0) *bufsize=512;

  m_srate=srate;
  m_bps=bps;

  // enumerate audio renderers
  HRESULT hr;
  CKsEnumFilters* pEnumerator = new CKsEnumFilters(&hr);
	if(pEnumerator == NULL)
	{
		myPrintf("Failed to allocate CKsEnumFilters\n");
		return -1;
	}
	else if(!SUCCEEDED(pEnumerator))
	{
		myPrintf("Failed to create CKsEnumFilters\n");
		return -1;
	}

  GUID  aguidEnumCats[] = { STATIC_KSCATEGORY_AUDIO, STATIC_KSCATEGORY_RENDER };
  GUID  aguidEnumCatsCap[] = { STATIC_KSCATEGORY_AUDIO, STATIC_KSCATEGORY_CAPTURE };

  hr = 
        pEnumerator->EnumFilters
        (
            eAudRen,            // create audio render filters ...
            iswrite?aguidEnumCats:aguidEnumCatsCap,      // ... of these categories
            2,                  // There are 2 categories
            TRUE,               // While you're at it, enumerate the pins
            FALSE,              // ... but don't bother with nodes
            TRUE                // Instantiate the filters
        );

  if(!SUCCEEDED(hr))
	{
		myPrintf("CKsEnumFilters::EnumFilters failed\n");
		return -1;
	}

  if(iswrite)
  {
    //render

  pEnumerator->m_listFilters.GetHead((CKsFilter**)&m_pFilter);	// just grab the first one we find TODO> fixme
	/*	// picking a specific output device
	LISTPOS lp = pEnumerator->m_listFilters.GetHeadPosition();
	pEnumerator->m_listFilters.GetNext(lp, (CKsFilter**)&pFilter);	// once gives my WaveForce
	pEnumerator->m_listFilters.GetNext(lp, (CKsFilter**)&pFilter);	// do 2 times to get my Creative AudioPCI
	*/
  if(m_pFilter == NULL)
	{
		myPrintf("No filters available for rendering\n");
		return -1;
	}

  int nch=2;
	// instantiate the pin
  // use WAVEFORMATEXTENSIBLE to describe wave format
  WAVEFORMATEXTENSIBLE wfx;
  wfx.Format.wFormatTag = WAVE_FORMAT_EXTENSIBLE;
  wfx.Format.nChannels = nch;
  wfx.Format.nSamplesPerSec = srate;
  wfx.Format.nBlockAlign = nch*bps/8;
  wfx.Format.nAvgBytesPerSec = wfx.Format.nSamplesPerSec * wfx.Format.nBlockAlign;
  wfx.Format.wBitsPerSample = bps;
  wfx.Format.cbSize = sizeof(WAVEFORMATEXTENSIBLE)-sizeof(WAVEFORMATEX);
  wfx.Samples.wValidBitsPerSample = bps;
  wfx.dwChannelMask = KSAUDIO_SPEAKER_STEREO;
  wfx.SubFormat = KSDATAFORMAT_SUBTYPE_PCM;

  m_pPin = m_pFilter->CreateRenderPin(&wfx.Format, FALSE);

  if (!m_pPin)
  {
    // driver can't handle WAVEFORMATEXTENSIBLE, so fall back to
    // WAVEFORMATEX format descriptor and try again
    wfx.Format.wFormatTag = WAVE_FORMAT_PCM;
    // set unused members to zero
    wfx.Format.cbSize = 0;
    wfx.Samples.wValidBitsPerSample = 0;
    wfx.dwChannelMask = 0;
    wfx.SubFormat = GUID_NULL;

    m_pPin = m_pFilter->CreateRenderPin(&wfx.Format, FALSE);
  }

  if (!m_pPin)
  {
    myPrintf("Can't create pin for rendering!\n");
    return -1;
  }

  }
  else 
  {
    //capture
    pEnumerator->m_listFilters.GetHead((CKsFilter**)&m_pFilter_cap);	// just grab the first one we find TODO> fixme
    if(m_pFilter_cap == NULL)
  	{
  		myPrintf("No filters available for capturing\n");
  		return -1;
  	}
    int nch=2;
  	// instantiate the pin
    // use WAVEFORMATEXTENSIBLE to describe wave format
    WAVEFORMATEXTENSIBLE wfx;
    wfx.Format.wFormatTag = WAVE_FORMAT_EXTENSIBLE;
    wfx.Format.nChannels = nch;
    wfx.Format.nSamplesPerSec = srate;
    wfx.Format.nBlockAlign = nch*bps/8;
    wfx.Format.nAvgBytesPerSec = wfx.Format.nSamplesPerSec * wfx.Format.nBlockAlign;
    wfx.Format.wBitsPerSample = bps;
    wfx.Format.cbSize = sizeof(WAVEFORMATEXTENSIBLE)-sizeof(WAVEFORMATEX);
    wfx.Samples.wValidBitsPerSample = bps;
    wfx.dwChannelMask = KSAUDIO_SPEAKER_STEREO;
    wfx.SubFormat = KSDATAFORMAT_SUBTYPE_PCM;

    m_pPin_cap = m_pFilter_cap->CreateCapturePin(&wfx.Format, FALSE);

    if (!m_pPin_cap)
    {
      // driver can't handle WAVEFORMATEXTENSIBLE, so fall back to
      // WAVEFORMATEX format descriptor and try again
      wfx.Format.wFormatTag = WAVE_FORMAT_PCM;
      // set unused members to zero
      wfx.Format.cbSize = 0;
      wfx.Samples.wValidBitsPerSample = 0;
      wfx.dwChannelMask = 0;
      wfx.SubFormat = GUID_NULL;

      m_pPin_cap = m_pFilter_cap->CreateCapturePin(&wfx.Format, FALSE);
    }

    if (!m_pPin_cap)
    {
      myPrintf("Can't create pin for capturing!\n");
      return -1;
    }

  }

  // allocate m_nbufs *bufsize byte buffers - should *always* work because Write() should never write more than 8192b
	cbPartialBuffer = *bufsize;
	cbBuffer = m_nbufs * cbPartialBuffer;
  Packets = (DATA_PACKET *)calloc(sizeof(DATA_PACKET),m_nbufs);
  hEventPool = (HANDLE *)calloc(sizeof(HANDLE),m_nbufs);

  pBuffer = (BYTE *)calloc(cbBuffer,1);

	// set up the data packets
  for(int i = 0; i < m_nbufs; i++)
  {
#define Zero(s) ZeroMemory(&s, sizeof(s))
    Zero(Packets[i]);
    hEventPool[i] = CreateEvent(NULL, TRUE, TRUE, NULL);	// NO autoreset!

    Packets[i].Signal.hEvent = hEventPool[i];
    Packets[i].Header.Data = pBuffer + i * cbPartialBuffer;
    Packets[i].Header.FrameExtent = cbPartialBuffer;
    Packets[i].Header.DataUsed = iswrite?cbPartialBuffer:0;  // if we were capturing, we would init this to 0
    Packets[i].Header.Size = sizeof(Packets[i].Header);
    Packets[i].Header.PresentationTime.Numerator = 1;
    Packets[i].Header.PresentationTime.Denominator = 1;
  }

  // set new state
  m_state = KSSTATE_RUN;
  if(iswrite)
    hr = m_pPin->SetState(m_state);
  else
    hr = m_pPin_cap->SetState(m_state);

	if(!SUCCEEDED(hr))
	{
		myPrintf("Error in pPin->SetState(KSSTATE_RUN) !\n");
		return -1;
	}

  return 0;
}

void audioStreamer_KS::StartRead(int idx, int len) // returns 0 if blocked, < 0 if error, > 0 if data
{
	ResetEvent(Packets[idx].Signal.hEvent);
  Packets[idx].Header.FrameExtent = len;
  Packets[idx].Header.DataUsed = 0;
  HRESULT hr = m_pPin_cap->ReadData(&Packets[idx].Header, &Packets[idx].Signal);
	if(!SUCCEEDED(hr)) 
  {
    SetEvent(Packets[idx].Signal.hEvent);
    return;
  }
}

int audioStreamer_KS::Read(char *buf, int len) // returns 0 if blocked, < 0 if error, > 0 if data
{
  if (read_pos < 0)
  {
    int i;
    for (i = 0; i < m_nbufs; i ++)
    {
      StartRead(i,len);
    }
    read_pos=0;
  }
  WaitForSingleObject(hEventPool[read_pos],INFINITE);
  //myPrintf("read: %x\n",Packets[dwWait].Header.DataUsed);

  int rlen=Packets[read_pos].Header.DataUsed;
  memcpy(buf,Packets[read_pos].Header.Data,rlen);
  StartRead(read_pos,len);

  read_pos++;
  if (read_pos >= m_nbufs) read_pos=0;
  return rlen;
}

int audioStreamer_KS::Write(char *buf, int len) // returns 0 on success
{ 
	DWORD dwWait = WaitForMultipleObjects(m_nbufs, hEventPool, FALSE, INFINITE);
	if(dwWait == WAIT_FAILED) return 1;

  if (m_inqueue>0)
    m_inqueue--;

	dwWait -= WAIT_OBJECT_0;

	memcpy(Packets[dwWait].Header.Data, buf, len);

	Packets[dwWait].Header.FrameExtent = len;
	Packets[dwWait].Header.DataUsed = len;

	// key - no using autoreset events!!! (yes, I spent 3 hours figuring this out the *hard* way)
	ResetEvent(Packets[dwWait].Signal.hEvent);

  m_inqueue++;
	HRESULT hr = m_pPin->WriteData(&Packets[dwWait].Header, &Packets[dwWait].Signal);
	if(!SUCCEEDED(hr)) return 1;	// or does it really matter?

  return 0;
}

#endif

