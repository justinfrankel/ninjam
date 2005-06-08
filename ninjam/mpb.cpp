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


void mpb_server_userinfo_change_notify::build_add_rec(int isActive, int channelid, 
                                                      short volume, int pan, int flags, char *username, char *chname)
{
  int size=1+ // is remove
           1+ // channel index
           2+ // volume
           1+ // pan
           1+ // flags
           strlen(username?username:"")+1+strlen(chname?chname:"")+1;

  if (!m_intmsg) 
  {
    m_intmsg = new Net_Message;
    m_intmsg->set_type(MESSAGE_SERVER_USERINFO_CHANGE_NOTIFY); 
  }
  int oldsize=m_intmsg->get_size();
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

    strcpy((char*)p,username);
    p+=strlen(username)+1;
    strcpy((char*)p,chname);
    p+=strlen(chname)+1;
  }
}


// returns offset of next item on success, or <= 0 if out of items
int mpb_server_userinfo_change_notify::parse_get_rec(int offs, int *isActive, int *channelid, short *volume, 
                                                     int *pan, int *flags, char **username, char **chname)
{
  int hdrsize=1+ // is remove
           1+ // channel index
           2+ // volume
           1+ // pan
           1; // flags

  if (!m_intmsg) return 0;
  unsigned char *p=(unsigned char *)m_intmsg->get_data();
  int len=m_intmsg->get_size()-offs;
  if (!p || len < hdrsize+2) return 0;
  p+=offs;

  unsigned char *hdrbuf=p;
  char *unp;
  char *cnp;

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


  return p - (unsigned char *)m_intmsg->get_data();
}


// MESSAGE_SERVER_DOWNLOAD_INTERVAL_BEGIN
int mpb_server_download_interval_begin::parse(Net_Message *msg) // return 0 on success
{
  if (msg->get_type() != MESSAGE_SERVER_DOWNLOAD_INTERVAL_BEGIN) return -1;
  if (msg->get_size() < 25+1) return 1;
  unsigned char *p=(unsigned char *)msg->get_data();
  if (!p) return 2;

  memcpy(guid,p,sizeof(guid));
  p+=sizeof(guid);
  estsize = (int)*p++;
  estsize |= ((int)*p++)<<8;
  estsize |= ((int)*p++)<<16;
  estsize |= ((int)*p++)<<24;
  fourcc = (int)*p++;
  fourcc |= ((int)*p++)<<8;
  fourcc |= ((int)*p++)<<16;
  fourcc |= ((int)*p++)<<24;
  chidx = (int)*p++;
  int len=msg->get_size()-25;

  username=(char *)p;


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
  
  nm->set_size(25+strlen(username?username:"")+1);

  unsigned char *p=(unsigned char *)nm->get_data();

  if (!p)
  {
    delete nm;
    return 0;
  }

  memcpy(p,guid,sizeof(guid));
  p+=sizeof(guid);
  *p++=(unsigned char)((estsize)&0xff);
  *p++|=(unsigned char)((estsize>>8)&0xff);
  *p++|=(unsigned char)((estsize>>16)&0xff);
  *p++|=(unsigned char)((estsize>>24)&0xff);
  *p++=(unsigned char)((fourcc)&0xff);
  *p++|=(unsigned char)((fourcc>>8)&0xff);
  *p++|=(unsigned char)((fourcc>>16)&0xff);
  *p++|=(unsigned char)((fourcc>>24)&0xff);
  *p++=(unsigned char)((chidx)&0xff);

  strcpy((char *)p,username?username:"");


  return nm;
}


// MESSAGE_SERVER_DOWNLOAD_INTERVAL_WRITE
int mpb_server_download_interval_write::parse(Net_Message *msg) // return 0 on success
{
  if (msg->get_type() != MESSAGE_SERVER_DOWNLOAD_INTERVAL_WRITE) return -1;
  if (msg->get_size() < 17) return 1;
  unsigned char *p=(unsigned char *)msg->get_data();
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
  
  nm->set_size(17+(audio_data?audio_data_len:0));

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
  unsigned char *p=(unsigned char *)msg->get_data();
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
  

  int capsl=len; // additional bytes are caps bytes
  client_caps=0;
  int shift=0;

  if (capsl > 4) capsl=4;

  while (capsl--)
  {
    client_caps |= ((int)*p++)<<shift;
    shift+=8;
  }

  return 0;
}

Net_Message *mpb_client_auth_user::build()
{
  Net_Message *nm=new Net_Message;
  nm->set_type(MESSAGE_CLIENT_AUTH_USER);
  
  int scsize=0;
  int sc=client_caps;
  while (sc)
  {
    scsize++;
    sc>>=8;
  }

  nm->set_size(sizeof(passhash) + strlen(username?username:"")+1 + scsize);

  unsigned char *p=(unsigned char *)nm->get_data();

  if (!p) 
  {
    delete nm;
    return 0;
  }

  memcpy(p,passhash,sizeof(passhash));
  p+=sizeof(passhash);

  strcpy((char*)p,username?username:"");
  p+=strlen(username?username:"")+1;

  sc=client_caps;
  while (scsize--)
  {
    *p++=sc&0xff;
    sc>>=8;
  }

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


void mpb_client_set_usermask::build_add_rec(char *username, unsigned int chflags)
{
  int size=4+strlen(username?username:"")+1;

  if (!m_intmsg) 
  {
    m_intmsg = new Net_Message;
    m_intmsg->set_type(MESSAGE_CLIENT_SET_USERMASK); 
  }
  int oldsize=m_intmsg->get_size();
  m_intmsg->set_size(size+oldsize);
  unsigned char *p=(unsigned char *)m_intmsg->get_data();
  if (p)
  {
    p+=oldsize;

    strcpy((char*)p,username);
    p+=strlen(username)+1;

    *p++=chflags&0xff;
    *p++=(chflags>>8)&0xff;
    *p++=(chflags>>16)&0xff;
    *p++=(chflags>>24)&0xff;
  }
}


// returns offset of next item on success, or <= 0 if out of items
int mpb_client_set_usermask::parse_get_rec(int offs, char **username, unsigned int *chflags)
{
  if (!m_intmsg) return 0;
  unsigned char *p=(unsigned char *)m_intmsg->get_data();
  int len=m_intmsg->get_size()-offs;
  if (!p || len < 5) return 0;
  p+=offs;

  *username=(char*)p;
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

  return p - (unsigned char *)m_intmsg->get_data();
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


void mpb_client_set_channel_info::build_add_rec(char *chname, short volume, int pan, int flags)
{
  int size=mpisize+strlen(chname?chname:"")+1;

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
  int oldsize=m_intmsg->get_size();
  m_intmsg->set_size(size+oldsize);
  unsigned char *p=(unsigned char *)m_intmsg->get_data();
  if (p)
  {
    p+=oldsize;

    strcpy((char*)p,chname);
    p+=strlen(chname)+1;
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
int mpb_client_set_channel_info::parse_get_rec(int offs, char **chname, short *volume, int *pan, int *flags)
{
  if (!m_intmsg) return 0;
  unsigned char *p=(unsigned char *)m_intmsg->get_data();
  if (!p || m_intmsg->get_size() <= 2) return 0;
  int len=m_intmsg->get_size()-offs;

  mpisize=(int)p[0] | (((int)p[1])<<8);
  if (len < mpisize) return 0;

  p+=offs+2;

  *chname=(char*)p;
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

  return (p+mpisize) - ((unsigned char *)m_intmsg->get_data()+2);
}

// MESSAGE_CLIENT_UPLOAD_INTERVAL_BEGIN

int mpb_client_upload_interval_begin::parse(Net_Message *msg) // return 0 on success
{
  if (msg->get_type() != MESSAGE_CLIENT_UPLOAD_INTERVAL_BEGIN) return -1;
  if (msg->get_size() < 25) return 1;
  unsigned char *p=(unsigned char *)msg->get_data();
  if (!p) return 2;

  memcpy(guid,p,sizeof(guid));
  p+=sizeof(guid);
  estsize = (int)*p++;
  estsize |= ((int)*p++)<<8;
  estsize |= ((int)*p++)<<16;
  estsize |= ((int)*p++)<<24;
  fourcc = (int)*p++;
  fourcc |= ((int)*p++)<<8;
  fourcc |= ((int)*p++)<<16;
  fourcc |= ((int)*p++)<<24;
  chidx = (int)*p++;

  return 0;
}


Net_Message *mpb_client_upload_interval_begin::build()
{
  Net_Message *nm=new Net_Message;
  nm->set_type(MESSAGE_CLIENT_UPLOAD_INTERVAL_BEGIN);
  
  nm->set_size(25);

  unsigned char *p=(unsigned char *)nm->get_data();

  if (!p)
  {
    delete nm;
    return 0;
  }

  memcpy(p,guid,sizeof(guid));
  p+=sizeof(guid);
  *p++=(unsigned char)((estsize)&0xff);
  *p++|=(unsigned char)((estsize>>8)&0xff);
  *p++|=(unsigned char)((estsize>>16)&0xff);
  *p++|=(unsigned char)((estsize>>24)&0xff);
  *p++=(unsigned char)((fourcc)&0xff);
  *p++|=(unsigned char)((fourcc>>8)&0xff);
  *p++|=(unsigned char)((fourcc>>16)&0xff);
  *p++|=(unsigned char)((fourcc>>24)&0xff);
  *p++=(unsigned char)((chidx)&0xff);


  return nm;
}


// MESSAGE_CLIENT_UPLOAD_INTERVAL_WRITE
int mpb_client_upload_interval_write::parse(Net_Message *msg) // return 0 on success
{
  if (msg->get_type() != MESSAGE_CLIENT_UPLOAD_INTERVAL_WRITE) return -1;
  if (msg->get_size() < 17) return 1;
  unsigned char *p=(unsigned char *)msg->get_data();
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
  
  nm->set_size(17+(audio_data?audio_data_len:0));

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
