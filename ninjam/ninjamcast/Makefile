CC=gcc
CXX=g++
CFLAGS =  -O2 

OBJS = njcast.o
OBJS += ninjamcast.o

ifdef MAC
CFLAGS += -D_MAC
LFLAGS = -framework coreaudio -lncurses.5 -lm
else
LFLAGS = -lncurses -ldl -lm -lasound -lvorbis -lvorbisenc -logg -lmp3lame
endif

OBJS += ../../WDL/jnetlib/asyncdns.o
OBJS += ../../WDL/jnetlib/connection.o
OBJS += ../../WDL/jnetlib/listen.o
OBJS += ../../WDL/jnetlib/util.o
OBJS += ../../WDL/jnetlib/httpget.o
OBJS += ../../WDL/lameencdec.o
OBJS += ../../WDL/rng.o
OBJS += ../../WDL/sha.o
OBJS += ../mpb.o
OBJS += ../netmsg.o
OBJS += ../njclient.o
OBJS += ../njmisc.o

CXXFLAGS = $(CFLAGS)

default: ninjamcast

ninjamcast: $(OBJS)
	$(CXX) $(CXXFLAGS) -o ninjamcast $(OBJS) -lpthread $(LFLAGS)

clean:
	-rm $(OBJS) ninjamcast
