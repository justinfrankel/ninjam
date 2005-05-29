#ifndef _NETMSG_H_
#define _NETMSG_H_

#include "../WDL/heapbuf.h"

#define NET_MESSAGE_MAX_SIZE 16384




#define MESSAGE_SERVER_AUTH_CHALLENGE 0x00
#define MESSAGE_SERVER_AUTH_REPLY 0x01
#define MESSAGE_SERVER_CONFIG_CHANGE_NOTIFY 0x02
#define MESSAGE_SERVER_USERLIST_CHANGE_NOTIFY 0x03
#define MESSAGE_SERVER_DOWNLOAD_INTERVAL_BEGIN 0x04
#define MESSAGE_SERVER_DOWNLOAD_INTERVAL_WRITE 0x05

#define MESSAGE_CLIENT_AUTH_USER 0x80
#define MESSAGE_CLIENT_SET_USERMASK 0x81
#define MESSAGE_CLIENT_SET_CHANNEL_INFO 0x82
#define MESSAGE_CLIENT_UPLOAD_INTERVAL_BEGIN 0x83
#define MESSAGE_CLIENT_UPLOAD_INTERVAL_WRITE 0x84



#define MESSAGE_EXTENDED 0xfe
#define MESSAGE_INVALID 0xff

class Net_Message
{
	public:
		Net_Message() : m_refcnt(0), m_type(MESSAGE_INVALID)
		{
		}
		~Net_Message()
		{
		}


		void set_type(int type)	{ m_type=type; }
		int  get_type() { return m_type; }

		void set_size(int newsize) { m_hb.Resize(newsize); }
		int get_size() { return m_hb.GetSize(); }

		void *get_data() { return m_hb.Get(); }


		int parseFromBuffer(void *data, int len); // returns bytes used, if any (or 0 if more data needed), or -1 if invalid
		int makeMessageHeader(void *data); // makes message header, returns length. data should be at least 16 bytes to be safe


		void addRef() { ++m_refcnt; }
		void releaseRef() { if (--m_refcnt < 1) delete this; }

	private:
		int m_type;
		int m_refcnt;
		WDL_HeapBuf m_hb;
};



#endif