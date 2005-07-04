#import "LocalListView.h"
#import "LocalListItem.h"

#include "../njclient.h"

extern NJClient *g_client;

@implementation LocalListView

- (id)initWithFrame:(NSRect)frameRect
{
  frameRect.size.width = 640;
	if ((self = [super initWithFrame:frameRect]) != nil) {
		// Add initialization code here
    addbutton=[[NSButton alloc] init];
    
      [addbutton setButtonType:NSMomentaryPushButton];
      [addbutton setBezelStyle:NSRoundedBezelStyle];
      [addbutton setTitle:@"Add Channel"];
      [addbutton setFrame:NSMakeRect(0,0,120,32)];
      
      [addbutton setTarget:self];
      [addbutton setAction:@selector(onAction:)];
      [self addSubview:addbutton];
    
	}
	return self;
}

- (BOOL)isOpaque
{
  return YES;
}



- (void)drawRect:(NSRect)rect
{
  double XDIM,YDIM;
  
  NSRect wb=[self bounds];

  XDIM = wb.size.width;
  YDIM = wb.size.height;
  
  NSDrawWindowBackground(NSMakeRect(0,0,XDIM,YDIM));
  
}

- (void)runVUMeters
{
  int x;
  for (x = 0; x < [[self subviews] count]; x ++)
  {
    NSView *p=[[self subviews] objectAtIndex:x];
    if ([p tag] >= 1024 && [p tag] < 1024+MAX_LOCAL_CHANNELS)
    {
      LocalListItem *pp=p;
    // check tag to see if it's the LocalListItem
      [pp runVUmeter];
    }
  }
}


- (void)onAction:(id)sender
{
  if (sender == addbutton)
  {
    int idx;
    for (idx = 0; idx < MAX_LOCAL_CHANNELS && g_client->GetLocalChannelInfo(idx,NULL,NULL,NULL); idx++);
    if (idx < MAX_LOCAL_CHANNELS)
    {
      g_client->SetLocalChannelInfo(idx,"new channel",true,0,false,0,true,false);
//    g_client->SetLocalChannelMonitoring(idx,false,0.0f,false,0.0f,false,false,false,false);
      [self newChannel:idx];
      g_client->NotifyServerOfChannelChange();  
    }
  }
}

- (void)newChannel:(int)index
{
  int svc=[[self subviews] count] - 1;
  LocalListItem *p = [[LocalListItem alloc] initWithCh:index atPos:svc];
  [self addSubview:p];
  NSRect r=[p frame];
  NSRect r2;

  NSRect r3=[addbutton frame];
  r3.origin.y = r.origin.y + r.size.height;
    
  r2.size.width = r.size.width;
  r2.size.height = r3.size.height + r3.origin.y;
  r2.origin.x=0;
  r2.origin.y=0;
  [self setFrame:r2];
  
  [addbutton setFrame:r3];
  [addbutton setNeedsDisplay:YES];
  [p setNeedsDisplay:YES];

  [self setNeedsDisplay:YES];
  [self displayIfNeeded];
  [p displayIfNeeded];
  [addbutton displayIfNeeded];
  [p release];
}

- (void)removeChannel:(id)chan
{
  NSRect f=[chan frame];
  [chan removeFromSuperview];
  
  int x;
  for (x = 0; x < [[self subviews] count]; x ++)
  {
    NSView *p=[[self subviews] objectAtIndex:x];
    NSRect tf=[p frame];
    if (tf.origin.y >= f.origin.y)
    {
      tf.origin.y -= f.size.height;
      [p setFrame:tf];
      [p setNeedsDisplay:YES];
    }
  }
  [self setNeedsDisplay:YES];
  [self displayIfNeeded];
}

- (BOOL)isFlipped
{
return YES;
}
@end
