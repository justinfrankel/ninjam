#import "LocalListItem.h"
#import "LocalListView.h"
#import "VUMeter.h"

#include "../njclient.h"
#include "../audiostream_mac.h"


extern audioStreamer *myAudio;
extern NJClient *g_client;

extern double DB2VAL(double x);
extern double VAL2DB(double x);
extern double SLIDER2DB(double y);
extern double DB2SLIDER(double x);
extern void mkvolpanstr(char *str, double vol, double pan);

@implementation LocalListItem

- (void)dealloc
{
  if (channelname) [channelname release];
  if (transmit) [transmit release];
  if (sourcesel) [sourcesel release];
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
  if (sender == remove)
  {
    g_client->DeleteLocalChannel(m_idx);
    g_client->NotifyServerOfChannelChange();  
    LocalListView *par=[self superview];
    [par removeChannel:self];
  }
  else if (sender == volslider)
  {
    double pos=[sender doubleValue];
    float vol,pan;
    g_client->SetLocalChannelMonitoring(m_idx,true,DB2VAL(SLIDER2DB(pos)), false, false, false,false,false,false);
    g_client->GetLocalChannelMonitoring(m_idx, &vol, &pan,NULL,NULL);
    [self updateVolInfo:vol Pan:pan];
  }
  else if (sender == panslider)
  {
    double pos=[sender doubleValue];
    if (fabs(pos) < 0.08) pos=0.0;
    float vol,pan;
    g_client->SetLocalChannelMonitoring(m_idx,false,0.0, true, pos, false,false,false,false);
    g_client->GetLocalChannelMonitoring(m_idx, &vol, &pan,NULL,NULL);
    [self updateVolInfo:vol Pan:pan];
  }
  else if (sender == transmit)
  {
    g_client->SetLocalChannelInfo(m_idx, NULL, false, 0, false, 0, true, !![sender intValue]);
    g_client->NotifyServerOfChannelChange();
  }
  else if (sender == mute)
  {
    g_client->SetLocalChannelMonitoring(m_idx,false,0.0, false, 0.0, true,!![sender intValue],false,false);
  }
  else if (sender == solo)
  {
    g_client->SetLocalChannelMonitoring(m_idx,false,0.0, false, 0.0,false,false, true,!![sender intValue]);
  }
  else if (sender == sourcesel)
  {
    char name[512];
    [[[sender selectedItem] title] getCString:(char *)name maxLength:(unsigned)(sizeof(name)-1)];
    int a=atoi(name+6);
    if (a > 0)
    {
      g_client->SetLocalChannelInfo(m_idx, NULL, true, a-1, false, 0, false, false);
    }
  }
}

- (id)initWithCh:(int)ch atPos:(int)ypos
{
  m_idx=ch;
	if ((self = [super initWithFrame:NSMakeRect(0,ypos*60,520,60)]) != nil) {
		// Add initialization code here
    
    int sch;
    bool bc;
    char *buf=g_client->GetLocalChannelInfo(m_idx,&sch,NULL,&bc);
    NSRect of=[self frame];
    float vol=0.0,pan=0.0 ;
    bool ismute=0,issolo=0;
    g_client->GetLocalChannelMonitoring(m_idx, &vol, &pan, &ismute, &issolo);
    float h=22;
    float xpos=0;
    float ypos=of.size.height-h;
    
    {
      channelname=[[NSTextField alloc] init];
      [channelname setEditable:YES];
      [channelname setDrawsBackground:NO];
      [channelname setSelectable:YES];
      [channelname setFrame:NSMakeRect(xpos,ypos,of.size.width/4,h)];
      xpos += of.size.width/4;
      [channelname setStringValue:[(NSString *)CFStringCreateWithCString(NULL,buf,kCFStringEncodingUTF8) autorelease]];
      [channelname setDelegate:self];
      [self addSubview:channelname];
    }
    {
      transmit=[[NSButton alloc] init];
      
      [transmit setButtonType:NSSwitchButton];
      [transmit setTitle:@"Transmit"];
      if (bc) [transmit setState:NSOnState];
      [transmit setFrame:NSMakeRect(xpos,ypos,of.size.width/5,h)];
      xpos += of.size.width/5;
      
      [transmit setTarget:self];
      [transmit setAction:@selector(onAction:)];
      [self addSubview:transmit];
    }     
    {
      sourcesel = [[NSPopUpButton alloc] init];

      [sourcesel removeAllItems];
      int x;
      for (x = 0; x < 8; x ++)
      {
        NSString *t=[NSString stringWithFormat:@"Input %d",(x+1)];
        [sourcesel addItemWithTitle:t];
        if (sch == x || ((!x) && (sch < 0 || sch >= 8))) [sourcesel selectItemWithTitle:t];
      }
      [sourcesel setFrame:NSMakeRect(xpos,ypos,of.size.width/3,h)];
      xpos += of.size.width/3;
      [sourcesel setTarget:self];
      [sourcesel setAction:@selector(onAction:)];
      
      [self addSubview:sourcesel];      
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
      [volslider setFloatValue:DB2SLIDER(VAL2DB(vol))];
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
      [panslider setFloatValue:pan];
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
      [self updateVolInfo:vol Pan:pan];
    }
    {
      mute=[[NSButton alloc] init];
      
      [mute setButtonType:NSSwitchButton];
      [mute setTitle:@"Mute"];
      if (ismute) [mute setState:NSOnState];
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
      if (issolo) [solo setState:NSOnState];
      [solo setFrame:NSMakeRect(xpos,ypos,of.size.width/8,h)];
      xpos += of.size.width/8;
      
      [solo setTarget:self];
      [solo setAction:@selector(onAction:)];
      [self addSubview:solo];
    }     
    {
      remove=[[NSButton alloc] init];
      
      [remove setButtonType:NSMomentaryPushButton];
//      [remove setBordered:YES];
      [remove setBezelStyle:NSRoundedBezelStyle];
      [remove setTitle:@"Remove"];
      [remove setFrame:NSMakeRect(xpos,ypos-4,of.size.width-xpos,h+6)];
      
      [remove setTarget:self];
      [remove setAction:@selector(onAction:)];
      [self addSubview:remove];
    }
    xpos=0;
    ypos-=h+3;
    if (0) {
      NSBox *box=[[NSBox alloc] init];
      [box setBoxType:NSBoxSeparator];
      [box setBorderType:NSBezelBorder];
      [box setFrame:NSMakeRect(0,ypos,of.size.width,2)];
      [self addSubview:box];
      [box release];
    }
	}
	return self;
}

- (void)controlTextDidChange:(NSNotification *)aNotification
{
    char name[512];
    [[channelname stringValue] getCString:(char *)name maxLength:(unsigned)(sizeof(name)-1)];
    g_client->SetLocalChannelInfo(m_idx, name, false, 0, false, 0, false, false);
    g_client->NotifyServerOfChannelChange();  
}

- (void)runVUmeter
{
  double d=g_client->GetLocalChannelPeak(m_idx);
  d=VAL2DB(d);
  [vumeter setDoubleValue:d];
}

- (void)drawRect:(NSRect)rect
{
}

-(int)tag
{
  return 1024+m_idx;
}


@end
