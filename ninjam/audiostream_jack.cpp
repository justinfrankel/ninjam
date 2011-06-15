/*
    NINJAM - audiostream_alsa.cpp
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

  This file implements a audioStreamer that uses ALSA.
  It only exposes the following functions:

    audioStreamer *create_audioStreamer_ALSA(char *cfg, SPLPROC proc);
  
    cfg is a string that has a list of parameter/value pairs (space delimited) 
    for the config:
      in     - input device i.e. hw:0,0
      out    - output device i.e. hw:0,0
      srate  - sample rate i.e. 48000
      bps    - bits/sample i.e. 16
      nch    - channels i.e. 2
      bsize  - block size (bytes) i.e. 2048
      nblock - number of blocks i.e. 16


  (everything else in this file is used internally)

*/

#include <stdio.h>
#include <stdlib.h>
#include <errno.h>

#include <signal.h>
#include <unistd.h>
#include <fcntl.h>
#include <pthread.h>

#include <jack/jack.h>
#include <string.h>

#include "../WDL/pcmfmtcvt.h"

#include "../WDL/ptrlist.h"
#include "audiostream.h"

static void audiostream_onunder() { }
static void audiostream_onover() { }





class audioStreamer_JACK : public audioStreamer
{
    public:
	audioStreamer_JACK( char *cfg, SPLPROC proc );
	~audioStreamer_JACK();

	int process( jack_nframes_t nframes );
	const char *GetChannelName(int idx)
	{
	    if (idx == 0) return "Left";
	    if (idx == 1) return "Right";
	    return NULL;
	}
    private:
	jack_client_t *client;
	jack_port_t *in1, *in2;
	jack_port_t *out1, *out2;

	SPLPROC splproc;
};


int
process_cb( jack_nframes_t nframes, audioStreamer_JACK *as ) {
    return as->process( nframes );
}


//////////////// ALSA driver
audioStreamer_JACK::audioStreamer_JACK( char *cfg, SPLPROC proc) 
{ 

    splproc = proc;

    if ((client = jack_client_new ("ninjam")) == 0) {
	fprintf (stderr, "jack server not running?\n");
	exit(20);
    }

    jack_set_process_callback (client, (JackProcessCallback) process_cb, this);


    out1 = jack_port_register (client, "out1", JACK_DEFAULT_AUDIO_TYPE, JackPortIsOutput, 0);
    out2 = jack_port_register (client, "out2", JACK_DEFAULT_AUDIO_TYPE, JackPortIsOutput, 0);

    in1 = jack_port_register (client, "in1", JACK_DEFAULT_AUDIO_TYPE, JackPortIsInput, 0);
    in2 = jack_port_register (client, "in2", JACK_DEFAULT_AUDIO_TYPE, JackPortIsInput, 0);

    if (jack_activate (client)) {
	fprintf (stderr, "cannot activate client");
	exit(20);
    }
}

audioStreamer_JACK::~audioStreamer_JACK() 
{
    jack_deactivate( client );
    sleep(1);
}

int
audioStreamer_JACK::process( jack_nframes_t nframes ) {
    float *inports[2];
    float *outports[2];

    inports[0] = (float *) jack_port_get_buffer( in1, nframes );
    inports[1] = (float *) jack_port_get_buffer( in2, nframes );
    outports[0] = (float *) jack_port_get_buffer( out1, nframes );
    outports[1] = (float *) jack_port_get_buffer( out2, nframes );

    splproc( inports, 2, outports, 2, nframes, jack_get_sample_rate( client ) );
    return 0;
}

audioStreamer *create_audioStreamer_JACK(char *cfg, SPLPROC proc)
{
  return new audioStreamer_JACK( cfg, proc);
}
