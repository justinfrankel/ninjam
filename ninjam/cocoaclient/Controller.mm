/*
    NINJAM Cocoa Client - controller.mm
    Copyright (C) 2005 Cockos Incorporated

    NINJAM is free software; you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation; either version 2 of the License, or
    (at your option) any later version.

    NINJAM is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU General Public License for more details.

    You should have received a copy of the GNU General Public License
    along with NINJAM; if not, write to the Free Software
    Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
*/


#import "Controller.h"
#import "VUMeter.h"
#import "LocalListView.h"
#import "RemoteListView.h"
#import "IntervalProgressMeter.h"
#include "../njclient.h"
#include "../njmisc.h"
#include "../audiostream.h"
#include "../../WDL/dirscan.h"


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
        if (parms[2][0])
        {
          tmp.Append(" sets topic to: ");
          tmp.Append(parms[2]);
        }
        else
        {
          tmp.Append(" removes topic.");
        }  
      }
      else
      {
        if (parms[2][0])
        {
          tmp.Set("Topic is: ");
          tmp.Append(parms[2]);
        }
        else tmp.Set("No topic is set.");
      }

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
     [NSNumber numberWithInt:1], @"savelocal",
     [NSNumber numberWithInt:0], @"savelocalwav",
     @"~/Documents", @"sessiondir",
     [NSNumber numberWithInt:0], @"savewave",
     [NSNumber numberWithInt:0], @"saveogg",
     [NSNumber numberWithInt:128], @"saveoggbr",
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
    
  if (a)
  {
    [cdlg_pass setHidden:TRUE];
    [cdlg_passlabel setHidden:TRUE];  
    [cdlg_passsave setHidden:TRUE];  
  }  
  [cdlg_passsave setIntValue:[[NSUserDefaults standardUserDefaults] integerForKey:@"passsave"]];

  
  #if 0
  // for now just always have one channel, heh
  g_client->SetLocalChannelInfo(0,"channel0",true,0,false,0,true,true);
  g_client->SetLocalChannelMonitoring(0,false,0.0f,false,0.0f,false,false,false,false);
  [loclv newChannel:0];

  g_client->SetLocalChannelInfo(1,"channel1",true,1,false,0,true,false);
  g_client->SetLocalChannelMonitoring(1,false,0.0f,false,0.0f,false,false,false,false);
  [loclv newChannel:1];

  #endif
  
  NSArray *locchlist=[[NSUserDefaults standardUserDefaults] objectForKey:@"locchlist"];
  if (locchlist)
  {
    int i;
    for (i = 0 ; i < [locchlist count]; i ++)
    {
      NSArray *chinfo=[locchlist objectAtIndex:i];
      if (chinfo && [chinfo count] >= 2)
      {
        int nitems=[chinfo count];
        int sch=0;
        bool bc=true;
        bool m=false,s=false;
        float v=0.0;
        float p=0.0;
        int ch = [[chinfo objectAtIndex:0] intValue];
        if (ch < 0 || ch >= MAX_LOCAL_CHANNELS) continue;
        
        char name[512];
        [[chinfo objectAtIndex:1] getCString:(char *)name maxLength:(unsigned)(sizeof(name)-1)];
        
        if (nitems > 2) sch=[[chinfo objectAtIndex:2] intValue];
        if (nitems > 3) bc=[[chinfo objectAtIndex:3] boolValue];
        if (nitems > 4) v=[[chinfo objectAtIndex:4] floatValue];
        if (nitems > 5) p=[[chinfo objectAtIndex:5] floatValue];
        if (nitems > 6) m=[[chinfo objectAtIndex:6] boolValue];
        if (nitems > 7) s=[[chinfo objectAtIndex:7] boolValue];
      
        g_client->SetLocalChannelInfo(ch,name,true,sch,false,0,true,bc);
        g_client->SetLocalChannelMonitoring(ch,true,v,true,p,true,m,true,s);
        [loclv newChannel:ch];
      }
    }
  }
  
  
  g_client->config_mastervolume=[[NSUserDefaults standardUserDefaults] floatForKey:@"mastervol"];
  g_client->config_masterpan=[[NSUserDefaults standardUserDefaults] floatForKey:@"masterpan"];
  g_client->config_mastermute=[[NSUserDefaults standardUserDefaults] integerForKey:@"mastermute"];
  g_client->config_metronome=[[NSUserDefaults standardUserDefaults] floatForKey:@"metrovol"];
  g_client->config_metronome_pan=[[NSUserDefaults standardUserDefaults] floatForKey:@"metropan"];
  g_client->config_metronome_mute=[[NSUserDefaults standardUserDefaults] integerForKey:@"metromute"];
  
   
    g_client->ChatMessage_Callback=chatmsg_cb;
    g_client->ChatMessage_User32 = (int)self;
  
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

  struct timespec ts={0,100000*1000};
  nanosleep(&ts,NULL);

  //save local channel state
  {
    NSMutableArray *chlist=[[NSMutableArray alloc] init];
    int i=0;
    for (;;)
    {
      int ch=g_client->EnumLocalChannels(i++);
      if (ch < 0) break;
            
      int sch=0;
      bool bc=0;
      char *lcn;
      float v=0.0f,p=0.0f;
      bool m=0,s=0;
      
      lcn=g_client->GetLocalChannelInfo(ch,&sch,NULL,&bc);
      g_client->GetLocalChannelMonitoring(ch,&v,&p,&m,&s);
      
      if (lcn)
      {
        NSString *label=(NSString *)CFStringCreateWithCString(NULL,lcn,kCFStringEncodingUTF8);
        NSMutableArray *thisch = [[NSMutableArray alloc] init];

        [thisch addObject:[NSNumber numberWithInt:ch]];

        [thisch addObject:label];        
        [label release];
        
        [thisch addObject:[NSNumber numberWithInt:sch]];
        [thisch addObject:[NSNumber numberWithBool:bc]];
        [thisch addObject:[NSNumber numberWithFloat:v]];
        [thisch addObject:[NSNumber numberWithFloat:p]];
        [thisch addObject:[NSNumber numberWithBool:m]];
        [thisch addObject:[NSNumber numberWithBool:s]];
        
      
        [chlist addObject:thisch];
        [thisch release];
      }
    }


    [[NSUserDefaults standardUserDefaults] setObject:chlist forKey:@"locchlist"];
    [chlist release];
  }


  [self ondisconnect:0];
  
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

  if (ns < 0)
  {
    [g_client_mutex unlock];
    [self ondisconnect:0];
    [g_client_mutex lock];
  }  

  
  if (ns == NJClient::NJC_STATUS_DISCONNECTED)
    [status setStringValue:@"Status: disconnected from host."];
  if (ns == NJClient::NJC_STATUS_INVALIDAUTH)
    [status setStringValue:@"Status: invalid authentication info."];
  if (ns == NJClient::NJC_STATUS_CANTCONNECT)
    [status setStringValue:@"Status: can't connect to host."];
  if (ns == NJClient::NJC_STATUS_OK)
    [status setStringValue:[NSString stringWithFormat:@"Status: Connected to %s as %s",g_client->GetHostName(),g_client->GetUserName()]];

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
  NSString *tmp=[[chat_text string] stringByAppendingString:needadd];
  [chat_text setString:tmp];
  [chat_text scrollRangeToVisible:NSMakeRange([tmp length],0)];
}

[progmet setVal:intp length:intl bpm:g_client->GetActualBPM()];
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

  [self ondisconnect:0];
  

 if ([NSApp runModalForWindow:(NSWindow *)cdlg])
 {
    // get connect info from cdlg_*
    char srv[512],user[512],pass[512];
    [[cdlg_srv stringValue] getCString:(char *)srv maxLength:(unsigned)(sizeof(srv)-1)];
    [[cdlg_user stringValue] getCString:(char *)user maxLength:(unsigned)(sizeof(user)-1)];
    [[cdlg_pass stringValue] getCString:(char *)pass maxLength:(unsigned)(sizeof(pass)-1)];
  
    if (![cdlg_passsave intValue]) [cdlg_pass setStringValue:@""];
    
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

  
    char buf[1024];
    int cnt=0;
    char fmt[1024];
    [[[NSUserDefaults standardUserDefaults] stringForKey:@"sessiondir"] getCString:fmt maxLength:(sizeof(fmt)-1)];
    
    while (cnt < 16)
    {
      time_t tv;
      time(&tv);
      struct tm *t=localtime(&tv);
    
      buf[0]=0;
      if (fmt[0] == '~' && (fmt[1] == '/' || !fmt[1]))
      {
        strcpy(buf,getenv("HOME"));
        strcat(buf,fmt+1);
      }
      else
      {
        strcpy(buf,fmt);
      }
      sprintf(buf+strlen(buf),"/%04d%02d%02d_%02d%02d",
          t->tm_year+1900,t->tm_mon+1,t->tm_mday,t->tm_hour,t->tm_min);

      if (cnt) sprintf(buf+strlen(buf),"_%d",cnt);
      strcat(buf,".ninjam");

      if (!mkdir(buf,0700)) break;

      cnt++;
    }
    if (cnt >= 16)
    {      
      [status setStringValue:@"Status: ERROR CREATING SESSION DIRECTORY"];
      return;
    }
  
  char tmp[512];
  char *d=tmp;
  tmp[0]=0;

  NSString *indev=[[NSUserDefaults standardUserDefaults] stringForKey:@"indevs"];
  NSString *outdev=[[NSUserDefaults standardUserDefaults] stringForKey:@"outdevs"];

  if (indev && outdev)
  {
    [indev getCString:tmp maxLength:254];
    strcat(tmp,",");
    [outdev getCString:(tmp+strlen(tmp)) maxLength:254];
  }
  
   audioStreamer *p=create_audioStreamer_CoreAudio(&d,44100,2,16,audiostream_onsamples);

  if (!p)
  {
    [status setStringValue:@"Status: error initializing sound hardware"];
  }
  else
  {

  myAudio=p;
    
  [g_client_mutex lock];   
  g_client->SetWorkDir(buf);

  g_client->config_savelocalaudio=0;
  if ([[NSUserDefaults standardUserDefaults] integerForKey:@"savelocal"])
  {
    g_client->config_savelocalaudio|=1;
    if ([[NSUserDefaults standardUserDefaults] integerForKey:@"savelocalwav"])
      g_client->config_savelocalaudio|=2;
  }
  if ([[NSUserDefaults standardUserDefaults] integerForKey:@"savewave"])
  {
    WDL_String wf;
    wf.Set(g_client->GetWorkDir());
    wf.Append("output.wav");
    g_client->waveWrite = new WaveWriter(wf.Get(),24,myAudio->m_outnch>1?2:1,myAudio->m_srate);
  }
  
  if ([[NSUserDefaults standardUserDefaults] integerForKey:@"saveogg"])
  {
    WDL_String wf;
    wf.Set(g_client->GetWorkDir());
    wf.Append("output.ogg");
    g_client->SetOggOutFile(fopen(wf.Get(),"ab"),myAudio->m_srate,myAudio->m_outnch>1?2:1,[[NSUserDefaults standardUserDefaults] integerForKey:@"saveoggbr"]);
  }
  
  if (g_client->config_savelocalaudio)
  {
    WDL_String lf;
    lf.Set(g_client->GetWorkDir());
    lf.Append("clipsort.log");
    g_client->SetLogFile(lf.Get());
  }


    g_client->Connect(srv,userstr.Get(),passstr.Get());
    g_audio_enable=1;
    [status setStringValue:@"Status: Connecting..."];
       
  [g_client_mutex unlock]; 

  }
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
  
  
  WDL_String sessiondir(g_client->GetWorkDir()); // save a copy of the work dir before we blow it away
  
  g_client->Disconnect();
  delete g_client->waveWrite;
  g_client->waveWrite=0;
  g_client->SetWorkDir(NULL);
  g_client->SetOggOutFile(NULL,0,0,0);
  g_client->SetLogFile(NULL);
  
  if (sessiondir.Get()[0])
  {
    addChatLine("","Disconnected from server");
    if (!g_client->config_savelocalaudio)
    {
      int n;
      for (n = 0; n < 16; n ++)
      {
        WDL_String s(sessiondir.Get());
        char buf[32];
        sprintf(buf,"%x",n);
        s.Append(buf);
    
        {
          WDL_DirScan ds;
          if (!ds.First(s.Get()))
          {
            do
            {
              if (ds.GetCurrentFN()[0] != '.')
              {
                WDL_String t;
                ds.GetCurrentFullFN(&t);
                unlink(t.Get());          
              }
            }
            while (!ds.Next());
          }
        }
        rmdir(s.Get());
      }
      rmdir(sessiondir.Get());
    }
    
  }
  
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


  int sp=!![cdlg_passsave intValue];
  [[NSUserDefaults standardUserDefaults] setInteger:sp forKey:@"passsave"];
  
  if (sp)
    [[NSUserDefaults standardUserDefaults] setObject:[cdlg_pass stringValue] forKey:@"pass"];
  else
    [[NSUserDefaults standardUserDefaults] setObject:@"" forKey:@"pass"];
    
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
  [cdlg_passsave setHidden:a];
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
    [[NSUserDefaults standardUserDefaults] setObject:[[adlg_in selectedItem] title] forKey:@"indevs"];
    [[NSUserDefaults standardUserDefaults] setObject:[[adlg_out selectedItem] title] forKey:@"outdevs"];    
  }
  
  [[NSUserDefaults standardUserDefaults] setObject:[[[adlg contentView] viewWithTag:1] objectValue] forKey:@"savewave"];    
  [[NSUserDefaults standardUserDefaults] setObject:[[[adlg contentView] viewWithTag:2] objectValue] forKey:@"saveogg"];    
  [[NSUserDefaults standardUserDefaults] setObject:[[[adlg contentView] viewWithTag:3] objectValue] forKey:@"saveoggbr"];    
  [[NSUserDefaults standardUserDefaults] setObject:[[[adlg contentView] viewWithTag:4] objectValue] forKey:@"savelocal"];    
  [[NSUserDefaults standardUserDefaults] setObject:[[[adlg contentView] viewWithTag:5] objectValue] forKey:@"savelocalwav"];    
  [[NSUserDefaults standardUserDefaults] setObject:[[[adlg contentView] viewWithTag:6] objectValue] forKey:@"sessiondir"];    

  
  
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

  NSString *indev, *outdev;
  
  indev=[[NSUserDefaults standardUserDefaults] stringForKey:@"indevs"];
  outdev=[[NSUserDefaults standardUserDefaults] stringForKey:@"outdevs"];
  
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
  
  [[[adlg contentView] viewWithTag:1] setIntValue:[[NSUserDefaults standardUserDefaults] integerForKey:@"savewave"]];
  [[[adlg contentView] viewWithTag:2] setIntValue:[[NSUserDefaults standardUserDefaults] integerForKey:@"saveogg"]];
  [[[adlg contentView] viewWithTag:3] setIntValue:[[NSUserDefaults standardUserDefaults] integerForKey:@"saveoggbr"]];
  [[[adlg contentView] viewWithTag:4] setIntValue:[[NSUserDefaults standardUserDefaults] integerForKey:@"savelocal"]];
  [[[adlg contentView] viewWithTag:5] setIntValue:[[NSUserDefaults standardUserDefaults] integerForKey:@"savelocalwav"]];
  [[[adlg contentView] viewWithTag:6] setStringValue:[[NSUserDefaults standardUserDefaults] stringForKey:@"sessiondir"]];
  
  
  
  [NSApp runModalForWindow:(NSWindow *)adlg];
  
}

@end
