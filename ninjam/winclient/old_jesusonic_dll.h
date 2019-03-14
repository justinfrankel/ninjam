/*
    Jesusonic DLL interface - jesusoninc_dll.h
    Copyright (C) 2004-2005 Cockos Incorporated

*/


#ifndef _JESUSONIC_DLL_H_
#define _JESUSONIC_DLL_H_

#define JESUSONIC_API_VERSION_CURRENT 0xf0f0107A

// to get the API, GetProcAddress("JesusonicAPI"), call with no parms

#ifndef _WIN32
#include "../WDL/swell/swell.h"
#endif

typedef struct
{
  int ver;
  void *(*createInstance)();
  void (*destroyInstance)(void *);
  void (*set_rootdir)(void *, char *);
  void (*set_status)(void *,char *b, char *bufstr);
  void (*set_sample_fmt)(void *,int srate, int nch, int bps);
  void (*set_midi_ctx)(void *, double (*midi_sendrecv)(void *ctx, int action, double *ts, double *msg1, double *msg23), void *midi_ctxdata);
  void (*ui_init)(void *);
  void (*ui_run)(void *,int forcerun);
  void (*ui_quit)(void *);
  void (*osc_run)(void *,char *buf, int nbytes); // run this from your main thread, with the splbuf
  void (*jesus_process_samples)(void *,char *splbuf, int nbytes, double tempo, double playstate, double playpos, double playpos_b);

  void (*on_sounderr)(void *, int under, int over);

  void (*set_opts)(void *, int noquit, int nowriteout, int done); // -1 to not change
  void (*get_opts)(void *, int *noquit, int *nowriteout, int *done); // NULL to not query

  HWND (*ui_wnd_create)(void *);
  HWND (*ui_wnd_gethwnd)(void *);
  void (*ui_wnd_destroy)(void *);

  void (*preset_load)(void *, char *fn);
  void (*preset_save)(void *, char *fn);

  void (*knob_tweak)(void *, int wk, double amt, int isset);
  void (*trigger)(void *, int trigmask);


  // single effect mode
  void *(*sx_createInstance)(const char *dir_root, const char *effect_name);
  int (*sx_processSamples)(void *, double *buf, int cnt, int nch, int srate, double tempo, double playstate, double playpos, double playpos_b); // cnt is sample pairs
  HWND (*sx_createUI)(void *, HINSTANCE hDllInstance, HWND hwndParent, void *hostpostparam);
  HWND (*sx_getUIwnd)(void *);
  void (*sx_deleteUI)(void *);
  void (*sx_destroyInstance)(void *);
  const char *(*sx_getEffectName)(void *);
  void (*sx_loadState)(void *, const char *);
  void (*sx_saveState)(void *, char *buf, int bufout_l);

  int (*sx_getNumParms)(void *);      // these scale parms to 0..1
  double (*sx_getParmVal)(void *, int parm, double *minval, double *maxval);
  void (*sx_getParmName)(void *, int parm, char *name, int namelen);
  void (*sx_setParmVal)(void *, int parm, double val);
  void (*sx_set_midi_ctx)(void *, double (*midi_sendrecv)(void *ctx, int action, double *ts, double *msg1, double *msg23), void *midi_ctxdata);

  void (*setREAPERapi)(void *(*getFunc)(const char *name));
  
  int pad[440];//for future expansion

} jesusonicAPI;


#ifndef _WIN32
#undef HWND
#undef HINSTANCE
#endif

#endif
