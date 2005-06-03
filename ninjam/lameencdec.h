#ifndef _LAMEENCDEC_H_
#define _LAMEENCDEC_H_

#include "../WDL/queue.h"


class LameEncoder
{
  public:
    LameEncoder(int srate, int nch, int bitrate);
    ~LameEncoder();

    int Status() { return errorstat; } // 1=no dll, 2=error

    void Encode(float *in, int in_spls);

    WDL_Queue outqueue;

    void reinit() 
    { 
      spltmp[0].Advance(spltmp[0].Available());  
      spltmp[0].Compact();
      spltmp[1].Advance(spltmp[1].Available());  
      spltmp[1].Compact();
    }

  private:
    int m_nch;
    WDL_Queue spltmp[2];
    WDL_HeapBuf outtmp;
    int errorstat;
    int in_size_samples;
    unsigned long hbeStream;
};

class LameDecoder
{
  public:
    LameDecoder();
    ~LameDecoder();

    int GetSampleRate() { return m_srate?m_srate:48000; }
    int GetNumChannels() { return m_nch?m_nch:1; }

    WDL_HeapBuf m_samples; // we let the size get as big as it needs to, so we don't worry about tons of mallocs/etc
    int m_samples_used;

    void *DecodeGetSrcBuffer(int srclen)
    {
      if (srctmp.GetSize() < srclen) srctmp.Resize(srclen);
      return srctmp.Get();
    }
    void DecodeWrote(int srclen);

    int GetError() { return errorstat; }

    void Reset();

    int m_samplesdec;

  private:
    WDL_HeapBuf srctmp;
    int errorstat;
    int m_srate,m_nch;

    void *decinst;
};



#endif