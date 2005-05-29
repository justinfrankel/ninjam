#ifndef _MPB_H_
#define _MPB_H_ // mpb.h, message parsing and building


#include "netmsg.h"


#define MESSAGE_SERVER_AUTH_CHALLENGE 0x00

class mpb_server_auth_challenge 
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

#define MESSAGE_SERVER_AUTH_REPLY 0x01

class mpb_server_auth_reply
{
  public:
    mpb_server_auth_reply() : flag(0) { }
    ~mpb_server_auth_reply() { }

    int parse(Net_Message *msg); // return 0 on success
    Net_Message *build();


    // public data
    char flag;
};



#define MESSAGE_SERVER_CONFIG_CHANGE_NOTIFY 0x02
#define MESSAGE_SERVER_USERLIST_CHANGE_NOTIFY 0x03
#define MESSAGE_SERVER_DOWNLOAD_INTERVAL_BEGIN 0x04
#define MESSAGE_SERVER_DOWNLOAD_INTERVAL_WRITE 0x05

#define MESSAGE_CLIENT_AUTH_USER 0x80
#define MESSAGE_CLIENT_SET_USERMASK 0x81
#define MESSAGE_CLIENT_SET_CHANNEL_INFO 0x82
#define MESSAGE_CLIENT_UPLOAD_INTERVAL_BEGIN 0x83
#define MESSAGE_CLIENT_UPLOAD_INTERVAL_WRITE 0x84





#endif//_MPB_H_