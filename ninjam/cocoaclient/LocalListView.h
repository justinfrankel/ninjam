/* LocalListView */

#import <Cocoa/Cocoa.h>

@class LocalListItem;
@interface LocalListView : NSView
{
  NSButton *addbutton;
}
- (void)onAction:(id)sender;
- (void)newChannel:(int)index;
- (void)runVUMeters;
- (void)removeChannel:(id)chan;
@end
