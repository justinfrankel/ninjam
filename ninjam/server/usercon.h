#ifndef _USERCON_H_
#define _USERCON_H_


#include "../netmsg.h"
#include "../../WDL/string.h"
#include "../../WDL/ptrlist.h"
#include "../mpb.h"

#define MAX_USER_CHANNELS 32
#define MAX_USERS 64
#define MAX_UPLOADS 32
#define MAX_DOWNLOADS 128

#define PRIV_TOPIC 1
#define PRIV_CHATSEND 2
#define PRIV_BPM 4
#define PRIV_KICK 8


class User_SubscribeMask
{
public:
  User_SubscribeMask()  {} 
  ~User_SubscribeMask() {}
  WDL_String username;
  unsigned int channelmask;
};

class User_Channel
{
public:
  User_Channel() { active=0; volume=0; panning=128; flags=0; }
  ~User_Channel() { }
  int active;
  WDL_String name;
  int volume;
  int panning;
  int flags;
};



class User_TransferState
{
public:
  User_TransferState() : fourcc(0), bytes_estimated(0), bytes_sofar(0), fp(0)
  { 
    time(&last_acttime);
    memset(guid,0,sizeof(guid));
  }
  ~User_TransferState() 
  { 
    if (fp) fclose(fp);
    fp=0;
  }

  time_t last_acttime;
  unsigned char guid[16];
  unsigned int fourcc;
  unsigned int bytes_estimated;

  unsigned int bytes_sofar;
  
  FILE *fp;
};


class User_Group;

class User_Connection
{
  public:
    User_Connection(JNL_Connection *con, User_Group *grp);
    ~User_Connection();

    int Run(User_Group *group, int *wantsleep=0); // returns 1 if disconnected, -1 if error in data. 0 if ok.
    void SendConfigChangeNotify(int bpm, int bpi);

    void Send(Net_Message *msg) { m_netcon.Send(msg); }



    Net_Connection m_netcon;
    WDL_String m_username;
    
    // auth info
    time_t m_connect_time;
    int m_auth_state;    
    unsigned char m_challenge[8];
    int m_clientcaps;

    int m_auth_privs;

    User_Channel m_channels[MAX_USER_CHANNELS];

    WDL_PtrList<User_SubscribeMask> m_sublist; // people+channels we subscribe to

    WDL_PtrList<User_TransferState> m_recvfiles;
    WDL_PtrList<User_TransferState> m_sendfiles;
};


class User_Group
{
  public:
    User_Group();
    ~User_Group();

    void AddConnection(JNL_Connection *con);

    int Run(); // return 1 if safe to sleep
    void SetConfig(int bpi, int bpm);
    void SetLicenseText(char *text) { m_licensetext.Set(text); }
    void Broadcast(Net_Message *msg, User_Connection *nosend=0);


    // sends a message to the people subscribing to a channel of a user
    void BroadcastToSubs(Net_Message *msg, User_Connection *src, int channel);

    int (*GetUserPass)(User_Group *group, char *username, char *sha1buf_user, char **isanon, unsigned int *privs); // return nonzero if valid, SHA1(user:pass) or username if isanon

    void onChatMessage(User_Connection *con, mpb_chat_message *msg);

    WDL_PtrList<User_Connection> m_users;

    int m_last_bpm, m_last_bpi;

    WDL_String m_licensetext;
    WDL_String m_topictext;
};



#endif//_USERCON_H_
