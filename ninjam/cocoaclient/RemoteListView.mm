/*
    NINJAM Cocoa Client - RemoteListView.mm
    Copyright (C) 2005 Cockos Incorporated

    NINJAM is free software; you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation; either version 2 of the License, or
    (at your option) any later version.

    NINJAM is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU General Public License for more details.

    You should have received a copy of the GNU General Public License
    along with NINJAM; if not, write to the Free Software
    Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
*/

#import "RemoteListView.h"
#import "RemoteListItem.h"

#include "../njclient.h"

extern NJClient *g_client;
extern NSLock *g_client_mutex;


@implementation RemoteListView

- (id)initWithFrame:(NSRect)frameRect
{
  frameRect.size.width = 320;
	if ((self = [super initWithFrame:frameRect]) != nil) {
		// Add initialization code here
	}
	return self;
}

- (void)updateList
{
  int pos=0;
  int us;
  NSRect lastr;
  [g_client_mutex lock];  
  for (us = 0; us < g_client->GetNumUsers(); us ++)
  {
    int ch=0;
    for (;;)
    {
      int i=g_client->EnumUserChannels(us,ch++);
      if (i < 0) break;
      RemoteListItem *foo=[self viewWithTag:(1024+pos)];
      if (!foo)
      {
        // create a new view
        foo=[[RemoteListItem alloc] initWithPos:pos];
      //  NSLog(@"Created a new remoteitem");
        [self addSubview:foo];
      }
      lastr=[foo frame];
      [foo updateWithUser:us withChannel:i];
      
      pos++;
    }
  }
  [g_client_mutex unlock];
  
  if (pos)
  {
    NSRect r2;
    r2.size.width = lastr.size.width+lastr.origin.x;
    r2.size.height = lastr.size.height + lastr.origin.y;
    r2.origin.x=0;
    r2.origin.y=0;
    
    [self setFrame:r2];
  }
  // remove any remaining items
  int a=[[self subviews] count];
  for (; pos < a; pos ++)
  {
    id foo=[self viewWithTag:(1024+pos)];
    if (foo)
    {
      [foo removeFromSuperview];
    }
  }
  [self setNeedsDisplay:YES];
  [self displayIfNeeded];
}

- (BOOL)isOpaque
{
  return YES;
}

- (void)runVUMeters
{
  int x;
  for (x = 0; x < [[self subviews] count]; x ++)
  {
    RemoteListItem *pp=[[self subviews] objectAtIndex:x];
    if ([pp tag] >= 1024)
    {
      [pp runVUmeter];
    }
  }
}

- (void)onAction:(id)sender
{
}

- (BOOL)isFlipped
{
return YES;
}


- (void)drawRect:(NSRect)rect
{
  NSDrawWindowBackground(rect);
}

@end
