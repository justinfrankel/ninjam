#include "njasiodrv_if.h"


static audioStreamer *(*cas)(char **dev, SPLPROC proc);
static HWND (*cacd)(HWND parent, asio_config_type *cfg);

static void init_dll()
{
  static HINSTANCE hlib;
  if (hlib)
    return;
  char buf[1024];
  GetModuleFileName(GetModuleHandle(NULL),buf,sizeof(buf));
  char *p=buf+strlen(buf);
  while (p >= buf && *p != '\\') p--;
  strcpy(++p,"njasiodrv.dll");

  hlib=LoadLibrary(buf);
  if (hlib)
  {
    *((void**)&cas) = (void *)GetProcAddress(hlib,"create_asio_streamer");
    *((void**)&cacd) = (void *)GetProcAddress(hlib,"create_asio_configdlg");    
  }
}

audioStreamer *njasiodrv_create_asio_streamer(char **dev, SPLPROC proc)
{
  init_dll();
  return cas?cas(dev,proc):NULL;
}

HWND njasiodrv_create_asio_configdlg(HWND parent, asio_config_type *cfg)
{
  init_dll();
  return cacd?cacd(parent,cfg):NULL;
}
