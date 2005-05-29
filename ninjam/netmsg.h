#ifndef _NETMSG_H_
#define _NETMSG_H_

#include "../WDL/heapbuf.h"

#define NET_MESSAGE_MAX_SIZE 16384

class Net_Message
{
	public:
		Net_Message() : m_refcnt(0), m_type(0xff)
		{
		}
		~Net_Message()
		{
		}

		void addRef() { m_refcnt++; }
		void releaseRef() { if (--m_refcnt < 1) delete this; }


		void set_type(int type)	{ m_type=type; }
		int  get_type() { return m_type; }

		void set_size(int newsize) { m_hb.Resize(newsize); }
		int get_size() { return m_hb.GetSize(); }

		void *get_data() { return m_hb.Get(); }


		int parseFromBuffer(void *data, int len); // returns bytes used, if any (or 0 if more data needed), or -1 if invalid
		int makeMessageHeader(void *data); // makes message header, returns length. data should be at least 16 bytes to be safe

	private:
		int m_type;
		int m_refcnt;
		WDL_HeapBuf m_hb;
};



#endif