#include <windows.h>

#include "netmsg.h"

int Net_Message::parseFromBuffer(void *data, int len) // returns bytes used, if any (or 0 if more data needed)
{
	unsigned char *dp=(unsigned char *)data;
  if (len < 5) return 0;

  int type=*dp++;

  unsigned int size = *dp++; 
  size |= ((unsigned int)*dp++)<<8; 
  size |= ((unsigned int)*dp++)<<16; 
  size |= ((unsigned int)*dp++)<<24; 
  len -= 5;
  if (size < 0 || size > NET_MESSAGE_MAX_SIZE) return -1;

  if (size < len) return 0;

  m_type=type;
  set_size(size);

  void *t=get_data();
  if (t) memcpy(t,data,size);

  return size+5;
}

int Net_Message::makeMessageHeader(void *data) // makes message header, data should be at least 16 bytes to be safe
{
	if (!data) return 0;

	unsigned char *dp=(unsigned char *)data;
  *dp++ = (unsigned char) m_type;
  unsigned int size=(unsigned int) get_size();
  *dp++=size&0xff; size>>=8;
  *dp++=size&0xff; size>>=8;
  *dp++=size&0xff; size>>=8;
  *dp++=size&0xff;

  return (dp-(unsigned char *)data);
}
