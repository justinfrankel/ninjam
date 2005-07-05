#import "Controller.h"
#import "VUMeter.h"
#import "LocalListView.h"
#import "RemoteListView.h"
#import "IntervalProgressMeter.h"
#include "../njclient.h"
#include "../audiostream_mac.h"

NSLock *g_client_mutex;
audioStreamer *myAudio;
NJClient *g_client;

int g_audio_enable=0;
int g_thread_quit=0;
char *g_need_license;
int g_license_result;

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

WDL_String g_chat_needadd;

void addChatLine(char *src, char *text)
{
  WDL_String tmp;
  if (src && *src && !strncmp(text,"/me ",4))
  {
    tmp.Set("* ");
    tmp.Append(src);
    tmp.Append(" ");
    char *p=text+3;
    while (*p == ' ') p++;
    tmp.Append(p);
  }
  else
  {
   if (src&&*src)
   {
     tmp.Set("<");
     tmp.Append(src);
     tmp.Append("> ");
   }
   else if (src)
   {
     tmp.Set("*** ");
   }
   tmp.Append(text);
  }
  tmp.Append("\n");
  g_chat_needadd.Append(tmp.Get());
}


WDL_String g_topic;

void chatmsg_cb(int user32, NJClient *inst, char **parms, int nparms)
{
  if (!parms[0]) return;

  if (!strcmp(parms[0],"TOPIC"))
  {
    if (parms[2])
    {
      WDL_String tmp;
      if (parms[1] && *parms[1])
      {
        tmp.Set(parms[1]);
        tmp.Append(" sets topic to: ");
      }
      else tmp.Set("Topic is: ");
      tmp.Append(parms[2]);

      g_topic.Set(parms[2]);
      addChatLine("",tmp.Get());
    
    }
  }
  else if (!strcmp(parms[0],"MSG"))
  {
    if (parms[1] && parms[2])
      addChatLine(parms[1],parms[2]);
  } 
  else if (!strcmp(parms[0],"PRIVMSG"))
  {
    if (parms[1] && parms[2])
    {
      WDL_String tmp;
      tmp.Set("*");
      tmp.Append(parms[1]);
      tmp.Append("* ");
      tmp.Append(parms[2]);
      addChatLine(NULL,tmp.Get());
    }
  } 
  else if (!strcmp(parms[0],"JOIN") || !strcmp(parms[0],"PART"))
  {
    if (parms[1] && *parms[1])
    {
      WDL_String tmp(parms[1]);
      tmp.Append(" has ");
      tmp.Append(parms[0][0]=='P' ? "left" : "joined");
      tmp.Append(" the server");
      addChatLine("",tmp.Get());
    }
  } 
}

int licensecallback(int user32, char *licensetext)
{
g_need_license=licensetext;
g_license_result=0;
[g_client_mutex unlock];
while (g_need_license)
{
  struct timespec ts={0,10000*1000};
  nanosleep(&ts,NULL);
}
[g_client_mutex lock];
return g_license_result;
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
double DB2VAL(double x) { return (pow(2.0,(x)/6.0)); }
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
  g_client_mutex = [[NSLock alloc] init];
  
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
  
  #if 0
  // for now just always have one channel, heh
  g_client->SetLocalChannelInfo(0,"channel0",true,0,false,0,true,true);
  g_client->SetLocalChannelMonitoring(0,false,0.0f,false,0.0f,false,false,false,false);
  [loclv newChannel:0];

  g_client->SetLocalChannelInfo(1,"channel1",true,1,false,0,true,false);
  g_client->SetLocalChannelMonitoring(1,false,0.0f,false,0.0f,false,false,false,false);
  [loclv newChannel:1];

  #endif
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
  
  [NSThread detachNewThreadSelector:@selector(threadFunc:) toTarget:self withObject:self]; 
  
  if (!timer)
     timer=[NSTimer scheduledTimerWithTimeInterval:0.1 target:self selector:@selector(tick:) userInfo:0 repeats:YES];
}

- (void)threadFunc:(id)obj
{
  while (!g_thread_quit)
  {
    [g_client_mutex lock];
    while (!g_thread_quit && !g_client->Run());
    [g_client_mutex unlock];
    struct timespec ts={0,1000*1000};
    nanosleep(&ts,NULL);
  }
}

- (BOOL)validateMenuItem:(id)sender
{
  if (sender == menudisconnect) return !!g_audio_enable;
  if (sender == menuconnect) return !g_audio_enable;
  return TRUE;
}


- (void)applicationWillTerminate:(NSNotification *)notification
{
  g_thread_quit=1;
  delete myAudio;
  myAudio=0;  

  g_audio_enable=0;
  [g_client_mutex release];
  g_client_mutex=0;
  
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

- (IBAction)ldlg_ok:(id)sender
{
  [NSApp stopModalWithCode:1];
  [licensedlg close];
}
- (IBAction)ldlg_cancel:(id)sender
{
  [NSApp stopModalWithCode:0];
  [licensedlg close];
}

- (IBAction)tick:(id)sender
{

if (g_need_license)
{
  [licensedlg_text setString:[(NSString *)CFStringCreateWithCString(NULL,g_need_license,kCFStringEncodingUTF8) autorelease]];
  g_license_result=[NSApp runModalForWindow:(NSWindow *)licensedlg];
  g_need_license=0;
}

[g_client_mutex lock];
if (g_client->HasUserInfoChanged())
{
  [g_client_mutex unlock];
  [remlv updateList];
  [g_client_mutex lock];
}

double d=g_client->GetOutputPeak();
d=VAL2DB(d);
if (!g_audio_enable) d=-120.0;
[mastervumeter setDoubleValue:d];
[loclv runVUMeters];
[remlv runVUMeters];

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
    [status setStringValue:[NSString stringWithFormat:@"Status: Connected to %s as %s",g_client->GetHostName(),g_client->GetUserName()]];

  if (ns < 0)
  {
  delete myAudio;
  myAudio=0;  

  g_audio_enable=0;
  }  
}

int intp, intl;
int pos, len;
g_client->GetPosition(&pos,&len);
if (!len) len=1;
intl=g_client->GetBPI();
intp = (pos * intl)/len;

NSString *needadd=NULL;
if (g_chat_needadd.Get()[0])
{
  needadd = [(NSString *)CFStringCreateWithCString(NULL,g_chat_needadd.Get(),kCFStringEncodingUTF8) autorelease];
  g_chat_needadd.Set("");
}


[g_client_mutex unlock];

if (needadd)
{
  [chat_text setString:[[chat_text string] stringByAppendingString:needadd]];
}

[progmet setVal:intp length:intl];
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
   
    g_client->ChatMessage_Callback=chatmsg_cb;
    g_client->ChatMessage_User32 = (int)self;
  
    char buf[1024];
    int cnt=0;
    for (;;)
    {
      sprintf(buf,"/tmp/njsession_%d_%d",time(NULL),cnt);

      if (!mkdir(buf,0700)) break;

      cnt++;
    }
    
  [g_client_mutex lock];   
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
   
  [g_client_mutex unlock]; 

  }
}

- (IBAction)chat_enter:(id)sender
{
    char str[1024];
    [[sender stringValue] getCString:(char *)str maxLength:(unsigned)(sizeof(str)-1)];
    if (str[0])
    {
    
      [g_client_mutex lock]; 


                    if (str[0] == '/')
                    {
                      if (!strncasecmp(str,"/me ",4))
                      {
                        g_client->ChatMessage_Send("MSG",str);
                      }
                      else if (!strncasecmp(str,"/topic ",7)||
                               !strncasecmp(str,"/kick ",6) ||                        
                               !strncasecmp(str,"/bpm ",5) ||
                               !strncasecmp(str,"/bpi ",5)
                        ) // alias to /admin *
                      {
                        g_client->ChatMessage_Send("ADMIN",str+1);
                      }
                      else if (!strncasecmp(str,"/admin ",7))
                      {
                        char *p=str+7;
                        while (*p == ' ') p++;
                        g_client->ChatMessage_Send("ADMIN",p);
                      }
                      else if (!strncasecmp(str,"/msg ",5))
                      {
                        char *p=str+5;
                        while (*p == ' ') p++;
                        char *n=p;
                        while (*p && *p != ' ') p++;
                        if (*p == ' ') *p++=0;
                        while (*p == ' ') p++;
                        if (*p)
                        {
                          g_client->ChatMessage_Send("PRIVMSG",n,p);
                          WDL_String tmp;
                          tmp.Set("-> *");
                          tmp.Append(n);
                          tmp.Append("* ");
                          tmp.Append(p);
                          addChatLine(NULL,tmp.Get());
                        }
                        else
                        {
                          addChatLine("","error: /msg requires a username and a message.");
                        }
                      }
                      else
                      {
                        addChatLine("","error: unknown command.");
                      }
                    }
                    else
                    {
                      g_client->ChatMessage_Send("MSG",str);
                    }


      [g_client_mutex unlock]; 
    }
    [sender setStringValue:@""];

}

- (IBAction)ondisconnect:(id)sender
{

  delete myAudio;
  myAudio=0;  

  [g_client_mutex lock];
  g_client->Disconnect();
  [g_client_mutex unlock];
  
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
