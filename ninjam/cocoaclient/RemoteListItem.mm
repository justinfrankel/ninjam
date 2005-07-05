#import "RemoteListItem.h"
#import "VUMeter.h"


#include "../njclient.h"
extern NJClient *g_client;
extern double DB2VAL(double x);
extern double VAL2DB(double x);
extern double SLIDER2DB(double y);
extern double DB2SLIDER(double x);
extern void mkvolpanstr(char *str, double vol, double pan);


@implementation RemoteListItem

- (void)dealloc
{
  if (username) [username release];
  if (channelname) [channelname release];
  if (recvtog) [recvtog release];
  if (vumeter) [vumeter release];
  if (volslider) [volslider release];
  if (panslider) [panslider release];
  if (volinfo) [volinfo release];
  if (mute) [mute release];
  if (solo) [solo release];
  [super dealloc];
}

- (void)updateVolInfo:(float)vol Pan:(float)pan;
{
  if (volinfo)
  {
    char buf[512];
    mkvolpanstr(buf,vol,pan);
    [volinfo setStringValue:[(NSString *)CFStringCreateWithCString(NULL,buf,kCFStringEncodingUTF8) autorelease]];
  }
}

- (void)onAction:(id)sender
{
  if (m_user < 0 || m_ch < 0) return;
  
  if (sender == volslider)
  {
    double pos=[sender doubleValue];
    g_client->SetUserChannelState(m_user, m_ch, false,false, true, DB2VAL(SLIDER2DB(pos)), false, 0.0, false, false, false, false);
    float vol,pan;
    if (g_client->GetUserChannelState(m_user, m_ch, NULL, &vol, &pan, NULL,NULL))
    {
      [self updateVolInfo:vol Pan:pan];
    }
  }
  else if (sender == panslider)
  {
    double pos=[sender doubleValue];
    if (fabs(pos) < 0.08) pos=0.0;
    g_client->SetUserChannelState(m_user, m_ch, false,false, false,0.0, true, pos, false, false, false, false);
    float vol,pan;
    if (g_client->GetUserChannelState(m_user, m_ch, NULL, &vol, &pan, NULL,NULL))
    {
      [self updateVolInfo:vol Pan:pan];
    }
  }
  else if (sender == recvtog)
  {
    g_client->SetUserChannelState(m_user, m_ch, true,!![sender intValue], false,0.0, false, 0.0, false, false, false, false);
  }
  else if (sender == mute)
  {
    g_client->SetUserChannelState(m_user, m_ch, false, false, false,0.0, false, 0.0, true,!![sender intValue], false, false);
  }
  else if (sender == solo)
  {
    g_client->SetUserChannelState(m_user, m_ch, false, false, false,0.0, false, 0.0, false,false,true,!![sender intValue]);
  }
}


- (void)drawRect:(NSRect)rect
{
}

- (void)updateWithUser:(int)user withChannel:(int)ch
{
  m_user=user;
  m_ch=ch;
  // update controls to reflect values
  
  char *name=g_client->GetUserState(m_user,NULL,NULL,NULL);

  [username setStringValue:[(NSString *)CFStringCreateWithCString(NULL,name?name:"<error>",kCFStringEncodingUTF8) autorelease]];

  if (!name) return;
  
  
  
  bool sub, ismute, issolo;
  float vol,pan;
  char *chn=g_client->GetUserChannelState(m_user, m_ch, &sub, &vol, &pan, &ismute, &issolo);

  [channelname setStringValue:[NSString stringWithFormat:@"%s (%d)",chn?chn:"<error>",m_ch]];

  if (!chn) return;

  [recvtog setState:(sub?NSOnState:NSOffState)];
  [volslider setFloatValue:DB2SLIDER(VAL2DB(vol))];
  [panslider setFloatValue:pan];
  [mute setState:(ismute?NSOnState:NSOffState)];
  [solo setState:(issolo?NSOnState:NSOffState)];
  
  [self updateVolInfo:vol Pan:pan];
}

- (id)initWithPos:(int)ypos;
{
  m_user=-1;
  m_ch=-1;
  m_ypos=ypos+1;
	if ((self = [super initWithFrame:NSMakeRect(4,4+ypos*60,516,60)]) != nil) {
		// Add initialization code here
    
    NSRect of=[self frame];
    float h=22;
    float xpos=0;
    float ypos=of.size.height-h;
    
    {
      username=[[NSTextField alloc] init];
      [username setEditable:NO];
      [username setDrawsBackground:NO];
      [username setSelectable:NO];
      [username setFrame:NSMakeRect(xpos,ypos,of.size.width/4,h)];
      xpos += of.size.width/4;
      [self addSubview:username];
    }
    {
      channelname=[[NSTextField alloc] init];
      [channelname setEditable:NO];
      [channelname setDrawsBackground:NO];
      [channelname setSelectable:NO];
      [channelname setFrame:NSMakeRect(xpos,ypos,of.size.width/4,h)];
      xpos += of.size.width/4;
      [self addSubview:channelname];
    }
    {
      recvtog=[[NSButton alloc] init];
      
      [recvtog setButtonType:NSSwitchButton];
      [recvtog setTitle:@"Receive"];
      [recvtog setFrame:NSMakeRect(xpos,ypos,of.size.width/5,h)];
      xpos += of.size.width/5;
      
      [recvtog setTarget:self];
      [recvtog setAction:@selector(onAction:)];
      [self addSubview:recvtog];
    }     
    {
      vumeter = [[VUMeter alloc] init];
      [vumeter setFrame:NSMakeRect(xpos,ypos+(h-16)/2,of.size.width-xpos,16)];
      
      [self addSubview:vumeter];          
    }
    // second line
    xpos=0;
    ypos -= h+5;

    {
      volslider = [[NSSlider alloc] init];
      [volslider setMinValue:0.0];
      [volslider setMaxValue:100.0];
      [volslider setFrame:NSMakeRect(xpos,ypos,of.size.width/4,h)];
      xpos += of.size.width/4;
      [volslider setTarget:self];
      [volslider setAction:@selector(onAction:)];
      
      [self addSubview:volslider];
    }
    {
      panslider = [[NSSlider alloc] init];
      [panslider setMinValue:-1.0];
      [panslider setMaxValue:1.0];
      [panslider setFrame:NSMakeRect(xpos,ypos,of.size.width/8,h)];
      xpos += of.size.width/8;
      [panslider setTarget:self];
      [panslider setAction:@selector(onAction:)];
      
      [self addSubview:panslider];
    }
    {
      volinfo=[[NSTextField alloc] init];
      [volinfo setEditable:NO];
      [volinfo setDrawsBackground:NO];
      [volinfo setSelectable:NO];
      [volinfo setFrame:NSMakeRect(xpos,ypos+(h-20)/2,of.size.width/6,20)];
      [volinfo setFont:[NSFont systemFontOfSize:10]];
      xpos += of.size.width/6;
      [self addSubview:volinfo];
    }
    {
      mute=[[NSButton alloc] init];
      
      [mute setButtonType:NSSwitchButton];
      [mute setTitle:@"Mute"];
      [mute setFrame:NSMakeRect(xpos,ypos,of.size.width/8,h)];
      xpos += of.size.width/8;
      
      [mute setTarget:self];
      [mute setAction:@selector(onAction:)];
      [self addSubview:mute];
    }     
    {
      solo=[[NSButton alloc] init];
      
      [solo setButtonType:NSSwitchButton];
      [solo setTitle:@"Solo"];
      [solo setFrame:NSMakeRect(xpos,ypos,of.size.width/8,h)];
      xpos += of.size.width/8;
      
      [solo setTarget:self];
      [solo setAction:@selector(onAction:)];
      [self addSubview:solo];
    }     
    xpos=0;
    ypos-=h+3;
	}
  return self;
}

- (void)runVUmeter
{
  double d=g_client->GetUserChannelPeak(m_user,m_ch);
  d=VAL2DB(d);
  [vumeter setDoubleValue:d];
}

-(int)tag
{
  if (!m_ypos) return 0;
  return m_ypos+1024-1;
}

@end
