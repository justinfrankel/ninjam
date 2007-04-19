/*
    NINJAM Server - usercon.h
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

  This header provides the class declarations for the User_Group (which the server uses)
  and other classes it is built upon. Additionally, it provides the abstract declaration
  of IUserInfoLookup, which the server needs to override to provide user authentication 
  lookup.

*/




#ifndef _USERCON_H_
#define _USERCON_H_


#include "../netmsg.h"
#include "../../WDL/string.h"
#include "../../WDL/sha.h"
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
#define PRIV_RESERVE 16
#define PRIV_ALLOWMULTI 32 // allows multiple users by the same name (subsequent users append -X to them)
#define PRIV_HIDDEN 64   // hidden user, doesn't count for a slot, too
#define PRIV_VOTE 128

#define MAX_BPM 400
#define MAX_BPI 64
#define MIN_BPM 40
#define MIN_BPI 2

class IUserInfoLookup // abstract base class, overridden by server
{
public:
  IUserInfoLookup() { is_status=0; user_valid=0; reqpass=1; privs=0; max_channels=0; }
  virtual ~IUserInfoLookup() { }

  virtual int Run()=0; // return 1 if run is complete, 0 if still needs to run more

  int user_valid; // 1 if valid
  int reqpass; // password required, 1 is default
  unsigned int privs;
  int max_channels;
  int is_status;
  unsigned char sha1buf_user[WDL_SHA1SIZE];

  WDL_String hostmask;
  WDL_String username; // can modify this to change the username


  unsigned char sha1buf_request[WDL_SHA1SIZE]; // don't use, internal for User_Connection
};


class User_Connection;

class User_Group
{
  public:
    User_Group();
    ~User_Group();

    void AddConnection(JNL_Connection *con, int isres=0);

    int Run(); // return 1 if safe to sleep
    void SetConfig(int bpi, int bpm);
    void SetLicenseText(char *text) { m_licensetext.Set(text); }
    void Broadcast(Net_Message *msg, User_Connection *nosend=0);


    void SetLogDir(char *path); // NULL to not log

    // sends a message to the people subscribing to a channel of a user
    void BroadcastToSubs(Net_Message *msg, User_Connection *src, int channel);

    IUserInfoLookup *(*CreateUserLookup)(char *username);

    void onChatMessage(User_Connection *con, mpb_chat_message *msg);
    

    WDL_PtrList<User_Connection> m_users;

    int m_max_users;
    int m_last_bpm, m_last_bpi;
    int m_keepalive;


    int m_voting_threshold; // can be 1-100, or >100 to disable
    int m_voting_timeout; // seconds

    int m_loopcnt;

    unsigned int m_run_robin;

    int m_allow_hidden_users;

    WDL_String m_licensetext;
    WDL_String m_topictext;

    WDL_String m_logdir;
    FILE *m_logfp;

#ifdef _WIN32
    DWORD m_next_loop_time;
#else
    struct timeval m_next_loop_time;
#endif
};



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


class User_Connection
{
  public:
    User_Connection(JNL_Connection *con, User_Group *grp);
    ~User_Connection();

    int Run(User_Group *group, int *wantsleep=0); // returns 1 if disconnected, -1 if error in data. 0 if ok.
    void SendConfigChangeNotify(int bpm, int bpi);

    void Send(Net_Message *msg);

    int OnRunAuth(User_Group *group);

    void SendUserList(User_Group *group);

    Net_Connection m_netcon;
    WDL_String m_username;
    
    // auth info
    time_t m_connect_time;
    int m_auth_state;      // 1 if authorized, 0 if not yet, -1 if auth pending
    unsigned char m_challenge[8];
    int m_clientcaps;

    int m_auth_privs;

    int m_reserved;

    int m_max_channels;

    int m_vote_bpm;
    time_t m_vote_bpm_lasttime;
    int m_vote_bpi;
    time_t m_vote_bpi_lasttime;

    User_Channel m_channels[MAX_USER_CHANNELS];

    WDL_PtrList<User_SubscribeMask> m_sublist; // people+channels we subscribe to

    WDL_PtrList<User_TransferState> m_recvfiles;
    WDL_PtrList<User_TransferState> m_sendfiles;

    IUserInfoLookup *m_lookup;
};



#endif//_USERCON_H_
