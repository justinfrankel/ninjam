#############################################################
# CPU optimization section
#############################################################

OPTFLAGS =  -O2

ifdef MAC
OPTFLAGS += -D_MAC 
LFLAGS = -framework coreaudio -lncurses.5 -lm
else
LFLAGS = -lncurses -lm -lasound -ljack
endif

VORBISDIR = ../../sdks/libvorbis-1.3.1
OGGDIR = ../../sdks/libogg-1.2.0
OPTFLAGS += -I$(VORBISDIR)/include -I$(OGGDIR)/include -I$(VORBISDIR)/lib

#############################################################
# Basic Configuration
#############################################################

# we MUST have -fomit-frame-pointer and -lm, otherwise we hate life
CFLAGS = $(OPTFLAGS) -s 
# CFLAGS += -Wshadow
CC=gcc
CXX=g++

OBJS = ../../WDL/jnetlib/asyncdns.o
OBJS += ../../WDL/jnetlib/connection.o
OBJS += ../../WDL/jnetlib/listen.o
OBJS += ../../WDL/jnetlib/util.o
OBJS += ../../WDL/rng.o
OBJS += ../../WDL/sha.o
OBJS += ../mpb.o
OBJS += ../netmsg.o
OBJS += ../njclient.o

OBJS += $(VORBISDIR)/lib/analysis.o $(VORBISDIR)/lib/bitrate.o $(VORBISDIR)/lib/block.o $(VORBISDIR)/lib/codebook.o $(VORBISDIR)/lib/envelope.o $(VORBISDIR)/lib/floor0.o $(VORBISDIR)/lib/floor1.o $(VORBISDIR)/lib/info.o $(VORBISDIR)/lib/lookup.o $(VORBISDIR)/lib/lpc.o $(VORBISDIR)/lib/lsp.o $(VORBISDIR)/lib/mapping0.o $(VORBISDIR)/lib/mdct.o $(VORBISDIR)/lib/psy.o $(VORBISDIR)/lib/registry.o $(VORBISDIR)/lib/res0.o $(VORBISDIR)/lib/sharedbook.o $(VORBISDIR)/lib/smallft.o $(VORBISDIR)/lib/synthesis.o $(VORBISDIR)/lib/vorbisenc.o $(VORBISDIR)/lib/vorbisfile.o $(VORBISDIR)/lib/window.o $(OGGDIR)/src/bitwise.o $(OGGDIR)/src/framing.o

ifdef MAC
OBJS += ../audiostream_mac.o
else
OBJS += ../audiostream_alsa.o ../audiostream_jack.o
endif

OBJS += ../njmisc.o
OBJS += cursesclient.o


CXXFLAGS = $(CFLAGS)

default: cninjam

cninjam: $(OBJS)
	$(CXX) $(CXXFLAGS) -o cninjam $(OBJS) -lpthread $(LFLAGS) 

clean:
	-rm $(OBJS) cninjam
