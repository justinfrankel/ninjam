#include <windows.h>

#include "usercon.h"
#include "../mpb.h"


User_Connection::User_Connection(JNL_Connection *con) : m_clientcaps(0), m_auth_state(0),
                                                        m_last_bpm(120), m_last_bpi(32)
{
  m_netcon.attach(con);

  // fucko: randomize m_challenge

  mpb_server_auth_challenge ch;
  memcpy(ch.challenge,m_challenge,sizeof(ch.challenge));

  ch.server_caps=0;

  m_netcon.Send(ch.build());
}

User_Connection::~User_Connection()
{
}


void User_Connection::SendConfigChangeNotify(int bpm, int bpi)
{
  if (bpm>0) m_last_bpm = bpm;
  if (bpi>0) m_last_bpi = bpi;
  if (m_auth_state > 0)
  {
    mpb_server_config_change_notify mk;
    mk.beats_interval=m_last_bpi;
    mk.beats_minute=m_last_bpm;
    m_netcon.Send(mk.build());
  }
}

int User_Connection::Run(User_Group *group)
{
  if (m_netcon.GetStatus()) return m_netcon.GetStatus();

  Net_Message *msg=m_netcon.Run();
  if (msg)
  {
    if (m_auth_state < 1)
    {
      mpb_client_auth_user authrep;
      if (msg->get_type() != MESSAGE_CLIENT_AUTH_USER || authrep.parse(msg) || !authrep.username || !authrep.username[0])
      {
        m_netcon.Kill();
        return -1;
      }
      // fucko: verify user/password hash

      {
        mpb_server_auth_reply bh;
        bh.flag=1;
        m_netcon.Send(bh.build());
      }

      m_clientcaps=authrep.client_caps;
      m_auth_state=1;

      SendConfigChangeNotify(-1,-1);

      // send user list to user
      {
        mpb_server_userinfo_change_notify bh;

        int user;
        for (user = 0; user < group->m_users.GetSize(); user++)
        {
          User_Connection *u=group->m_users.Get(user);
          int channel;
          if (u) for (channel = 0; channel < MAX_USER_CHANNELS; channel ++)
          {
            if (u->m_channels[channel].name.Get()[0])
            {
              bh.build_add_rec(0,channel,u->m_channels[channel].volume,u->m_channels[channel].panning,u->m_channels[channel].flags,
                                u->m_username.Get(),u->m_channels[channel].name.Get());
            }
          }
        }       
        m_netcon.Send(bh.build());
      }
    } // m_auth_state < 1


    switch (msg->get_type())
    {
      default:
      break;
    }
  }


  return 0;
}