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

LIBS=


#this shouldn't be necessary, but it seems to be necessary on OS X, at least
ifdef MAC
LIBS+=/usr/local/lib/libogg.a
endif

default: cliplogcvt

cliplogcvt: $(OBJS)
	$(CXX) $(CXXFLAGS) -o cliplogcvt $(OBJS) -logg -lvorbis  $(LIBS)

clean:
	-rm $(OBJS) cliplogcvt
