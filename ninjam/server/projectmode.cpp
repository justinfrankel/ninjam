#ifdef _WIN32
#include <windows.h>
#else
#include <stdlib.h>
#include <string.h>
#endif

#include <ctype.h>

#include "projectmode.h"

/********
todo: 
messages for:

  client->server:
    put data
    request data (optional revision#)
    request list of data
    request revision history of data
    request project switch?

  server->client
    put data reply (ok?)
    request data reply (data...)
    request list of data reply (list of items, hashes, timestamps, lengths)
    revision history request reply
    notify client of new revision (includes revision data)


  client on connect, would go and request everything, detect what was different, request it
    -- detect conflicts, rename old tracks to XXXconflict - (private), auto-ignore tracks with (private), also auto-ignore unnamed tracks

  subsequently server would notify if other users modded things



  // dir format:

  .tmp.* has uploading data (will get appended atomically to appropriate place)

  track_guid(_items|_cfg|_fx), project_misc

  media_whatever_filename.ext(.ogg)


  each file(database) consists of:

  <10 bytes magic>
  <4 bytes data length> 
  <2 bytes trailing header length>

  <data> 

  trailing header, last 128 bytes are:
     16 byte magic
     4 byte start of data offset from magic
     4 byte data length ( can be less if we want more data stuffed before this block)
     20 byte SHA1
     4 byte timestamp
     32 byte user
     8 byte revision count
     (40) byte pad/future
  


  todo: proxy PCM source that will use xxx.ext-proxy.ogg but serialize as xxx.ext, if xxx.ext is not found
*********/




extern void logText(char *s, ...);

void ProjectConnection::Send(Net_Message *msg)
{
  if (m_con && m_con->Send(msg))
  {
    logText("Error sending message to user '%s', type %d, queue full!\n",m_username.Get(),msg->get_type());
  }
}

int ProjectConnection::Run(ProjectInstance *proj, const char *name)
{
  if (!m_con) return -1;
  int wantsleep=1;
  Net_Message *msg=m_con->Run(&wantsleep);

  if (!m_con || m_con->GetStatus()) 
  {
    delete msg;
    return -1;
  }

  if (msg)
  {
    switch (msg->get_type())
    {
      case MESSAGE_CHAT_MESSAGE:
        {
          mpb_chat_message poo;
          if (!poo.parse(msg))
          {
            proj->onChatMessage(this,&poo);
          }
        }
      break;
    }
    msg->releaseRef();
  }

  return !wantsleep;
}

ProjectInstance::ProjectInstance() 
{ 
}

ProjectInstance::~ProjectInstance() 
{ 
  m_cons.Empty(true); 
}

void ProjectInstance::Broadcast(Net_Message *msg, ProjectConnection *nosend)
{
  if (msg)
  {
    msg->addRef(); // we do this so that if nobody actually gets to send it, we delete it

    int x;
    for (x = 0; x < m_cons.GetSize(); x ++)
    {
      ProjectConnection *p=m_cons.Get(x);
      if (p && p != nosend)
      {
        p->Send(msg);
      }
    }

    msg->releaseRef();
  }
}

void ProjectInstance::onChatMessage(ProjectConnection *con, mpb_chat_message *msg)
{
  if (!strcmp(msg->parms[0],"MSG")) // chat message
  {
    WDL_PtrList<Net_Message> need_bcast;

    if (msg->parms[1] && *msg->parms[1])
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
}

int ProjectInstance::Run(const char *name)
{
  int cnt=0;
  int x;
  for(x=0;x<m_cons.GetSize();x++)
  {
    int a=  m_cons.Get(x)->Run(this,name);
    if (a<0) m_cons.Delete(x--,true);
    else cnt+=a;
  }
  // run connections
  return cnt;
}