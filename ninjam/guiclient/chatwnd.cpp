#include "precomp.h"

#include "resource.h"

#include <bfc/std_wnd.h>
#include <bfc/string/bigstring.h>

#include "../njclient.h"

#include "attrs.h"
#include "chatwnd.h"

#define CHATHISTLEN 128	//FUCKO configable, minimum 40 or something

#define CMD_CHAR '/'

//builtin
#define CMD_HELP "help"

//outgoing
#define CMD_BPM "bpm"
#define CMD_BPI "bpi"
#define CMD_KICK "kick"
#define CMD_TOPIC "topic"

//incoming
#define CMD_ME "/me"

#define HELPTEXT \
"Help:\r\n" \
"Anything you type will be sent to all other users.\r\n" \
"Commands:\r\n" \
"\t/me\temote\r\n" \
"\t/topic\tchanges the topic\r\n" \
"\t/help\tshows this text\r\n"

#define ERROR_NOTCONNECTED "*** Not connected to server."

enum {
  TIMER_KILLME
};

extern NJClient *g_client;
extern String g_user_name;

static PtrList<String> chathist;
static String cur_topic;

static void chatcb(int user32, NJClient *inst, char **parms, int nparms) {
  ChatWnd *chatwnd = reinterpret_cast<ChatWnd *>(user32);
  ASSERT(chatwnd != NULL);
//DebugString("chatcb: %d params", nparms);
  String p0 = parms[0];
  if (p0.isequal("MSG")) {
    chatwnd->addChatLine(parms[1], parms[2]);
  } else if (p0.isequal("JOIN") || p0.isequal("PART")) {
    if (parms[1] && *parms[1]) {
      chatwnd->addChatLine(NULL,StringPrintf("*** %s has %s the server",parms[1],parms[0][0]=='P' ? "left" : "joined"));
    }
  } else if (p0.isequal("TOPIC")) {
    String newtopic = parms[2];
    newtopic.trim();
    if (newtopic.notempty()) {
      String who = parms[1];
      who.trim();
      if (who.notempty()) {
        chatwnd->addChatLine(NULL, StringPrintf("*** %s sets topic: '%s'", who.v(), newtopic.v()));
      } else {
        chatwnd->addChatLine(NULL, StringPrintf("*** Topic is '%s'", newtopic.v()));
      }
      cur_topic = newtopic;
      chatwnd->setTopic(cur_topic);
    }
  }

}

ChatWnd::ChatWnd(OSWINDOWHANDLE parwnd) : OSDialog(IDD_CHAT, parwnd) {
  ASSERT(g_client != NULL);
  g_client->ChatMessage_User32 = reinterpret_cast<int>(this);
  g_client->ChatMessage_Callback = chatcb;

  registerAttribute(chat_disp, IDC_CHAT_DISP);
  registerAttribute(chat_entry, IDC_CHAT_ENTRY);
  registerAttribute(chat_topic, IDC_CHAT_TOPIC);

  addSizeBinding(IDC_CHAT_HIST, OSDialogSizeBind::RIGHTEDGETORIGHT|OSDialogSizeBind::BOTTOMEDGETOBOTTOM);
  addSizeBinding(IDC_CHAT_EDIT, OSDialogSizeBind::RIGHTEDGETORIGHT|OSDialogSizeBind::BOTTOM);

  setDisp();	// in case there is already history
}

ChatWnd::~ChatWnd() {
  unsubclass();
  ASSERT(g_client != NULL);
  g_client->ChatMessage_User32 = 0;
  g_client->ChatMessage_Callback = 0;
  if (m_icon_small) { DestroyIcon(m_icon_small); m_icon_small = NULL; }
  if (m_icon_big) { DestroyIcon(m_icon_big); m_icon_big = NULL; }
}

void ChatWnd::addChatLine(const char *from, const char *txt) {
  String *line = NULL;
  if (from == NULL) {
    line = new String(txt);
  } else if (!STRNINCMP(txt, CMD_ME " ")) {
    txt += STRLEN(CMD_ME);
    line = new StringPrintf("* %s%s", from, txt);
  } else if (from == NULL || *from == '\0') {
    line = new StringPrintf("*** %s", txt);
  } else {
    line = new StringPrintf("<%s> %s", from, txt);
  }
  if (timestamp_chat && from != NULL) {
    line->prepend(StringStrftime(0, "[%H:%M] "));
  }
  chathist += line;
  while (chathist.n() > CHATHISTLEN) {
//DebugString("kill line '%s'", chathist[0]->v());
    delete chathist[0];
    chathist.removeByPos(0);
  }

  setDisp();
}

void ChatWnd::processUserEntry(const char *ln) {
  if (*ln == CMD_CHAR && STRNINCMP(ln, CMD_ME " ")) {
    if ((!STRNINCMP(ln+1, CMD_HELP) && (ln[1+strlen(CMD_HELP)] == 0 || ln[1+strlen(CMD_HELP)] == ' '))) {
      addChatLine(NULL, HELPTEXT);
    } else if ((!STRNINCMP(ln+1, CMD_BPI) && (ln[1+strlen(CMD_BPI)] == 0 || ln[1+strlen(CMD_BPI)] == ' ')) ||
               (!STRNINCMP(ln+1, CMD_BPM) && (ln[1+strlen(CMD_BPM)] == 0 || ln[1+strlen(CMD_BPM)] == ' ')) ||
               (!STRNINCMP(ln+1, CMD_KICK) && (ln[1+strlen(CMD_KICK)] == 0 || ln[1+strlen(CMD_KICK)] == ' ')) ||
               (!STRNINCMP(ln+1, CMD_TOPIC) && (ln[1+strlen(CMD_TOPIC)] == 0 || ln[1+strlen(CMD_TOPIC)] == ' '))
              ) {

      if (g_client->GetStatus() != NJClient::NJC_STATUS_OK) 
        addChatLine(NULL, ERROR_NOTCONNECTED);
      else
        g_client->ChatMessage_Send("ADMIN", (char*)ln+1);
    } else {
      // unknown command
      addChatLine(NULL, "Unknown command. For a list of commands, type \"/help\".");
    }
  } else {
    if (g_client->GetStatus() != NJClient::NJC_STATUS_OK) 
      addChatLine(NULL, ERROR_NOTCONNECTED);
    else
      g_client->ChatMessage_Send("MSG", (char*)ln);
  }
}

void ChatWnd::setTopic(const char *topic) {
  String t(topic);
  String s = "NINJAM chat";
  if (t.notempty()) s += StringPrintf(" - %s", t.vs());
  setName(s);
  chat_topic = s;
}

void ChatWnd::setDisp() {
  BigString ploo;
  int needcr = 0;
  for (int i = 0; i < chathist.n(); i++) {
    if (needcr) ploo += "\r\n";
    ploo += chathist[i]->v();
    needcr = 1;
  }

  // assign to dialog
  chat_disp = ploo;

  // scroll it to bottom too
  HWND ec = GetDlgItem(getOsWindowHandle(), IDC_CHAT_HIST);
  int lc = SendMessage(ec, EM_GETLINECOUNT, 0, 0);
  SendMessage(ec, EM_LINESCROLL, 0, lc);
}

void ChatWnd::onCreate() {
  subclass(GetDlgItem(getOsWindowHandle(), IDC_CHAT_DISP));

  int resourceid = IDI_NINJAM;
  m_icon_small = (HICON) LoadImage(WASABI_API_APP->main_gethInstance(), MAKEINTRESOURCE(resourceid), IMAGE_ICON, 16, 16, LR_DEFAULTCOLOR);
  m_icon_big = (HICON) LoadImage(WASABI_API_APP->main_gethInstance(), MAKEINTRESOURCE(resourceid), IMAGE_ICON, 32, 32, LR_DEFAULTCOLOR);
  SendMessage(getOsWindowHandle(), WM_SETICON, ICON_SMALL, (int)m_icon_small);
  SendMessage(getOsWindowHandle(), WM_SETICON, ICON_BIG, (int)m_icon_big);

  setTopic(cur_topic);
}

void ChatWnd::onUserClose() {
  timerclient_postDeferredCallback(TIMER_KILLME);
}

void ChatWnd::onUserButton(int id) {
  if (id == IDC_CHAT_ENTER) {
    processUserEntry(chat_entry);
    chat_entry = "";
  }
}

void ChatWnd::onPostApplyDlgToAttr(Attribute *attr, const char *newval, int dlgid) {
  CHATWND_PARENT::onPostApplyDlgToAttr(attr, newval, dlgid);
}

int ChatWnd::timerclient_onDeferredCallback(int param1, int param2) {
  switch (param1) {
    case TIMER_KILLME:
      StdWnd::hideWnd(getOsWindowHandle());
      chat_was_showing = 0;
    break;
  }
  return 1;
}

int ChatWnd::onSubclassWndProc(HWND hwnd, UINT uMsg, WPARAM wParam, LPARAM lParam, LRESULT &retval) {
  retval = 0;
  switch (uMsg) {
    case WM_CHAR:
      if (lParam & (1<<23)) return 0;	// not if control/shift/alt is down
      SendMessage(GetDlgItem(getOsWindowHandle(), IDC_CHAT_ENTRY), uMsg, wParam, lParam);
      SetFocus(GetDlgItem(getOsWindowHandle(), IDC_CHAT_ENTRY));
    break;
    default:
      return 0;
  }
  return 1;
}
