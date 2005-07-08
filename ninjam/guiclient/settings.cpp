#include "precomp.h"

#include <bfc/file/choosedir.h>

#include "attrs.h"
#include "resource.h"
#include "session.h"

#include "settings.h"

Settings::Settings(HWND parwnd) : OSDialog(IDD_CONFIG_MAIN, parwnd) {
  registerAttribute(jesus_install_dir, IDC_JESUSDIR);

  registerAttribute(main_session_dir, IDC_SESSIONDIR);
  registerAttribute(session_format, IDC_SESSION_FORMAT);
  registerAttribute(exampledir, IDC_EXAMPLEDIR);

  exampledir = makeSessionDirName();

  registerAttribute(write_ogg, IDC_WRITE_OGG);
  registerAttribute(ogg_bitrate, IDC_OGG_BITRATE);

  registerAttribute(write_wav, IDC_WRITE_WAV);

  registerAttribute(write_log, IDC_WRITE_LOG);
}

void Settings::onUserButton(int id) {
  switch (id) {
    case IDC_SESSIONDIR_BROWSE: {
      ChooseDir cd(getOsWindowHandle());
      int r = cd.choose("Select the directory to which NINJAM should record sessions");
      if (r) {
        main_session_dir = cd.getResult();
      }
    }
    break;
    case IDC_JS_BROWSE: {
      ChooseDir cd(getOsWindowHandle());
      int r = cd.choose("Indicate the location of the hidden rebel base, I mean Jesusonic install directory");

      if (r) {
        jesus_install_dir = cd.getResult();
      }
    }
    break;
  }
}

void Settings::onPostApplyDlgToAttr(Attribute *attr, const char *newval, int dlgid) {
  switch (dlgid) {
    case IDC_SESSION_FORMAT:
      exampledir = makeSessionDirName();
    break;
  }
}
