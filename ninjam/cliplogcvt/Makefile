CFLAGS =  -O2 -s -Wall 

ifdef MAC
CFLAGS += -D_MAC
else
CFLAGS += -pthread
endif

CC=gcc
CXX=g++
CXXFLAGS = $(CFLAGS)

OBJS = cliplogcvt.o

default: cliplogcvt

cliplogcvt: $(OBJS)
	$(CXX) $(CXXFLAGS) -o cliplogcvt $(OBJS) -logg -lvorbis

clean:
	-rm $(OBJS) cliplogcvt
