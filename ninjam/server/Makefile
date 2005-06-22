#############################################################
# CPU optimization section
#############################################################

OPTFLAGS =  -O2

ifdef MAC
OPTFLAGS += -D_MAC
else
OPTFLAGS += -malign-double -pthread
endif

#############################################################
# Basic Configuration
#############################################################

# we MUST have -fomit-frame-pointer and -lm, otherwise we hate life
CFLAGS = $(OPTFLAGS) -s  -Wall 
# CFLAGS += -Wshadow
LFLAGS = -lm
CC=gcc
CXX=g++

OBJS = ../../WDL/jnetlib/asyncdns.o
OBJS += ../../WDL/jnetlib/connection.o
OBJS += ../../WDL/jnetlib/listen.o
OBJS += ../../WDL/jnetlib/util.o
OBJS += ../../WDL/jnetlib/httpget.o
OBJS += ../../WDL/rng.o
OBJS += ../../WDL/sha.o
OBJS += ../mpb.o
OBJS += ../netmsg.o
OBJS += usercon.o
OBJS += ninjamsrv.o


CXXFLAGS = $(CFLAGS)

default: ninjamsrv

ninjamsrv: $(OBJS)
	$(CXX) $(CXXFLAGS) -o ninjamsrv $(OBJS)
