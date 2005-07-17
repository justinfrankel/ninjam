#ifndef _NJASIODRV_IF_H_
#define _NJASIODRV_IF_H_

#include <windows.h>

#include "../audiostream.h"

typedef struct
{
  int asio_driver;
  int asio_input[2];
  int asio_output[2];
} asio_config_type;

audioStreamer *njasiodrv_create_asio_streamer(char **dev, SPLPROC proc);
HWND njasiodrv_create_asio_configdlg(HWND parent, asio_config_type *cfg);

#endif