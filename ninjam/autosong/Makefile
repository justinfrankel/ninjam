CXX=g++
CFLAGS=-O2
default: autosong


OBJS=autosong.o ../../WDL/lameencdec.o

autosong: $(OBJS)
	$(CXX) -static -o autosong $(OBJS) -lvorbis -lmp3lame -logg
