#include <windows.h>

#include "../../WDL/jnetlib/jnetlib.h"
#include "../netmsg.h"
#include "../mpb.h"
#include "usercon.h"

int main(int argc, char **argv)
{
  JNL::open_socketlib();


  {
    JNL_Listen listener(2049);

    for (;;)
    {
      JNL_Connection *con=listener.get_connect(65536,65536);
      if (con)
      {
      }

      Sleep(10);
    }


  }


  JNL::close_socketlib();
	return 0;
}