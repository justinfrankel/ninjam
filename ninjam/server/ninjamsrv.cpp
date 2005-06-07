#include <windows.h>
#include <conio.h>

#include "../../WDL/jnetlib/jnetlib.h"
#include "../netmsg.h"
#include "../mpb.h"
#include "usercon.h"

#include "../../WDL/rng.h"
#include "../../WDL/lineparse.h"
#include "../../WDL/ptrlist.h"
#include "../../WDL/string.h"


User_Group *m_group;
JNL_Listen *m_listener;


class UserPassEntry
{
public:
  UserPassEntry() {} 
  ~UserPassEntry() {} 
  WDL_String name, pass;
};

WDL_PtrList<UserPassEntry> g_userlist;
int g_config_bpm, g_config_bpi;
int g_config_port;
bool g_config_allowanonymous;

static char *myGetUserPass(User_Group *group, char *username, int *isanon)
{
  if (!strcmp(username,"anonymous"))
  {
    *isanon=1;
    return g_config_allowanonymous?"":NULL;
  }
  int x;
  for (x = 0; x < g_userlist.GetSize(); x ++)
  {
    if (!strcmp(username,g_userlist.Get(x)->name.Get()))
    {
      return g_userlist.Get(x)->pass.Get();
    }
  }
  return NULL;
}


static int ConfigOnToken(LineParser *lp)
{
  const char *t=lp->gettoken_str(0);
  if (!stricmp(t,"Port"))
  {
    if (lp->getnumtokens() != 2) return -1;
    int p=lp->gettoken_int(1);
    if (!p) return -2;
    g_config_port=p;
  }
  else if (!stricmp(t,"BPM"))
  {
    if (lp->getnumtokens() != 2) return -1;
    int p=lp->gettoken_int(1);
    if (!p) return -2;
    g_config_bpm=p;
  }
  else if (!stricmp(t,"BPI"))
  {
    if (lp->getnumtokens() != 2) return -1;
    int p=lp->gettoken_int(1);
    if (!p) return -2;
    g_config_bpi=p;
  }
  else if (!stricmp(t,"User"))
  {
    if (lp->getnumtokens() != 3) return -1;
    UserPassEntry *p=new UserPassEntry;
    p->name.Set(lp->gettoken_str(1));
    p->pass.Set(lp->gettoken_str(2));
    g_userlist.Add(p);
  }
  else if (!stricmp(t,"AnonymousUsers"))
  {
    if (lp->getnumtokens() != 2) return -1;

    int x=lp->gettoken_enum(1,"no\0yes\0");
    if (x <0)
    {
      return -2;
    }
    g_config_allowanonymous=!!x;
  }
  else return -3;
  return 0;

};


static int ReadConfig(char *configfile)
{
  bool comment_state=0;
  int linecnt=0;
  WDL_String linebuild;
  FILE *fp=fopen(configfile,"rt");
  if (!fp)
  {
    printf("[config] error opening configfile '%s'\n",configfile);
    return -1;
  }

  // clear user list, etc
  g_config_bpm=120;
  g_config_bpi=8;
  g_config_port=2049;
  g_config_allowanonymous=0;

  int x;
  for(x=0;x<g_userlist.GetSize(); x++)
  {
    delete g_userlist.Get(x);
  }
  g_userlist.Empty();

  for (;;)
  {
    char buf[8192];
    buf[0]=0;
    fgets(buf,sizeof(buf),fp);
    linecnt++;
    if (!buf[0] || feof(fp)) break;
    if (buf[strlen(buf)-1]=='\n') buf[strlen(buf)-1]=0;

    LineParser lp(comment_state);

    if (buf[0] && buf[strlen(buf)-1]=='\\')
    {
      buf[strlen(buf)-1]=0;
      linebuild.Append(buf);
      continue;
    }
    linebuild.Append(buf);

    int res=lp.parse(linebuild.Get());

    linebuild.Set("");

    if (res)
    {
      if (res==-2) printf("[config] warning: unterminated string parsing line %d of %s\n",linecnt,configfile);
      else printf("[config] warning: error parsing line %d of %s\n",linecnt,configfile);
    }
    else
    {
      comment_state = lp.InCommentBlock();

      if (lp.getnumtokens()>0)
      {
        int err=ConfigOnToken(&lp);
        if (err)
        {
          if (err == -1)
            printf("[config] warning: wrong number of tokens on line %d of %s\n",linecnt,configfile);
          if (err == -2)
            printf("[config] warning: invalid parameter on line %d of %s\n",linecnt,configfile);
          if (err == -3)
            printf("[config] warning: invalid config command \"%s\" on line %d of %s\n",lp.gettoken_str(0),linecnt,configfile);
        }
      }
    }
  }


  fclose(fp);
  return 0;
}

int main(int argc, char **argv)
{

  if (argc != 2)
  {
    printf("Usage: ninjamserver config.cfg\n");
    exit(1);
  }

  printf("Ninjam v0.001 server starting up...\n");
  printf("Copyright (C) 2005, Cockos, Inc.\n");
  if (ReadConfig(argv[1]))
  {
    printf("Error loading config file!\n");
    exit(1);
  }


  DWORD v=GetTickCount();
  WDL_RNG_addentropy(&v,sizeof(v));
  v=(DWORD)time(NULL);
  WDL_RNG_addentropy(&v,sizeof(v));

  JNL::open_socketlib();

  {
    printf("Listening on port %d...",g_config_port);    
    m_listener = new JNL_Listen(g_config_port);
    if (m_listener->is_error()) printf("Error!\n");
    else printf("OK\n");

    m_group=new User_Group;

    m_group->GetUserPass=myGetUserPass;

    printf("Using %d BPM, %d beats/interval\n",g_config_bpm,g_config_bpi);
    m_group->SetConfig(g_config_bpi,g_config_bpm);

    int needprompt=2;
    int esc_state=0;
    for (;;)
    {
      JNL_Connection *con=m_listener->get_connect(65536,65536);
      if (con) 
      {
        char str[512];
        JNL::addr_to_ipstr(con->get_remote(),str,sizeof(str));
        printf("Incoming connection from %s!\n",str);
        m_group->AddConnection(con);
      }

      if (m_group->Run()) 
      {
        if (needprompt)
        {
          if (needprompt>1) printf("\nKeys:\n"
               "  [S]how user table\n"
               "  [R]eload config file\n"
               "  [K]ill user\n"
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
          else if (c == 'K')
          {
            printf("(be quick, server is paused while you type!!!)\nKill username: ");
            char buf[512];
            fgets(buf,sizeof(buf),stdin);
            if (buf[0] && buf[strlen(buf)-1]=='\n') buf[strlen(buf)-1]=0;
            if (buf[0])
            {
              int x;
              int killcnt=0;
              for (x = 0; x < m_group->m_users.GetSize(); x ++)
              {
                User_Connection *c=m_group->m_users.Get(x);
                if (!strcmp(c->m_username.Get(),buf))
                {
                  char str[512];
                  JNL::addr_to_ipstr(c->m_netcon.GetConnection()->get_remote(),str,sizeof(str));
                  printf("Killing user %s on %s\n",c->m_username.Get(),str);
                  killcnt++;
                }
              }
              if (!killcnt)
              {
                printf("User %s not found!\n",buf);
              }
              needprompt=1;
            }
          }
          else if (c == 'S')
          {
            needprompt=1;
            int x;
            for (x = 0; x < m_group->m_users.GetSize(); x ++)
            {
              User_Connection *c=m_group->m_users.Get(x);
              char str[512];
              JNL::addr_to_ipstr(c->m_netcon.GetConnection()->get_remote(),str,sizeof(str));
              printf("%s:%s\n",c->m_auth_state?c->m_username.Get():"<unauthorized>",str);
            }
          }
          else if (c == 'R')
          {
            if (ReadConfig(argv[1]))
            {
              printf("Error opening config file!\n");
            }
            else
            {
              delete m_listener;
              printf("Listening on port %d...",g_config_port);    
              m_listener = new JNL_Listen(g_config_port);
              if (m_listener->is_error()) printf("Error!\n");
              else printf("OK\n");
              printf("Using %d BPM, %d beats/interval\n",g_config_bpm,g_config_bpi);
              m_group->SetConfig(g_config_bpi,g_config_bpm);
            }
            needprompt=1;
          }
         

        }
  
        Sleep(1);
      }
    }


  }

  delete m_group;
  delete m_listener;

  JNL::close_socketlib();
	return 0;
}