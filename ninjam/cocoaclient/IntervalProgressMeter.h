/* IntervalProgressMeter */

#import <Cocoa/Cocoa.h>

@interface IntervalProgressMeter : NSView
{
  int last_v;
  int last_len;
}
-(void)setVal:(int)v length:(int)len;
@end
