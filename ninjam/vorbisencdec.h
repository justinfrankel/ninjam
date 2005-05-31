#ifndef _VORBISENCDEC_H_
#define _VORBISENCDEC_H_

#include "vorbis/vorbisenc.h"
#include "../WDL/queue.h"


class VorbisEncoder
{
public:
  VorbisEncoder(int srate, int nch, float qv)
  {

    memset(&vi,0,sizeof(vi));
    memset(&vc,0,sizeof(vc));
    memset(&vd,0,sizeof(vd));
    memset(&vb,0,sizeof(vb));

    m_nch=nch;
    vorbis_info_init(&vi);

    m_err=vorbis_encode_init_vbr(&vi,nch,srate,qv);

    vorbis_comment_init(&vc);
    vorbis_analysis_init(&vd,&vi);
    vorbis_block_init(&vd,&vb);
    ogg_stream_init(&os,rand());

    if (m_err) return;


    vorbis_analysis_headerout(&vd,&vc,&header,&header_comm,&header_code);
    ogg_stream_packetin(&os,&header); /* automatically placed in its own page */
    ogg_stream_packetin(&os,&header_comm);
    ogg_stream_packetin(&os,&header_code);

	  for (;;)
    {
		  int result=ogg_stream_flush(&os,&og);
		  if(result==0)break;
      m_outqueue.Add(og.header,og.header_len);
		  m_outqueue.Add(og.body,og.body_len);
	  }


  }

  int Encode(float *in, int inlen, char *out, int outlen) // length in sample (PAIRS)
  {
    if (m_err) return 0;

    if (inlen == 0)
    {
      vorbis_analysis_wrote(&vd,0);
    }
    else
    {
      float **buffer=vorbis_analysis_buffer(&vd,inlen);
      int i,i2=0;
      for (i = 0; i < inlen; i ++)
      {
        buffer[0][i]=in[i2];
        if (m_nch==2) buffer[1][i]=in[i2+1];
        i2+=m_nch;
      }
      vorbis_analysis_wrote(&vd,i);
    }

    int eos=0;
    while(vorbis_analysis_blockout(&vd,&vb)==1)
    {
      vorbis_analysis(&vb,NULL);
      vorbis_bitrate_addblock(&vb);

      while(vorbis_bitrate_flushpacket(&vd,&op))
      {
	
      	ogg_stream_packetin(&os,&op);

	      while (!eos)
        {
		      int result=ogg_stream_flush(&os,&og);
		      if(result==0)break;
          m_outqueue.Add(og.header,og.header_len);
		      m_outqueue.Add(og.body,og.body_len);
          if(ogg_page_eos(&og)) eos=1;
	      }
      }
    }

    int l=min(m_outqueue.Available(),outlen);
    memcpy(out,m_outqueue.Get(),l);
    m_outqueue.Advance(l);
    m_outqueue.Compact();
    return l;
  }

  int isError() { return m_err; }


  ~VorbisEncoder()
  {
    ogg_stream_clear(&os);
    vorbis_block_clear(&vb);
    vorbis_dsp_clear(&vd);
    vorbis_comment_clear(&vc);
    if (!m_err) vorbis_info_clear(&vi);
  }

private:
  int m_err,m_nch;
  WDL_Queue m_outqueue;

  ogg_stream_state os;
  ogg_page         og;
  ogg_packet       op;
  vorbis_info      vi;
  vorbis_comment   vc;
  vorbis_dsp_state vd;
  vorbis_block     vb;

  ogg_packet header;
  ogg_packet header_comm;
  ogg_packet header_code;

};


#endif//_VORBISENCDEC_H_