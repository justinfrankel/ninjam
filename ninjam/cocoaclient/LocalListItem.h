/* LocalListItem */

#import <Cocoa/Cocoa.h>

@class VUMeter;
@interface LocalListItem : NSView
{
  int m_idx;
  NSTextField *channelname;
  NSButton *transmit;
  NSPopUpButton *sourcesel;
  VUMeter *vumeter;
  NSSlider *volslider;
  NSSlider *panslider;
  NSTextField *volinfo;
  NSButton *mute;
  NSButton *solo;
  NSButton *remove;
}
- (void)onAction:(id)sender;
- (id)initWithCh:(int)ch atPos:(int)ypos;
- (int)tag; // returns channel_index + 1024
- (void)runVUmeter;
- (void)updateVolInfo:(float)vol Pan:(float)pan;
@end
