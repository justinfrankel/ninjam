#include <windows.h>

#include "mpb.h"


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
