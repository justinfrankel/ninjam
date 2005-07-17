#include <windows.h>
#include <mmsystem.h>
#include "../WDL/pcmfmtcvt.h"

#include "../WDL/ptrlist.h"
#include "audiostream_asio.h"

#define DS_SLEEP 1
#define WO_SLEEP 1



class audioStreamer_int
{
	public:
		audioStreamer_int() { m_srate=48000; m_nch=2; m_bps=16; }
		virtual ~audioStreamer_int() { }

		virtual int Read(char *buf, int len)=0; // returns 0 if blocked, < 0 if error, > 0 if data
		virtual int Write(char *buf, int len)=0; // returns 0 on success

		int m_srate, m_nch, m_bps;
};




class audioStreamer_waveOut : public audioStreamer_int
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
  m_sleep =   WO_SLEEP;

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
	  if(waveOutOpen(&m_hwo,device,&wfx,(DWORD)0,0,WAVE_FORMAT_DIRECT)!=MMSYSERR_NOERROR) return -1; 
  }
  else
  {
  	if(waveInOpen(&m_hwi,device,&wfx,0,0,WAVE_FORMAT_DIRECT)!=MMSYSERR_NOERROR) return -1; 
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
//      audiostream_onover();
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
  while (!(th->dwFlags & WHDR_DONE)) 
  {
    Sleep(WO_SLEEP); 
  }
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
      Sleep(WO_SLEEP);
#else
//      audiostream_onover();
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
//    audiostream_onunder();

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





class audioStreamer_win32_asiosim : public audioStreamer
{
	public:
		audioStreamer_win32_asiosim(audioStreamer_int *i, audioStreamer_int *o, int bufsize, int srate, int bps)
    {
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
		~audioStreamer_win32_asiosim()
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
      if (idx == 0) return "Left";
      if (idx == 1) return "Right";
      return NULL;
    }

	private:
    void tp();
    static DWORD WINAPI threadProc(LPVOID p)
    {
      audioStreamer_win32_asiosim *t=(audioStreamer_win32_asiosim*)p;
      t->tp();
      return 0;
    }
    audioStreamer_int *in, *out;
    
    HANDLE hThread;
    int m_done,m_bufsize;
    char *m_buf;
    float *m_procbuf;

};

void audioStreamer_win32_asiosim::tp()
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

      audiostream_onsamples(inptrs,2,outptrs,2,spllen,m_srate);

      floatsToPcm(outptrs[0],1,spllen,m_buf,m_bps,2);
      floatsToPcm(outptrs[1],1,spllen,m_buf+(m_bps/8),m_bps,2);
  
      out->Write(m_buf,a);
    }
    else
    {
      Sleep(1);
    }
  }
}


#include <dsound.h>

class audioStreamer_ds : public audioStreamer_int
{
	public:
		audioStreamer_ds();
		~audioStreamer_ds();
		int Open(int iswrite, int srate, int nch, int bps, int sleep, int nbufs, int bufsize, GUID *device=NULL);// guid anyway for device
		int Read(char *buf, int len); // returns 0 if blocked, < 0 if error, > 0 if data
		int Write(char *buf, int len); // returns 0 on success

	private:
	
    int m_bps,m_nch,m_srate;

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
//    audiostream_onover();
  }

  for (;;)
  {
    if (m_i_lw < m_i_dw || m_bufpos + m_bufsize < cappos) break;

    Sleep(DS_SLEEP);
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
//    audiostream_onunder();
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
      return -1;
    }


  }
  else
  {
//  	audiostream_onunder(); // g_sound_in_overruns?
  }

  return 0;
}



audioStreamer *create_audioStreamer_WO(int srate, int bps, int devs[2], int *nbufs, int *bufsize)
{
  audioStreamer_waveOut *in=new audioStreamer_waveOut();
  audioStreamer_waveOut *out=new audioStreamer_waveOut();
  if (in->Open(0,srate,2,bps,0,*nbufs,*bufsize,devs[0]))
  {
    delete in;
    delete out;
    return 0;
  }
  if (out->Open(1,srate,2,bps,0,*nbufs,*bufsize,devs[1]))
  {
    delete in;
    delete out;
    return 0;
  }

  return new audioStreamer_win32_asiosim(in,out,*bufsize,srate,bps);
}

audioStreamer *create_audioStreamer_DS(int srate, int bps, GUID devs[2], int *nbufs, int *bufsize)
{
  audioStreamer_ds *in=new audioStreamer_ds();
  if (in->Open(0,srate,2,bps,0,*nbufs,*bufsize,&devs[0]))
  {
    delete in;
    return 0;
  }
  audioStreamer_ds *out=new audioStreamer_ds();
  if (out->Open(1,srate,2,bps,0,*nbufs,*bufsize,&devs[1]))
  {
    delete in;
    delete out;
    return 0;
  }

  return new audioStreamer_win32_asiosim(in,out,*bufsize,srate,bps);
}
