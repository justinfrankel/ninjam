#include "precomp.h"

#include <time.h>
#include <bfc/std_wnd.h>
#include <bfc/file/readdir.h>
#include <parse/pathparse.h>
#include <wnd/wnds/os/osdialog.h>

#include "../audiostream_asio.h"
#include "../njclient.h"

#include "resource.h"

#include "attrs.h"
#include "mainwnd.h"

#include "session.h"

#define int64 __int64

static int in_session=0;
static String session_name;
static String cur_session_dir;

extern NJClient *g_client;
extern audioStreamer *g_audio;

String makeSessionDirName(int cnt, time_t t) {
  String ret = StringStrftime(t, session_format);
  if (cnt > 0)
    ret.cat(StringPrintf("_%d",cnt));
  ret.cat(".ninjam");
  return ret;
}

int Session::newSession() {
  endSession();

  ASSERT(!in_session);

  String sessiondir = main_session_dir;
  if (sessiondir.isempty()) {	// no chosen dir, use cur dir
    char buf[WA_MAX_PATH];
    Std::getCurDir(buf, WA_MAX_PATH);
    sessiondir = buf;
  } else {
    Std::createDirectory(sessiondir);	// create it
//CUT    cur_session_dir = sessiondir;
  }

  // sessiondir has trailing /
  if (!Std::isDirChar(sessiondir.lastChar()))
    sessiondir += DIRCHARSTR;

  String dirname;	// keep dirname
  int cnt=0;
  time_t now = time(NULL);
  while (cnt < 99) {
    String dir = sessiondir;
    dirname = makeSessionDirName(cnt, now);
    dir += dirname;
    if (CreateDirectory(dir,NULL)) {
      cur_session_dir = dir;
      break;
    }
    cnt++;
    Sleep(10);
  }
  if (cnt >= 99) {
  StdWnd::messageBox(
    "Error creating session recording directory. Maybe it's in use already?"
    " Or is the drive out of space?",
    "Error creation session recording directory",
    MB_OK|MB_TASKMODAL);
    return 0;
  }

//  if (sessiondir.Get()[0] && sessiondir.Get()[strlen(sessiondir.Get())-1]!='\\' && sessiondir.Get()[strlen(sessiondir.Get())-1]!='/')
//    sessiondir.Append("\\");
  if (!Std::isDirChar(cur_session_dir.lastChar()))
    cur_session_dir += DIRCHARSTR;

  //g_client->SetWorkDir(sessiondir.Get());
  g_client->SetWorkDir(cur_session_dir.ncv());

  if (write_wav)
  {
    String wf = cur_session_dir;
    wf += "output.wav";
    g_client->waveWrite = new WaveWriter(wf.ncv(),24,g_audio->m_outnch>1?2:1,g_audio->m_srate);
  }
  if (write_ogg) {
    String wf = cur_session_dir;
    wf += "output.ogg";
    g_client->SetOggOutFile(fopen(wf,"ab"),g_audio->m_srate,g_audio->m_outnch>1?2:1,ogg_bitrate);
  }
  if (1/*!nolog*/)
  {
//    WDL_String lf;
    String lf;
//    lf.Set(sessiondir.Get());
    lf = cur_session_dir;
//    lf.Append("clipsort.log");
    lf += "clipsort.log";
//    g_client->SetLogFile(lf.Get());
    g_client->SetLogFile(lf.ncv());
  }
// end session stuff

  in_session = 1;

//CUT  PathParser pp(sessiondir, "/\\");
//CUT  session_name = pp.getLastString();
  session_name = dirname;

  extern MainWnd *mainw;
  mainw->setFormattedName(/*CUTsession_name.vs()*/dirname);

  return 1;
}

void Session::endSession() {
  if (!in_session) return;	// no big deal

  if (g_client != NULL) {
    g_client->Disconnect();
    delete g_client->waveWrite; g_client->waveWrite = NULL;
    g_client->SetOggOutFile(NULL,0,0,0);
    g_client->SetLogFile(NULL);
    g_client->SetWorkDir(NULL);
  }

#if 0
  // pop up rename box
  extern MainWnd *mainw;
  OSDialog sess(IDD_CLOSESESSION, mainw->getOsWindowHandle());
  _string sessname;
  sessname = session_name;
  sess.registerAttribute(sessname, IDC_SESSION_NAME);

  _string sesslen;
  int64 sp = g_client->GetSessionPosition();
DebugString("sp is %d", sp);
  int ms = sp % 1000;
  sp /= 1000;
  int sec = sp % 60;
  sp /= 60;
  int mins = sp;
  sesslen = StringPrintf("%d:%02d.%03d", mins, sec, ms);
  sess.registerAttribute(sesslen, IDC_SESSION_LEN);

  int r = 0;
  for (int again=1; again; ) {
    r = sess.createModal();

    if (!r) {	// they picked delete
      //FUCKO: confirm delete
      StringPrintf txt("Please confirm that you wish to discard session '%s'.", session_name.vs());
      int r2 = StdWnd::messageBox(txt, "Confirm session discard", MB_OK|MB_TASKMODAL);
      if (!r2) continue;	// again!
      again = 0;

//chdir to cur_session_dir
SEXO
    } else {
      again = 0;
      break;
    }
  }

  if (r) {
    // otherwise keep
    // rename the dir to match
    if (!STREQL(sessname, session_name)) {
      char buf[WA_MAX_PATH];
      Std::getCurDir(buf, WA_MAX_PATH);

#if 1//CUT
      String path = cur_session_dir;

      PathParser pp(path, "\\/");
      String recon;
      int needslash=0;
      for (int i = 0; i < pp.getNumStrings()-1; i++) {
        if (needslash) recon += DIRCHARSTR;
        recon += pp.enumString(i);
        needslash = 1;
      }
#endif

      // go to just below it
      Std::setCurDir(recon);

      // rename!
      int res = rename(session_name, sessname);
    
      Std::setCurDir(buf);
      if (res != 0) {
        int b = GetLastError();
      }
    }
  }
#else
  if (!write_log) {
    killIntervalsDir(cur_session_dir);
  }

  RemoveDirectory(cur_session_dir);	// remove the dir if it's empty
#endif

  extern MainWnd *mainw;
  if (mainw) mainw->setFormattedName();

  in_session = 0;
}

void Session::killIntervalsDir(const char *dir) {
  String root = dir;
  if (!Std::isDirChar(root.lastChar()))
    root += DIRCHARSTR;

  // kill the subdirs
  for (int i = 0; i < 16; i++) {
    String subdir = root;
    subdir += StringPrintf("%x", i);
DebugString("kill subdir: '%s'", subdir.v());
    {
      ReadDir rd(subdir, "*.ogg");
      while (rd.next()) {
        String fn = rd.getFilename();
        fn.prepend(DIRCHARSTR);
        fn.prepend(subdir);
DebugString("kill filename: '%s'", fn.v());
        FDELETE(fn);
      }
    }
    RemoveDirectory(subdir);	// remove the dir
  }
  // delete clipsort.log
  String clipsortfn = root;
  clipsortfn += "clipsort.log";
  FDELETE(clipsortfn);

  // delete output.wav
  // delete output.log
}
