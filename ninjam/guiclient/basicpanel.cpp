#include "precomp.h"

#include <commctrl.h>
#include <wnd/popup.h>

#include "resource.h"

#include "../njclient.h"
#include "jesus.h"
#include "rackwnd.h"

#include "basicpanel.h"

#define VOLSNAPW 1
#define PANSNAPW 0.03f

#define MAXX_VOLUME 100	// slider range
#define ZERO_VOLUME 63	// 0 volume change at

#define DEF_VOLUME ZERO_VOLUME	// starting volume

#define MIN_VOL_DB -100	// keep this negative, it's in dB
#define MAX_VOL_DB 20	// and this one positive

#define DEF_PAN 50

#define CLIP_VOL	1.0

const char *printfIfNotNull(const char *format, const char *str) {
  static String ret;
  if (str == NULL) return NULL;
  ret.printf(format, str);
  return ret.vs();
}

BasicPanel::BasicPanel(RackWnd *wnd, int dlgid, const char *cfgname)
  : PanelSlot(wnd, dlgid),
  volume(printfIfNotNull("%s/volume", cfgname), DEF_VOLUME),
  pan(printfIfNotNull("%s/pan", cfgname), DEF_PAN),
  mute(printfIfNotNull("%s/mute", cfgname), FALSE),
  solo(printfIfNotNull("%s/solo", cfgname), FALSE),
  fx(printfIfNotNull("%s/fx", cfgname), FALSE)
{
//CUT  if (cfgname != NULL) {
//CUT    volume.setName(StringPrintf("%s volume"));
//CUT  }

// add size bindings here
//CUT  addSizeBinding(IDC_SOLO, OSDialogSizeBind::RIGHT);
  addSizeBinding(IDC_PAN, OSDialogSizeBind::RIGHT);
  addSizeBinding(IDC_PAN_TEXT, OSDialogSizeBind::RIGHT);
  addSizeBinding(IDC_VOLUME, OSDialogSizeBind::RIGHT);
  addSizeBinding(IDC_VOLUME_TEXT, OSDialogSizeBind::RIGHT);
  addSizeBinding(IDC_MUTE, OSDialogSizeBind::RIGHT);
  addSizeBinding(IDC_SOLO, OSDialogSizeBind::RIGHT);
  addSizeBinding(IDC_FX, OSDialogSizeBind::RIGHT);
  addSizeBinding(IDC_EDIT_FX, OSDialogSizeBind::RIGHT);
//CUT  addSizeBinding(IDC_VU, OSDialogSizeBind::RIGHTEDGETORIGHT);

  addSizeBinding(IDC_VUMETER, OSDialogSizeBind::RIGHTEDGETORIGHT);
  registerAttribute(&vumeter, IDC_VUMETER);
  vumeter_avg = 0;

  addSizeBinding(IDC_VUCLIP, OSDialogSizeBind::RIGHT);
  clip = 0;
  registerAttribute(&clip, IDC_VUCLIP);
  subclass(GetDlgItem(getOsWindowHandle(), IDC_VUCLIP));

// pan control
  registerAttribute(&pan, IDC_PAN);
  setSliderRange(IDC_PAN, 0, 100);
// show the center tick
  SendMessage(GetDlgItem(getOsWindowHandle(), IDC_PAN), TBM_SETTIC, 0, 50);
  pan_text = "Center";
  registerAttribute(&pan_text, IDC_PAN_TEXT);

// volume control
  SendMessage(GetDlgItem(getOsWindowHandle(), IDC_VOLUME), TBM_SETTIC, 0, ZERO_VOLUME);
  registerAttribute(&volume, IDC_VOLUME);
  setSliderRange(IDC_VOLUME, 0, MAXX_VOLUME);
  vol_text = "0.0";
  registerAttribute(&vol_text, IDC_VOLUME_TEXT);

//mute
  registerAttribute(&mute, IDC_MUTE);

//solo
  registerAttribute(&solo, IDC_SOLO);

//fx
  registerAttribute(&fx, IDC_FX);
  if (!jesusAvailable()) {
    enableControl(IDC_FX, FALSE);
    enableControl(IDC_EDIT_FX, FALSE);
  }

  if (cfgname != NULL) {	// have to set on a callback since in cons
    timerclient_postDeferredCallback(2);
  }
}

BasicPanel::~BasicPanel() {
  unsubclass();
}

template <class T> float snapAround(T v, T snapat, T snapw) {
  if (ABS(snapat - v) <= snapw) return snapat;	// so snapped
#if 0
  if (v < snapat) {
    return (v + snapw);
  } else {
    return (v - snapw);
  }
#else
  return v;
#endif
}

double DB2SLIDER(double x)
{
  double d=pow(2110.54*fabs(x),1.0/3.0);
  if (x < 0.0) d=-d;
  return d + 63.0;
}
double SLIDER2DB(double y)
{
  return pow(y-63.0,3.0) * (1.0/2110.54);
}

static double VAL2DB(double x)
{
double g_ilog2x6 = 6.0/log10(2.0);
  double v=(log10(x)*g_ilog2x6);
  if (v < -120.0) v=-120.0;
  return v;
}
#define DB2VAL(x) (pow(2.0,(x)/6.0))

void BasicPanel::onPostApplyDlgToAttr(Attribute *attr, const char *newval, int dlgid) {
  extern NJClient *g_client;
ASSERT(g_client != NULL);
  if (g_client == NULL) return;

//CUT  int userid = getUserIdFromName(user_name);

  if (attr == &volume) {
    int nval = ATOI(newval);
    float scaleval = 0;
    float dbval = 0;
#if 0
    nval = snapAround(nval, ZERO_VOLUME, VOLSNAPW);
    if (nval < ZERO_VOLUME) {	// < 0
      val = (float)nval/(float)ZERO_VOLUME;	// val is 0..1 of sliderpos
      dbval = (1.0 - val) * MIN_VOL_DB;	// to db
      scaleval = pow(2.,dbval/6.);	// val from db
    } else {
      val = ((float)nval-ZERO_VOLUME)/(float)(MAXX_VOLUME-ZERO_VOLUME);//0..1
      dbval = val * MAX_VOL_DB;		// to db
      scaleval = pow(2.,dbval/6.);	// val from db
    }
#else
//    x = pow(2110.54*y, 1.0/3.0)+63.0;
//float x = nval;
//    float y = pow(x-63.0,3.0) * (1.0 / 2110.54);
//scaleval = y;

dbval = SLIDER2DB(nval);

scaleval = DB2VAL(dbval);

#endif


//DebugString("scaleval = %5.2f, dbval = %5.2f", scaleval, dbval);

    onVol(scaleval);

    String valtext;
//CUT    if (scaleval <= 0.001) {
//CUT      valtext = "-inf";
//CUT    } else {
//CUT//      float dbval = log10(val)*6.0/log10(2.0);
      valtext = StringPrintf("%3.2f", dbval);
      int cutoff=4;
      if (dbval<0) cutoff++;	//allow for -
      if (valtext.len() > cutoff) valtext.ncv()[cutoff] = 0;	// cut it off eh

      if (dbval>=0) {
        valtext.prepend("+");
        if (dbval > 0.001) {
          valtext += "\nboost";
        }
      }

//CUT    }

    vol_text = valtext;

  } else if (attr == &pan) {
    float val = (float)(ATOI(newval)*2)/100.f-1.f;
    val = snapAround<float>(val, 0, PANSNAPW);
    onPan(val);
    String valtext;
    if (val < 0) valtext = StringPrintf("%d%% l", (int)(-val*100));
    else if (val == 0) valtext = "Center";
    else if (val > 0) valtext = StringPrintf("%d%% r", (int)(val*100));
    pan_text = valtext;
  } else if (attr == &mute) {
    onMute(!!ATOI(newval));
    if (mute) solo = FALSE;
  } else if (attr == &solo) {
    onSolo(!!ATOI(newval));
    if (solo) mute = FALSE;
  } else if (attr == &fx) {
    onFx(!!ATOI(newval));
  } else {
  }
}

int BasicPanel::timerclient_onDeferredCallback(int param1, int param2) {
  switch (param1) {
    case 2:
// reapply old settings right away if we have em
      onPostApplyDlgToAttr(&volume, StringPrintf((int)volume), IDC_VOLUME);
      onPostApplyDlgToAttr(&pan, StringPrintf((int)pan), IDC_PAN);
      onPostApplyDlgToAttr(&mute, StringPrintf((int)mute), IDC_MUTE);
      onPostApplyDlgToAttr(&solo, StringPrintf((int)solo), IDC_SOLO);
      onPostApplyDlgToAttr(&fx, StringPrintf((int)fx), IDC_FX);

      basicpanelOnApplyPrevAttrs();
    break;
    default:
      return 0;
  }
  return 1;
}

void BasicPanel::onDlgContextMenu() {
  PopupMenu pop;
int ofs=1000, parent_ofs = 2000;

//// local basic panel stuff
//  pop.addCommand("Mute", 0);
//  pop.addCommand("Solo", 0);

// panel adds stuff
  panelAppendToPopup(&pop, ofs);
  int nfrompanel = pop.getNumCommands();

// rackwnd adds stuff
  panel_parent->rackwndAppendToPopup(&pop, parent_ofs);

  if (pop.getNumCommands() <= 0) return;	// no cmds?

  // insert sep if needed
  if (nfrompanel > 0 && pop.getNumCommands() > nfrompanel)
    pop.addSeparator(nfrompanel);

  // pop it
  int r = pop.popAtMouse();

  if (r >= ofs && r < parent_ofs)
    panelOnPopupCommand(r-ofs);
  else
    panel_parent->rackwndOnPopupCommand(r-parent_ofs);
}

void BasicPanel::onUserDblClick(int id) {
  switch (id) {
    case IDC_PAN_TEXT: {
      pan = 50;
      onPostApplyDlgToAttr(&pan, StringPrintf((int)pan), IDC_PAN);
    }
    break;
    case IDC_VOLUME_TEXT: {
      volume = ZERO_VOLUME;
      onPostApplyDlgToAttr(&volume, StringPrintf((int)volume), IDC_VOLUME);
    }
    break;
//    case IDC_VUCLIP: {
//      clip = 0;
//    }
//    break;
  }
}

int BasicPanel::onSubclassWndProc(HWND hwnd, UINT uMsg, WPARAM wParam, LPARAM lParam, LRESULT &retval) {
  retval = 0;
  switch (uMsg) {
    case WM_LBUTTONDOWN:
      clip = 0;
    break;
    default:
      return 0;
  }
  return 1;
}

void BasicPanel::onRefresh() {
  float amt = getVU();
  if (amt < 0) {	// not implemented possibly
    vumeter = 0;
    return;
  }

  if (amt >= CLIP_VOL) {
    clip = 101;
  }

//      amt *= 100;
  amt = VAL2DB(amt);

#define MAXVU 0.f
#define MINVU -80.f
  amt = MINMAX<float>(amt, MINVU, MAXVU);
  float vuamt = 1.0 - (amt / (MINVU - MAXVU));

//if (getRackSlotType() == RS_TYPE_MASTER) {
//DebugString("amt is %5.2f, vuamt %5.2f", amt, vuamt);
//}
#if 1
  vumeter_avg += (int)floor(vuamt*100);
  vumeter_avg /= 2;
  vumeter = vumeter_avg;
#else
  vumeter = (int)floor(vuamt*100);
#endif
}
