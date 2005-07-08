#include "precomp.h"

#include <bfc/attrib/attribs.h>

#include "../audiostream_asio.h"
#include "../njclient.h"

#include "attrs.h"

#include "jesus.h"

#define MAX_JESUS_INST 32

jesusonicAPI *JesusonicAPI;  
HINSTANCE hDllInst;
String jesusdir;
String jesusonic_configfile;

int jesusAvailable() {
  return (JesusonicAPI != NULL);
}

void jesusInit() {
  jesusdir = jesus_install_dir;
  if (jesusdir.notempty())
  {
    jesusonic_configfile = jesusdir;
    jesusonic_configfile += "\\guiclient.jesusonicpreset";
    String dll;
    dll = jesusdir;
    dll += "\\jesus.dll";

    hDllInst = LoadLibrary(dll);
    if (!hDllInst) hDllInst = LoadLibrary(".\\jesus.dll"); // load from current dir
    if (hDllInst) 
    {
      *(void **)(&JesusonicAPI) = (void *)GetProcAddress(hDllInst,"JesusonicAPI");
      if (JesusonicAPI && JesusonicAPI->ver == JESUSONIC_API_VERSION_CURRENT)
      {
#if 0
        myInst=JesusonicAPI->createInstance();
        JesusonicAPI->set_rootdir(myInst,const_cast<char*>(jesusdir));
        JesusonicAPI->ui_init(myInst);
        JesusonicAPI->set_opts(myInst,1,1,-1);
ASSERT(g_audio != NULL);
        JesusonicAPI->set_sample_fmt(myInst,g_audio->m_srate,g_audio->m_nch,33);
        JesusonicAPI->set_status(myInst,"","ninjam embedded");
        HWND h=JesusonicAPI->ui_wnd_create(myInst);
//        ShowWindow(h,SW_SHOWNA);
        SetTimer(h,1,40,NULL);
        jesuswnd = h;

        JesusonicAPI->preset_load(myInst,jesusonic_configfile.Get());
#endif
      }
      else JesusonicAPI = 0;
    }
  }
  // end jesusonic init
}

void jesusShutdown() {
  ///// jesusonic stuff
#if 0
  if (myInst) 
  {
    JesusonicAPI->preset_save(myInst,jesusonic_configfile.Get());
    JesusonicAPI->ui_wnd_destroy(myInst);
    JesusonicAPI->set_opts(myInst,-1,-1,1);
    //WaitForSingleObject(hUIThread,1000);
    //CloseHandle(hUIThread);
    JesusonicAPI->ui_quit(myInst);
//    m_hwnd=0;

    JesusonicAPI->destroyInstance(myInst);
    myInst=0;
  }
#endif
  if (hDllInst) FreeLibrary(hDllInst);
  hDllInst=0;
  JesusonicAPI=0;
}

void jesusonic_processor(float *buf, int len, void *inst)
{
#ifdef _WIN32
  if (inst)
  {
    _controlfp(_RC_CHOP,_MCW_RC);
    JesusonicAPI->jesus_process_samples(inst,(char*)buf,len*sizeof(float));
    JesusonicAPI->osc_run(inst,(char*)buf,len);
  }
#endif
}

void deleteJesusonicProc(void *i, int chi)
{
#ifdef _WIN32
  if (JesusonicAPI && i)
  {
#if 0
      char buf[4096];
      sprintf(buf,"%s\\ninjam.p%02d",jesusdir?jesusdir:".",chi);
      JesusonicAPI->preset_save(i,buf);
#else
            StringPrintf presetname("%s\\ninjam.p%02d", jesusdir.v(), chi);
      JesusonicAPI->preset_save(i,presetname.ncv());
#endif
      JesusonicAPI->ui_wnd_destroy(i);
      JesusonicAPI->set_opts(i,-1,-1,1);
      JesusonicAPI->ui_quit(i);
      JesusonicAPI->destroyInstance(i);
  }
#endif
}

void attachInstToLocChan(int channel_id) {
int a = channel_id;
extern audioStreamer *g_audio;
          // start it up
          if (JesusonicAPI && g_audio)
          {
            void *myInst=JesusonicAPI->createInstance();
            JesusonicAPI->set_rootdir(myInst,jesusdir.ncv());
            JesusonicAPI->ui_init(myInst);
            JesusonicAPI->set_opts(myInst,1,1,-1);
            JesusonicAPI->set_sample_fmt(myInst,g_audio->m_srate,1,33);
            JesusonicAPI->set_status(myInst,"",StringPrintf("ninjam channel #%02d", a+1).ncv());
#if 0
            HWND h=JesusonicAPI->ui_wnd_create(myInst);
            ShowWindow(h,SW_SHOWNA);
            SetTimer(h,1,40,NULL);
            SetForegroundWindow(h);
#endif

#if 0
            char buf[4096];
            sprintf(buf,"%s\\ninjam.p%02d",jesusdir?jesusdir:".",a);
            JesusonicAPI->preset_load(myInst,buf);
#else
            StringPrintf presetname("%s\\ninjam.p%02d", jesusdir.v(), channel_id);
            JesusonicAPI->preset_load(myInst,presetname.ncv());
#endif


  extern NJClient *g_client;
            g_client->SetLocalChannelProcessor(a,jesusonic_processor,myInst);
          }
}
