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

  private:
    int m_nch;
    WDL_Queue spltmp[2];
    WDL_HeapBuf outtmp;
    int errorstat;
    int in_size_samples;
    unsigned long hbeStream;
};



#endif