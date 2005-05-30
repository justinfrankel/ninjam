#include <windows.h>

#include "../../WDL/jnetlib/jnetlib.h"
#include "../netmsg.h"
#include "../mpb.h"


int main(int argc, char **argv)
{
  printf("Ninjam v0.0 command line client starting up...\n");

  if (argc != 2)
  {
    printf("Usage: ninjam hostname\n");
    return 0;
  }
  JNL::open_socketlib();


  Net_Connection *netcon=0;
  {
    printf("Connecting to %s...\n",argv[1]);
    JNL_Connection *con=new JNL_Connection(JNL_CONNECTION_AUTODNS,65536,65536);
    con->connect(argv[1],2049);

    for (;;)
    {
      con->run();
      if (con->get_state() < JNL_Connection::STATE_RESOLVING || con->get_state() >= JNL_Connection::STATE_CONNECTED)
        break;
      Sleep(100);
    }
    if (con->get_state() != JNL_Connection::STATE_CONNECTED)
    {
      printf("Error connecting!\n");
      delete con;
    }
    else
    {
      printf("Connected!\n");

      netcon = (Net_Connection*)new Net_Connection;
      netcon->attach(con);
    }
  }

  if (netcon)
  {

  }



  JNL::close_socketlib();
  return 0;
}
