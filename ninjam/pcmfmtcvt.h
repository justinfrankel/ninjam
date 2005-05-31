#ifndef _PCMFMTCVT_H_
#define _PCMFMTCVT_H_


static inline int float2int(double d)
{
  return (int) d;
//	  int tmp;
  //  __asm__ __volatile__ ("fistpl %0" : "=m" (tmp) : "t" (d) : "st") ;
  //  return tmp;
}


#define float_TO_INT16(out,in) \
		if ((in)<0.0) { if ((in) <= -1.0) (out) = -32768; else (out) = (short) (float2int(((in) * 32768.0)-0.5)); } \
		else { if ((in) >= 1.0) (out) = 32767; else (out) = (short) float2int((in) * 32767.0 + 0.5); }

#define INT16_TO_float(out,in) { if ((in) < 0) (out) = (float)(((double)in)/32768.0); else (out) = (float)(((double)in)/32767.0); }


static inline void i32_to_float(int i32, float *p)
{
  *p = (float) ((((double) i32)) * (1.0 / (2147483648.0)));
}

static inline void float_to_i32(float *vv, int *i32)
{
  float v = *vv;
  if (v < 0.0) 
  {
	  if (v < -1.0)
	  {
		*i32 = 0x80000000;
	  }
	  else
	  {
    		*i32=float2int(v*2147483648.0-0.5);
	  }
  }
  else
  {
	  if (v >= 1.0)
	  {
		*i32 = 0x7FFFFFFF;
	  }
	  else
	  {
  		
    		*i32=float2int(v*2147483648.0+0.5);
	  }
  }
}



static inline void i24_to_float(unsigned char *i24, float *p)
{
  int val=(i24[0]) | (i24[1]<<8) | (i24[2]<<16);
  if (val&0x800000) 
  {
	  val|=0xFF000000;
  	  *p = (float) ((((double) val)) * (1.0 / (8388608.0)));
  }
  else 
  {
	  val&=0xFFFFFF;
  	  *p = (float) ((((double) val)) * (1.0 / (8388607.0)));
  }

}

static inline void float_to_i24(float *vv, unsigned char *i24)
{
  float v = *vv;
  if (v < 0.0) 
  {
	  if (v < -1.0)
	  {
    		i24[0]=i24[1]=0x00;
    		i24[2]=0x80;
	  }
	  else
	  {
    		int i=float2int(v*8388608.0-0.5);
    		i24[0]=(i)&0xff;
    		i24[1]=(i>>8)&0xff;
    		i24[2]=(i>>16)&0xff;
	  }
  }
  else
  {
	  if (v >= 1.0)
	  {
    		i24[0]=i24[1]=0xff;
    		i24[2]=0x7f;
	  }
	  else
	  {
  		
    		int i=float2int(v*8388607+0.5);
    		i24[0]=(i)&0xff;
    		i24[1]=(i>>8)&0xff;
    		i24[2]=(i>>16)&0xff;
	  }
  }
}

#endif //_PCMFMTCVT_H_