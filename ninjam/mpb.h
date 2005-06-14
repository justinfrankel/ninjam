#ifndef _MPB_H_
#define _MPB_H_ // mpb.h, message parsing and building


#include "netmsg.h"


#define MESSAGE_SERVER_AUTH_CHALLENGE 0x00

class mpb_server_auth_challenge 
{
  public:
    mpb_server_auth_challenge() : server_caps(0), license_agreement(0) { memset(challenge,0,sizeof(challenge)); }
    ~mpb_server_auth_challenge() { }

    int parse(Net_Message *msg); // return 0 on success
    Net_Message *build();


    // public data
    unsigned char challenge[8];
    int server_caps; // low bit is license agreement
    char *license_agreement;
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
    char flag;  // low bit is success bit
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
    // flags, &1 = no default subscribe
    void build_add_rec(int isActive, int channelid, short volume, int pan, int flags, char *username, char *chname);
    int parse_get_rec(int offs, int *isActive, int *channelid, short *volume, int *pan, int *flags, char **username, char **chname); // returns offset of next item on success, or <0 if out of items

   private:

     Net_Message *m_intmsg;
};


#define MESSAGE_SERVER_DOWNLOAD_INTERVAL_BEGIN 0x04
class mpb_server_download_interval_begin
{
  public:
    mpb_server_download_interval_begin() : estsize(0), fourcc(0), chidx(0), username(0) { memset(guid,0,sizeof(guid)); }
    ~mpb_server_download_interval_begin() { }

    int parse(Net_Message *msg); // return 0 on success
    Net_Message *build();

    // public data
    unsigned char guid[16];
    int estsize;
    int fourcc;
    int chidx;       // only 1 byte
    char *username;
};


#define MESSAGE_SERVER_DOWNLOAD_INTERVAL_WRITE 0x05
class mpb_server_download_interval_write
{
  public:
    mpb_server_download_interval_write() : flags(0), audio_data(0), audio_data_len(0) { memset(guid,0,sizeof(guid)); }
    ~mpb_server_download_interval_write() { }

    int parse(Net_Message *msg); // return 0 on success
    Net_Message *build();

    // public data
    unsigned char guid[16]; // transfer id
    char flags; // & 1 = end

    void *audio_data;
    int audio_data_len; // not encoded in, just used internally
};



#define MESSAGE_CLIENT_AUTH_USER 0x80
class mpb_client_auth_user
{
  public:
    mpb_client_auth_user() : client_caps(0), username(0) { memset(passhash,0,sizeof(passhash)); }
    ~mpb_client_auth_user() { }

    int parse(Net_Message *msg); // return 0 on success
    Net_Message *build();


    // public data
    unsigned char passhash[20];
    int client_caps; // low bit is agreeing to license
    char *username;
};



#define MESSAGE_CLIENT_SET_USERMASK 0x81
class mpb_client_set_usermask
{
  public:
    mpb_client_set_usermask() : m_intmsg(0) { }
    ~mpb_client_set_usermask() { }

    int parse(Net_Message *msg); // return 0 on success
    Net_Message *build();


    void build_add_rec(char *username, unsigned int chflags);
    int parse_get_rec(int offs, char **username, unsigned int *chflags); // returns offset of next item on success, or <0 if out of items

   private:

     Net_Message *m_intmsg;
};

#define MESSAGE_CLIENT_SET_CHANNEL_INFO 0x82
class mpb_client_set_channel_info
{
  public:
    mpb_client_set_channel_info() : mpisize(4), m_intmsg(0) { }
    ~mpb_client_set_channel_info() { }

    int parse(Net_Message *msg); // return 0 on success
    Net_Message *build();


    // pan is -128..127
    // volume is dB gain, so 0=0dB, 10=1dB, -30=-3 dB, etc
    // flags, &1 = no default subscribe
    void build_add_rec(char *chname, short volume, int pan, int flags);
    int parse_get_rec(int offs, char **chname, short *volume, int *pan, int *flags); // returns offset of next item on success, or <0 if out of items

    int mpisize;

   private:

     Net_Message *m_intmsg;
};


#define MESSAGE_CLIENT_UPLOAD_INTERVAL_BEGIN 0x83
class mpb_client_upload_interval_begin
{
  public:
    mpb_client_upload_interval_begin() : estsize(0), fourcc(0), chidx(0){ memset(guid,0,sizeof(guid)); }
    ~mpb_client_upload_interval_begin() { }

    int parse(Net_Message *msg); // return 0 on success
    Net_Message *build();

    // public data
    unsigned char guid[16];
    int estsize;
    int fourcc;
    int chidx;       // only 1 byte
};



// this uses the exact same message format as the server version
#define MESSAGE_CLIENT_UPLOAD_INTERVAL_WRITE 0x84
class mpb_client_upload_interval_write
{
  public:
    mpb_client_upload_interval_write() : flags(0), audio_data(0), audio_data_len(0) { memset(guid,0,sizeof(guid)); }
    ~mpb_client_upload_interval_write() { }

    int parse(Net_Message *msg); // return 0 on success
    Net_Message *build();

    // public data
    unsigned char guid[16];
    char flags; // & 1 = end

    void *audio_data;
    int audio_data_len; // not encoded in, just used internally
};




#endif//_MPB_H_
