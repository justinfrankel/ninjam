#ifdef _WIN32
#include <windows.h>
#include <conio.h>
#else
#include <stdlib.h>
#include <string.h>
#include <signal.h>
#include <time.h>
#endif

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


#define ACL_FLAG_DENY 1
#define ACL_FLAG_RESERVE 2
typedef struct
{
  unsigned long addr;
  unsigned long mask;
  int flags;
} ACLEntry;


WDL_HeapBuf g_acllist;
void aclAdd(unsigned long addr, unsigned long mask, int flags)
{
  addr=ntohl(addr);
//  printf("adding acl entry for %08x + %08x\n",addr,mask);
  ACLEntry f={addr,mask,flags};
  int os=g_acllist.GetSize();
  g_acllist.Resize(os+sizeof(f));
  memcpy((char *)g_acllist.Get()+os,&f,sizeof(f));
}

int aclGet(unsigned long addr)
{
  addr=ntohl(addr);

  ACLEntry *p=(ACLEntry *)g_acllist.Get();
  int x=g_acllist.GetSize()/sizeof(ACLEntry);
  while (x--)
  {
  //  printf("comparing %08x to %08x\n",addr,p->addr);
    if ((addr & p->mask) == p->addr) return p->flags;
    p++;
  }
  return 0;
}


WDL_PtrList<UserPassEntry> g_userlist;
int g_config_bpm, g_config_bpi;
int g_config_port,g_config_max_users;
bool g_config_allowanonymous;
WDL_String g_config_license;

static char *myGetUserPass(User_Group *group, char *username, int *isanon)
{
  if (!strcmp(username,"anonymous"))
  {
    printf("got anonymous request (%s)\n",g_config_allowanonymous?"allowing":"denying");
    *isanon=1;
    return (char *)(g_config_allowanonymous?"":NULL);
  }
  int x;
  printf("got login request for '%s'\n",username);
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
  else if (!stricmp(t,"MaxUsers"))
  {
    if (lp->getnumtokens() != 2) return -1;
    int p=lp->gettoken_int(1);
    g_config_max_users=p;
  }
  else if (!stricmp(t,"ServerLicense"))
  {
    if (lp->getnumtokens() != 2) return -1;
    FILE *fp=fopen(lp->gettoken_str(1),"rt");
    if (!fp) 
    {
      printf("Error opening license file %s\n",lp->gettoken_str(1));
      return -2;
    }
    g_config_license.Set("");
    for (;;)
    {
      char buf[1024];
      buf[0]=0;
      fgets(buf,sizeof(buf),fp);
      if (!buf[0]) break;
      g_config_license.Append(buf);
    }

    fclose(fp);
    
  }
  else if (!stricmp(t,"ACL"))
  {
    if (lp->getnumtokens() != 3) return -1;
    int suc=0;
    char *v=lp->gettoken_str(1);
    char *t=strstr(v,"/");
    if (t)
    {
      *t++=0;
      unsigned long addr=JNL::ipstr_to_addr(v);
      if (addr != INADDR_NONE)
      {
        int maskbits=atoi(t);
        if (maskbits >= 0 && maskbits <= 32)
        {
          int flag=lp->gettoken_enum(2,"allow\0deny\0reserve\0");
          if (flag >= 0)
          {
            suc=1;
            unsigned long mask=~(0xffffffff>>maskbits);
            aclAdd(addr,mask,flag);
          }
        }
      }
    }

    if (!suc)
    {
      printf("Usage: ACL xx.xx.xx.xx/X [ban|allow|reserve]\n");
      return -2;
    }
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
  g_config_max_users=0; // unlimited users
  g_acllist.Resize(0);
  g_config_license.Set("");

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
    if (!buf[0]) break;
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

int g_reloadconfig;

#ifndef _WIN32

void sighandler(int sig)
{
  if (sig == SIGHUP)
  {
  }
  if (sig == SIGINT)
  { 
    g_reloadconfig=1;
  }
}
#endif

void enforceACL()
{
  int x;
  int killcnt=0;
  for (x = 0; x < m_group->m_users.GetSize(); x ++)
  {
    User_Connection *c=m_group->m_users.Get(x);
    if (aclGet(c->m_netcon.GetConnection()->get_remote()) == ACL_FLAG_DENY)
    {
      c->m_netcon.Kill();
      killcnt++;
    }
  }
  if (killcnt) printf("killed %d users by enforcing ACL\n",killcnt);
}

int main(int argc, char **argv)
{

  if (argc != 2)
  {
    printf("Usage: ninjamserver config.cfg\n");
    exit(1);
  }

  printf("Ninjam v0.004 server starting up...\n");
  printf("Copyright (C) 2005, Cockos, Inc.\n");
  if (ReadConfig(argv[1]))
  {
    printf("Error loading config file!\n");
    exit(1);
  }


#ifdef _WIN32
  DWORD v=GetTickCount();
  WDL_RNG_addentropy(&v,sizeof(v));
  v=(DWORD)time(NULL);
  WDL_RNG_addentropy(&v,sizeof(v));
#else
  time_t v=time(NULL);
  WDL_RNG_addentropy(&v,sizeof(v));

  signal(SIGPIPE,sighandler);
  signal(SIGHUP,sighandler);
  signal(SIGINT,sighandler);
#endif

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

    m_group->SetLicenseText(g_config_license.Get());

#ifdef _WIN32
    int needprompt=2;
    int esc_state=0;
#endif
    for (;;)
    {
      JNL_Connection *con=m_listener->get_connect(65536,65536);
      if (con) 
      {
        char str[512];
        int flag=aclGet(con->get_remote());
        JNL::addr_to_ipstr(con->get_remote(),str,sizeof(str));
        printf("Incoming connection from %s!\n",str);

        if (flag == ACL_FLAG_DENY)
        {
          printf("Denying connection (via ACL)\n");
          delete con;
        }
        else
        {
          if (flag != ACL_FLAG_RESERVE && g_config_max_users && m_group->m_users.GetSize() >= g_config_max_users)
          {
            printf("Too many users, refusing connection\n");
            // check ACL
            delete con;
          }
          else m_group->AddConnection(con);
        }
      }

      if (m_group->Run()) 
      {
#ifdef _WIN32
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
                  c->m_netcon.Kill();
                  killcnt++;
                }
              }
              if (!killcnt)
              {
                printf("User %s not found!\n",buf);
              }
            }
            else printf("Kill aborted with no input\n");
            needprompt=1;
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
              enforceACL();
              m_group->SetLicenseText(g_config_license.Get());
            }
            needprompt=1;
          }
          else needprompt=2;
         

        }
        Sleep(1);
#else
	struct timespec ts={0,1000*1000};
	nanosleep(&ts,NULL);

    if (g_reloadconfig && !ReadConfig(argv[1]))
    {
      delete m_listener;
      m_listener = new JNL_Listen(g_config_port);
      m_group->SetConfig(g_config_bpi,g_config_bpm);
      g_reloadconfig=0;
      enforceACL();
      m_group->SetLicenseText(g_config_license.Get());
    }
#endif
  
      }
    }


  }

  delete m_group;
  delete m_listener;

  JNL::close_socketlib();
	return 0;
}
