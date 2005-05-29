#ifndef _NETMSG_H_
#define _NETMSG_H_

#include "../WDL/heapbuf.h"

class NetMsg_SharedBuf
{
	public:
		NetMsg_SharedBuf() : m_refcnt(0)
		{
		}
		~NetMsg_SharedBuf()
		{
		}

		void addRef() { m_refcnt++; }
		void releaseRef()
		{
			if (--m_refcnt < 1) delete this;
		}

		void *Get() { return m_hb.Get(); }
		int GetSize() { return m_hb.GetSize(); }
		void *Resize(int newsize) { return m_hb.Resize(newsize); }


	private:
		int m_refcnt;
		WDL_HeapBuf m_hb;

};

#endif