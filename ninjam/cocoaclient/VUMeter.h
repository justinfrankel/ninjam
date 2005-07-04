/* VUMeter */

#import <Cocoa/Cocoa.h>

@interface VUMeter : NSView
{
  float last_v;
}
-(void)setDoubleValue:(double)v;
@end
