#ifndef _USERCON_H_
#define _USERCON_H_


#include "../netmsg.h"
#include "../../WDL/string.h"
#include "../../WDL/ptrlist.h"

#define MAX_USER_CHANNELS 32
#define MAX_USERS 64
#define MAX_UPLOADS 32
#define MAX_DOWNLOADS 128

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
  User_TransferState() { }
  ~User_TransferState() { }

  unsigned int bytes_sofar;
  unsigned int bytes_estimated;
  unsigned int fourcc;
  
  FILE *fp;


};


class User_Group;

class User_Connection
{
  public:
    User_Connection(JNL_Connection *con);
    ~User_Connection();

    int Run(User_Group *group); // returns 1 if disconnected, -1 if error in data. 0 if ok.
    void SendConfigChangeNotify(int bpm, int bpi);

    void Send(Net_Message *msg) { m_netcon.Send(msg); }



    Net_Connection m_netcon;
    WDL_String m_username;
    
    // auth info
    int m_auth_state;    
    unsigned char m_challenge[8];
    int m_clientcaps;


    User_Channel m_channels[MAX_USER_CHANNELS];
    User_SubscribeMask m_subscribemask[MAX_USERS];

    User_TransferState m_uploads[MAX_UPLOADS];
    User_TransferState m_downloads[MAX_DOWNLOADS];

};


class User_Group
{
  public:
    User_Group();
    ~User_Group();

    void AddConnection(JNL_Connection *con);

    void Run();
    void SetConfig(int bpi, int bpm);
    void Broadcast(Net_Message *msg, User_Connection *nosend=0);





    WDL_PtrList<User_Connection> m_users;

    int m_last_bpm, m_last_bpi;

};



#endif//_USERCON_H_