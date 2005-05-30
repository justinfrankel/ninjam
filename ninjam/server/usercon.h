#ifndef _USERCON_H_
#define _USERCON_H_


#include "../netmsg.h"


#define MAX_USER_CHANNELS 32


class User_Channel
{
public:
  User_Channel() { name=0; volume=0; panning=128; }
  ~User_Channel() { free(name); }
  char *name;
  int volume;
  int panning;
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
    User_Channel m_channels[32];

};



#endif//_USERCON_H_