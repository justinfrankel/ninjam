#ifndef __PROJECTMODE_H_
#define __PROJECTMODE_H_

#include "../../WDL/ptrlist.h"
#include "../../WDL/wdlstring.h"

#include "../netmsg.h"
#include "../mpb.h"

class ProjectInstance;

class ProjectConnection
{
public:
  ProjectConnection(Net_Connection *con, const char *username) : m_con(con) { m_username.Set(username); }
  ~ProjectConnection() { delete m_con; }

  int Run(ProjectInstance *proj, const char *name);

  void Send(Net_Message *msg);


  Net_Connection *m_con;
  WDL_String m_username;

};

class ProjectInstance
{
public:
  ProjectInstance();
  ~ProjectInstance();

  int Run(const char *name);

  void onChatMessage(ProjectConnection *con, mpb_chat_message *msg);


  void Broadcast(Net_Message *msg, ProjectConnection *nosend=NULL);
  


  WDL_PtrList<ProjectConnection> m_cons;
};


#endif