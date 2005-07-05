/* IntervalProgressMeter */

#import <Cocoa/Cocoa.h>

@interface IntervalProgressMeter : NSView
{
  int last_v;
  int last_len;
  float last_bpm;
}
-(void)setVal:(int)v length:(int)len bpm:(float)bpmrate;
@end
