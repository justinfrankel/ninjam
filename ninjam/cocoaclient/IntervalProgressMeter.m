#import "IntervalProgressMeter.h"

@implementation IntervalProgressMeter

- (id)initWithFrame:(NSRect)frameRect
{
	if ((self = [super initWithFrame:frameRect]) != nil) {
		// Add initialization code here
    last_v=0;
    last_len=0;
    last_bpm=120.0;
	}
	return self;
}

-(void)setVal:(int)v length:(int)len bpm:(float)bpmrate;
{
  if (v != last_v || len != last_len || last_bpm != bpmrate)
  {
    last_bpm=bpmrate;
    last_v=v;
    last_len=len;
    [self setNeedsDisplay:YES];
  }
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

  double xpos=0;
  if (last_len) xpos = XDIM*(double)last_v / (double) last_len;
  
  [[NSColor blackColor] set];
  NSRectFill(NSMakeRect(0,0,xpos,YDIM) );
  
  NSDrawWindowBackground(NSMakeRect(xpos,0,XDIM-xpos,YDIM));
  
  NSFont *font = [NSFont systemFontOfSize:YDIM*0.75];
    NSMutableDictionary *attrs = [[NSMutableDictionary alloc] init];
    [attrs setObject:font forKey:NSFontAttributeName];
    [attrs setObject:[NSColor redColor] forKey:NSForegroundColorAttributeName];
    NSMutableParagraphStyle *p = [[NSMutableParagraphStyle alloc] init];
    [p setAlignment:NSCenterTextAlignment];
    [attrs setObject:p forKey:NSParagraphStyleAttributeName];

    [[NSString stringWithFormat:@"%d/%d @ %.0f BPM",last_v+1,last_len,last_bpm] drawInRect:wb withAttributes:attrs];
    [attrs release];
    [p release];
  }

@end
