#include <windows.h>

#include "mpb.h"



// MESSAGE_SERVER_AUTH_CHALLENGE 
int mpb_server_auth_challenge::parse(Net_Message *msg) // return 0 on success
{
  if (msg->get_type() != MESSAGE_SERVER_AUTH_CHALLENGE) return -1;
  int capsl=msg->get_size() - sizeof(challenge);
  if (capsl < 0) return 1;
  unsigned char *p=(unsigned char *)msg->get_data();
  if (!p) return 2;

  memcpy(challenge,p,sizeof(challenge));
  p+=sizeof(challenge);
  server_caps=0;
  int shift=0;


  if (capsl > 4) capsl=4;

  while (capsl--)
  {
    server_caps |= ((int)*p++)<<shift;
    shift+=8;
  }

  return 0;
}

Net_Message *mpb_server_auth_challenge::build()
{
  Net_Message *nm=new Net_Message;
  nm->set_type(MESSAGE_SERVER_AUTH_CHALLENGE);
  
  int scsize=0;
  int sc=server_caps;
  while (sc)
  {
    scsize++;
    sc>>=8;
  }

  nm->set_size(sizeof(challenge) + scsize);

  unsigned char *p=(unsigned char *)nm->get_data();

  if (!p) 
  {
    delete nm;
    return 0;
  }

  memcpy(p,challenge,sizeof(challenge));
  p+=sizeof(challenge);

  sc=server_caps;
  while (scsize--)
  {
    *p++=sc&0xff;
    sc>>=8;
  }

  return nm;
}



// MESSAGE_SERVER_AUTH_REPLY
int mpb_server_auth_reply::parse(Net_Message *msg) // return 0 on success
{
  if (msg->get_type() != MESSAGE_SERVER_AUTH_REPLY) return -1;
  if (msg->get_size() < 1) return 1;
  unsigned char *p=(unsigned char *)msg->get_data();
  if (!p) return 2;

  flag=*p;

  return 0;
}

Net_Message *mpb_server_auth_reply::build()
{
  Net_Message *nm=new Net_Message;
  nm->set_type(MESSAGE_SERVER_AUTH_REPLY);
  
  nm->set_size(1);

  unsigned char *p=(unsigned char *)nm->get_data();

  if (!p)
  {
    delete nm;
    return 0;
  }

  *p=flag;

  return nm;
}


// MESSAGE_SERVER_CONFIG_CHANGE_NOTIFY
int mpb_server_config_change_notify::parse(Net_Message *msg) // return 0 on success
{
  if (msg->get_type() != MESSAGE_SERVER_CONFIG_CHANGE_NOTIFY) return -1;
  if (msg->get_size() < 4) return 1;
  unsigned char *p=(unsigned char *)msg->get_data();
  if (!p) return 2;

  beats_minute = *p++;
  beats_minute |= ((int)*p++)<<8;
  beats_interval = *p++;
  beats_interval |= ((int)*p++)<<8;

  return 0;
}

Net_Message *mpb_server_config_change_notify::build()
{
  Net_Message *nm=new Net_Message;
  nm->set_type(MESSAGE_SERVER_CONFIG_CHANGE_NOTIFY);
  
  nm->set_size(4);

  unsigned char *p=(unsigned char *)nm->get_data();

  if (!p)
  {
    delete nm;
    return 0;
  }

  *p++=beats_minute&0xff;
  *p++=(beats_minute>>8)&0xff;
  *p++=beats_interval&0xff;
  *p++=(beats_interval>>8)&0xff;

  return nm;
}


// MESSAGE_SERVER_USERINFO_CHANGE_NOTIFY
int mpb_server_userinfo_change_notify::parse(Net_Message *msg) // return 0 on success
{
  if (msg->get_type() != MESSAGE_SERVER_USERINFO_CHANGE_NOTIFY) return -1;
  if (msg->get_size() < 1) return 1;

  m_intmsg = msg;
  return 0;
}

Net_Message *mpb_server_userinfo_change_notify::build()
{
  if (m_intmsg) 
  {
    Net_Message *n=m_intmsg;
    m_intmsg=0;
    return n;
  }

  Net_Message *nm=new Net_Message;
  nm->set_type(MESSAGE_SERVER_USERINFO_CHANGE_NOTIFY); 
  nm->set_size(0);

  return nm;
}


void mpb_server_userinfo_change_notify::build_add_rec(int isRemove, int channelid, 
                                                      int volume, int pan, char *username, char *chname)
{
  int size=1+ // is remove
           1+ // channel index
           1+ // volume
           1+ // pan
           strlen(username?username:"")+1+strlen(chname?chname:"")+1;

  if (!m_intmsg) m_intmsg = new Net_Message;
  int oldsize=m_intmsg->get_size();
  m_intmsg->set_size(size+oldsize);
  unsigned char *p=(unsigned char *)m_intmsg->get_data();
  if (p)
  {
    p+=oldsize;
    *p++=!!isRemove;
    
    if (channelid < 0) channelid=0;
    else if (channelid>255)channelid=255;
    *p++=channelid;

    if (volume< 0) volume=0;
    else if (volume>255)volume=255;
    *p++=volume;

    if (pan<-128) pan=-128;
    else if (pan>127)pan=127;
    *p++=(unsigned char)pan;

    strcpy((char*)p,username);
    p+=strlen(username)+1;
    strcpy((char*)p,chname);
    p+=strlen(chname)+1;
  }
}

int mpb_server_userinfo_change_notify::parse_get_rec(int idx, int *isRemove, int *channelid, int *volume, 
                                                     int *pan, char **username, char **chname)
{
  return -1;
}
