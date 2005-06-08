#############################################################
# CPU optimization section
#############################################################

OPTFLAGS =  -O2

ifdef MAC
OPTFLAGS += -D_MAC -fast
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
OBJS += ninjamcmdclient.o


CXXFLAGS = $(CFLAGS)

default: cmdclient

cmdclient: $(OBJS)
	$(CXX) $(CXXFLAGS) -o cmdclient $(OBJS) -lpthread -L.. -lvorbisenc -lvorbis -logg -lncurses -framework coreaudio
