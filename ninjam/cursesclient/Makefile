#############################################################
# CPU optimization section
#############################################################

OPTFLAGS =  -O2

ifdef MAC
OPTFLAGS += -D_MAC -mcpu=7450
else
OPTFLAGS += -malign-double 
endif

#############################################################
# Basic Configuration
#############################################################

# we MUST have -fomit-frame-pointer and -lm, otherwise we hate life
CFLAGS = $(OPTFLAGS) -s 
# CFLAGS += -Wshadow
LFLAGS = -lm
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
OBJS += ../audiostream_mac.o
OBJS += ../njmisc.o
OBJS += cursesclient.o


CXXFLAGS = $(CFLAGS)

default: cursesclient

cursesclient: $(OBJS)
	$(CXX) $(CXXFLAGS) -o cursesclient $(OBJS) -lpthread -framework coreaudio -logg -lvorbis -lvorbisenc -lncurses.5

clean:
	-rm $(OBJS) cursesclient
