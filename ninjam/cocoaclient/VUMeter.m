#import "VUMeter.h"

@implementation VUMeter

- (id)initWithFrame:(NSRect)frameRect
{
	if ((self = [super initWithFrame:frameRect]) != nil) {
		// Add initialization code here
    last_v=-120.0;
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
 
  double x=(last_v+100)*XDIM/112.0;
  if (x < 0.0) x=0.0;
  else if (x > XDIM) x=XDIM;
  
  int cnt=2;
  if (last_v < -12.0) cnt=0;
  else if (last_v < -3.0) cnt=1;
  
  double lx=0.0;
  int y;
  for (y = 0; y <= cnt; y ++)
  {
    double nx=x;
    if (y == 0)
    {
      [[NSColor greenColor] set];
      if (nx > (100.0-12.0)*XDIM/112.0) nx=(100.0-6.0)*XDIM/112.0;
    }
    else if (y == 1)
    {
      if (nx > (100.0-3.0)*XDIM/112.0) nx=(100.0)*XDIM/112.0;
      [[NSColor yellowColor] set];
    }
    else if (y == 2)
    {
      [[NSColor redColor] set];
    }
    NSRectFill(NSMakeRect(lx,0,nx-lx,YDIM) );
    lx=nx;
  }
  
  NSDrawWindowBackground(NSMakeRect(x,0,XDIM-x,YDIM));
  
  
  #if 1 // why does font not get initialized properly?
  
    NSFont *font = [NSFont systemFontOfSize:YDIM*0.7];
    NSMutableDictionary *attrs = [[NSMutableDictionary alloc] init];
    [attrs setObject:font forKey:NSFontAttributeName];
    [attrs setObject:[NSColor blackColor] forKey:NSForegroundColorAttributeName];
    NSMutableParagraphStyle *p = [[NSMutableParagraphStyle alloc] init];
    [p setAlignment:NSCenterTextAlignment];
    [attrs setObject:p forKey:NSParagraphStyleAttributeName];

    [[NSString stringWithFormat:@"%.2f dB",last_v] drawInRect:wb withAttributes:attrs];
    [attrs release];
    [p release];
  
  #endif
  
}

-(void)setDoubleValue:(double)v
{
  last_v=v;
  [self setNeedsDisplay:YES];
}

@end
