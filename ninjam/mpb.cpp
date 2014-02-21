/*
    NINJAM - mpb.cpp
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

  This file provides implementation of Message Parser and Builder (mpb_) 
  classes for constructing and parsing Net_Messages.
 
*/


#ifdef _WIN32
#include <windows.h>
#else
#include <stdlib.h>
#include <memory.h>
#endif

#include "mpb.h"



// MESSAGE_SERVER_AUTH_CHALLENGE 
int mpb_server_auth_challenge::parse(Net_Message *msg) // return 0 on success
{
  if (msg->get_type() != MESSAGE_SERVER_AUTH_CHALLENGE) return -1;
  if (msg->get_size() < 4+4+(int)sizeof(challenge)) return 1;
  const unsigned char *p=(unsigned char *)msg->get_data();
  if (!p) return 2;

  memcpy(challenge,p,sizeof(challenge));
  p+=sizeof(challenge);

  server_caps = ((int)*p++);
  server_caps |= ((int)*p++)<<8;
  server_caps |= ((int)*p++)<<16;
  server_caps |= ((int)*p++)<<24;

  protocol_version = ((int)*p++);
  protocol_version |= ((int)*p++)<<8;
  protocol_version |= ((int)*p++)<<16;
  protocol_version |= ((int)*p++)<<24;

  if (server_caps&1)
  {
    const char *s=(const char *)p;
    const unsigned char *endp = (const unsigned char *)msg->get_data() + msg->get_size();
    while (p < endp) 
    {
      if (!*p)
      {
        license_agreement=s;
        break;
      }
      p++;
    }
  }

  return 0;
}

Net_Message *mpb_server_auth_challenge::build()
{
  Net_Message *nm=new Net_Message;
  nm->set_type(MESSAGE_SERVER_AUTH_CHALLENGE);
  
  const int la_sz = (license_agreement ? (int)strlen(license_agreement) + 1 : 0);
  const int sz = (int)sizeof(challenge)+8 + la_sz;
  nm->set_size(sz);

  unsigned char *p=(unsigned char *)nm->get_data();

  if (!p) 
  {
    delete nm;
    return 0;
  }

  memcpy(p,challenge,sizeof(challenge));
  p+=sizeof(challenge);

  int sc=server_caps;
  if (license_agreement) sc|=1;
  else sc&=~1;

  *p++ = sc&0xff;
  *p++ = (sc>>8)&0xff;
  *p++ = (sc>>16)&0xff;
  *p++ = (sc>>24)&0xff;

  *p++ = protocol_version&0xff;
  *p++ = (protocol_version>>8)&0xff;
  *p++ = (protocol_version>>16)&0xff;
  *p++ = (protocol_version>>24)&0xff;


  if (license_agreement)
  {
    memcpy(p,license_agreement,la_sz);
    p += la_sz;
  }


  return nm;
}



// MESSAGE_SERVER_AUTH_REPLY
int mpb_server_auth_reply::parse(Net_Message *msg) // return 0 on success
{
  if (msg->get_type() != MESSAGE_SERVER_AUTH_REPLY) return -1;
  if (msg->get_size() < 1) return 1;
  const unsigned char *p=(unsigned char *)msg->get_data();
  const unsigned char *endp = p + msg->get_size();
  if (!p) return 2;

  flag=*p++;
  if (msg->get_size()>1)
  {
    const char *t=(const char*)p;
    while (p < endp && *p) p++;

    if (p < endp)
    {
      errmsg=t;

      p++;
      if (p < endp)
      {
        maxchan=*p++;
      }
    }
  }

  return 0;
}

Net_Message *mpb_server_auth_reply::build()
{
  Net_Message *nm=new Net_Message;
  nm->set_type(MESSAGE_SERVER_AUTH_REPLY);
  
  const int errmsg_sz = errmsg ? (int)strlen(errmsg)+1 : 0;
  const int sz = 1 + (errmsg ? errmsg_sz + 1 : 0);
  nm->set_size(sz);

  unsigned char *p=(unsigned char *)nm->get_data();

  if (!p)
  {
    delete nm;
    return 0;
  }

  *p++=flag;
  if (errmsg)
  {
    memcpy(p, errmsg, errmsg_sz);
    p += errmsg_sz;
    *p++ = maxchan;
  }

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


void mpb_server_userinfo_change_notify::build_add_rec(int isActive, int channelid, 
                                                      short volume, int pan, int flags, const char *username, const char *chname)
{
  const int username_len = username ? (int)strlen(username) : 0;
  const int chname_len = chname ? (int)strlen(chname) : 0;

  const int size=1+ // is remove
           1+ // channel index
           2+ // volume
           1+ // pan
           1+ // flags
           username_len+1+chname_len+1;

  if (!m_intmsg) 
  {
    m_intmsg = new Net_Message;
    m_intmsg->set_type(MESSAGE_SERVER_USERINFO_CHANGE_NOTIFY); 
  }
  const int oldsize=m_intmsg->get_size();
  m_intmsg->set_size(size+oldsize);
  unsigned char *p=(unsigned char *)m_intmsg->get_data();
  if (p)
  {
    p+=oldsize;
    *p++=!!isActive;
    
    if (channelid < 0) channelid=0;
    else if (channelid>255)channelid=255;
    *p++=channelid;

    *p++=volume&0xff;
    *p++=(volume>>8)&0xff;

    if (pan<-128) pan=-128;
    else if (pan>127)pan=127;
    *p++=(unsigned char)pan;

    *p++=(unsigned char)flags;

    if (username_len) memcpy(p,username,username_len+1);
    else *p = 0;
    p+=username_len+1;

    if (chname_len) memcpy(p,chname,chname_len+1);
    else *p = 0;

    p+=chname_len+1;
  }
}


// returns offset of next item on success, or <= 0 if out of items
int mpb_server_userinfo_change_notify::parse_get_rec(int offs, int *isActive, int *channelid, short *volume, 
                                                     int *pan, int *flags, const char **username, const char **chname)
{
  const int hdrsize=1+ // is remove
           1+ // channel index
           2+ // volume
           1+ // pan
           1; // flags

  if (!m_intmsg) return 0;
  const unsigned char *p=(unsigned char *)m_intmsg->get_data();
  int len=m_intmsg->get_size()-offs;
  if (!p || len < hdrsize+2) return 0;
  p+=offs;

  const unsigned char *hdrbuf=p;
  const char *unp;
  const char *cnp;

  if (len < hdrsize+2) return 0;
  hdrbuf=p;
  len -= hdrsize;
  unp=(char *)hdrbuf+hdrsize;
  cnp=unp;
  while (*cnp)
  {
    cnp++;
    if (!len--) return 0;
  }
  cnp++;
  if (!len--) return 0;

  p=(unsigned char *)cnp;
  while (*p)
  {
    p++;
    if (!len--) return 0;
  }
  p++;
  if (!len--) return 0;

  *isActive=(int)*hdrbuf++;
  *channelid=(int)*hdrbuf++;
  *volume=(int)*hdrbuf++;
  *volume |= ((int)*hdrbuf++)<<8;  
  *pan = (int) *hdrbuf++;
  *flags = (int) *hdrbuf++;

  *username = unp;
  *chname = cnp;


  return (int) (p - (unsigned char *)m_intmsg->get_data());
}


// MESSAGE_SERVER_DOWNLOAD_INTERVAL_BEGIN
int mpb_server_download_interval_begin::parse(Net_Message *msg) // return 0 on success
{
  if (msg->get_type() != MESSAGE_SERVER_DOWNLOAD_INTERVAL_BEGIN) return -1;
  if (msg->get_size() < 25+1) return 1;
  const unsigned char *p=(const unsigned char *)msg->get_data();
  if (!p) return 2;

  memcpy(guid,p,sizeof(guid));
  p+=sizeof(guid);
  estsize = (int)*p++;
  estsize |= ((int)*p++)<<8;
  estsize |= ((int)*p++)<<16;
  estsize |= ((int)*p++)<<24;
  fourcc = (unsigned int)*p++;
  fourcc |= ((unsigned int)*p++)<<8;
  fourcc |= ((unsigned int)*p++)<<16;
  fourcc |= ((unsigned int)*p++)<<24;
  chidx = (int)*p++;
  int len=msg->get_size()-25;

  username=(const char *)p;


  // validate null termination for now
  while (len)
  {
    if (!*p) break;
    p++;
    len--;
  }
  if (!len) return -1;

  return 0;
}


Net_Message *mpb_server_download_interval_begin::build()
{
  Net_Message *nm=new Net_Message;
  nm->set_type(MESSAGE_SERVER_DOWNLOAD_INTERVAL_BEGIN);
  
  const int username_len = username ? (int)strlen(username) : 0;

  const int sz = 25 + username_len + 1;
  nm->set_size(sz);

  unsigned char *p=(unsigned char *)nm->get_data();

  if (!p||nm->get_size()!=sz)
  {
    delete nm;
    return 0;
  }

  memcpy(p,guid,sizeof(guid));
  p+=sizeof(guid);
  *p++=(unsigned char)((estsize)&0xff);
  *p++=(unsigned char)((estsize>>8)&0xff);
  *p++=(unsigned char)((estsize>>16)&0xff);
  *p++=(unsigned char)((estsize>>24)&0xff);
  *p++=(unsigned char)((fourcc)&0xff);
  *p++=(unsigned char)((fourcc>>8)&0xff);
  *p++=(unsigned char)((fourcc>>16)&0xff);
  *p++=(unsigned char)((fourcc>>24)&0xff);
  *p++=(unsigned char)((chidx)&0xff);

  if (username_len) memcpy(p, username, username_len + 1);
  else *p = 0;

  return nm;
}


// MESSAGE_SERVER_DOWNLOAD_INTERVAL_WRITE
int mpb_server_download_interval_write::parse(Net_Message *msg) // return 0 on success
{
  if (msg->get_type() != MESSAGE_SERVER_DOWNLOAD_INTERVAL_WRITE) return -1;
  if (msg->get_size() < 17) return 1;
  const unsigned char *p=(const unsigned char *)msg->get_data();
  if (!p) return 2;

  memcpy(guid,p,sizeof(guid));
  p+=sizeof(guid);
  flags = (char)*p++;

  audio_data = p;
  audio_data_len = msg->get_size()-17;

  return 0;
}


Net_Message *mpb_server_download_interval_write::build()
{
  Net_Message *nm=new Net_Message;
  nm->set_type(MESSAGE_SERVER_DOWNLOAD_INTERVAL_WRITE);
  
  const int sz = 17 + (audio_data ? audio_data_len : 0);
  nm->set_size(sz);

  unsigned char *p=(unsigned char *)nm->get_data();

  if (!p)
  {
    delete nm;
    return 0;
  }
  memcpy(p,guid,sizeof(guid));
  p+=sizeof(guid);
  *p++=(unsigned char) flags;

  if (audio_data&&audio_data_len) memcpy(p,audio_data,audio_data_len);

  return nm;
}






/////////////////////////////////////////////////////////////////////////
//////////// client to server messages
/////////////////////////////////////////////////////////////////////////


// MESSAGE_CLIENT_AUTH_USER
int mpb_client_auth_user::parse(Net_Message *msg) // return 0 on success
{
  if (msg->get_type() != MESSAGE_CLIENT_AUTH_USER) return -1;
  if (msg->get_size() < (int)sizeof(passhash)+1) return 1;
  const unsigned char *p=(const unsigned char *)msg->get_data();
  if (!p) return 2;
  int len=msg->get_size();

  memcpy(passhash,p,sizeof(passhash));
  p+=sizeof(passhash);
  len -= sizeof(passhash);

  username=(char *)p;
  while (*p && len>0)
  {
    p++;
    len--;
  }
  if (!len) return 3;
  p++;
  len--;
  
  if (len < 8) return 3;

  client_caps = ((int)*p++);
  client_caps |= ((int)*p++)<<8;
  client_caps |= ((int)*p++)<<16;
  client_caps |= ((int)*p++)<<24;

  client_version = ((int)*p++);
  client_version |= ((int)*p++)<<8;
  client_version |= ((int)*p++)<<16;
  client_version |= ((int)*p++)<<24;
  
  //printf("bla (len=%d, caps=%d) decoded client version %08x\n",len,client_caps,client_version);

  return 0;
}

Net_Message *mpb_client_auth_user::build()
{
  Net_Message *nm=new Net_Message;
  nm->set_type(MESSAGE_CLIENT_AUTH_USER);
  
  const int username_len = username ? (int)strlen(username) : 0;
  const int sz = (int)sizeof(passhash)+username_len + 1 + 4 + 4;
  nm->set_size(sz);

  unsigned char *p=(unsigned char *)nm->get_data();

  if (!p) 
  {
    delete nm;
    return 0;
  }

  memcpy(p,passhash,sizeof(passhash));
  p+=sizeof(passhash);

  if (username_len) memcpy(p, username, username_len+1);
  else *p = 0;
  p+=username_len+1;

  *p++=(client_caps&0xff);
  *p++=(client_caps&0xff00)>>8;
  *p++=(client_caps&0xff0000)>>16;
  *p++=(client_caps&0xff000000)>>24;

  *p++=(client_version&0xff);
  *p++=(client_version&0xff00)>>8;
  *p++=(client_version&0xff0000)>>16;
  *p++=(client_version&0xff000000)>>24;

  return nm;
}


// MESSAGE_CLIENT_SET_USERMASK
int mpb_client_set_usermask::parse(Net_Message *msg) // return 0 on success
{
  if (msg->get_type() != MESSAGE_CLIENT_SET_USERMASK) return -1;
  if (msg->get_size() < 1) return 1;

  m_intmsg = msg;
  return 0;
}

Net_Message *mpb_client_set_usermask::build()
{
  if (m_intmsg) 
  {
    Net_Message *n=m_intmsg;
    m_intmsg=0;
    return n;
  }

  Net_Message *nm=new Net_Message;
  nm->set_type(MESSAGE_CLIENT_SET_USERMASK); 
  nm->set_size(0);

  return nm;
}


void mpb_client_set_usermask::build_add_rec(const char *username, unsigned int chflags)
{
  const int username_len = username ? (int)strlen(username) : 0;
  const int size=4+username_len+1;

  if (!m_intmsg) 
  {
    m_intmsg = new Net_Message;
    m_intmsg->set_type(MESSAGE_CLIENT_SET_USERMASK); 
  }
  const int oldsize=m_intmsg->get_size();
  m_intmsg->set_size(size+oldsize);
  unsigned char *p=(unsigned char *)m_intmsg->get_data();
  if (p)
  {
    p+=oldsize;

    if (username_len) memcpy(p,username,username_len+1);
    else *p = 0;
    p+=username_len+1;

    *p++=chflags&0xff;
    *p++=(chflags>>8)&0xff;
    *p++=(chflags>>16)&0xff;
    *p++=(chflags>>24)&0xff;
  }
}


// returns offset of next item on success, or <= 0 if out of items
int mpb_client_set_usermask::parse_get_rec(int offs, const char **username, unsigned int *chflags)
{
  if (!m_intmsg) return 0;
  const unsigned char *p=(const unsigned char *)m_intmsg->get_data();
  int len=m_intmsg->get_size()-offs;
  if (!p || len < 5) return 0;
  p+=offs;

  *username=(const char*)p;
  while (*p && len > 0)
  {
    len--;
    p++;
  }
  p++;
  len--;

  if (len<4) return -1;

  *chflags = ((int)*p++); 
  *chflags |= ((int)*p++)<<8;
  *chflags |= ((int)*p++)<<16;
  *chflags |= ((int)*p++)<<24;

  return (int) (p - (unsigned char *)m_intmsg->get_data());
}


// MESSAGE_CLIENT_SET_CHANNEL_INFO
int mpb_client_set_channel_info::parse(Net_Message *msg) // return 0 on success
{
  if (msg->get_type() != MESSAGE_CLIENT_SET_CHANNEL_INFO) return -1;

  m_intmsg = msg;

  return 0;
}

Net_Message *mpb_client_set_channel_info::build()
{
  if (m_intmsg) 
  {
    Net_Message *n=m_intmsg;
    m_intmsg=0;
    return n;
  }

  Net_Message *nm=new Net_Message;
  nm->set_type(MESSAGE_CLIENT_SET_CHANNEL_INFO); 
  nm->set_size(0);

  return nm;
}


void mpb_client_set_channel_info::build_add_rec(const char *chname, short volume, int pan, int flags)
{
  const int chname_len = chname ? (int)strlen(chname) : 0;
  const int size=mpisize+chname_len+1;

  if (!m_intmsg) 
  {
    m_intmsg = new Net_Message;
    m_intmsg->set_type(MESSAGE_CLIENT_SET_CHANNEL_INFO); 
    m_intmsg->set_size(2);
    unsigned char *p=(unsigned char*)m_intmsg->get_data();
    if (!p) return;
    *p++ = mpisize&0xff;
    *p++ = (mpisize>>8)&0xff;
  }
  const int oldsize=m_intmsg->get_size();
  m_intmsg->set_size(size+oldsize);
  unsigned char *p=(unsigned char *)m_intmsg->get_data();
  if (p)
  {
    p+=oldsize;

    if (chname_len) memcpy(p,chname,chname_len+1);
    else *p = 0;
    p+=chname_len+1;

    if (pan < -128) pan=-128;
    else if (pan > 127) pan=127;
    if (mpisize>0) *p++=(volume)&0xff;
    if (mpisize>1) *p++=(volume>>8)&0xff;
    if (mpisize>2) *p++=(unsigned char)pan;
    if (mpisize>3) *p++=(unsigned char)flags;
    if (mpisize>4)
      memset(p,0,mpisize-4);

  }
}


// returns offset of next item on success, or <= 0 if out of items
int mpb_client_set_channel_info::parse_get_rec(int offs, const char **chname, short *volume, int *pan, int *flags)
{
  if (!m_intmsg) return 0;
  const unsigned char *p=(const unsigned char *)m_intmsg->get_data();
  if (!p || m_intmsg->get_size() <= 2) return 0;
  int len=m_intmsg->get_size()-offs;

  mpisize=(int)p[0] | (((int)p[1])<<8);
  if (len < mpisize) return 0;

  p+=offs+2;

  *chname=(const char*)p;
  while (*p && len > 0)
  {
    len--;
    p++;
  }
  p++;
  len--;

  if (len<mpisize) return -1;

  if (mpisize>1)
  {
    *volume=(int)p[0];
    *volume|=((int)p[1])<<8;
  }
  else *volume=0;
  if (mpisize>2) *pan=(int)p[2];
  else *pan=0;
  if (mpisize>3) *flags=(int)p[3];
  else *flags=0;

  return (int) ((p+mpisize) - ((unsigned char *)m_intmsg->get_data()+2));
}

// MESSAGE_CLIENT_UPLOAD_INTERVAL_BEGIN

int mpb_client_upload_interval_begin::parse(Net_Message *msg) // return 0 on success
{
  if (msg->get_type() != MESSAGE_CLIENT_UPLOAD_INTERVAL_BEGIN) return -1;
  if (msg->get_size() < 25) return 1;
  const unsigned char *p=(const unsigned char *)msg->get_data();
  if (!p) return 2;

  memcpy(guid,p,sizeof(guid));
  p+=sizeof(guid);
  estsize = (int)*p++;
  estsize |= ((int)*p++)<<8;
  estsize |= ((int)*p++)<<16;
  estsize |= ((int)*p++)<<24;
  fourcc = (unsigned int)*p++;
  fourcc |= ((unsigned int)*p++)<<8;
  fourcc |= ((unsigned int)*p++)<<16;
  fourcc |= ((unsigned int)*p++)<<24;
  chidx = (int)*p++;

  return 0;
}


Net_Message *mpb_client_upload_interval_begin::build()
{
  Net_Message *nm=new Net_Message;
  nm->set_type(MESSAGE_CLIENT_UPLOAD_INTERVAL_BEGIN);
  
  const int sz = 25;
  nm->set_size(sz);

  unsigned char *p=(unsigned char *)nm->get_data();

  if (!p)
  {
    delete nm;
    return 0;
  }

  memcpy(p,guid,sizeof(guid));
  p+=sizeof(guid);
  *p++=(unsigned char)((estsize)&0xff);
  *p++=(unsigned char)((estsize>>8)&0xff);
  *p++=(unsigned char)((estsize>>16)&0xff);
  *p++=(unsigned char)((estsize>>24)&0xff);
  *p++=(unsigned char)((fourcc)&0xff);
  *p++=(unsigned char)((fourcc>>8)&0xff);
  *p++=(unsigned char)((fourcc>>16)&0xff);
  *p++=(unsigned char)((fourcc>>24)&0xff);
  *p++=(unsigned char)((chidx)&0xff);


  return nm;
}


// MESSAGE_CLIENT_UPLOAD_INTERVAL_WRITE
int mpb_client_upload_interval_write::parse(Net_Message *msg) // return 0 on success
{
  if (msg->get_type() != MESSAGE_CLIENT_UPLOAD_INTERVAL_WRITE) return -1;
  if (msg->get_size() < 17) return 1;
  const unsigned char *p=(const unsigned char *)msg->get_data();
  if (!p) return 2;

  memcpy(guid,p,sizeof(guid));
  p+=sizeof(guid);
  flags = (char)*p++;

  audio_data = p;
  audio_data_len = msg->get_size()-17;

  return 0;
}


Net_Message *mpb_client_upload_interval_write::build()
{
  Net_Message *nm=new Net_Message;
  nm->set_type(MESSAGE_CLIENT_UPLOAD_INTERVAL_WRITE);
  
  const int sz = 17 + (audio_data ? audio_data_len : 0);
  nm->set_size(sz);

  unsigned char *p=(unsigned char *)nm->get_data();

  if (!p)
  {
    delete nm;
    return 0;
  }
  memcpy(p,guid,sizeof(guid));
  p+=sizeof(guid);
  *p++=(unsigned char) flags;

  if (audio_data&&audio_data_len) memcpy(p,audio_data,audio_data_len);

  return nm;
}


// MESSAGE_CLIENT_OPENPROJECT
int mpb_client_openproject::parse(Net_Message *msg) // return 0 on success
{
  if (msg->get_type() != MESSAGE_CLIENT_OPENPROJECT) return -1;
  if (msg->get_size() < 1) return 1;
  const unsigned char *p=(const unsigned char *)msg->get_data();
  if (!p) return 2;
  const unsigned char *endp = p + msg->get_size();
  const unsigned char *pp = p;
  while (pp < endp && *pp) pp++;
  if (pp >= endp) return -1;

  m_projname = (const char*)p;

  return 0;
}

Net_Message *mpb_client_openproject::build()
{
  Net_Message *nm=new Net_Message;
  nm->set_type(MESSAGE_CLIENT_OPENPROJECT);
  const int projname_sz = m_projname ? (int)strlen(m_projname) + 1 : 0;
  nm->set_size(projname_sz);
  unsigned char *p=(unsigned char *)nm->get_data();
  if (!p)
  {
    delete nm;
    return 0;
  }
  if (projname_sz) memcpy(p,m_projname,projname_sz);

  return nm;
}


/////////////////////////////////////////////////////////////////////////
//////////// bidirectional generic  messages
/////////////////////////////////////////////////////////////////////////


// MESSAGE_CHAT_MESSAGE

int mpb_chat_message::parse(Net_Message *msg) // return 0 on success
{
  if (msg->get_type() != MESSAGE_CHAT_MESSAGE) return -1;
  if (msg->get_size() < 1) return 1;
  const char *p=(char *)msg->get_data();
  if (!p) return 2;

  const char *endp=(char*)msg->get_data()+msg->get_size();

  int x;
  memset(parms,0,sizeof(parms));
  for (x = 0; x < (int) (sizeof(parms)/sizeof(parms[0])); x ++)
  {
    parms[x]=p;
    while (p < endp && *p) p++;
    p++;
    if (p >= endp) break;
  }
  return x?0:3;
}


Net_Message *mpb_chat_message::build()
{
  Net_Message *nm=new Net_Message;
  nm->set_type(MESSAGE_CHAT_MESSAGE);

  int x;
  int sz=0;
  for (x = 0; x < (int)(sizeof(parms)/sizeof(parms[0])); x ++)
  {
    if (parms[x]) sz += (int)strlen(parms[x]);
    sz += 1;
  }
  
  nm->set_size(sz);

  char *p=(char *)nm->get_data();

  if (!p)
  {
    delete nm;
    return 0;
  }

  for (x = 0; x < sizeof(parms)/sizeof(parms[0]); x ++)
  {
    const char *sp=parms[x];
    if (sp) 
    {
      const int l = (int)strlen(sp);
      memcpy(p, sp, l);
      p += l;
    }
    *p++ = 0;
  }

  return nm;
}
