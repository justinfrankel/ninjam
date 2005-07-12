#include <windows.h>
#include <commctrl.h>
#include <math.h>
#include <float.h>

#include "winclient.h"


// dB related utilities

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

double VAL2DB(double x)
{
  static double g_ilog2x6;
  static int a;
  if (!a)
  {
    a++;
    g_ilog2x6 = 6.0/log10(2.0);
  }
  double v=(log10(x)*g_ilog2x6);
  if (v < -120.0) v=-120.0;
  return v;
}

void mkvolpanstr(char *str, double vol, double pan)
{
  double v=VAL2DB(vol);
  if (vol < 0.0000001 || v < -120.0) v=-120.0;
    sprintf(str,"%s%2.1fdB ",v>0.0?"+":"",v);   
    if (fabs(pan) < 0.0001) strcat(str,"center");
    else sprintf(str+strlen(str),"%d%%%s", (int)fabs(pan*100.0),(pan>0.0 ? "R" : "L"));
}



/// jesusonic interfacing

void deleteJesusonicProc(void *i, int chi)
{
  if (JesusonicAPI && i)
  {
      char buf[4096];
      sprintf(buf,"%s\\ninjam.p%02d",jesusdir.Get()[0]?jesusdir.Get():".",chi);
      JesusonicAPI->preset_save(i,buf);
      JesusonicAPI->ui_wnd_destroy(i);
      JesusonicAPI->set_opts(i,-1,-1,1);
      JesusonicAPI->ui_quit(i);
      JesusonicAPI->destroyInstance(i);
  }
}


void jesusonic_processor(float *buf, int len, void *inst)
{
  if (inst)
  {
    _controlfp(_RC_CHOP,_MCW_RC);
    JesusonicAPI->jesus_process_samples(inst,(char*)buf,len*sizeof(float));
    JesusonicAPI->osc_run(inst,(char*)buf,len);
  }
}


void JesusUpdateInfo(void *myInst, char *chdesc)
{
  if (myInst)
  {
    JesusonicAPI->set_sample_fmt(myInst,g_audio?g_audio->m_srate:44100,1,33);
    WDL_String tmp("NINJAM embedded: ");
    tmp.Append(chdesc);
    JesusonicAPI->set_status(myInst,"",tmp.Get());
  }
}

int CreateJesusInstance(int a, char *chdesc)
{
  if (JesusonicAPI)
  {
    void *myInst=JesusonicAPI->createInstance();
    if (!myInst) return 0;
    JesusonicAPI->set_rootdir(myInst,jesusdir.Get());
    JesusonicAPI->ui_init(myInst);
    JesusonicAPI->set_opts(myInst,1,1,-1);

    JesusUpdateInfo(myInst,chdesc);

    char buf[4096];
    sprintf(buf,"%s\\ninjam.p%02d",jesusdir.Get()[0]?jesusdir.Get():".",a);

    JesusonicAPI->preset_load(myInst,buf);

    g_client_mutex.Enter();
    g_client->SetLocalChannelProcessor(a,jesusonic_processor,myInst);
    g_client_mutex.Leave();
    return 1;
  }
  return 0;
}


