#include <windows.h>
#include <conio.h>

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

  printf("Ninjam v0.001 server starting up...\n");
  JNL::open_socketlib();


  {
    printf("Listening on port 2049...");    
    JNL_Listen listener(2049);
    if (listener.is_error()) printf("Error!\n");
    else printf("OK\n");

    User_Group m_group;

    m_group.SetConfig(8,90);
    printf("Using defaults of 90 BPM, 8 beats/interval (hit C to change)\n");
    int needprompt=2;
    int esc_state=0;
    for (;;)
    {
      JNL_Connection *con=listener.get_connect(65536,65536);
      if (con) 
      {
        char str[512];
        JNL::addr_to_ipstr(con->get_remote(),str,sizeof(str));
        printf("Incoming connection from %s!\n",str);
        m_group.AddConnection(con);
      }

      if (m_group.Run()) 
      {
        if (needprompt)
        {
          if (needprompt>1) printf("\nKeys:\n"
               "  [S]how user table\n"
               "  [C]hange BPM and BPI\n"
               "  [Q]uit server\n");
          printf(": ");
          needprompt=0;
        }
        if (kbhit())
        {
          int c=toupper(getch());
          printf("%c\n",isalpha(c)?c:'?');
          if (esc_state)
          {
            if (c == 'Y') break;
            printf("Exit aborted\n");
            needprompt=2;
            esc_state=0;
          }
          else if (c == 'Q')
          {
            if (!esc_state)
            {
              esc_state++;
              printf("Q pressed -- hit Y to exit, any other key to continue\n");
              needprompt=1;
            }
          }
          else if (c == 'S')
          {
            needprompt=1;
            int x;
            for (x = 0; x < m_group.m_users.GetSize(); x ++)
            {
              User_Connection *c=m_group.m_users.Get(x);
              char str[512];
              JNL::addr_to_ipstr(c->m_netcon.GetConnection()->get_remote(),str,sizeof(str));
              printf("%s:%s\n",c->m_auth_state?c->m_username.Get():"<unauthorized>",str);
            }
          }
          else if (c == 'C')
          {
            printf("OK, be decisive (the network connections are halted while you type these in:\n");
            printf("Hit enter on either of these to abort!\n");
            printf("BPM: "); 
            char buf[512];
            fgets(buf,sizeof(buf),stdin);
            int bpm=atoi(buf);
            if (bpm) 
            {
              printf("Beats/Interval: ");
              fgets(buf,sizeof(buf),stdin);
              int bpi=atoi(buf);
              if (bpi) 
              {
                m_group.SetConfig(bpi,bpm);
                printf("Set server to %d BPM, %d beats/interval!\n",bpm,bpi);
                needprompt=1;
              }
              else 
              {
                printf("Invalid Beats/Interval entry, aborting setting\n");
                needprompt=2;
              }

            }
            else 
            {
              printf("Invalid BPM entry, aborting setting\n");
              needprompt=2;
            }
          }
          else
          {
            printf("Unknown command\n");
            needprompt=2;
          }
         

        }
  
        Sleep(1);
      }
    }


  }


  JNL::close_socketlib();
	return 0;
}