#ifndef _USERCON_H_
#define _USERCON_H_


#include "../netmsg.h"
#include "../../WDL/string.h"

#define MAX_USER_CHANNELS 32
#define MAX_USERS 64
#define MAX_UPLOADS 32
#define MAX_DOWNLOADS 128

class User_SubscribeMask
{
public:
  User_SubscribeMask();
  ~User_SubscribeMask();
  WDL_String username;
  unsigned int channelmask;
};

class User_Channel
{
public:
  User_Channel() { volume=0; panning=128; }
  ~User_Channel() { }
  WDL_String name;
  int volume;
  int panning;
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



class User_Connection
{
  public:
    User_Connection();
    ~User_Connection();

  private:

    Net_Connection *m_netcon;
    char *m_username;
    
    // auth info
    unsigned char m_challenge[8];
    int m_clientcaps;


    int m_num_channels;
    User_Channel m_channels[MAX_USER_CHANNELS];
    User_SubscribeMask m_subscribemask[MAX_USERS];

    User_TransferState m_uploads[MAX_UPLOADS];
    User_TransferState m_downloads[MAX_DOWNLOADS];

};



#endif//_USERCON_H_