/*
    NINJAM - mpb.h
    Copyright (C) 2005-2007 Cockos Incorporated

    NINJAM is free software; you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation; either version 2 of the License, or
    (at your option) any later version.

    NINJAM is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU General Public License for more details.

    You should have received a copy of the GNU General Public License
    along with NINJAM; if not, write to the Free Software
    Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
*/

/*

  This header provides message-type defines, as well as Message Parser and Builder (mpb_) 
  classes for constructing and parsing Net_Messages.
 
*/


#ifndef _MPB_H_
#define _MPB_H_ // mpb.h, message parsing and building


#include "netmsg.h"


#define PROTO_VER_MIN 0x00020000
#define PROTO_VER_MAX 0x0002ffff
#define PROTO_VER_CUR 0x00020000


#define MESSAGE_SERVER_AUTH_CHALLENGE 0x00

class mpb_server_auth_challenge 
{
  public:
    mpb_server_auth_challenge() : server_caps(0), license_agreement(0), protocol_version(0) { memset(challenge,0,sizeof(challenge)); }
    ~mpb_server_auth_challenge() { }

    int parse(Net_Message *msg); // return 0 on success
    Net_Message *build();


    // public data
    unsigned char challenge[8];
    int server_caps; // low bit is license agreement, bits 8-16 are keepalive
    char *license_agreement;
    int protocol_version; // version should be 1 to start.
};

#define MESSAGE_SERVER_AUTH_REPLY 0x01

class mpb_server_auth_reply
{
  public:
    mpb_server_auth_reply() : flag(0), errmsg(0), maxchan(32) { }
    ~mpb_server_auth_reply() { }

    int parse(Net_Message *msg); // return 0 on success
    Net_Message *build();


    // public data
    char flag;  // low bit is success bit
    char *errmsg; // if success bit is set, and this is also set, then it is the effective username of the client
    char maxchan;
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
    // flags, &1 = no default subscribe, &2=instamode
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
    unsigned int fourcc;
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
    mpb_client_auth_user() : client_caps(0), client_version(0), username(0) { memset(passhash,0,sizeof(passhash)); }
    ~mpb_client_auth_user() { }

    int parse(Net_Message *msg); // return 0 on success
    Net_Message *build();


    // public data
    unsigned char passhash[20];
    int client_caps; // low bit is agreeing to license
    int client_version; // client version, only present if second bit of caps is there
                     // second bit should be set, otherwise server will disconnect anyway.
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
    // flags, &1 = no default subscribe, &2=instamode, &4=session mode, 0x80=filler (inactive)
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
    unsigned int fourcc;
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


#define MESSAGE_CHAT_MESSAGE 0xC0
class mpb_chat_message
{
  public:
    mpb_chat_message() { memset(parms,0,sizeof(parms)); }
    ~mpb_chat_message() { }

    int parse(Net_Message *msg); // return 0 on success
    Net_Message *build();

    char *parms[5];

    // currently defined client->server commands:
    // MSG <text>   - sends a message to everybody
    // PRIVMSG username <text>   - sends a private message to username
    // TOPIC <topic>   - set server topic (need permissions)

    // and server->client commands:
    // MSG <username> <text>   - a message from username
    // PRIVMSG username <text>   - a private message from username
    // TOPIC <topic>   - server topic change
};




#endif//_MPB_H_
