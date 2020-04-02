#ifdef _WIN32
#include <windows.h>
#include <commctrl.h>
#else
#include "../../../WDL/swell/swell.h"
#endif
#include <math.h>
#include <stdio.h>
#include "../../aeffectx.h"
#include "../../../WDL/mutex.h"
#include "../../../WDL/fastqueue.h"
#include "../../../WDL/queue.h"
#include "../../../WDL/wdlcstring.h"

#include "../../reaper_plugin.h"
#define WDL_WIN32_HIDPI_IMPL
#include "../../../WDL/win32_hidpi.h" // for mmon SetWindowPos() tweaks

#include "resource.h"

#define VENDOR "Cockos"
#define EFFECT_NAME "ReaNINJAM"

#define SAMPLETYPE float


#define NUM_INPUTS 8
#define NUM_OUTPUTS 2

DWORD g_object_allocated;
void audiostream_onsamples(float **inbuf, int innch, float **outbuf, int outnch, int len, int srate, bool isPlaying, bool isSeek, double curpos) ;
void InitializeInstance();
void QuitInstance();


void (*format_timestr_pos)(double tpos, char *buf, int buflen, int modeoverride); // actually implemented in tracklist.cpp for now


audioMasterCallback g_hostcb;

#ifndef _WIN32
static HWND customControlCreator(HWND parent, const char *cname, int idx, const char *classname, int style, int x, int y, int w, int h);
#endif

#include "../../../WDL/db2val.h"

HANDLE * (*GetIconThemePointer)(const char *name);
HWND (*GetMainHwnd)();
double (*DB2SLIDER)(double);
double (*SLIDER2DB)(double);
void *(*CreateVorbisEncoder)(int srate, int nch, int serno, float qv, int cbr, int minbr, int maxbr);
void *(*CreateVorbisDecoder)();
void (*PluginWantsAlwaysRunFx)(int amt);
int (*GetWindowDPIScaling)(HWND hwnd);
#ifdef _WIN32
LRESULT (*handleCheckboxCustomDraw)(HWND, LPARAM, const unsigned short *list, int listsz, bool isdlg);
#endif
INT_PTR (*autoRepositionWindowOnMessage)(HWND hwnd, int msg, const char *desc_str, int flags); // flags unused currently
int (*GetPlayStateEx)(void *proj);
void (*OnPlayButtonEx)(void *proj);
void (*SetEditCurPos2)(void *proj, double time, bool moveview, bool seekplay);
void (*SetCurrentBPM)(void *proj, double bpm, bool wantUndo);
void (*GetSet_LoopTimeRange2)(void* proj, bool isSet, bool isLoop, double* startOut, double* endOut, bool allowautoseek);
int (*GetSetRepeatEx)(void* proj, int val);
double (*GetCursorPositionEx)();

void (*GetProjectPath)(char *buf, int bufsz);
const char *(*get_ini_file)();
BOOL	(WINAPI *InitializeCoolSB)(HWND hwnd);
HRESULT (WINAPI *UninitializeCoolSB)(HWND hwnd);
BOOL (WINAPI *CoolSB_SetVegasStyle)(HWND hwnd, BOOL active);
int	 (WINAPI *CoolSB_SetScrollInfo)(HWND hwnd, int fnBar, LPSCROLLINFO lpsi, BOOL fRedraw);
BOOL (WINAPI *CoolSB_GetScrollInfo)(HWND hwnd, int fnBar, LPSCROLLINFO lpsi);
int (WINAPI *CoolSB_SetScrollPos)(HWND hwnd, int nBar, int nPos, BOOL fRedraw);
int (WINAPI *CoolSB_SetScrollRange)(HWND hwnd, int nBar, int nMinPos, int nMaxPos, BOOL fRedraw);
BOOL (WINAPI *CoolSB_SetMinThumbSize)(HWND hwnd, UINT wBar, UINT size);
int (*plugin_register)(const char *name, void *infostruct);

int reaninjamAccelProc(MSG *msg, accelerator_register_t *ctx)
{
  extern HWND g_hwnd;
  if (g_hwnd && (msg->message == WM_KEYDOWN || msg->message == WM_KEYUP || msg->message == WM_CHAR) && msg->hwnd && (g_hwnd==msg->hwnd || IsChild(g_hwnd,msg->hwnd)))
  {
    HWND list = GetDlgItem(g_hwnd,IDC_CHATDISP);
    HWND e = GetDlgItem(g_hwnd,IDC_CHATENT);
    if (e)
    {
      bool didHit=false;
      if (msg->hwnd == e || IsChild(e,msg->hwnd))  
      {
        didHit=true;
      }
      else if (list && (msg->hwnd == list || IsChild(list,msg->hwnd)))
      {
        msg->hwnd = e;
        didHit=true;
      }
      if (didHit)
      {
#ifdef _WIN32
        if (msg->message == WM_CHAR && msg->wParam == VK_RETURN) 
#else
        if (msg->message == WM_KEYDOWN && msg->wParam == VK_RETURN) 
#endif
        {
          SendMessage(g_hwnd,WM_COMMAND,IDC_CHATOK,0);
          return 1;
        }

        return -1;
      }
    }
    return -1;
  }
  return 0;
}


HINSTANCE g_hInst;
class VSTEffectClass
{
public:
  VSTEffectClass(audioMasterCallback cb)
  {
    m_cb=cb;
    memset(&m_effect,0,sizeof(m_effect));
    m_samplerate=44100.0;
    m_hwndcfg=0;
    m_effect.magic = kEffectMagic;
    m_effect.dispatcher = staticDispatcher;
    m_effect.process = staticProcess;
    m_effect.getParameter = staticGetParameter;
    m_effect.setParameter = staticSetParameter;
    m_effect.numPrograms = 1;
    m_effect.numParams = 0;
    m_effect.numInputs=NUM_INPUTS;
    m_effect.numOutputs=NUM_OUTPUTS;

    m_effect.flags=effFlagsCanReplacing|effFlagsHasEditor;
    m_effect.processReplacing=staticProcessReplacing;//do nothing
    m_effect.uniqueID=CCONST('r','e','n','j');
    m_effect.version=1100;

    m_effect.object=this;
    m_effect.ioRatio=1.0;
    m_lasttransportpos=-100000000.0;
    m_lastplaytrackpos=-100000000.0;
  }

  audioMasterCallback m_cb;


  ~VSTEffectClass()
  {
    if (m_hwndcfg) DestroyWindow(m_hwndcfg);
  }  


  WDL_DLGRET CfgProc(HWND hwndDlg, UINT uMsg, WPARAM wParam, LPARAM lParam)
  {
    switch (uMsg)
    {
      case WM_INITDIALOG:
        m_hwndcfg=hwndDlg;

        ShowWindow(hwndDlg,SW_SHOWNA);
      case WM_USER+6606:

      return 0;
      case WM_TIMER:
      return 0;
      case WM_COMMAND:
        if (LOWORD(wParam)==IDC_BUTTON1)
        {
          extern HWND g_hwnd;
          if (g_hwnd) 
          {
            ShowWindow(g_hwnd,SW_SHOWNORMAL);
            SetForegroundWindow(g_hwnd); 
          }
        }
      return 0;
      case WM_DESTROY:
        m_hwndcfg=0;
      return 0;
    }
    return 0;
  }

  static WDL_DLGRET dlgProc(HWND hwndDlg, UINT uMsg, WPARAM wParam, LPARAM lParam)
  {
    if (uMsg == WM_INITDIALOG) SetWindowLongPtr(hwndDlg,GWLP_USERDATA,lParam);
    VSTEffectClass *_this = (VSTEffectClass *)GetWindowLongPtr(hwndDlg,GWLP_USERDATA);
    return _this->CfgProc(hwndDlg,uMsg,wParam,lParam);
  } 

  static VstIntPtr VSTCALLBACK staticDispatcher(AEffect *effect, VstInt32 opCode, VstInt32 index, VstIntPtr value, void *ptr, float opt)
  {
    VSTEffectClass *_this = (VSTEffectClass *)effect->object;
    switch (opCode)
    {
      case effCanDo:
        if (ptr && !strcmp((char *)ptr,"hasCockosViewAsConfig")) return 0xbeef0000;
        if (ptr && !strcmp((char *)ptr,"hasCockosExtensions")) return 0xbeef0000;
      return 0;
      case effStopProcess:
      case effMainsChanged:
        if (_this && !value) 
        {
        }
      return 0;
      case effSetBlockSize:

        // initialize, yo
        if (GetCurrentThreadId()==g_object_allocated) 
        {
          static int first;
          if (!first)
          {
            first=1;
            char buf[4096];
            GetModuleFileName(g_hInst,buf,sizeof(buf));
            LoadLibrary(buf);// keep us resident //-V530
#ifndef _WIN32
            SWELL_RegisterCustomControlCreator(customControlCreator);
#endif

            if (plugin_register)
            {
              static accelerator_register_t accel = { reaninjamAccelProc, TRUE};
              plugin_register("accelerator",&accel);
            }            
          }
          InitializeInstance();
        }

      return 0;
      case effSetSampleRate:
        if (_this) 
        {
          _this->m_samplerate=opt;
        }
      return 0;
      case effGetInputProperties:
      case effGetOutputProperties:
        if (_this && ptr)
        {
          if (index<0) return 0;

          VstPinProperties *pp=(VstPinProperties*)ptr;
          if (opCode == effGetInputProperties)
          {
            if (index >= _this->m_effect.numInputs) return 0;
            sprintf(pp->label,"Input %s",index&1?"R":"L");
          }
          else
          {
            if (index >= _this->m_effect.numOutputs) return 0;
            sprintf(pp->label,"Output %s",index&1?"R":"L");
          }
          pp->flags=0;
          pp->arrangementType=kSpeakerArrStereo;
          if (index==0||index==2)
            pp->flags|=kVstPinIsStereo;
          return 1;
        }
      return 0;
      case effGetPlugCategory: return kPlugCategEffect;
      case effGetEffectName:
        if (ptr) lstrcpyn((char*)ptr,EFFECT_NAME,32);
      return 0;

      case effGetProductString:
        if (ptr) lstrcpyn((char*)ptr,EFFECT_NAME,32);
      return 0;
      case effGetVendorString:
        if (ptr) lstrcpyn((char*)ptr,VENDOR,32);
      return 0;

      case effEditOpen:
        if (_this)
        {
          if (_this->m_hwndcfg) DestroyWindow(_this->m_hwndcfg);

          return !!CreateDialogParam(g_hInst,MAKEINTRESOURCE(IDD_VSTCFG),(HWND)ptr,dlgProc,(LPARAM)_this);
        }
      return 0;
      case effEditClose:
        if (_this && _this->m_hwndcfg) DestroyWindow(_this->m_hwndcfg);
      return 0;
      case effClose:
        QuitInstance();
        g_object_allocated=0;
        if (PluginWantsAlwaysRunFx) PluginWantsAlwaysRunFx(-1);
        delete _this;
      return 0;
      case effOpen:
      return 0;
      case effEditGetRect:
        if (_this)// && _this->m_hwndcfg) 
        {
          RECT r;
          if (_this->m_hwndcfg) GetClientRect(_this->m_hwndcfg,&r);
          else {r.left=r.top=0; r.right=400; r.bottom=300; }
          _this->cfgRect[0]=(short)r.top;
          _this->cfgRect[1]=(short)r.left;
          _this->cfgRect[2]=(short)r.bottom;
          _this->cfgRect[3]=(short)r.right;

          *(void **)ptr = _this->cfgRect;

          return 1;

        }
      return 0;
      case effIdentify:
        return CCONST ('N', 'v', 'E', 'f');    }
    return 0;
  }
  short cfgRect[4];
  
	static void VSTCALLBACK staticProcessReplacing(AEffect *effect, SAMPLETYPE **inputs, SAMPLETYPE **outputs, VstInt32 sampleframes)
  {
    VSTEffectClass *_this=(VSTEffectClass *)effect->object;
    if (_this)
    {
      VstTimeInfo *p=(VstTimeInfo *)g_hostcb(effect,audioMasterVendorSpecific,0xdeadbeef,audioMasterGetTime,NULL,0.0f);

      bool isPlaying=false;
      bool isSeek=false;

      // we call our extended audiomastergettime which just returns everything as seconds
      if (p) // &&  &&
           // )
      {
        isPlaying = !!(p->flags&(kVstTransportPlaying|kVstTransportRecording));


        if (isPlaying)
        {
          // if we've looped, and are before the start position (meaning we're a duped block)
          if ((p->flags&(kVstPpqPosValid|kVstCyclePosValid)) == (kVstPpqPosValid|kVstCyclePosValid) &&
              p->ppqPos < _this->m_lasttransportpos-0.001 && p->ppqPos < p->cycleStartPos-1.0/_this->m_samplerate+0.0000000001)
          {
            _this->m_lasttransportpos=p->ppqPos;
            // leave the output buffers as is (which should preserve them from the last time, we hope)
            return;
          }
          _this->m_lasttransportpos=p->ppqPos;

        }
        else _this->m_lasttransportpos=-100000000.0;

      }
      else _this->m_lasttransportpos=-100000000.0;


      if (_this->m_lasttransportpos <= _this->m_lastplaytrackpos || _this->m_lasttransportpos > _this->m_lastplaytrackpos + 0.5)
      {
        isSeek=true;
      }
      _this->m_lastplaytrackpos=_this->m_lasttransportpos;

      audiostream_onsamples(inputs,NUM_INPUTS,outputs,NUM_OUTPUTS,sampleframes,(int)(_this->m_samplerate+0.5),isPlaying,isSeek,_this->m_lastplaytrackpos);
    }
  }

  static void VSTCALLBACK staticProcess(AEffect *effect, float **inputs, float **outputs, VstInt32 sampleframes)
  {

  }


  static void VSTCALLBACK staticSetParameter(AEffect *effect, VstInt32 index, float parameter)
  {
  }

  static float VSTCALLBACK staticGetParameter(AEffect *effect, VstInt32 index)
  {
    return 0.0f;
  }

  HWND m_hwndcfg;
  double m_samplerate;
  AEffect m_effect;

  double m_lasttransportpos,m_lastplaytrackpos;

};


extern "C" {


#ifdef _WIN32
__declspec(dllexport) AEffect *VSTPluginMain(audioMasterCallback hostcb)
#else
  __attribute__ ((visibility ("default"))) AEffect *VSTPluginMain(audioMasterCallback hostcb)
#endif
{
  if (g_object_allocated) return 0;

  g_hostcb=hostcb;

  if (hostcb)
  {
    *((VstIntPtr *)&get_ini_file) = hostcb(NULL,0xdeadbeef,0xdeadf00d,0,(void*)"get_ini_file",0.0);
    *((VstIntPtr *)&plugin_register) = hostcb(NULL,0xdeadbeef,0xdeadf00d,0,(void*)"plugin_register",0.0);   
    *((VstIntPtr *)&GetProjectPath) = hostcb(NULL,0xdeadbeef,0xdeadf00d,0,(void*)"GetProjectPath",0.0);
    
    *((VstIntPtr *)&InitializeCoolSB) = hostcb(NULL,0xdeadbeef,0xdeadf00d,0,(void*)"InitializeCoolSB",0.0);
    *((VstIntPtr *)&UninitializeCoolSB) = hostcb(NULL,0xdeadbeef,0xdeadf00d,0,(void*)"UninitializeCoolSB",0.0);
    *((VstIntPtr *)&CoolSB_SetVegasStyle) = hostcb(NULL,0xdeadbeef,0xdeadf00d,0,(void*)"CoolSB_SetVegasStyle",0.0);
    *((VstIntPtr *)&CoolSB_SetScrollInfo) = hostcb(NULL,0xdeadbeef,0xdeadf00d,0,(void*)"CoolSB_SetScrollInfo",0.0);
    *((VstIntPtr *)&CoolSB_GetScrollInfo) = hostcb(NULL,0xdeadbeef,0xdeadf00d,0,(void*)"CoolSB_GetScrollInfo",0.0);
    *((VstIntPtr *)&CoolSB_SetScrollPos) = hostcb(NULL,0xdeadbeef,0xdeadf00d,0,(void*)"CoolSB_SetScrollPos",0.0);
    *((VstIntPtr *)&CoolSB_SetScrollRange) = hostcb(NULL,0xdeadbeef,0xdeadf00d,0,(void*)"CoolSB_SetScrollRange",0.0);
    *((VstIntPtr *)&CoolSB_SetMinThumbSize) = hostcb(NULL,0xdeadbeef,0xdeadf00d,0,(void*)"CoolSB_SetMinThumbSize",0.0);
    
    *(VstIntPtr *)&format_timestr_pos = hostcb(NULL,0xdeadbeef,0xdeadf00d,0,(void*)"format_timestr_pos",0.0);
    *(VstIntPtr *)&DB2SLIDER=hostcb(NULL,0xdeadbeef,0xdeadf00d,0,(void*)"DB2SLIDER",0.0);
    *(VstIntPtr *)&SLIDER2DB=hostcb(NULL,0xdeadbeef,0xdeadf00d,0,(void*)"SLIDER2DB",0.0);
    *(VstIntPtr *)&GetMainHwnd=hostcb(NULL,0xdeadbeef,0xdeadf00d,0,(void*)"GetMainHwnd",0.0);
    *(VstIntPtr *)&GetIconThemePointer=hostcb(NULL,0xdeadbeef,0xdeadf00d,0,(void*)"GetIconThemePointer",0.0);
    *(VstIntPtr *)&PluginWantsAlwaysRunFx=hostcb(NULL,0xdeadbeef,0xdeadf00d,0,(void*)"PluginWantsAlwaysRunFx",0.0);
    *(VstIntPtr *)&GetWindowDPIScaling = hostcb(NULL,0xdeadbeef,0xdeadf00d,0,(void*)"GetWindowDPIScaling",0.0);
    *(VstIntPtr *)&autoRepositionWindowOnMessage = hostcb(NULL,0xdeadbeef,0xdeadf00d,0,(void*)"autoRepositionWindowOnMessage",0.0);
    *(VstIntPtr *)&GetPlayStateEx = hostcb(NULL,0xdeadbeef,0xdeadf00d,0,(void*)"GetPlayStateEx",0.0);
    *(VstIntPtr *)&OnPlayButtonEx = hostcb(NULL,0xdeadbeef,0xdeadf00d,0,(void*)"OnPlayButtonEx",0.0);
    *(VstIntPtr *)&SetEditCurPos2 = hostcb(NULL,0xdeadbeef,0xdeadf00d,0,(void*)"SetEditCurPos2",0.0);
    *(VstIntPtr *)&SetCurrentBPM = hostcb(NULL,0xdeadbeef,0xdeadf00d,0,(void*)"SetCurrentBPM",0.0);
    *(VstIntPtr *)&GetSet_LoopTimeRange2 = hostcb(NULL,0xdeadbeef,0xdeadf00d,0,(void*)"GetSet_LoopTimeRange2",0.0);
    *(VstIntPtr *)&GetSetRepeatEx = hostcb(NULL,0xdeadbeef,0xdeadf00d,0,(void*)"GetSetRepeatEx",0.0);
    *(VstIntPtr *)&GetCursorPositionEx = hostcb(NULL,0xdeadbeef,0xdeadf00d,0,(void*)"GetCursorPositionEx",0.0);
#ifdef _WIN32
    *(VstIntPtr *)&handleCheckboxCustomDraw=hostcb(NULL,0xdeadbeef,0xdeadf00d,0,(void*)"handleCheckboxCustomDraw",0.0);
#endif
    
  }
  if (!GetMainHwnd || !GetIconThemePointer||!DB2SLIDER||!SLIDER2DB) return 0;

  if ((!CreateVorbisDecoder || !CreateVorbisEncoder) && GetMainHwnd())
  {
#define GETAPI(x) *(VstIntPtr *)&(x) = hostcb(NULL,0xdeadbeef,0xdeadf00d,0,(void *)#x,0.0);
    GETAPI(CreateVorbisDecoder)
    GETAPI(CreateVorbisEncoder)

    if (!CreateVorbisEncoder||!CreateVorbisDecoder) return 0;
  }

  g_object_allocated=GetCurrentThreadId();

  if (PluginWantsAlwaysRunFx) PluginWantsAlwaysRunFx(1);
  VSTEffectClass *obj = new VSTEffectClass(hostcb);
  if (obj)
    return &obj->m_effect;
  return 0;
}

BOOL WINAPI DllMain(HINSTANCE hDllInst, DWORD fdwReason, LPVOID res)
{
  if (fdwReason==DLL_PROCESS_ATTACH) 
  {
    g_hInst=hDllInst;
  }
  return TRUE;
}

};



#ifndef _WIN32 // MAC resources

#define SET_IDD_EMPTY_SCROLL_STYLE SWELL_DLG_FLAGS_AUTOGEN|SWELL_DLG_WS_CHILD
#define SET_IDD_EMPTY_STYLE SWELL_DLG_FLAGS_AUTOGEN|SWELL_DLG_WS_CHILD


#include "../../../WDL/swell/swell-dlggen.h"
#include "res.rc_mac_dlg"
#undef BEGIN
#undef END
#include "../../../WDL/swell/swell-menugen.h"
#include "res.rc_mac_menu"


static HWND customControlCreator(HWND parent, const char *cname, int idx, const char *classname, int style, int x, int y, int w, int h)
{
  if (!stricmp(classname,"RichEditChild"))
  {
    if ((style & 0x2800))
    {
      return SWELL_MakeEditField(idx,-x,-y,-w,-h,ES_READONLY|WS_VSCROLL|ES_MULTILINE);
    }
    else
      return SWELL_MakeEditField(idx,-x,-y,-w,-h,0);
  }
  return 0;
}

#endif
