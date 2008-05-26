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

#include "../../reaper_plugin.h"

#include "../../midi_io.h"

#include "resource.h"

#define VENDOR "Cockos"
#define EFFECT_NAME "ReaNINJAM"

#define SAMPLETYPE float


#define NUM_INPUTS 8
#define NUM_OUTPUTS 2

#define NUM_PARAMS 1
#define PARM_COMP 0

#define USE_BOOL -1000.0
#define USE_DB -10000.0
typedef struct
{
   double parm_maxval;
   char *name;
   char *label;
   double defparm;
   double minval; // can be USE_DB,USE_BOOL
   double maxval;
   unsigned short ui_id_slider, ui_id_lbl;
   int precisiondigits;
   double slidershape;
} parameterInfo; 

DWORD g_object_allocated;
void audiostream_onsamples(float **inbuf, int innch, float **outbuf, int outnch, int len, int srate, bool isPlaying, bool isSeek, double curpos) ;
void InitializeInstance();
void QuitInstance();


void (*format_timestr_pos)(double tpos, char *buf, int buflen, int modeoverride); // actually implemented in tracklist.cpp for now


static double sliderscale_sq(double in, int dir, double n)
{
  if (dir < 0) return (pow((double) in,n)/pow(1000.0,n-1.0));
  return (pow(in * pow(1000.0,n-1),1/n));
}

static parameterInfo param_infos[NUM_PARAMS]=
{
  0,
};

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
void (*RemoveXPStyle)(HWND hwnd, int rem);

BOOL	(WINAPI *InitializeCoolSB)(HWND hwnd);
HRESULT (WINAPI *UninitializeCoolSB)(HWND hwnd);
BOOL (WINAPI *CoolSB_SetVegasStyle)(HWND hwnd, BOOL active);
int	 (WINAPI *CoolSB_SetScrollInfo)(HWND hwnd, int fnBar, LPSCROLLINFO lpsi, BOOL fRedraw);
BOOL (WINAPI *CoolSB_GetScrollInfo)(HWND hwnd, int fnBar, LPSCROLLINFO lpsi);
int (WINAPI *CoolSB_SetScrollPos)(HWND hwnd, int nBar, int nPos, BOOL fRedraw);
int (WINAPI *CoolSB_SetScrollRange)(HWND hwnd, int nBar, int nMinPos, int nMaxPos, BOOL fRedraw);
BOOL (WINAPI *CoolSB_SetMinThumbSize)(HWND hwnd, UINT wBar, UINT size);

double NormalizeParm(int parm, double val)
{
  if (parm < 0 || parm >= NUM_PARAMS) return 0.0;

  if (param_infos[parm].minval == USE_DB) return DB2VAL(val); // don't clip
  else if (param_infos[parm].minval == USE_BOOL) val=val>=0.5?1.0:0.0;
  else val=(val-param_infos[parm].minval)/(param_infos[parm].maxval-param_infos[parm].minval);

  if (val<0.0) return 0.0;
  if (val>1.0) return 1.0;
  return val;
}

double DenormalizeParm(int parm, double val)
{
  if (parm < 0 || parm >= NUM_PARAMS) return 0.0;

  if (param_infos[parm].minval == USE_DB) return VAL2DB(val);
  if (param_infos[parm].minval == USE_BOOL) return val >= 0.5?1.0:0.0;

  val /= param_infos[parm].parm_maxval;

  return val*(param_infos[parm].maxval-param_infos[parm].minval) + param_infos[parm].minval;
}

static void format_parm(int parm, double val, char *ptr)
{
  if (parm < 0 || parm >= NUM_PARAMS) { *ptr=0; return; }

  char tmp[32];
  sprintf(tmp,"%s%%.%df",(param_infos[parm].minval == USE_DB && val >= 1.0)?"+":"",param_infos[parm].precisiondigits);
  sprintf(ptr,tmp,DenormalizeParm(parm,val));
}

int g_refcnt;

HINSTANCE g_hInst;
class VSTEffectClass
{
public:
  VSTEffectClass(audioMasterCallback cb)
  {
    m_cb=cb;
    int x;
    for (x = 0; x < NUM_PARAMS; x ++)
    {
      m_parms[x]=NormalizeParm(x,param_infos[x].defparm)*param_infos[x].parm_maxval;
    }
    memset(&m_effect,0,sizeof(m_effect));
    m_samplerate=44100.0;
    m_hwndcfg=0;
    configChanged=0;
    m_effect.magic = kEffectMagic;
    m_effect.dispatcher = staticDispatcher;
    m_effect.process = staticProcess;
    m_effect.getParameter = staticGetParameter;
    m_effect.setParameter = staticSetParameter;
    m_effect.numPrograms=1;
    m_effect.numParams = NUM_PARAMS;
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

    if (!g_refcnt++)
    {
    #ifndef _WIN32
      SWELL_RegisterCustomControlCreator(customControlCreator);
      if (g_hostcb)SWELL_RegisterCustomControlCreator((SWELL_ControlCreatorProc)g_hostcb(NULL,0xdeadbeef,0xdeadf00d,0,(void*)"Mac_CustomControlCreator",0.0));      
    #endif
    }

    onParmChange();

    Reset();
  }

  audioMasterCallback m_cb;

  int configChanged;

  ~VSTEffectClass()
  {
    if (m_hwndcfg) DestroyWindow(m_hwndcfg);
    if (!--g_refcnt)
    {
    #ifndef _WIN32
      SWELL_UnregisterCustomControlCreator(customControlCreator);
      if (g_hostcb)SWELL_UnregisterCustomControlCreator((SWELL_ControlCreatorProc)g_hostcb(NULL,0xdeadbeef,0xdeadf00d,0,(void*)"Mac_CustomControlCreator",0.0));
    #endif
    }
  }  

  int delayAdjust()
  {
    int ret=(int) DenormalizeParm(PARM_COMP,m_parms[PARM_COMP]);
    if (ret<0)ret=0;
    if (ret>200000) ret=200000;
    return ret;

    
  }

  void Reset()
  {
    m_effect.initialDelay = 0;
  }

  void onParmChange()
  {
    //WDL_MutexLock m(&m_mutex);

  }
  void ProcessEvents(VstEvents *eventlist)
  {
//    if (eventlist && eventlist->numEvents)
  //    m_pelist=eventlist;
  }

  WDL_Queue m_cfgchunk;
  int GetChunk(void **ptr)
  {
    m_cfgchunk.Clear();    
    *ptr = m_cfgchunk.Get();
    return m_cfgchunk.Available();

  }

  void SetChunk(void *ptr, int size)
  {
  }


  BOOL WINAPI CfgProc(HWND hwndDlg, UINT uMsg, WPARAM wParam, LPARAM lParam)
  {
    static int m_ignore_editmsg;
    switch (uMsg)
    {
      case WM_INITDIALOG:
        m_hwndcfg=hwndDlg;

        {
          int x;
          for (x = 0; x < NUM_PARAMS; x ++)
          {
            if (param_infos[x].minval != USE_BOOL)
            {
              if (param_infos[x].ui_id_slider)
              {
                SetWindowLong(GetDlgItem(hwndDlg,param_infos[x].ui_id_slider),0, 1);
                if (param_infos[x].minval == USE_DB)
                {
                  SendDlgItemMessage(hwndDlg,param_infos[x].ui_id_slider,TBM_SETTIC,0,-1);
                }
                else
                {
                  double dp=NormalizeParm(x,param_infos[x].defparm)*1000.0;

                  if (param_infos[x].slidershape>1.0) dp=sliderscale_sq(dp,1,param_infos[x].slidershape);

                  if (dp<0)dp=0;
                  else if (dp>1000)dp=1000;
                  SendDlgItemMessage(hwndDlg,param_infos[x].ui_id_slider,TBM_SETTIC,0,(LPARAM)(dp+0.5));
                }
              }
            }
          }

        }

        ShowWindow(hwndDlg,SW_SHOWNA);
        configChanged=~0;
      case WM_USER+6606:

        m_ignore_editmsg++;
        {
          int cc=configChanged;
          configChanged=0;
          int x;
          for (x = 0; x < NUM_PARAMS; x ++)
          {
            if (cc & (1<<x))
            {
              if (param_infos[x].minval==USE_BOOL)
              {
                CheckDlgButton(hwndDlg,param_infos[x].ui_id_slider,m_parms[x]>=0.5 ? BST_CHECKED:BST_UNCHECKED);
              }
              else
              {
                if (param_infos[x].ui_id_lbl)
                {
                  char buf[512];
                  format_parm(x,m_parms[x],buf);
                  SetDlgItemText(hwndDlg,param_infos[x].ui_id_lbl,buf);
                }
                if (param_infos[x].ui_id_slider)
                {
                  double val;
                  if (param_infos[x].minval == USE_DB) val=(DB2SLIDER(VAL2DB(m_parms[x])));
                  else if (param_infos[x].slidershape>1.0) 
                    val=sliderscale_sq(m_parms[x]*1000.0/param_infos[x].parm_maxval,1,param_infos[x].slidershape);
                  else val=(m_parms[x]*1000.0)/param_infos[x].parm_maxval;
                  SendDlgItemMessage(hwndDlg,param_infos[x].ui_id_slider,TBM_SETPOS,0,(LPARAM)val);
                }
              }
            }
          }

  
        }
        m_ignore_editmsg--;

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
        if (!m_ignore_editmsg && HIWORD(wParam) == CBN_SELCHANGE)
        {
          int v=SendDlgItemMessage(hwndDlg,LOWORD(wParam),CB_GETCURSEL,0,0);
          if (v != CB_ERR)
          {
            v=SendDlgItemMessage(hwndDlg,LOWORD(wParam),CB_GETITEMDATA,v,0);
          }
        }
        if (!m_ignore_editmsg && HIWORD(wParam) == BN_CLICKED)
        {
          int x;
          for (x = 0; x < NUM_PARAMS; x ++)
          {
            if (param_infos[x].minval == USE_BOOL && LOWORD(wParam) == param_infos[x].ui_id_slider)
            {
              m_parms[x]=IsDlgButtonChecked(hwndDlg,LOWORD(wParam))?1.0:0.0;
              onParmChange();
              if (g_hostcb) g_hostcb(&m_effect,audioMasterAutomate,x,0,NULL,(float)m_parms[x]);
            }
          }
        }
        if (!m_ignore_editmsg && HIWORD(wParam) == EN_CHANGE)
        {
          char buf[512];
          GetDlgItemText(hwndDlg,LOWORD(wParam),buf,sizeof(buf));
          double f=atof(buf);
          int x;
          for (x = 0; x < NUM_PARAMS; x ++)
          {
            if (param_infos[x].ui_id_lbl && param_infos[x].ui_id_lbl == LOWORD(wParam) && param_infos[x].minval != USE_BOOL)
            {
              m_parms[x]=NormalizeParm(x,f)*param_infos[x].parm_maxval;
              if (param_infos[x].ui_id_slider)
              {
                double val;
                if (param_infos[x].minval == USE_DB) val=(DB2SLIDER(VAL2DB(m_parms[x])));
                else if (param_infos[x].slidershape>1.0) val=sliderscale_sq((m_parms[x]*1000.0)/param_infos[x].parm_maxval,1,param_infos[x].slidershape);
                else val=(m_parms[x]*1000.0)/param_infos[x].parm_maxval;
                SendDlgItemMessage(hwndDlg,param_infos[x].ui_id_slider,TBM_SETPOS,0,(LPARAM)val);
              }
              onParmChange();
              if (g_hostcb) g_hostcb(&m_effect,audioMasterAutomate,x,0,NULL,(float)m_parms[x]);
              break;
            }
          }
        }
 
      return 0;
      case WM_HSCROLL:
      case WM_VSCROLL:
        {
          char buf[512];
          int pos=SendMessage((HWND)lParam,TBM_GETPOS,0,0);
          m_ignore_editmsg++;

          int x;
          for (x = 0; x < NUM_PARAMS; x ++)
          {
            if (param_infos[x].ui_id_slider && param_infos[x].minval != USE_BOOL && (HWND)lParam == GetDlgItem(hwndDlg,param_infos[x].ui_id_slider))
            {
              if (param_infos[x].minval == USE_DB) m_parms[x]=(float)(DB2VAL(SLIDER2DB((double)pos)));
              else if (param_infos[x].slidershape>1.0) m_parms[x]=sliderscale_sq(pos,-1,param_infos[x].slidershape)/1000.0 * param_infos[x].parm_maxval;
              else m_parms[x]=pos/1000.0 * param_infos[x].parm_maxval;


              if (param_infos[x].ui_id_lbl)
              {
                format_parm(x,m_parms[x],buf);
                SetDlgItemText(hwndDlg,param_infos[x].ui_id_lbl,buf);
              }


              onParmChange();
              if (g_hostcb) 
              {
                if (LOWORD(wParam)==SB_ENDSCROLL) g_hostcb(&m_effect,audioMasterEndEdit,x,0,NULL,0.0f);
                else g_hostcb(&m_effect,audioMasterAutomate,x,0,NULL,(float)m_parms[x]);
              }
              break;
            }
          }
          m_ignore_editmsg--;
        }
      return 0;
      case WM_DESTROY:
        m_hwndcfg=0;
      return 0;
    }
    return 0;
  }

  static BOOL WINAPI dlgProc(HWND hwndDlg, UINT uMsg, WPARAM wParam, LPARAM lParam)
  {
    if (uMsg == WM_INITDIALOG) SetWindowLong(hwndDlg,GWL_USERDATA,lParam);
    VSTEffectClass *_this = (VSTEffectClass *)GetWindowLong(hwndDlg,GWL_USERDATA);
    return _this->CfgProc(hwndDlg,uMsg,wParam,lParam);
  } 

  static long VSTCALLBACK staticDispatcher(AEffect *effect, long opCode, long index, long value, void *ptr, float opt)
  {
    VSTEffectClass *_this = (VSTEffectClass *)effect->object;
    switch (opCode)
    {
      case effCanDo:
        if (ptr && !strcmp((char *)ptr,"hasCockosViewAsConfig")) return 0xbeef0000;
        if (ptr && !strcmp((char *)ptr,"hasCockosExtensions")) return 0xbeef0000;
        if (ptr)
        {
          if (//!stricmp((char*)ptr,"sendVstEvents") || 
              !stricmp((char*)ptr,"sendVstMidiEvent") || 
              !stricmp((char*)ptr,"receiveVstEvents") || 
              !stricmp((char*)ptr,"receiveVstMidiEvent"))
            return 1;
        }
      return 0;
      case effMainsChanged:
        if (_this && !value) 
        {
          WDL_MutexLock m(&_this->m_mutex);
          _this->Reset();
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
            LoadLibrary(buf);// keep us resident
          }
          InitializeInstance();
        }

      return 0;
      case effSetSampleRate:
        if (_this) 
        {
          WDL_MutexLock m(&_this->m_mutex);
          _this->m_samplerate=opt;
          _this->onParmChange();
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

          return !!CreateDialogParam(g_hInst,MAKEINTRESOURCE(IDD_VSTCFG),(HWND)ptr,dlgProc,(long)_this);
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
      case effProcessEvents:
        if (ptr && _this)
          _this->ProcessEvents((VstEvents*)ptr);
      return 0;
      case effVendorSpecific:
        if (ptr && _this && index == 0xdeadbeef+1)
        {
          if (value>=0 && value<NUM_PARAMS) 
          {
            double *buf=(double *)ptr;  buf[0]=0.0;
            if (param_infos[value].minval == USE_DB) buf[1]=2.0; else buf[1]=param_infos[value].parm_maxval;
            return 0xbeef;
          }
        }
        else if (ptr && _this && index == effGetParamDisplay)
        {
          if (value>=0 && value<NUM_PARAMS) { format_parm(value,opt,(char *)ptr); return 0xbeef; }
          else *(char *)ptr = 0;          
        }
      return 0;
      case effGetParamName:
        if (ptr)
        {
          if (index>=0 && index<NUM_PARAMS) lstrcpyn((char *)ptr,param_infos[index].name,9);
          else *(char *)ptr = 0;
        }
      return 0;

      case effGetParamLabel:
        if (ptr)
        {
          if (index>=0 && index<NUM_PARAMS) lstrcpyn((char *)ptr,param_infos[index].label,9);
          else *(char *)ptr = 0;
        }
      return 0;

      case effGetParamDisplay:
        if (ptr&&_this)
        {
          if (index>=0 && index<NUM_PARAMS) format_parm(index,_this->m_parms[index],(char *)ptr);
          else *(char *)ptr = 0;
        }
      return 0;
      case effEditGetRect:
        if (_this)// && _this->m_hwndcfg) 
        {
          RECT r;
          if (_this->m_hwndcfg) GetClientRect(_this->m_hwndcfg,&r);
          else {r.left=r.top=0; r.right=400; r.bottom=300; }
          _this->cfgRect[0]=r.top;
          _this->cfgRect[1]=r.left;
          _this->cfgRect[2]=r.bottom;
          _this->cfgRect[3]=r.right;

          *(void **)ptr = _this->cfgRect;

          return 1;

        }
      return 0;
      case effSetChunk:
        if (_this) 
        {
          _this->SetChunk(ptr,value);
          return 1;
        }
      return 0;
      case effGetChunk:
        if (_this) return _this->GetChunk((void **)ptr);
      return 0;
      case effIdentify:
        return CCONST ('N', 'v', 'E', 'f');    }
    return 0;
  }
  short cfgRect[4];
  
	static void VSTCALLBACK staticProcessReplacing(AEffect *effect, SAMPLETYPE **inputs, SAMPLETYPE **outputs, long sampleframes)
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

	static void VSTCALLBACK staticProcess(AEffect *effect, float **inputs, float **outputs, long sampleframes)
  {

  }


	static void VSTCALLBACK staticSetParameter(AEffect *effect, long index, float parameter)
  {
    VSTEffectClass *_this = (VSTEffectClass *)effect->object;
    if (!_this || index < 0 || index >= NUM_PARAMS) return;
    WDL_MutexLock m(&_this->m_mutex);
    _this->m_parms[index]=parameter;    
    _this->onParmChange();
    _this->configChanged|=1<<index;
  }

	static float VSTCALLBACK staticGetParameter(AEffect *effect, long index)
  {
    VSTEffectClass *_this = (VSTEffectClass *)effect->object;
    if (!_this || index < 0 || index >= NUM_PARAMS) return 0.0;
    WDL_MutexLock m(&_this->m_mutex);
    return (float)_this->m_parms[index];
  }

  HWND m_hwndcfg;
  double m_samplerate;
  AEffect m_effect;
  double m_parms[NUM_PARAMS];
  WDL_Mutex m_mutex;

  double m_lasttransportpos,m_lastplaytrackpos;

};


extern "C" {


#ifdef _WIN32
  __declspec(dllexport) AEffect *main(audioMasterCallback hostcb)
#else
  __attribute__ ((visibility ("default"))) AEffect *VSTPluginMain(audioMasterCallback hostcb)
#endif
{
  if (g_object_allocated) return 0;

  g_hostcb=hostcb;

  if (hostcb)
  {
    *((long *)&InitializeCoolSB) = hostcb(NULL,0xdeadbeef,0xdeadf00d,0,(void*)"InitializeCoolSB",0.0);
    *((long *)&UninitializeCoolSB) = hostcb(NULL,0xdeadbeef,0xdeadf00d,0,(void*)"UninitializeCoolSB",0.0);
    *((long *)&CoolSB_SetVegasStyle) = hostcb(NULL,0xdeadbeef,0xdeadf00d,0,(void*)"CoolSB_SetVegasStyle",0.0);
    *((long *)&CoolSB_SetScrollInfo) = hostcb(NULL,0xdeadbeef,0xdeadf00d,0,(void*)"CoolSB_SetScrollInfo",0.0);
    *((long *)&CoolSB_GetScrollInfo) = hostcb(NULL,0xdeadbeef,0xdeadf00d,0,(void*)"CoolSB_GetScrollInfo",0.0);
    *((long *)&CoolSB_SetScrollPos) = hostcb(NULL,0xdeadbeef,0xdeadf00d,0,(void*)"CoolSB_SetScrollPos",0.0);
    *((long *)&CoolSB_SetScrollRange) = hostcb(NULL,0xdeadbeef,0xdeadf00d,0,(void*)"CoolSB_SetScrollRange",0.0);
    *((long *)&CoolSB_SetMinThumbSize) = hostcb(NULL,0xdeadbeef,0xdeadf00d,0,(void*)"CoolSB_SetMinThumbSize",0.0);
    
    *(long *)&format_timestr_pos = hostcb(NULL,0xdeadbeef,0xdeadf00d,0,(void*)"format_timestr_pos",0.0);
    *(long *)&DB2SLIDER=hostcb(NULL,0xdeadbeef,0xdeadf00d,0,(void*)"DB2SLIDER",0.0);
    *(long *)&SLIDER2DB=hostcb(NULL,0xdeadbeef,0xdeadf00d,0,(void*)"SLIDER2DB",0.0);
    *(long *)&GetMainHwnd=hostcb(NULL,0xdeadbeef,0xdeadf00d,0,(void*)"GetMainHwnd",0.0);
    *(long *)&GetIconThemePointer=hostcb(NULL,0xdeadbeef,0xdeadf00d,0,(void*)"GetIconThemePointer",0.0);
    *(long *)&PluginWantsAlwaysRunFx=hostcb(NULL,0xdeadbeef,0xdeadf00d,0,(void*)"PluginWantsAlwaysRunFx",0.0);
#ifdef _WIN32
    *(long *)&RemoveXPStyle=hostcb(NULL,0xdeadbeef,0xdeadf00d,0,(void*)"RemoveXPStyle",0.0);
#endif
    
  }
  if (!GetMainHwnd || !GetIconThemePointer||!DB2SLIDER||!SLIDER2DB) return 0;

  if (!CreateVorbisDecoder || !CreateVorbisEncoder)
  {
#define GETAPI(x) *(long *)&(x) = hostcb(NULL,0xdeadbeef,0xdeadf00d,0,(void *)#x,0.0);
    GETAPI(CreateVorbisDecoder)
    GETAPI(CreateVorbisEncoder)

    if (!CreateVorbisEncoder||!CreateVorbisDecoder)
    {
      // if main window not created, then plugins arent loaded, 
      // and this means its just a quick scan, so safe to create anyway
      if (GetMainHwnd()) return 0; 
    }
  }

  g_object_allocated=GetCurrentThreadId();

  if (PluginWantsAlwaysRunFx) PluginWantsAlwaysRunFx(1);
  VSTEffectClass *obj = new VSTEffectClass(hostcb);
  if (obj)
    return &obj->m_effect;
  return 0;
}

#ifdef _WIN32
BOOL WINAPI DllMain(HINSTANCE hDllInst, DWORD fdwReason, LPVOID res)
{
  if (fdwReason==DLL_PROCESS_ATTACH) 
  {
    g_hInst=hDllInst;
  }
  return TRUE;
}
#endif

};



#ifndef _WIN32 // MAC resources

#define SS_ETCHEDHORZ 0
#define SS_ETCHEDVERT 0


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
      return __SWELL_MakeEditField(idx,-x,-y,-w,-h,ES_READONLY|WS_VSCROLL);          
    }
    else
      return __SWELL_MakeEditField(idx,-x,-y,-w,-h,0);          
  }
  return 0;
}

#endif