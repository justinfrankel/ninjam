#include <windows.h>

#include "../audiostream.h"

extern "C"
{

__declspec(dllexport) audioStreamer *create_asio_streamer(char **dev, SPLPROC proc)
{
  return create_audioStreamer_ASIO(dev,proc);
}

};