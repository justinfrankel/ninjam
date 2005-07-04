#import "Controller.h"
#import "VUMeter.h"
#import "LocalListView.h"

#include "../njclient.h"
#include "../audiostream_mac.h"


audioStreamer *myAudio;
NJClient *g_client;

int g_audio_enable=0;

void audiostream_onsamples(float **inbuf, int innch, float **outbuf, int outnch, int len, int srate) 
{ 
  if (!g_audio_enable) 
  {
    int x;
    // clear all output buffers
    for (x = 0; x < outnch; x ++) memset(outbuf[x],0,sizeof(float)*len);
    return;
  }
  g_client->AudioProc(inbuf,innch, outbuf, outnch, len,srate);
}

int licensecallback(int user32, char *licensetext)
{
return 1;
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

double VAL2DB(double x)
{
  double g_ilog2x6 = 6.0/log10(2.0);

  double v=(log10(x)*g_ilog2x6);
  if (v < -120.0) v=-120.0;
  return v;
}
#define DB2VAL(x) (pow(2.0,(x)/6.0))
void mkvolpanstr(char *str, double vol, double pan)
{
  double v=VAL2DB(vol);
  if (vol < 0.0000001 || v < -120.0) v=-120.0;
    sprintf(str,"%s%2.1fdB ",v>=0.0?"+":"",v);   
    if (fabs(pan) < 0.0001) strcat(str,"center");
    else sprintf(str+strlen(str),"%d%%%s", (int)fabs(pan*100.0),(pan>0.0 ? "R" : "L"));
}

@implementation Controller

- (void)awakeFromNib
{  
  [NSApp setDelegate:self];
  g_client = new NJClient;
  
  
  
  g_client->LicenseAgreementCallback=licensecallback;
 
  [[NSUserDefaults standardUserDefaults] registerDefaults:[NSDictionary dictionaryWithObjectsAndKeys:
     [NSNumber numberWithFloat:1.0], @"mastervol",
     [NSNumber numberWithFloat:0.0], @"masterpan",
     [NSNumber numberWithBool:NO], @"mastermute",
     [NSNumber numberWithBool:NO], @"metromute",
     [NSNumber numberWithFloat:0.5], @"metrovol",
     [NSNumber numberWithFloat:0.0], @"metropan",  
     [NSNumber numberWithBool:NO], @"anon",
     @"", @"indevs",
     @"", @"outdevs",
  NULL]];


  NSString *s=[[NSUserDefaults standardUserDefaults] stringForKey:@"host"];
  if (s) [cdlg_srv setStringValue:s];
  s=[[NSUserDefaults standardUserDefaults] stringForKey:@"user"];
  if (s) [cdlg_user setStringValue:s];
  s=[[NSUserDefaults standardUserDefaults] stringForKey:@"pass"];
  if (s) [cdlg_pass setStringValue:s];
  int a=[[NSUserDefaults standardUserDefaults] integerForKey:@"anon"];
  [cdlg_anon setIntValue:a];
  
  indev=[[NSUserDefaults standardUserDefaults] stringForKey:@"indevs"];
  outdev=[[NSUserDefaults standardUserDefaults] stringForKey:@"outdevs"];
  
  if (a)
  {
    [cdlg_pass setHidden:TRUE];
    [cdlg_passlabel setHidden:TRUE];  
  }  
  
  // for now just always have one channel, heh
  g_client->SetLocalChannelInfo(0,"channel0",true,0,false,0,true,true);
  g_client->SetLocalChannelMonitoring(0,false,0.0f,false,0.0f,false,false,false,false);
  [loclv newChannel:0];

  g_client->SetLocalChannelInfo(1,"channel1",true,1,false,0,true,false);
  g_client->SetLocalChannelMonitoring(1,false,0.0f,false,0.0f,false,false,false,false);
  [loclv newChannel:1];

  g_client->config_mastervolume=[[NSUserDefaults standardUserDefaults] floatForKey:@"mastervol"];
  g_client->config_masterpan=[[NSUserDefaults standardUserDefaults] floatForKey:@"masterpan"];
  g_client->config_mastermute=[[NSUserDefaults standardUserDefaults] integerForKey:@"mastermute"];
  g_client->config_metronome=[[NSUserDefaults standardUserDefaults] floatForKey:@"metrovol"];
  g_client->config_metronome_pan=[[NSUserDefaults standardUserDefaults] floatForKey:@"metropan"];
  g_client->config_metronome_mute=[[NSUserDefaults standardUserDefaults] integerForKey:@"metromute"];
  
  [mastermute setIntValue:g_client->config_mastermute];
  [metromute setIntValue:g_client->config_metronome_mute];
  [mastervol setFloatValue:DB2SLIDER(VAL2DB(g_client->config_mastervolume))];

  [metrovol setFloatValue:DB2SLIDER(VAL2DB(g_client->config_metronome))];
//  [metrovol setFloatValue:50.0];
  [masterpan setFloatValue:(g_client->config_masterpan)];
  [metropan setFloatValue:(g_client->config_metronome_pan)];
  
      
  [self updateMasterIndicators];
  
}

- (BOOL)validateMenuItem:(id)sender
{
  if (sender == menudisconnect) return !!g_audio_enable;
  if (sender == menuconnect) return !g_audio_enable;
  return TRUE;
}


- (void)applicationWillTerminate:(NSNotification *)notification
{
  delete myAudio;
  myAudio=0;  

  g_audio_enable=0;
  delete g_client;
  g_client=0;
}

- (void)updateMasterIndicators
{
   char buf[512];
   mkvolpanstr(buf,g_client->config_mastervolume,g_client->config_masterpan);
   [mastervoldisp setStringValue:[(NSString *)CFStringCreateWithCString(NULL,buf,kCFStringEncodingUTF8) autorelease]];
   mkvolpanstr(buf,g_client->config_metronome,g_client->config_metronome_pan);
   [metrovoldisp setStringValue:[(NSString *)CFStringCreateWithCString(NULL,buf,kCFStringEncodingUTF8) autorelease]];
}

- (IBAction)tick:(id)sender
{
while (!g_client->Run());

static int a;
if (a ++ > 10)
{
double d=g_client->GetOutputPeak();
d=VAL2DB(d);
[mastervumeter setDoubleValue:d];
[loclv runVUMeters];
a=0;

int ns=g_client->GetStatus();
if (ns != m_laststatus)
{
  m_laststatus=ns;
  
  if (ns == NJClient::NJC_STATUS_DISCONNECTED)
    [status setStringValue:@"Status: disconnected from host."];
  if (ns == NJClient::NJC_STATUS_INVALIDAUTH)
    [status setStringValue:@"Status: invalid authentication info."];
  if (ns == NJClient::NJC_STATUS_CANTCONNECT)
    [status setStringValue:@"Status: can't connect to host."];
  if (ns == NJClient::NJC_STATUS_OK)
    [status setStringValue:@"Status: Connected"];

  if (ns < 0)
  {
  delete myAudio;
  myAudio=0;  

  g_audio_enable=0;
  }  
}

}
}

- (IBAction)mastermute:(NSButton *)sender
{
  g_client->config_mastermute = !![sender intValue];
  
  [[NSUserDefaults standardUserDefaults] setInteger:g_client->config_mastermute forKey:@"mastermute"];
  
}

- (IBAction)masterpan:(NSSlider *)sender
{
   double pos=[sender doubleValue];
   if (fabs(pos) < 0.08) pos=0.0;
   g_client->config_masterpan=pos;
   [[NSUserDefaults standardUserDefaults] setFloat:g_client->config_masterpan forKey:@"masterpan"];

  [self updateMasterIndicators];
}

- (IBAction)mastervol:(NSSlider *)sender
{
   double pos=[sender doubleValue];
   double v=DB2VAL(SLIDER2DB(pos));
   
   g_client->config_mastervolume=v;
   [[NSUserDefaults standardUserDefaults] setFloat:g_client->config_mastervolume forKey:@"mastervol"];

  [self updateMasterIndicators];
}

- (IBAction)metromute:(NSButton *)sender
{
  g_client->config_metronome_mute = !![sender intValue];
  [[NSUserDefaults standardUserDefaults] setInteger:g_client->config_metronome_mute forKey:@"metromute"];
}

- (IBAction)metropan:(NSSlider *)sender
{
   double pos=[sender doubleValue];
   if (fabs(pos) < 0.08) pos=0.0;
   g_client->config_metronome_pan=pos;
   [[NSUserDefaults standardUserDefaults] setFloat:g_client->config_metronome_pan forKey:@"metropan"];

  [self updateMasterIndicators];
}

- (IBAction)metrovol:(NSSlider *)sender
{
   double pos=[sender doubleValue];
   double v=DB2VAL(SLIDER2DB(pos));
   
   g_client->config_metronome=v;
   [[NSUserDefaults standardUserDefaults] setFloat:g_client->config_metronome forKey:@"metrovol"];

  [self updateMasterIndicators];
}

- (IBAction)onconnect:(id)sender
{
  delete myAudio;
  myAudio=0;  

  g_audio_enable=0;  

 if ([NSApp runModalForWindow:(NSWindow *)cdlg])
 {
    // get connect info from cdlg_*
    char srv[512],user[512],pass[512];
    [[cdlg_srv stringValue] getCString:(char *)srv maxLength:(unsigned)(sizeof(srv)-1)];
    [[cdlg_user stringValue] getCString:(char *)user maxLength:(unsigned)(sizeof(user)-1)];
    [[cdlg_pass stringValue] getCString:(char *)pass maxLength:(unsigned)(sizeof(pass)-1)];
    int anon=[cdlg_anon intValue];
    
    WDL_String userstr,passstr;
    if (anon)
    {
      userstr.Set("anonymous:");
      userstr.Append(user);
    }
    else
    {
      userstr.Set(user);
      passstr.Set(pass);
    }
   
 // g_client->ChatMessage_Callback=chatmsg_cb;
  
    char buf[1024];
    int cnt=0;
    for (;;)
    {
      sprintf(buf,"/tmp/njsession_%d_%d",time(NULL),cnt);

      if (!mkdir(buf,0700)) break;

      cnt++;
    }
    
    g_client->SetWorkDir(buf);
    g_client->Connect(srv,userstr.Get(),passstr.Get());
    g_audio_enable=1;
    [status setStringValue:@"Status: Connecting..."];
    
   audioStreamer_CoreAudio *p=new audioStreamer_CoreAudio;
  
  char tmp[512];
  char *d=tmp;
  tmp[0]=0;
  if (indev && outdev)
  {
    [indev getCString:tmp maxLength:254];
    strcat(tmp,",");
    [outdev getCString:(tmp+strlen(tmp)) maxLength:254];
  }
  p->Open(&d,44100,2,16);
  
  myAudio=p;
   
   if (!timer)
     timer=[NSTimer scheduledTimerWithTimeInterval:0.01 target:self selector:@selector(tick:) userInfo:0 repeats:YES];

  }
}


- (IBAction)ondisconnect:(id)sender
{

  delete myAudio;
  myAudio=0;  

  g_audio_enable=0;
  
  [status setStringValue:@"Status: Disconnected"];
    
}

- (IBAction)cdlg_ok:(id)sender
{
  [NSApp stopModalWithCode:1];
  [cdlg close];
  
  [[NSUserDefaults standardUserDefaults] setObject:[cdlg_srv stringValue] forKey:@"host"];
  [[NSUserDefaults standardUserDefaults] setObject:[cdlg_user stringValue] forKey:@"user"];
  [[NSUserDefaults standardUserDefaults] setObject:[cdlg_pass stringValue] forKey:@"pass"];
  [[NSUserDefaults standardUserDefaults] setInteger:[cdlg_anon intValue] forKey:@"anon"];
}

- (IBAction)cdlg_cancel:(id)sender
{
  [NSApp stopModalWithCode:0];
  [cdlg close];
}

- (IBAction)cdlg_anon:(NSButton *)sender
{  
  // enable other controls based on state of sender
  BOOL a=!![sender intValue];
  [cdlg_pass setHidden:a];
  [cdlg_passlabel setHidden:a];
}

- (IBAction)adlg_oncancel:(id)sender
{
  [NSApp stopModal];
  [adlg close];
}

- (IBAction)adlg_onclose:(id)sender
{
  [NSApp stopModal];
  [adlg close];
  
  // see which devices the user selected
  if (m_devlist && m_devlist_len)
  {
    indev = [[adlg_in selectedItem] title];
    [[NSUserDefaults standardUserDefaults] setObject:indev forKey:@"indevs"];
    outdev = [[adlg_out selectedItem] title];
    [[NSUserDefaults standardUserDefaults] setObject:outdev forKey:@"outdevs"];    
  }
}

- (IBAction)adlg_insel:(NSPopUpButton *)sender
{
}

- (IBAction)adlg_outsel:(NSPopUpButton *)sender
{
}

- (IBAction)onaudiocfg:(id)sender
{ 
  [adlg_in removeAllItems];
  [adlg_out removeAllItems];

  AudioDeviceID idin,idout;
  UInt32 theSize=sizeof(AudioDeviceID);
  AudioHardwareGetProperty(kAudioHardwarePropertyDefaultInputDevice,&theSize,&idin);
  theSize=sizeof(AudioDeviceID);
  AudioHardwareGetProperty(kAudioHardwarePropertyDefaultOutputDevice,&theSize,&idout);
  
  if (m_devlist) { free(m_devlist); m_devlist=0; m_devlist_len=0; }
	int s = AudioHardwareGetPropertyInfo(kAudioHardwarePropertyDevices, &theSize, NULL ); 
  int theNumberDevices = theSize / sizeof(AudioDeviceID); 
  if (theNumberDevices)
  {
    m_devlist_len=theNumberDevices;
    m_devlist=(AudioDeviceID *)malloc(sizeof(AudioDeviceID)*theNumberDevices);
    AudioHardwareGetProperty(kAudioHardwarePropertyDevices,&theSize,m_devlist);
    for (s = 0; s < theNumberDevices; s ++)
    {
      AudioDeviceID myDev;
      myDev = m_devlist[s];
      UInt32 os=0; 
      Boolean ow;
      AudioDeviceGetPropertyInfo(myDev,0,0,kAudioDevicePropertyDeviceName,&os,&ow);
      if (os > 0)
      {
        char *buf=(char *)malloc(os+1);
        AudioDeviceGetProperty(myDev,0,0,kAudioDevicePropertyDeviceName,&os,buf);
        if (os > 0) 
        {
          UInt32 os=0; 
          Boolean ow;
          AudioDeviceGetPropertyInfo(myDev,0,1,kAudioDevicePropertyStreamConfiguration,&os,&ow);
          if (os>=sizeof(AudioBufferList))
          {
             AudioBufferList *buf2=(AudioBufferList *)malloc(os);
             AudioDeviceGetProperty(myDev,0,1,kAudioDevicePropertyStreamConfiguration,&os,buf2);
             if (os>=sizeof(AudioBufferList)) 
             {
                NSString *t=[(NSString *)CFStringCreateWithCString(NULL,buf,kCFStringEncodingUTF8) autorelease];
                [adlg_in addItemWithTitle:t];
                if (!indev && idin == myDev) indev=t;
             }
             free(buf2);
          }
          AudioDeviceGetPropertyInfo(myDev,0,0,kAudioDevicePropertyStreamConfiguration,&os,&ow);
          if (os>=sizeof(AudioBufferList))
          {
             AudioBufferList *buf2=(AudioBufferList *)malloc(os);
             AudioDeviceGetProperty(myDev,0,0,kAudioDevicePropertyStreamConfiguration,&os,buf2);
             if (os>=sizeof(AudioBufferList)) 
             {
                NSString *t=[(NSString *)CFStringCreateWithCString(NULL,buf,kCFStringEncodingUTF8) autorelease];
                [adlg_out addItemWithTitle:t];
                if (!outdev && idout == myDev) outdev=t;
             }
             free(buf2);
          }
        }
        free(buf);
      }
    }
  }
  
  if (indev) [adlg_in selectItemWithTitle:indev];
  if (outdev) [adlg_out selectItemWithTitle:outdev];
    
  // populate list of devices
  
  [NSApp runModalForWindow:(NSWindow *)adlg];
  
}

@end
