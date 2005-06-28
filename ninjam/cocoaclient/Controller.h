/* Controller */

#import <Cocoa/Cocoa.h>


#include </System/Library/Frameworks/CoreAudio.framework/Headers/AudioHardware.h>


@interface Controller : NSObject
{
    IBOutlet NSTextField *mastervoldisp;
    IBOutlet NSTextField *mastervudisp;
    IBOutlet NSLevelIndicator *mastervumeter;
    IBOutlet NSTextField *metrovoldisp;
    IBOutlet NSTextField *status;
    IBOutlet NSButton *mastermute;
    IBOutlet NSButton *metromute;
    IBOutlet NSSlider *mastervol;
    IBOutlet NSSlider *masterpan;
    IBOutlet NSSlider *metrovol;
    IBOutlet NSSlider *metropan;
    
    IBOutlet NSMenuItem *menuconnect;
    IBOutlet NSMenuItem *menudisconnect;
    IBOutlet NSMenuItem *menuacfg;
    
    IBOutlet NSPanel *cdlg;
    IBOutlet NSTextField *cdlg_srv;
    IBOutlet NSTextField *cdlg_user;
    IBOutlet NSTextField *cdlg_pass;
    IBOutlet NSTextField *cdlg_passlabel;
    IBOutlet NSButton *cdlg_anon;
    IBOutlet NSPanel *adlg;
    IBOutlet NSPopUpButton *adlg_in;
    IBOutlet NSPopUpButton *adlg_out;
        
    int indev;
    int outdev;
    NSTimer *timer;
    AudioDeviceID *m_devlist;
    int m_devlist_len;
    int m_laststatus;
}
- (void)awakeFromNib;
- (void)applicationWillTerminate:(NSNotification *)notification;
- (BOOL)validateMenuItem:(id)sender;
- (IBAction)tick:(id)sender;
- (IBAction)mastermute:(NSButton *)sender;
- (IBAction)masterpan:(NSSlider *)sender;
- (IBAction)mastervol:(NSSlider *)sender;
- (IBAction)metromute:(NSButton *)sender;
- (IBAction)metropan:(NSSlider *)sender;
- (IBAction)metrovol:(NSSlider *)sender;
- (IBAction)onconnect:(id)sender;
- (IBAction)ondisconnect:(id)sender;
- (IBAction)onaudiocfg:(id)sender;
- (IBAction)cdlg_ok:(id)sender;
- (IBAction)cdlg_cancel:(id)sender;
- (IBAction)cdlg_anon:(NSButton *)sender;
- (IBAction)adlg_onclose:(id)sender;
- (IBAction)adlg_insel:(NSPopUpButton *)sender;
- (IBAction)adlg_outsel:(NSPopUpButton *)sender;
- (void)updateMasterIndicators;
- (void)readMasterConfigFromFile;
@end
