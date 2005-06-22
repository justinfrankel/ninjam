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
#include "../../WDL/jnetlib/httpget.h"
#include "../netmsg.h"
#include "../mpb.h"
#include "usercon.h"

#include "../../WDL/rng.h"
#include "../../WDL/sha.h"
#include "../../WDL/lineparse.h"
#include "../../WDL/ptrlist.h"
#include "../../WDL/string.h"


User_Group *m_group;
JNL_Listen *m_listener;
void onConfigChange();
void directory_onchange();
void directory_run();
void base64encode(const unsigned char *in, char *out);

class UserPassEntry
{
public:
  UserPassEntry() {priv_flag=0;} 
  ~UserPassEntry() {} 
  WDL_String name, pass;
  unsigned int priv_flag;
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
int g_config_allow_anonchat;
int g_config_port;
bool g_config_allowanonymous;
WDL_String g_config_license,g_config_pubuser,g_config_pubpass,g_config_pubdesc;

static int myGetUserPass(User_Group *group, char *username, char *sha1buf_user, char **isanon, unsigned int *privs)
{
  if (!strncmp(username,"anonymous",9) && (!username[9] || username[9] == ':'))
  {
    printf("got anonymous request (%s)\n",g_config_allowanonymous?"allowing":"denying");
    if (!g_config_allowanonymous) return 0;
    *isanon=username + (username[9] == ':' ? 10:9);
    *privs=(g_config_allow_anonchat?PRIV_CHATSEND:0);
    return 1; // allow
  }

  int x;
  printf("got login request for '%s'\n",username);
  for (x = 0; x < g_userlist.GetSize(); x ++)
  {
    if (!strcmp(username,g_userlist.Get(x)->name.Get()))
    {
      char *pass=g_userlist.Get(x)->pass.Get();
      WDL_SHA1 shatmp;
      shatmp.add(username,strlen(username));
      shatmp.add(":",1);
      shatmp.add(pass,strlen(pass));

      shatmp.result(sha1buf_user);

      *privs=g_userlist.Get(x)->priv_flag;
      *isanon=0;
      return 1;
    }
  }
  return 0;
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
  else if (!stricmp(t,"MaxUsers"))
  {
    if (lp->getnumtokens() != 2) return -1;
    int p=lp->gettoken_int(1);
    m_group->m_max_users=p;
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
  else if (!stricmp(t,"PublicServer"))
  {
    if (lp->getnumtokens() != 4) 
    {
      printf("PublicServer requires a userid, password and description\n");
      g_config_pubuser.Set(lp->gettoken_str(1));
      g_config_pubpass.Set(lp->gettoken_str(2));
      g_config_pubdesc.Set(lp->gettoken_str(3));
      return -1;
    }
    
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
    if (lp->getnumtokens() != 3 && lp->getnumtokens() != 4) return -1;
    UserPassEntry *p=new UserPassEntry;
    p->name.Set(lp->gettoken_str(1));
    p->pass.Set(lp->gettoken_str(2));
    if (lp->getnumtokens()>3)
    {
      char *ptr=lp->gettoken_str(3);
      while (*ptr)
      {
        if (*ptr == '*') p->priv_flag|=~0;
        else if (*ptr == 'T' || *ptr == 't') p->priv_flag |= PRIV_TOPIC;
        else if (*ptr == 'B' || *ptr == 'b') p->priv_flag |= PRIV_BPM;
        else if (*ptr == 'C' || *ptr == 'c') p->priv_flag |= PRIV_CHATSEND;
        else if (*ptr == 'K' || *ptr == 'k') p->priv_flag |= PRIV_KICK;        
        else if (*ptr == 'R' || *ptr == 'r') p->priv_flag |= PRIV_RESERVE;        
        else printf("Warning: Unknown user priviledge flag '%c'\n",*ptr);
        ptr++;
      }
    }
    else p->priv_flag=PRIV_CHATSEND;// default privs
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
  else if (!stricmp(t,"AnonymousUsersCanChat"))
  {
    if (lp->getnumtokens() != 2) return -1;

    int x=lp->gettoken_enum(1,"no\0yes\0");
    if (x <0)
    {
      return -2;
    }
    g_config_allow_anonchat=!!x;
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
  g_config_port=2049;
  g_config_allow_anonchat=1;
  g_config_allowanonymous=0;
  m_group->m_max_users=0; // unlimited users
  g_acllist.Resize(0);
  g_config_license.Set("");
  g_config_pubuser.Set("");
  g_config_pubpass.Set("");

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

  m_group=new User_Group;

  printf("Ninjam v0.01a server starting up...\n");
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

    m_group->GetUserPass=myGetUserPass;

    printf("Using default %d BPM, %d beats/interval\n",120,8);
    m_group->SetConfig(8,120);

    m_group->SetLicenseText(g_config_license.Get());

    directory_onchange();

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
          m_group->AddConnection(con,flag == ACL_FLAG_RESERVE);
        }
      }

      if (m_group->Run()) 
      {
        directory_run();
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
              printf("Listening on port %d...",g_config_port);    

              onConfigChange();
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
      g_reloadconfig=0;

      onConfigChange();
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


void onConfigChange()
{
  delete m_listener;
  m_listener = new JNL_Listen(g_config_port);
  //m_group->SetConfig(g_config_bpi,g_config_bpm);
  enforceACL();
  m_group->SetLicenseText(g_config_license.Get());
  directory_onchange();
}


/////////////////////// directory stuff

int directory_port;
int directory_state; // 0=not in directory, 1=in directory
JNL_HTTPGet *directory_con;
WDL_String directory_magic;

void directory_onchange()
{
  char *user=g_config_pubuser.Get();
  char *pass=g_config_pubpass.Get();

  int newstate = user && pass && *user && *pass;
  
  if (!newstate != !directory_state || (g_config_port != directory_port && newstate))
  {
    directory_port=g_config_port;
    if (directory_con) delete directory_con;

    directory_con=new JNL_HTTPGet(JNL_CONNECTION_AUTODNS,65536);

    WDL_String userstr("njs-user:");
    userstr.Append(user);

    WDL_String authstr("njs-auth:");
    // append auth base64 encoded
    {
      WDL_SHA1 shatmp;
      shatmp.add(user,strlen(user));
      shatmp.add(":",1);
      shatmp.add(pass,strlen(pass));

      unsigned char tmpbuf[WDL_SHA1SIZE];
      char outbuf[WDL_SHA1SIZE*2+1];
      shatmp.result(tmpbuf);

      base64encode(tmpbuf,outbuf);

      authstr.Append(outbuf);
    }
    char infobuf[512];
    sprintf(infobuf,"njs-port:%d",directory_port);

    directory_con->addheader(infobuf);
    directory_con->addheader("User-Agent:NINJAMsrv/0.0");

    directory_con->addheader(userstr.Get());
    directory_con->addheader(authstr.Get());
    directory_con->addheader(newstate ? "njs-action:add" : "njs-action:del");
    directory_con->addheader("Connection:close");
    directory_con->connect("http://www.ninjam.com/njd/njs/");

    directory_state=newstate;
  }
}

void directory_run()
{
  if (directory_con)
  {
    int a=directory_con->run();
    if (a < 0)
    {
      printf("Error reporting to NJD: %s\n",directory_con->geterrorstr());
      delete directory_con;
      directory_con=0;
      directory_state=0;
    }
    else if (a > 0)
    {
      // look at reply
      int success=0;
      if (directory_con->getreplycode() >= 200 && directory_con->getreplycode() < 300) // success
      {
        int l=directory_con->bytes_available();
        if (l > 65535) l=65535;
        if (l > 0)
        {
          char buf[65536];
          directory_con->get_bytes(buf,l);
          int x;
          for (x = 0; x < l; x ++) if (buf[x]=='\n') buf[x]=0; // remove newlines, make a null delimited list
          buf[l]=0;
          // look at text reply
          
          char *p=buf;
          while (p < buf+l) // go through list of strings
          {
            if (!strncmp(p,"STATUS:",7))
            {
              if (!strcmp(p+7,"OK"))
              {
                printf("NJD: Server replied OK\n");
                success=1;
              }
              else if (!strcmp(p+7,"REMOVED"))
              {
                directory_magic.Set("");
                if (directory_state) printf("Error reporting to NJD: server removed even though add requested\n");
              }
              else 
                printf("Error reporting to NJD: server replied \"%s\"\n",p+7);
            }            
            else if (!strncmp(p,"MAGIC:",6))
            {
              directory_magic.Set(p+6);
            }

            p+=strlen(p)+1;
          }

        }
        else printf("Error reporting to NJD: empty reply\n");
      }
      else printf("Error reporting to NJD: got reply %d\n",directory_con->getreplycode());

      directory_state=success;
      delete directory_con;
      directory_con=0;
    }
  }
}



void base64encode(const unsigned char *in, char *out)
{
  char alphabet[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
  int shift = 0;
  int accum = 0;

  while (*in)
  {
    if (*in)
    {
      accum <<= 8;
      shift += 8;
      accum |= *in++;
    }
    while ( shift >= 6 )
    {
      shift -= 6;
      *out++ = alphabet[(accum >> shift) & 0x3F];
    }
  }
  if (shift == 4)
  {
    *out++ = alphabet[(accum & 0xF)<<2];
    *out++='=';  
  }
  else if (shift == 2)
  {
    *out++ = alphabet[(accum & 0x3)<<4];
    *out++='=';  
    *out++='=';  
  }

  *out++=0;
}
