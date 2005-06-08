#ifndef _NETMSG_H_
#define _NETMSG_H_

#include "../WDL/queue.h"
#include "../WDL/jnetlib/jnetlib.h"

#define NET_MESSAGE_MAX_SIZE 16384


#define MESSAGE_EXTENDED 0xfe
#define MESSAGE_INVALID 0xff


class Net_Message
{
	public:
		Net_Message() : m_parsepos(0), m_refcnt(0), m_type(MESSAGE_INVALID)
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


		int parseMessageHeader(void *data, int len); // returns bytes used, if any (or 0 if more data needed), or -1 if invalid
    int parseBytesNeeded();
    int parseAddBytes(void *data, int len); // returns bytes actually added

		int makeMessageHeader(void *data); // makes message header, returns length. data should be at least 16 bytes to be safe


		void addRef() { ++m_refcnt; }
		void releaseRef() { if (--m_refcnt < 1) delete this; }

	private:
    		int m_parsepos;
		int m_refcnt;
		int m_type;
		WDL_HeapBuf m_hb;
};


class Net_Connection
{
  public:
    Net_Connection() : m_error(0),m_msgsendpos(-1), m_recvstate(0),m_recvmsg(0),m_con(0)
    { 
    }
    ~Net_Connection();

    void attach(JNL_Connection *con) { m_con=con; }

    Net_Message *Run(int *wantsleep=0);
    void Send(Net_Message *msg);
    int GetStatus(); // returns <0 on error, 0 on normal, 1 on disconnect
    JNL_Connection *GetConnection() { return m_con; }

    void Kill();

  private:
    int m_error;

    int m_msgsendpos;

    int m_recvstate;
    Net_Message *m_recvmsg;

    JNL_Connection *m_con;
    WDL_Queue m_sendq;


};


#endif
