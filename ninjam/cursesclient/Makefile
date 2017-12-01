CC=gcc
CXX=g++
CFLAGS = -O2

OBJS = cursesclient.o

ifdef MAC
  CFLAGS += -D_MAC 
  LFLAGS = -framework coreaudio -lncurses.5 -lm
  OBJS += ../audiostream_mac.o
  COMPILE_VORBIS = 1
else
  LFLAGS = -lncurses -lm -lasound -ljack
  OBJS += ../audiostream_alsa.o ../audiostream_jack.o
endif

OBJS += ../../WDL/jnetlib/asyncdns.o
OBJS += ../../WDL/jnetlib/connection.o
OBJS += ../../WDL/jnetlib/listen.o
OBJS += ../../WDL/jnetlib/util.o
OBJS += ../../WDL/rng.o
OBJS += ../../WDL/sha.o
OBJS += ../mpb.o
OBJS += ../netmsg.o
OBJS += ../njclient.o
OBJS += ../njmisc.o

ifdef COMPILE_VORBIS
  VORBISDIR = ../../sdks/libvorbis-1.3.1
  OGGDIR = ../../sdks/libogg-1.2.0
  CFLAGS += -I$(VORBISDIR)/include -I$(OGGDIR)/include -I$(VORBISDIR)/lib

  OBJS += $(VORBISDIR)/lib/analysis.o $(VORBISDIR)/lib/bitrate.o $(VORBISDIR)/lib/block.o $(VORBISDIR)/lib/codebook.o $(VORBISDIR)/lib/envelope.o 
  OBJS += $(VORBISDIR)/lib/floor0.o $(VORBISDIR)/lib/floor1.o $(VORBISDIR)/lib/info.o $(VORBISDIR)/lib/lookup.o $(VORBISDIR)/lib/lpc.o 
  OBJS += $(VORBISDIR)/lib/lsp.o $(VORBISDIR)/lib/mapping0.o $(VORBISDIR)/lib/mdct.o $(VORBISDIR)/lib/psy.o $(VORBISDIR)/lib/registry.o
  OBJS += $(VORBISDIR)/lib/res0.o $(VORBISDIR)/lib/sharedbook.o $(VORBISDIR)/lib/smallft.o $(VORBISDIR)/lib/synthesis.o 
  OBJS += $(VORBISDIR)/lib/vorbisenc.o $(VORBISDIR)/lib/vorbisfile.o $(VORBISDIR)/lib/window.o $(OGGDIR)/src/bitwise.o $(OGGDIR)/src/framing.o
else
  LFLAGS += -lvorbis -lvorbisenc -logg
endif

CXXFLAGS = $(CFLAGS)

default: cninjam

cninjam: $(OBJS)
	$(CXX) $(CXXFLAGS) -o cninjam $(OBJS) -lpthread $(LFLAGS) 

clean:
	-rm $(OBJS) cninjam
