/*
    NINJAM Server - usercon.cpp
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

  This header provides the implementations of User_Group and User_Connection etc.
  These is where the core logic of the server (message routing and upload/download
  management) happens.

*/

#ifdef _WIN32
#include <windows.h>
#else
#include <stdlib.h>
#include <string.h>
#endif

#include <ctype.h>

#include "usercon.h"
#include "../mpb.h"

#include "../../WDL/rng.h"
#include "../../WDL/sha.h"


#ifdef _WIN32
#define strncasecmp strnicmp
#endif

static void guidtostr(unsigned char *guid, char *str)
{
  int x;
  for (x = 0; x < 16; x ++) wsprintf(str+x*2,"%02x",guid[x]);
}



static int is_type_char_valid(int c)
{
  c&=0xff;
  return (c >= 'a' && c <= 'z') ||
         (c >= 'A' && c <= 'Z') ||
         (c >= '0' && c <= '9') ||
         c == ' ' || c == '-' || 
         c == '.' || c == '_';
}

static int is_type_valid(unsigned int t)
{
  return (t&0xff) != ' ' &&
          is_type_char_valid(t>>24) &&
          is_type_char_valid(t>>16) &&
          is_type_char_valid(t>>8) &&
          is_type_char_valid(t);
}


static void type_to_string(unsigned int t, char *out)
{
  if (is_type_valid(t))
  {
    out[0]=(t)&0xff;
    out[1]=(t>>8)&0xff;
    out[2]=(t>>16)&0xff;
    out[3]=' ';//(t>>24)&0xff;
    out[4]=0;
    int x=3;
    while (out[x]==' ' && x > 0) out[x--]=0;
  }
  else *out=0;
}



extern void logText(char *s, ...);

#define MAX_NICK_LEN 128 // not including null term

#define TRANSFER_TIMEOUT 8

User_Connection::User_Connection(JNL_Connection *con, User_Group *grp) : m_auth_state(0), m_clientcaps(0), m_auth_privs(0), m_reserved(0), m_max_channels(0),
      m_vote_bpm(0), m_vote_bpm_lasttime(0), m_vote_bpi(0), m_vote_bpi_lasttime(0)
{
  m_netcon.attach(con);

  WDL_RNG_bytes(m_challenge,sizeof(m_challenge));

  mpb_server_auth_challenge ch;
  memcpy(ch.challenge,m_challenge,sizeof(ch.challenge));


  ch.protocol_version = PROTO_VER_CUR;
  int ka=grp->m_keepalive;

  if (ka < 0)ka=0;
  else if (ka > 255) ka=255;
  ch.server_caps=ka<<8;

  if (grp->m_licensetext.Get()[0])
  {
    m_netcon.SetKeepAlive(45); // wait a max of 45s * 3
    ch.license_agreement=grp->m_licensetext.Get();
  }
  else m_netcon.SetKeepAlive(grp->m_keepalive);
  Send(ch.build());

  time(&m_connect_time);

  m_lookup=0;
}


void User_Connection::Send(Net_Message *msg)
{
  if (m_netcon.Send(msg))
  {
    logText("Error sending message to user '%s', type %d, queue full!\n",m_username.Get(),msg->get_type());
  }
}

User_Connection::~User_Connection()
{

  int x;
  for (x = 0; x < m_sublist.GetSize(); x ++)
    delete m_sublist.Get(x);
  m_sublist.Empty();
  for (x = 0; x < m_recvfiles.GetSize(); x ++)
    delete m_recvfiles.Get(x);
  m_recvfiles.Empty();
  for (x = 0; x < m_sendfiles.GetSize(); x ++)
    delete m_sendfiles.Get(x);
  m_sendfiles.Empty();

  delete m_lookup;
  m_lookup=0;
}


void User_Connection::SendConfigChangeNotify(int bpm, int bpi)
{
  if (m_auth_state > 0)
  {
    mpb_server_config_change_notify mk;
    mk.beats_interval=bpi;
    mk.beats_minute=bpm;
    Send(mk.build());
  }
}

int User_Connection::OnRunAuth(User_Group *group)
{
  char addrbuf[256];
  JNL::addr_to_ipstr(m_netcon.GetConnection()->get_remote(),addrbuf,sizeof(addrbuf));
 
  {
    WDL_SHA1 shatmp;

    shatmp.add(m_lookup->sha1buf_user,sizeof(m_lookup->sha1buf_user));

    shatmp.add(m_challenge,sizeof(m_challenge));

    char buf[WDL_SHA1SIZE];
    shatmp.result(buf);

    if ((m_lookup->reqpass && memcmp(buf,m_lookup->sha1buf_request,WDL_SHA1SIZE)) || !m_lookup->user_valid)
    {
      logText("%s: Refusing user, invalid login/password\n",addrbuf);
      mpb_server_auth_reply bh;
      bh.errmsg="invalid login/password";
      Send(bh.build());
      return 0;
    }
  }

  if (m_lookup->is_status)
  {
    SendUserList(group);

    {
      mpb_server_config_change_notify mk;
      mk.beats_interval=group->m_last_bpi;
      mk.beats_minute=group->m_last_bpm;
      Send(mk.build());
    }

    {
      mpb_chat_message newmsg;
      newmsg.parms[0]="TOPIC";
      newmsg.parms[1]="";
      newmsg.parms[2]=group->m_topictext.Get();
      Send(newmsg.build());
    }

    {
      int cnt=0;
      int user;
      for (user = 0; user < group->m_users.GetSize(); user ++)
      {
        User_Connection *u=group->m_users.Get(user);
        if (u != this && u->m_auth_state > 0 && !(u->m_auth_privs & PRIV_HIDDEN))
          cnt++;
      }
      char buf[64],buf2[64];
      sprintf(buf,"%d",cnt);
      sprintf(buf2,"%d",group->m_max_users);

      mpb_chat_message newmsg;
      newmsg.parms[0]="USERCOUNT";
      newmsg.parms[1]=buf;;
      newmsg.parms[2]=buf2;
      Send(newmsg.build());
    }

    return 0;
  }

  m_auth_privs=m_lookup->privs;
  m_max_channels = m_lookup->max_channels;

  {
    // fix any invalid characters in username
    char *p=m_lookup->username.Get();
    int l=MAX_NICK_LEN;
    while (*p)
    {
      char c=*p;
      if (!isalnum(c) && c != '-' && c != '_' && c != '@' && c != '.') c='_';
      *p++=c;

      if (!--l) *p=0;
    }
  }

  m_username.Set(m_lookup->username.Get());
  // disconnect any user by the same name
  // in allowmulti mode, append -<idx>
  {
    int user=0;
    int uw_pos=0;

    while (user < group->m_users.GetSize())
    {
      User_Connection *u=group->m_users.Get(user);
      if (u != this && !strcasecmp(u->m_username.Get(),m_username.Get()))
      {

        if ((m_auth_privs & PRIV_ALLOWMULTI) && ++uw_pos < 16)
        {
          char buf[64];
          sprintf(buf,".%d",uw_pos+1);
          m_username.Set(m_lookup->username.Get());
          m_username.Append(buf);
          user=0;
          continue; // start over
        }
        else
        {
          delete u;
          group->m_users.Delete(user);
          break;
        }
      }
      user++;
    }   
  }


  if (group->m_max_users && !m_reserved && !(m_auth_privs & PRIV_RESERVE))
  {
    int user;
    int cnt=0;
    for (user = 0; user < group->m_users.GetSize(); user ++)
    {
      User_Connection *u=group->m_users.Get(user);
      if (u != this && u->m_auth_state > 0 && !(u->m_auth_privs & PRIV_HIDDEN))
        cnt++;
    }
    if (cnt >= group->m_max_users)
    {
      logText("%s: Refusing user %s, server full\n",addrbuf,m_username.Get());
      // sorry, gotta kill this connection
      mpb_server_auth_reply bh;
      bh.errmsg="server full";
      Send(bh.build());
      return 0;
    }
  }



  logText("%s: Accepted user: %s\n",addrbuf,m_username.Get());

  {
    mpb_server_auth_reply bh;
    bh.flag=1;
    int ch=m_max_channels;
    if (ch > MAX_USER_CHANNELS) ch=MAX_USER_CHANNELS;
    if (ch < 0) ch = 0;

    bh.maxchan = ch;

    bh.errmsg=m_username.Get();
    Send(bh.build());
  }

  m_auth_state=1;

  SendConfigChangeNotify(group->m_last_bpm,group->m_last_bpi);


  SendUserList(group);


  {
    mpb_chat_message newmsg;
    newmsg.parms[0]="TOPIC";
    newmsg.parms[1]="";
    newmsg.parms[2]=group->m_topictext.Get();
    Send(newmsg.build());
  }
  {
    mpb_chat_message newmsg;
    newmsg.parms[0]="JOIN";
    newmsg.parms[1]=m_username.Get();
    group->Broadcast(newmsg.build(),this);
  }

  return 1;
}

// send user list to user
void User_Connection::SendUserList(User_Group *group)
{
  mpb_server_userinfo_change_notify bh;

  int user;
  for (user = 0; user < group->m_users.GetSize(); user++)
  {
    User_Connection *u=group->m_users.Get(user);
    int channel;
    if (u && u->m_auth_state>0 && u != this) 
    {
      int acnt=0;
      for (channel = 0; channel < u->m_max_channels && channel < MAX_USER_CHANNELS; channel ++)
      {
        if (u->m_channels[channel].active)
        {
          bh.build_add_rec(1,channel,u->m_channels[channel].volume,u->m_channels[channel].panning,u->m_channels[channel].flags,
                            u->m_username.Get(),u->m_channels[channel].name.Get());
          acnt++;
        }
      }
      if (!acnt && !group->m_allow_hidden_users && u->m_max_channels && !(u->m_auth_privs & PRIV_HIDDEN)) // give users at least one channel
      {
          bh.build_add_rec(1,0,0,0,0,u->m_username.Get(),"");
      }
    }
  }       
  Send(bh.build());
}


int User_Connection::Run(User_Group *group, int *wantsleep)
{
  Net_Message *msg=m_netcon.Run(wantsleep);
  if (m_netcon.GetStatus()) 
  {
    delete msg;
    return m_netcon.GetStatus();
  }

  if (!msg)
  {
    if (m_auth_state < 0)
    {
      if (!m_lookup || m_lookup->Run())
      {
        if (!m_lookup || !OnRunAuth(group))
        {
          m_netcon.Run();
          m_netcon.Kill();
        }
        delete m_lookup;
        m_lookup=0;
      }
    }
    else if (!m_auth_state)
    {
      if (time(NULL) > m_connect_time+120) // if we haven't gotten an auth reply in 120s, disconnect. The reason this is so long is to give
                                        // the user time to potentially read the license agreement.
      {
        char buf[256];
        JNL::addr_to_ipstr(m_netcon.GetConnection()->get_remote(),buf,sizeof(buf));
        logText("%s: Got an authorization timeout\n",buf);
        m_connect_time=time(NULL)+120;
        mpb_server_auth_reply bh;
        bh.errmsg="authorization timeout";
        Send(bh.build());
        m_netcon.Run();

        m_netcon.Kill();
      }
    }
    return 0;
  }

  msg->addRef();

  if (!m_auth_state)
  {
    mpb_client_auth_user authrep;

    // verify everything
    int          err_st = ( msg->get_type() != MESSAGE_CLIENT_AUTH_USER || authrep.parse(msg) || !authrep.username || !authrep.username[0] ) ? 1 : 0;
    if (!err_st) err_st = ( authrep.client_version < PROTO_VER_MIN || authrep.client_version > PROTO_VER_MAX ) ? 2 : 0;
    if (!err_st) err_st = ( group->m_licensetext.Get()[0] && !(authrep.client_caps & 1) ) ? 3 : 0;

    if (err_st)
    {
      static char *tab[] = { "invalid authorization reply", "incorrect client version", "license not agreed to" };
      mpb_server_auth_reply bh;
      bh.errmsg=tab[err_st-1];

      char addrbuf[256];
      JNL::addr_to_ipstr(m_netcon.GetConnection()->get_remote(),addrbuf,sizeof(addrbuf));

      logText("%s: Refusing user, %s\n",addrbuf,bh.errmsg);

      Send(bh.build());
      m_netcon.Run();

      m_netcon.Kill();
      msg->releaseRef();

      return 0;
    }

    m_netcon.SetKeepAlive(group->m_keepalive); // restore default keepalive behavior since we got a response

    m_clientcaps=authrep.client_caps;

    delete m_lookup;
    m_lookup=group->CreateUserLookup?group->CreateUserLookup(authrep.username):NULL;

    if (m_lookup)
    {
      char addrbuf[256];
      JNL::addr_to_ipstr(m_netcon.GetConnection()->get_remote(),addrbuf,sizeof(addrbuf));
      m_lookup->hostmask.Set(addrbuf);
      memcpy(m_lookup->sha1buf_request,authrep.passhash,sizeof(m_lookup->sha1buf_request));
    }

    m_auth_state=-1;
    

  } // !m_auth_state


  if (m_auth_state > 0) 
  {
    switch (msg->get_type())
    {
      case MESSAGE_CLIENT_SET_CHANNEL_INFO:
        {
          mpb_client_set_channel_info chi;
          if (!chi.parse(msg))
          {
            // update our channel list

            mpb_server_userinfo_change_notify mfmt;
            int mfmt_changes=0;
          
            int offs=0;
            short v;
            int p,f;
            int whichch=0;
            char *chnp=0;
            while ((offs=chi.parse_get_rec(offs,&chnp,&v,&p,&f))>0 && whichch < MAX_USER_CHANNELS && whichch < m_max_channels)
            {
              if (!chnp) chnp=""; 

              int doactive=!(f&0x80);

              // only if something changes, do we add it to the rec
              int hadch=!m_channels[whichch].active != !doactive;
              if (!hadch) hadch = strcmp(chnp,m_channels[whichch].name.Get());
              if (!hadch) hadch = m_channels[whichch].volume!=v;
              if (!hadch) hadch = m_channels[whichch].panning!=p;
              if (!hadch) hadch = m_channels[whichch].flags!=f;

              m_channels[whichch].active=doactive;
              m_channels[whichch].name.Set(chnp);
              m_channels[whichch].volume=v;
              m_channels[whichch].panning=p;
              m_channels[whichch].flags=f;

              if (hadch)
              {
                mfmt_changes++;
                mfmt.build_add_rec(m_channels[whichch].active,whichch,
                                  m_channels[whichch].volume,
                                  m_channels[whichch].panning,
                                  m_channels[whichch].flags,
                                  m_username.Get(),
                                  m_channels[whichch].name.Get());
              }

              whichch++;
            }
            while (whichch < MAX_USER_CHANNELS)
            {
              m_channels[whichch].name.Set("");

              if (m_channels[whichch].active) // only send deactivate if it was previously active
              {
                m_channels[whichch].active=0;
                mfmt_changes++;
                mfmt.build_add_rec(0,whichch,
                                  m_channels[whichch].volume,
                                  m_channels[whichch].panning,
                                  m_channels[whichch].flags,
                                  m_username.Get(),
                                  m_channels[whichch].name.Get());
              }

              whichch++;
            }
            if (!group->m_allow_hidden_users && m_max_channels && !(m_auth_privs&PRIV_HIDDEN))
            {
              for (whichch = 0; whichch < MAX_USER_CHANNELS && !m_channels[whichch].active; whichch ++);


              if (whichch == MAX_USER_CHANNELS) // if empty, tell the user about one channel
              {
                mfmt.build_add_rec(1,0,0,0,0,m_username.Get(),"");
                mfmt_changes++;
              }
            }


            if (mfmt_changes) group->Broadcast(mfmt.build(),this);
          }         
        }
      break;
      case MESSAGE_CLIENT_SET_USERMASK:
        {
          mpb_client_set_usermask umi;
          if (!umi.parse(msg))
          {
            int offs=0;
            char *unp=0;
            unsigned int fla=0;
            while ((offs=umi.parse_get_rec(offs,&unp,&fla))>0)
            {
              if (unp)
              {
                int x;
                for (x = 0; x < m_sublist.GetSize() && strcasecmp(unp,m_sublist.Get(x)->username.Get()); x ++);
                if (x == m_sublist.GetSize()) // add new
                {
                  if (fla) // only add if we need to subscribe
                  {
                    User_SubscribeMask *n=new User_SubscribeMask;
                    n->username.Set(unp);
                    n->channelmask = fla;
                    m_sublist.Add(n);
                  }
                }
                else
                {
                  if (fla) // update flag
                  {
                    m_sublist.Get(x)->channelmask=fla;
                  }
                  else // remove
                  {
                    delete m_sublist.Get(x);
                    m_sublist.Delete(x);
                  }
                }
              }
            }
          }
        }
      break;
      case MESSAGE_CLIENT_UPLOAD_INTERVAL_BEGIN:
        {
          mpb_client_upload_interval_begin mp;
          if (!mp.parse(msg) && mp.chidx < m_max_channels)
          {
            char *myusername=m_username.Get();

            mpb_server_download_interval_begin nmb;
            nmb.chidx=mp.chidx;
            nmb.estsize=mp.estsize;
            nmb.fourcc=mp.fourcc;
            memcpy(nmb.guid,mp.guid,sizeof(nmb.guid));
            nmb.username = myusername;

            Net_Message *newmsg=nmb.build();
            newmsg->addRef();
                    
            static unsigned char zero_guid[16];


            if (mp.fourcc && memcmp(mp.guid,zero_guid,sizeof(zero_guid))) // zero = silence, so simply rebroadcast
            {
              User_TransferState *newrecv=new User_TransferState;
              newrecv->bytes_estimated=mp.estsize;
              newrecv->fourcc=mp.fourcc;
              memcpy(newrecv->guid,mp.guid,sizeof(newrecv->guid));

              if (group->m_logdir.Get()[0])
              {
                char fn[512];
                char guidstr[64];
                guidtostr(mp.guid,guidstr);

                char ext[8];
                type_to_string(mp.fourcc,ext);
                sprintf(fn,"%c/%s.%s",guidstr[0],guidstr,ext);

                WDL_String tmp(group->m_logdir.Get());                
                tmp.Append(fn);

                newrecv->fp = fopen(tmp.Get(),"wb");

                if (group->m_logfp)
                {
                  // decide when to write new interval
                  char *chn="?";
                  if (mp.chidx >= 0 && mp.chidx < MAX_USER_CHANNELS) chn=m_channels[mp.chidx].name.Get();
                  fprintf(group->m_logfp,"user %s \"%s\" %d \"%s\"\n",guidstr,myusername,mp.chidx,chn);
                }
              }
            
              m_recvfiles.Add(newrecv);
            }


            int user;
            for (user=0;user<group->m_users.GetSize(); user++)
            {
              User_Connection *u=group->m_users.Get(user);
              if (u && u != this)
              {
                int i;
                for (i=0; i < u->m_sublist.GetSize(); i ++)
                {
                  User_SubscribeMask *sm=u->m_sublist.Get(i);
                  if (!strcasecmp(sm->username.Get(),myusername))
                  {
                    if (sm->channelmask & (1<<mp.chidx))
                    {
                      if (memcmp(mp.guid,zero_guid,sizeof(zero_guid))) // zero = silence, so simply rebroadcast
                      {
                        // add entry in send list
                        User_TransferState *nt=new User_TransferState;
                        memcpy(nt->guid,mp.guid,sizeof(nt->guid));
                        nt->bytes_estimated = mp.estsize;
                        nt->fourcc = mp.fourcc;
                        u->m_sendfiles.Add(nt);
                      }

                      u->Send(newmsg);
                    }
                    break;
                  }
                }
              }
            }
            newmsg->releaseRef();
          }
        }
        //m_recvfiles
      break;
      case MESSAGE_CLIENT_UPLOAD_INTERVAL_WRITE:
        {
          mpb_client_upload_interval_write mp;
          if (!mp.parse(msg))
          {
            time_t now;
            time(&now);
            msg->set_type(MESSAGE_SERVER_DOWNLOAD_INTERVAL_WRITE); // we rely on the fact that the upload/download write messages are identical
                                                                   // though we may need to update this at a later date if we change things.

            int user,x;


            for (x = 0; x < m_recvfiles.GetSize(); x ++)
            {
              User_TransferState *t=m_recvfiles.Get(x);
              if (!memcmp(t->guid,mp.guid,sizeof(mp.guid)))
              {
                t->last_acttime=now;

                if (t->fp) fwrite(mp.audio_data,1,mp.audio_data_len,t->fp);

                t->bytes_sofar+=mp.audio_data_len;
                if (mp.flags & 1)
                {
                  delete t;
                  m_recvfiles.Delete(x);
                }
                break;
              }
              if (now-t->last_acttime > TRANSFER_TIMEOUT)
              {
                delete t;
                m_recvfiles.Delete(x--);
              }
            }


            for (user=0;user<group->m_users.GetSize(); user++)
            {
              User_Connection *u=group->m_users.Get(user);
              if (u && u != this)
              {
                int i;
                for (i=0; i < u->m_sendfiles.GetSize(); i ++)
                {
                  User_TransferState *t=u->m_sendfiles.Get(i);
                  if (t && !memcmp(t->guid,mp.guid,sizeof(t->guid)))
                  {
                    t->last_acttime=now;
                    t->bytes_sofar += mp.audio_data_len;
                    u->Send(msg);
                    if (mp.flags & 1)
                    {
                      delete t;
                      u->m_sendfiles.Delete(i);
                      // remove from transfer list
                    }
                    break;
                  }
                  if (now-t->last_acttime > TRANSFER_TIMEOUT)
                  {
                    delete t;
                    u->m_sendfiles.Delete(i--);
                  }
                }
              }
            }
          }
        }
      break;

      case MESSAGE_CHAT_MESSAGE:
        {
          mpb_chat_message poo;
          if (!poo.parse(msg))
          {
            group->onChatMessage(this,&poo);
          }
        }
      break;

      default:
      break;
    }
  } // m_auth_state > 0
  msg->releaseRef();

  return 0;
}


User_Group::User_Group() : m_max_users(0), m_last_bpm(120), m_last_bpi(32), m_keepalive(0), 
  m_voting_threshold(110), m_voting_timeout(120),
  m_loopcnt(0), m_run_robin(0), m_allow_hidden_users(0), m_logfp(0)
{
  CreateUserLookup=0;
  memset(&m_next_loop_time,0,sizeof(m_next_loop_time));
}

User_Group::~User_Group()
{
  int x;
  for (x = 0; x < m_users.GetSize(); x ++)
  {
    delete m_users.Get(x);
  }
  m_users.Empty();
  if (m_logfp) fclose(m_logfp);
  m_logfp=0;
}


void User_Group::SetLogDir(char *path) // NULL to not log
{
  m_loopcnt=0;
  if (!path || !*path)
  {
    if (m_logfp) fclose(m_logfp);
    m_logfp=0;
    m_logdir.Set("");
    return;
  }

#ifdef _WIN32
    CreateDirectory(path,NULL);
#else
    mkdir(path,0755);
#endif

  m_logdir.Set(path);
  m_logdir.Append("/");

  WDL_String cl(path);
  cl.Append("/clipsort.log");
  m_logfp=fopen(cl.Get(),"at");

  int a;
  for (a = 0; a < 16; a ++)
  {
    WDL_String tmp(path);
    char buf[5];
    sprintf(buf,"/%x",a);
    tmp.Append(buf);
#ifdef _WIN32
    CreateDirectory(tmp.Get(),NULL);
#else
    mkdir(tmp.Get(),0755);
#endif
  }
}

void User_Group::Broadcast(Net_Message *msg, User_Connection *nosend)
{
  if (msg)
  {
    msg->addRef(); // we do this so that if nobody actually gets to send it, we delete it

    int x;
    for (x = 0; x < m_users.GetSize(); x ++)
    {
      User_Connection *p=m_users.Get(x);
      if (p && p->m_auth_state > 0 && p != nosend)
      {
        p->Send(msg);
      }
    }

    msg->releaseRef();
  }
}

int User_Group::Run()
{
    int wantsleep=1;
    int x;

    // track bpm/bpi stuff
#ifdef _WIN32
    DWORD now=GetTickCount();
    if (now >= m_next_loop_time)
    {
      m_next_loop_time = now + (60*1000*m_last_bpi) / (m_last_bpm?m_last_bpm:120);
#else
    struct timeval now;
    gettimeofday(&now,NULL);
    if (now.tv_sec > m_next_loop_time.tv_sec || 
        (now.tv_sec == m_next_loop_time.tv_sec && now.tv_usec >= m_next_loop_time.tv_usec))
    {
      int len_ms = ((60*1000*m_last_bpi) / (m_last_bpm?m_last_bpm:120));
      int len_s = len_ms/1000;
      len_ms %= 1000;
      m_next_loop_time.tv_sec = now.tv_sec + len_s;
      m_next_loop_time.tv_usec = now.tv_usec + len_ms*1000;
      if (m_next_loop_time.tv_usec >= 1000000)
      {
        m_next_loop_time.tv_sec += 1;
        m_next_loop_time.tv_usec -= 1000000;
      }
#endif

      m_loopcnt++;
      if (m_logfp) 
      {
        fprintf(m_logfp,"interval %d %d %d\n",m_loopcnt,m_last_bpm,m_last_bpi);
        fflush(m_logfp);
      }
    }


    for (x = 0; x < m_users.GetSize(); x ++)
    {
      int thispos=(x+m_run_robin)%m_users.GetSize();
      User_Connection *p=m_users.Get(thispos);
      if (p)
      {
        int ret=p->Run(this,&wantsleep);
        if (ret)
        {
          // broadcast to other users that this user is no longer present
          if (p->m_auth_state>0) 
          {
            mpb_chat_message newmsg;
            newmsg.parms[0]="PART";
            newmsg.parms[1]=p->m_username.Get();
            Broadcast(newmsg.build(),p);

            mpb_server_userinfo_change_notify mfmt;
            int mfmt_changes=0;

            int whichch=0;
            while (whichch < MAX_USER_CHANNELS)
            {
              p->m_channels[whichch].name.Set("");

              if (!whichch || p->m_channels[whichch].active) // only send deactivate if it was previously active
              {
                p->m_channels[whichch].active=0;
                mfmt_changes++;
                mfmt.build_add_rec(0,whichch,
                                  p->m_channels[whichch].volume,
                                  p->m_channels[whichch].panning,
                                  p->m_channels[whichch].flags,
                                  p->m_username.Get(),
                                  p->m_channels[whichch].name.Get());
              }

              whichch++;
            }

            if (mfmt_changes) Broadcast(mfmt.build(),p);
          }

          char addrbuf[256];
          JNL::addr_to_ipstr(p->m_netcon.GetConnection()->get_remote(),addrbuf,sizeof(addrbuf));
          logText("%s: disconnected (username:'%s', code=%d)\n",addrbuf,p->m_auth_state>0?p->m_username.Get():"",ret);

          delete p;
          m_users.Delete(thispos);
          x--;
        }
      }
    }
    m_run_robin++;

    return wantsleep;
}

void User_Group::SetConfig(int bpi, int bpm)
{
  m_last_bpi=bpi;
  m_last_bpm=bpm;
  mpb_server_config_change_notify mk;
  mk.beats_interval=bpi;
  mk.beats_minute=bpm;
  Broadcast(mk.build());
}

void User_Group::AddConnection(JNL_Connection *con, int isres)
{
  User_Connection *p=new User_Connection(con,this);
  if (isres) p->m_reserved=1;
  m_users.Add(p);
}

void User_Group::onChatMessage(User_Connection *con, mpb_chat_message *msg)
{
  if (!strcmp(msg->parms[0],"MSG")) // chat message
  {
    WDL_PtrList<Net_Message> need_bcast;
    if (msg->parms[1] && !strncmp(msg->parms[1],"!vote",5)) // chat message
    {
      if (!(con->m_auth_privs & PRIV_VOTE) || m_voting_threshold > 100 || m_voting_threshold < 1)
      {
        mpb_chat_message newmsg;
        newmsg.parms[0]="MSG";
        newmsg.parms[1]="";
        newmsg.parms[2]=(char *)(m_voting_threshold > 100 || m_voting_threshold < 1? "[voting system] Voting not enabled" : "[voting system] No vote permission");
        con->Send(newmsg.build());
        return;
      }
      char *p=msg->parms[1];
      while (*p && *p != ' ') p++;
      while (*p == ' ') p++;
      char *pn=p;
      while (*p && *p != ' ') p++;
      while (*p == ' ') p++;

      if (*p && !strncmp(pn,"bpm",3) && atoi(p) >= MIN_BPM && atoi(p) <= MAX_BPM)
      {
        con->m_vote_bpm=atoi(p);
        con->m_vote_bpm_lasttime=time(NULL);
      }
      else if (*p && !strncmp(pn,"bpi",3) && atoi(p) >= MIN_BPI && atoi(p) <= MAX_BPI)
      {
        con->m_vote_bpi=atoi(p);
        con->m_vote_bpi_lasttime=time(NULL);
      }
      else
      {
        mpb_chat_message newmsg;
        newmsg.parms[0]="MSG";
        newmsg.parms[1]="";
        newmsg.parms[2]="[voting system] !vote requires <bpm|bpi> <value> parameters";
        con->Send(newmsg.build());
        return;
      }
      // print voting stats
      {
        int bpis[1+MAX_BPI-MIN_BPI]={0,};
        int bpms[1+MAX_BPM-MIN_BPM]={0,};
        int x;
        int maxbpi=0, maxbpm=0;
        int vucnt=0;

        time_t now=time(NULL);
        for (x = 0; x < m_users.GetSize(); x ++)
        {
          User_Connection *p=m_users.Get(x);
          if (p->m_auth_state<=0) continue;
          if (!(p->m_auth_privs & PRIV_HIDDEN)) vucnt++;
          if (p->m_vote_bpi_lasttime >= now-m_voting_timeout && p->m_vote_bpi >= MIN_BPI && p->m_vote_bpi <= MAX_BPI)
          {
              int v=++bpis[p->m_vote_bpi-MIN_BPI];
              if (v > bpis[maxbpi]) maxbpi=p->m_vote_bpi-MIN_BPI;
          }
          if (p->m_vote_bpm_lasttime >= now-m_voting_timeout && p->m_vote_bpm >= MIN_BPM && p->m_vote_bpm <= MAX_BPM)
          {
              int v=++bpms[p->m_vote_bpm-MIN_BPM];
              if (v > bpms[maxbpm]) maxbpm=p->m_vote_bpm-MIN_BPM;
          }
        }
        if (bpms[maxbpm] > 0 && !strncmp(pn,"bpm",3))
        {
          if (bpms[maxbpm] >= (vucnt * m_voting_threshold + 50)/100)
          {
            m_last_bpm=maxbpm+MIN_BPM;
            char buf[512];
            sprintf(buf,"[voting system] setting BPM to %d",m_last_bpm);

            mpb_chat_message newmsg;
            newmsg.parms[0]="MSG";
            newmsg.parms[1]="";
            newmsg.parms[2]=buf;
            need_bcast.Add(newmsg.build());



            mpb_server_config_change_notify mk;
            mk.beats_interval=m_last_bpi;
            mk.beats_minute=m_last_bpm;
            need_bcast.Add(mk.build());
            for (x = 0; x < m_users.GetSize(); x ++)
              m_users.Get(x)->m_vote_bpm_lasttime = 0;
          }
          else
          {
            char buf[512];
            sprintf(buf,"[voting system] leading candidate: %d/%d votes for %d BPM [each vote expires in %ds]",bpms[maxbpm],(vucnt * m_voting_threshold + 50)/100,maxbpm+MIN_BPM,m_voting_timeout);

            mpb_chat_message newmsg;
            newmsg.parms[0]="MSG";
            newmsg.parms[1]="";
            newmsg.parms[2]=buf;
            need_bcast.Add(newmsg.build());
          }
        }
        if (bpis[maxbpi] > 0 && !strncmp(pn,"bpi",3))
        {
          if (bpis[maxbpi] >= (vucnt * m_voting_threshold + 50)/100)
          {
            m_last_bpi=maxbpi+MIN_BPI;
            char buf[512];
            sprintf(buf,"[voting system] setting BPI to %d",m_last_bpi);

            mpb_chat_message newmsg;
            newmsg.parms[0]="MSG";
            newmsg.parms[1]="";
            newmsg.parms[2]=buf;
            need_bcast.Add(newmsg.build());

            mpb_server_config_change_notify mk;
            mk.beats_interval=m_last_bpi;
            mk.beats_minute=m_last_bpm;
            need_bcast.Add(mk.build());
            for (x = 0; x < m_users.GetSize(); x ++)
              m_users.Get(x)->m_vote_bpi_lasttime = 0;
          }
          else
          {
            char buf[512];
            sprintf(buf,"[voting system] leading candidate: %d/%d votes for %d BPI [each vote expires in %ds]",bpis[maxbpi],(vucnt * m_voting_threshold + 50)/100,maxbpi+MIN_BPI,m_voting_timeout);

            mpb_chat_message newmsg;
            newmsg.parms[0]="MSG";
            newmsg.parms[1]="";
            newmsg.parms[2]=buf;
            need_bcast.Add(newmsg.build());
          }
        }
      }
     

    }

    if (!(con->m_auth_privs & PRIV_CHATSEND))
    {
      mpb_chat_message newmsg;
      newmsg.parms[0]="MSG";
      newmsg.parms[1]="";
      newmsg.parms[2]="No MSG permission";
      con->Send(newmsg.build());
    }
    else if (msg->parms[1] && *msg->parms[1])
    {
      mpb_chat_message newmsg;
      newmsg.parms[0]="MSG";
      newmsg.parms[1]=con->m_username.Get();
      newmsg.parms[2]=msg->parms[1];
      Broadcast(newmsg.build());
    }
    int x;
    for (x = 0; x < need_bcast.GetSize(); x ++)
      Broadcast(need_bcast.Get(x));
    need_bcast.Empty();
  }
  else if (!strcmp(msg->parms[0],"SESSION")) // session block descriptor message
  {
    mpb_chat_message newmsg;
    newmsg.parms[0]="SESSION";
    newmsg.parms[1]=con->m_username.Get();
    newmsg.parms[2]=msg->parms[1]; // guid
    newmsg.parms[3]=msg->parms[2]; // index
    newmsg.parms[4]=msg->parms[3]; // offset, length
    Broadcast(newmsg.build(),con);
  }
  else if (!strcmp(msg->parms[0],"PRIVMSG")) // chat message
  {
    if (!(con->m_auth_privs & PRIV_CHATSEND))
    {
      mpb_chat_message newmsg;
      newmsg.parms[0]="MSG";
      newmsg.parms[1]="";
      newmsg.parms[2]="No PRIVMSG permission";
      con->Send(newmsg.build());
    }
    else if (msg->parms[1] && *msg->parms[1] && msg->parms[2] && *msg->parms[2])
    {
      // send a privmsg to user in parm1, and if they don't
      int x;
      for (x = 0; x < m_users.GetSize(); x ++)
      {
        if (!strcasecmp(msg->parms[1],m_users.Get(x)->m_username.Get()))
        {
          mpb_chat_message newmsg;
          newmsg.parms[0]="PRIVMSG";
          newmsg.parms[1]=con->m_username.Get();
          newmsg.parms[2]=msg->parms[2];
          m_users.Get(x)->Send(newmsg.build());

          return;
        }
      }

      // send a privmsg back to sender, saying shit aint there
      WDL_String buf("No such user: ");
      buf.Append(msg->parms[1]);          
      mpb_chat_message newmsg;
      newmsg.parms[0]="MSG";
      newmsg.parms[1]="";
      newmsg.parms[2]=buf.Get();
      con->Send(newmsg.build());
    }
  }
  else if (!strcmp(msg->parms[0],"ADMIN")) // admin message
  {
    char *adminerr="ADMIN requires valid parameter, i.e. topic, kick, bpm, bpi";
    if (msg->parms[1] && *msg->parms[1])
    {
      if (!strncasecmp(msg->parms[1],"topic ",6))
      {
        if (!(con->m_auth_privs & PRIV_TOPIC))
        {
          mpb_chat_message newmsg;
          newmsg.parms[0]="MSG";
          newmsg.parms[1]="";
          newmsg.parms[2]="No TOPIC permission";
          con->Send(newmsg.build());
        }
        else
        {
          // set topic, notify everybody of topic change
          char *p=msg->parms[1]+6;
          while (*p == ' ') p++;
          if (*p)
          {
            m_topictext.Set(p);
            mpb_chat_message newmsg;
            newmsg.parms[0]="TOPIC";
            newmsg.parms[1]=con->m_username.Get();
            newmsg.parms[2]=m_topictext.Get();
            Broadcast(newmsg.build());
          }
        }

      }
      else if (!strncasecmp(msg->parms[1],"kick ",5))
      {
        if (!(con->m_auth_privs & PRIV_KICK))
        {
          mpb_chat_message newmsg;
          newmsg.parms[0]="MSG";
          newmsg.parms[1]="";
          newmsg.parms[2]="No KICK permission";
          con->Send(newmsg.build());
        }
        else
        {
          // set topic, notify everybody of topic change
          char *p=msg->parms[1]+5;
          while (*p == ' ') p++;
          if (*p)
          {
            // try to kick user
            int x;
            int killcnt=0;
            int pl=strlen(p);
            for (x = 0; x < m_users.GetSize(); x ++)
            {
              User_Connection *c=m_users.Get(x);
              if ((p[pl-1] == '*' && !strncasecmp(c->m_username.Get(),p,pl-1)) || !strcasecmp(c->m_username.Get(),p))
              {
                if (c != con)
                {
                  WDL_String buf("User ");
                  buf.Append(c->m_username.Get());
                  buf.Append(" kicked by ");
                  buf.Append(con->m_username.Get());

                  mpb_chat_message newmsg;
                  newmsg.parms[0]="MSG";
                  newmsg.parms[1]="";
                  newmsg.parms[2]=buf.Get();
                  Broadcast(newmsg.build());

                  c->m_netcon.Kill();
                }
                killcnt++;
              }
            }
            if (!killcnt)
            {
              WDL_String tmp;
              tmp.Set("User \"");
              tmp.Append(p);
              tmp.Append("\" not found!\n");

              mpb_chat_message newmsg;
              newmsg.parms[0]="MSG";
              newmsg.parms[1]="";
              newmsg.parms[2]=tmp.Get();
              con->Send(newmsg.build());
            }

          }
        }

      }
      else if (!strncasecmp(msg->parms[1],"bpm ",4) || !strncasecmp(msg->parms[1],"bpi ",4))
      {
        if (!(con->m_auth_privs & PRIV_BPM))
        {
          mpb_chat_message newmsg;
          newmsg.parms[0]="MSG";
          newmsg.parms[1]="";
          newmsg.parms[2]="No BPM/BPI permission";
          con->Send(newmsg.build());
        }
        else
        {
          int isbpm=tolower(msg->parms[1][2])=='m';

          char *p=msg->parms[1]+4;
          while (*p == ' ') p++;
          int v=atoi(p);
          if (isbpm && (v < 20 || v > 400))
          {
            mpb_chat_message newmsg;
            newmsg.parms[0]="MSG";
            newmsg.parms[1]="";
            newmsg.parms[2]="BPM parameter must be between 20 and 400";
            con->Send(newmsg.build());
          }
          else if (!isbpm && (v < 2 || v > 1024))
          {
            mpb_chat_message newmsg;
            newmsg.parms[0]="MSG";
            newmsg.parms[1]="";
            newmsg.parms[2]="BPI parameter must be between 2 and 1024";
            con->Send(newmsg.build());
          }
          else
          {
            if (isbpm) m_last_bpm=v;
            else m_last_bpi=v;

            mpb_server_config_change_notify mk;
            mk.beats_interval=m_last_bpi;
            mk.beats_minute=m_last_bpm;
            Broadcast(mk.build());

            WDL_String str(con->m_username.Get());
            str.Append(" sets ");
            if (isbpm) str.Append("BPM"); else str.Append("BPI");
            str.Append(" to ");
            char buf[64];
            sprintf(buf,"%d",v);
            str.Append(buf);

            mpb_chat_message newmsg;
            newmsg.parms[0]="MSG";
            newmsg.parms[1]="";
            newmsg.parms[2]=str.Get();
            Broadcast(newmsg.build());
          }
        }

      }
      else
      {
        mpb_chat_message newmsg;
        newmsg.parms[0]="MSG";
        newmsg.parms[1]="";
        newmsg.parms[2]=adminerr;
        con->Send(newmsg.build());
      }
    }
    else
    {
      mpb_chat_message newmsg;
      newmsg.parms[0]="MSG";
      newmsg.parms[1]="";
      newmsg.parms[2]=adminerr;
      con->Send(newmsg.build());
    }
  }
  else // unknown message
  {
  }
}
