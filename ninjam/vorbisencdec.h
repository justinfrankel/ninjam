#ifndef _VORBISENCDEC_H_
#define _VORBISENCDEC_H_

#include "vorbis/vorbisenc.h"
#include <vorbis/codec.h>
#include "../WDL/queue.h"
#include "../WDL/timing.h"

class VorbisDecoder
{
  public:
    VorbisDecoder()
    {
      m_samples_used=0;
    	packets=0;
	    memset(&oy,0,sizeof(oy));
	    memset(&os,0,sizeof(os));
	    memset(&og,0,sizeof(og));
	    memset(&op,0,sizeof(op));
	    memset(&vi,0,sizeof(vi));
	    memset(&vc,0,sizeof(vc));
	    memset(&vd,0,sizeof(vd));
	    memset(&vb,0,sizeof(vb));


      ogg_sync_init(&oy); /* Now we can read pages */
      m_err=0;
    }
    ~VorbisDecoder()
    {
      ogg_stream_clear(&os);
      vorbis_block_clear(&vb);
      vorbis_dsp_clear(&vd);
	    vorbis_comment_clear(&vc);
      vorbis_info_clear(&vi);

  	  ogg_sync_clear(&oy);
    }

    int GetSampleRate() { return vi.rate; }
    int GetNumChannels() { return vi.channels?vi.channels:1; }

    WDL_HeapBuf m_samples; // we let the size get as big as it needs to, so we don't worry about tons of mallocs/etc
    int m_samples_used;

    void *DecodeGetSrcBuffer(int srclen)
    {
		  return ogg_sync_buffer(&oy,srclen);
    }

    void DecodeWrote(int srclen)
    {
      ogg_sync_wrote(&oy,srclen);
  
		  while(ogg_sync_pageout(&oy,&og)>0)
		  {
			  int serial=ogg_page_serialno(&og);
			  if (!packets) ogg_stream_init(&os,serial);
			  else if (serial!=os.serialno)
			  {
				  vorbis_block_clear(&vb);
				  vorbis_dsp_clear(&vd);
				  vorbis_comment_clear(&vc);
				  vorbis_info_clear(&vi);

				  ogg_stream_clear(&os);
				  ogg_stream_init(&os,serial);
				  packets=0;
			  }
			  if (!packets)
			  {
				  vorbis_info_init(&vi);
				  vorbis_comment_init(&vc);
			  }
			  ogg_stream_pagein(&os,&og);
			  while(ogg_stream_packetout(&os,&op)>0)
			  {
				  if (packets<3)
				  {
					  if(vorbis_synthesis_headerin(&vi,&vc,&op)<0) return;
				  }
				  else
				  {
					  float ** pcm;
					  int samples;
					  if(vorbis_synthesis(&vb,&op)==0) vorbis_synthesis_blockin(&vd,&vb);
					  while((samples=vorbis_synthesis_pcmout(&vd,&pcm))>0)
					  {
						  int n,c;

              int newsize=(m_samples_used+(samples+4096)*vi.channels)*sizeof(float);

              if (m_samples.GetSize() < newsize) m_samples.Resize(newsize);

              float *bufmem = (float *)m_samples.Get();

						  for(n=0;n<samples;n++)
						  {
							  for(c=0;c<vi.channels;c++)
							  {
								  bufmem[m_samples_used++]=pcm[c][n];
							  }							
						  }
						  vorbis_synthesis_read(&vd,samples);
					  }
				  }
				  packets++;
				  if (packets==3)
				  {
					  vorbis_synthesis_init(&vd,&vi);
					  vorbis_block_init(&vd,&vb);
				  }
			  }
		  }
    }

    void Reset()
    {
//      packets=0;
      m_samples_used=0;
    //  ogg_stream_clear(&os);
  //	  ogg_sync_clear(&oy);

//      ogg_sync_init(&oy); /* Now we can read pages */
    }

  private:


    int m_err;
    int packets;

    ogg_sync_state   oy; /* sync and verify incoming physical bitstream */
    ogg_stream_state os; /* take physical pages, weld into a logical
			    stream of packets */
    ogg_page         og; /* one Ogg bitstream page.  Vorbis packets are inside */
    ogg_packet       op; /* one raw packet of data for decode */
  
    vorbis_info      vi; /* struct that stores all the static vorbis bitstream
			    settings */
    vorbis_comment   vc; /* struct that stores all the bitstream user comments */
    vorbis_dsp_state vd; /* central working state for the packet->PCM decoder */
    vorbis_block     vb; /* local working space for packet->PCM decode */

};


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
    ogg_stream_init(&os,m_ser=rand());

    if (m_err) return;


    reinit(1);
  }

  void reinit(int bla=0)
  {
    if (!bla)
    {
      //ogg_stream_clear(&os);
      //vorbis_block_clear(&vb);
      //vorbis_dsp_clear(&vd);
 
      //vorbis_analysis_init(&vd,&vi);
      //vorbis_block_init(&vd,&vb);
      ogg_stream_init(&os,m_ser); //++?
 
      outqueue.Advance(outqueue.Available());
      outqueue.Compact();
    }


    ogg_packet header;
    ogg_packet header_comm;
    ogg_packet header_code;
    vorbis_analysis_headerout(&vd,&vc,&header,&header_comm,&header_code);
    ogg_stream_packetin(&os,&header); /* automatically placed in its own page */
    ogg_stream_packetin(&os,&header_comm);
    ogg_stream_packetin(&os,&header_code);

	  for (;;)
    {
      ogg_page og;
		  int result=ogg_stream_flush(&os,&og);
		  if(result==0)break;
      outqueue.Add(og.header,og.header_len);
		  outqueue.Add(og.body,og.body_len);
	  }
  }

  void Encode(float *in, int inlen) // length in sample (PAIRS)
  {
    if (m_err) return;

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
      ogg_packet       op;

      while(vorbis_bitrate_flushpacket(&vd,&op))
      {
	
      	ogg_stream_packetin(&os,&op);

	      while (!eos)
        {
          ogg_page og;
		      int result=ogg_stream_flush(&os,&og);
		      if(result==0)break;
          outqueue.Add(og.header,og.header_len);
		      outqueue.Add(og.body,og.body_len);
          if(ogg_page_eos(&og)) eos=1;
	      }
      }
    }
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

  WDL_Queue outqueue;

private:
  int m_err,m_nch;

  ogg_stream_state os;
  vorbis_info      vi;
  vorbis_comment   vc;
  vorbis_dsp_state vd;
  vorbis_block     vb;
  int m_ser;

};


#endif//_VORBISENCDEC_H_