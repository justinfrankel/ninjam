#############################################################
# CPU optimization section
#############################################################

OPTFLAGS =  -O2

ifdef MAC
OPTFLAGS += -D_MAC -mcpu=7450
LFLAGS = -framework coreaudio -lncurses.5 -lm
else
LFLAGS = -lncurses -lm -lasound -ljack
endif

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
	$(CXX) $(CXXFLAGS) -o cninjam $(OBJS) -lpthread $(LFLAGS) -logg -lvorbis -lvorbisenc 

clean:
	-rm $(OBJS) cninjam
