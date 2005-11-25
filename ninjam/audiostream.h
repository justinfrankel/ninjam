/*
    NINJAM - audiostream.h
    Copyright (C) 2005 Cockos Incorporated

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

  This header is used by NINJAM clients to define an abstract audio streamer interface, as
  well as declare functions for creating instances of these audio streamers. 

  On Windows, these functions are primarily called from audioconfig.cpp, and on
  the Cocoa client the function is called from Controller.mm.

  The basic structure is:

  The client runs, creates an audiostreamer (below), giving it a SPLPROC, which is it's
  own function that then in turn calls NJClient::AudioProc. 

  But this is just the interface declaration etc.

*/

#ifndef _AUDIOSTREAM_H_
#define _AUDIOSTREAM_H_


class audioStreamer
{
	public:
		audioStreamer() { m_srate=48000; m_outnch=m_innch=2; m_bps=16; }
		virtual ~audioStreamer() { }

    virtual const char *GetChannelName(int idx)=0; // call with idx of 0x80000000 to get (int) numblocks_latency -- if NULL, should be 2.

		int m_srate, m_innch, m_outnch, m_bps;
};


typedef void (*SPLPROC)(float **inbuf, int innch, float **outbuf, int outnch, int len, int srate);


#ifdef _WIN32
audioStreamer *create_audioStreamer_KS(int srate, int bps, int *nbufs, int *bufsize, SPLPROC proc);

audioStreamer *create_audioStreamer_WO(int srate, int bps, int devs[2], int *nbufs, int *bufsize, SPLPROC proc);
audioStreamer *create_audioStreamer_DS(int srate, int bps, GUID devs[2], int *nbufs, int *bufsize, SPLPROC proc);

#else

#ifdef _MAC
audioStreamer *create_audioStreamer_CoreAudio(char **dev, int srate, int nch, int bps, SPLPROC proc);
#else
audioStreamer *create_audioStreamer_ALSA(char *cfg, SPLPROC proc);
#endif

#endif

#endif
