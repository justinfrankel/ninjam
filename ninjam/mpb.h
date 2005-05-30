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

class mpb_server_config_change_notify
{
  public:
    mpb_server_config_change_notify() : beats_minute(120), beats_interval(32) { }
    ~mpb_server_config_change_notify() { }

    int parse(Net_Message *msg); // return 0 on success
    Net_Message *build();


    // public data
    int beats_minute;  //bpm
    int beats_interval;  // beats/interval
};



#define MESSAGE_SERVER_USERINFO_CHANGE_NOTIFY 0x03

class mpb_server_userinfo_change_notify
{
  public:
    mpb_server_userinfo_change_notify() : m_intmsg(0) { }
    ~mpb_server_userinfo_change_notify() { }

    int parse(Net_Message *msg); // return 0 on success
    Net_Message *build(); // if you call build_add_rec at all, you must do delete x->build(); to avoid a mem leak.

    // public accessors
    // pan is -128..127
    // volume is dB gain, so 0=0dB, 10=1dB, -30=-3 dB, etc
    // flags ...
    void build_add_rec(int isRemove, int channelid, int volume, int pan, int flags, char *username, char *chname);
    int parse_get_rec(int offs, int *isRemove, int *channelid, int *volume, int *pan, int *flags, char **username, char **chname); // returns offset of next item on success, or <0 if out of items

   private:

     Net_Message *m_intmsg;
};


#define MESSAGE_SERVER_DOWNLOAD_INTERVAL_BEGIN 0x04
class mpb_server_download_interval_begin
{
  public:
    mpb_server_download_interval_begin() : estsize(0), fourcc(0), chidx(0), transfer_id(0), username(0) { memset(guid,0,sizeof(guid)); }
    ~mpb_server_download_interval_begin() { }

    int parse(Net_Message *msg); // return 0 on success
    Net_Message *build();

    // public data
    unsigned char guid[16];
    int estsize;
    int fourcc;
    int transfer_id; // 0 cached by guid, otherwise it is a transfer id (only 2 bytes)
    int chidx;       // only 2 bytes
    char *username;
};


#define MESSAGE_SERVER_DOWNLOAD_INTERVAL_WRITE 0x05
class mpb_server_download_interval_write
{
  public:
    mpb_server_download_interval_write() : transfer_id(0), flags(0), audio_data(0), audio_data_len(0) { }
    ~mpb_server_download_interval_write() { }

    int parse(Net_Message *msg); // return 0 on success
    Net_Message *build();

    // public data
    int transfer_id; // transfer id (only 2 bytes) 
    char flags; // & 1 = end

    void *audio_data;
    int audio_data_len; // not encoded in, just used internally
};



#define MESSAGE_CLIENT_AUTH_USER 0x80
#define MESSAGE_CLIENT_SET_USERMASK 0x81
#define MESSAGE_CLIENT_SET_CHANNEL_INFO 0x82
#define MESSAGE_CLIENT_UPLOAD_INTERVAL_BEGIN 0x83
#define MESSAGE_CLIENT_UPLOAD_INTERVAL_WRITE 0x84





#endif//_MPB_H_