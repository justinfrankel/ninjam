OGGDIR = ../../../sdks/libogg-1.2.0
VORBISDIR = ../../../sdks/libvorbis-1.3.1

PLUGIN_CFLAGS = -DREANINJAM -DSWELL_PROVIDED_BY_APP -fPIC -I$(OGGDIR)/include -I$(VORBISDIR)/include -Wno-reorder

ifndef OUTDIR
$(error OUTDIR not specified)
endif

override CFLAGS += $(PLUGIN_CFLAGS)
override CXXFLAGS += $(PLUGIN_CFLAGS)

WDL_PATH = ../../../WDL

vpath %.cpp . $(WDL_PATH) $(WDL_PATH)/swell $(WDL_PATH)/jnetlib $(WDL_PATH)/wingui ../../../ninjam 

ifdef TARGET_DARWIN
vpath %.mm $(WDL_PATH)/swell
%.o : %.mm
	$(CXX) -ObjC++ $(CXXFLAGS) -c -o $@ $<
endif

OBJS = $(SWELL_MODSTUB) vstframe.o sha.o wndsize.o rng.o chat.o license.o locchn.o remchn.o winclient.o mpb.o netmsg.o njclient.o asyncdns.o connection.o httpget.o util.o

TARGET = $(OUTDIR)/reaninjam.vst$(DLL_EXT)

$(TARGET): $(OBJS)
	$(CXX) -o $@ $(CFLAGS) $(LFLAGS) $^


.PHONY: clean

clean:
	-rm $(OBJS) $(TARGET)
