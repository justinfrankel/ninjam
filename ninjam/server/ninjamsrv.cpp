#include <windows.h>

#include "../../WDL/jnetlib/jnetlib.h"
#include "../netmsg.h"
#include "../mpb.h"
#include "usercon.h"

#include "../../WDL/rng.h"

int main(int argc, char **argv)
{

  DWORD v=GetTickCount();
  WDL_RNG_addentropy(&v,sizeof(v));
  v=(DWORD)time(NULL);
  WDL_RNG_addentropy(&v,sizeof(v));

  printf("Ninjam v0.0 server starting up...\n");
  JNL::open_socketlib();


  {
    printf("Listening on port 2049...");    
    JNL_Listen listener(2049);
    if (listener.is_error()) printf("Error!\n");
    else printf("OK\n");

    User_Group m_group;

    m_group.SetConfig(16,120);
    for (;;)
    {
      JNL_Connection *con=listener.get_connect(65536,65536);
      if (con) 
      {
        printf("Got connect!\n");
        m_group.AddConnection(con);
      }

      m_group.Run();
      Sleep(10);
    }


  }


  JNL::close_socketlib();
	return 0;
}