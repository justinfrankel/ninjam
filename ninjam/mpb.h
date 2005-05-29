#ifndef _MPB_H_
#define _MPB_H_ // mpb.h, message parsing and building


#include "netmsg.h"

class mpb_server_auth_challenge // MESSAGE_SERVER_AUTH_CHALLENGE
{
  public:
    mpb_server_auth_challenge() : server_caps(0) { memset(challenge,0,sizeof(challenge)); }
    ~mpb_server_auth_challenge() { }

    int parse(Net_Message *msg); // return 0 on success
    Net_Message *build();


    // public data
    unsigned char challenge[8];
    int server_caps;
};

#endif//_MPB_H_