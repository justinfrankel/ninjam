/*
    NINJAM ASIO driver - njasiodrv_if.cpp
    Copyright (C) 2005 Cockos Incorporated

    NJASIODRV is dual-licensed. You may modify and/or distribute NJASIODRV under either of 
    the following licenses:
    
      This software is provided 'as-is', without any express or implied
      warranty.  In no event will the authors be held liable for any damages
      arising from the use of this software.

      Permission is granted to anyone to use this software for any purpose,
      including commercial applications, and to alter it and redistribute it
      freely, subject to the following restrictions:

      1. The origin of this software must not be misrepresented; you must not
         claim that you wrote the original software. If you use this software
         in a product, an acknowledgment in the product documentation would be
         appreciated but is not required.
      2. Altered source versions must be plainly marked as such, and must not be
         misrepresented as being the original software.
      3. This notice may not be removed or altered from any source distribution.
      

    or:

      NJASIODRV is free software; you can redistribute it and/or modify
      it under the terms of the GNU General Public License as published by
      the Free Software Foundation; either version 2 of the License, or
      (at your option) any later version.

      NJASIODRV is distributed in the hope that it will be useful,
      but WITHOUT ANY WARRANTY; without even the implied warranty of
      MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
      GNU General Public License for more details.

      You should have received a copy of the GNU General Public License
      along with NJASIODRV; if not, write to the Free Software
      Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
*/

/*

  This file provides the basic code for loading NJASIODRV.DLL from the NINJAM 
  application.

*/


#include "njasiodrv_if.h"


static audioStreamer *(*cas)(char **dev, SPLPROC proc);
static HWND (*cacd)(HWND parent, asio_config_type *cfg);

static void init_dll()
{
  static HINSTANCE hlib;
  if (hlib) return;
  char buf[1024];
  GetModuleFileName(GetModuleHandle(NULL),buf,sizeof(buf));
  char *p=buf;
  while (*p) p++;
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
