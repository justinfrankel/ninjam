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


  int m_bpm=120,m_bpi=32;
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

    while (!netcon->GetStatus())
    {
      Net_Message *msg=netcon->Run();
      if (msg)
      {
        msg->addRef();

        switch (msg->get_type())
        {
          case MESSAGE_SERVER_AUTH_CHALLENGE:
            {

              mpb_server_auth_challenge cha;
              if (!cha.parse(msg))
              {
                printf("Got challenge.\n");

                char buf[512],pass[512];
                printf("Enter username: ");
                fflush(stdout);
                gets(buf);
                printf("Enter password: ");
                fflush(stdout);
                gets(pass);

                mpb_client_auth_user repl;
                repl.username=buf;
                // fucko: make password hash

                netcon->Send(repl.build());
              }
            }
          break;
          case MESSAGE_SERVER_AUTH_REPLY:
            {
              mpb_server_auth_reply ar;
              if (!ar.parse(msg))
              {
                printf("Got auth reply of %d\n",ar.flag);


                if (ar.flag) // send our channel information
                {
                  mpb_client_set_channel_info sci;

                  sci.build_add_rec("default channel",-18,0,0);

                  netcon->Send(sci.build());
                }
                else netcon->Kill();
              }
            }
          break;
          case MESSAGE_SERVER_CONFIG_CHANGE_NOTIFY:
            {
              mpb_server_config_change_notify ccn;
              if (!ccn.parse(msg))
              {
                m_bpm=ccn.beats_minute;
                m_bpi=ccn.beats_interval;
                printf("Got info update from server, bpm=%d, bpi=%d\n",m_bpm,m_bpi);
              }
            }

          break;
          case MESSAGE_SERVER_USERINFO_CHANGE_NOTIFY:
            {
              mpb_server_userinfo_change_notify ucn;
              if (!ucn.parse(msg))
              {
                int offs=0;
                int a=0, cid=0, v=0,p=0,f=0;
                char *un=0,*chn=0;
                while ((offs=ucn.parse_get_rec(offs,&a,&cid,&v,&p,&f,&un,&chn))>0)
                {
                  if (!un) un="";
                  if (!chn) chn="";
                  printf("user %s, channel %d \"%s\": %s v:%ddB p:%d flag=%d\n",un,cid,chn,a?"active":"inactive",v/10,p,f);
                }
              }
            }
          break;
          default:
            printf("Got unknown message %02X\n",msg->get_type());
          break;
        }

        msg->releaseRef();
      }
      else Sleep(10);
    }
  }



  JNL::close_socketlib();
  return 0;
}
