/* RemoteListItem */

#import <Cocoa/Cocoa.h>
@class VUMeter;
@interface RemoteListItem : NSView
{
  int m_user;
  int m_ch;
  int m_ypos;
  NSTextField *username;
  NSTextField *channelname;
  NSButton *recvtog;
  VUMeter *vumeter;
  
  NSSlider *volslider;
  NSSlider *panslider;
  NSTextField *volinfo;
  NSButton *mute;
  NSButton *solo;  
}
- (void)onAction:(id)sender;
- (id)initWithPos:(int)ypos;
- (void)updateWithUser:(int)user withChannel:(int)ch;
- (int)tag; // returns channel_index + 1024
- (void)runVUmeter;
- (void)updateVolInfo:(float)vol Pan:(float)pan;
@end
