#ifndef __WASABI_CFG_H
#define __WASABI_CFG_H

#define WASABI_COMPILE_APP

#define WASABI_COMPILE_SVC
#define WASABI_CUSTOM_MODULE_SVCMGR

//this enables system callback management
#define WASABI_COMPILE_SYSCB

//this enables the xml parser 
#define WASABI_COMPILE_XMLPARSER

//this enables multiplexed timers
#define WASABI_COMPILE_TIMERS

//this enables textmode windowing
//#define WASABI_COMPILE_TEXTMODE

//this allows component (external plugins)
//#define WASABI_COMPILE_COMPONENTS

//this enables the layered UI
//#define WASABI_COMPILE_WND

//this enables xml group loading within the window api
//#define WASABI_COMPILE_SKIN

//legacy system object compatibility
//#define WASABI_SCRIPT_SYSTEMOBJECT_WA3COMPATIBLE

//this enables i18n charsets
#define WASABI_COMPILE_UTF

//this enables UI scripting
//#define WASABI_COMPILE_SCRIPT

//this enables keyboard locales in UI
//#define WASABI_COMPILE_LOCALES

//this enables bitmap and truetype font rendering
#define WASABI_COMPILE_FONTS

//use win32 ttf renderer
//#define WASABI_FONT_RENDERER_USE_WIN32

//use freetype ttf renderer
//#define WASABI_FONT_RENDERER_USE_FREETYPE

//this lets you override all bitmapfonts using TTF fonts (for internationalization). define to a function call or a global value to change this value dynamically. 
//if you are compiling with api_config, the attribute to set is { 0x280876cf, 0x48c0, 0x40bc, { 0x8e, 0x86, 0x73, 0xce, 0x6b, 0xb4, 0x62, 0xe5 } }, "Use bitmap fonts (no international support)"
//#define WASABI_FONT_TTFOVERRIDE 0 // 1 does all rendering with TTF

//#define WASABI_WNDMGR_ANIMATEDRECTS 0 // if api_config is compiled, the item controlling this is {280876CF-48C0-40bc-8E86-73CE6BB462E5};"Animated rects"
//#define WASABI_WNDMGR_FINDOPENRECT 0 // if api_config is compiled, the item controlling this is {280876CF-48C0-40bc-8E86-73CE6BB462E5};"Find open rect"
//#define WASABI_WNDMGR_LINKLAYOUTSCALES 0 // if api_config is compiled, the item controlling this is {9149C445-3C30-4e04-8433-5A518ED0FDDE};"Link layouts scale"
//#define WASABI_WNDMGR_LINKLAYOUTSALPHA 0 // if api_config is compiled, the item controlling this is {9149C445-3C30-4e04-8433-5A518ED0FDDE};"Link layouts alpha"
//#define WASABI_WNDMGR_DESKTOPALPHA 1 // if api_config is compiled, the item controlling this is {9149C445-3C30-4e04-8433-5A518ED0FDDE};"Enable desktop alpha"

//this enables loading for pngs and jpgs. each service relies on the imageloader base service
#define WASABI_COMPILE_IMGLDR
#define WASABI_COMPILE_IMGLDR_PNGREAD
#define WASABI_COMPILE_IMGLDR_JPGREAD

//this enables metadb support
//#define WASABI_COMPILE_METADB

//this enables config file support
#define WASABI_COMPILE_CONFIG

//this enables the filereader pipeline
//#define WASABI_COMPILE_FILEREADER

//this enables the zip filereader
//#define WASABI_COMPILE_ZIPREADER

//this enables centralized memory allocation/deallocation
//#define WASABI_COMPILE_MEMMGR

//#define WASABI_DIRS_FROMEXEPATH // otherwise, if the lib is running from a dll in another path, undefining that means the path are relative to the DLL path
//#define WASABI_SKINS_SUBDIRECTORY "skins"
#define WASABI_RESOURCES_SUBDIRECTORY ""	// shut up

//#define WASABI_COMPILE_MEDIACORE

//#define WASABI_COMPILE_WNDMGR
//#define WASABI_COMPILE_PAINTSETS
//#define WASABI_COMPILE_MAKIDEBUG

//#define WASABI_CUSTOMIMPL_MEDIACORE
//#define WASABI_WIDGETS_MEDIASLIDERS

//#define WASABI_CUSTOM_CONTEXTMENUS

//#define WASABI_CUSTOM_QUIT
//#define WASABI_CUSTOM_QUITFN { extern void appQuit(); appQuit(); }

//#define WASABI_CUSTOM_ONTOP

//#define WASABI_EXTERNAL_GUIOBJECTS

//#define WASABI_WIDGETS_GUIOBJECT
//#define WASABI_WIDGETS_LAYER
//#define WASABI_WIDGETS_TEXT
//#define WASABI_WIDGETS_BUTTON
//#define WASABI_WIDGETS_TGBUTTON
//#define WASABI_WIDGETS_ANIMLAYER
//#define WASABI_WIDGETS_GROUPLIST
//#define WASABI_WIDGETS_MOUSEREDIR
//#define WASABI_WIDGETS_SLIDER
//#define WASABI_WIDGETS_MEDIAVIS
//#define WASABI_WIDGETS_MEDIAEQCURVE
//#define WASABI_WIDGETS_MEDIASTATUS
//#define WASABI_WIDGETS_SVCWND
//#define WASABI_WIDGETS_EDIT
//#define WASABI_WIDGETS_TITLEBAR
//#define WASABI_WIDGETS_COMPBUCK
//#define WASABI_WIDGETS_BROWSER
//#define WASABI_WIDGETS_FRAME
//#define WASABI_WIDGETS_GRID
//#define WASABI_WIDGETS_QUERYDRAG
//#define WASABI_WIDGETS_QUERYLIST
//#define WASABI_WIDGETS_FILTERLIST
//#define WASABI_WIDGETS_QUERYLINE
//#define WASABI_WIDGETS_TABSHEET
//#define WASABI_WIDGETS_CHECKBOX
//#define WASABI_WIDGETS_TITLEBOX
//#define WASABI_WIDGETS_CUSTOMOBJECT
//#define WASABI_WIDGETS_OSWNDHOST
//#define WASABI_WIDGETS_RADIOGROUP
//#define WASABI_WIDGETS_LIST
//#define WASABI_WIDGETS_TREE
//#define WASABI_WIDGETS_DROPDOWNLIST
//#define WASABI_WIDGETS_COMBOBOX
//#define WASABI_WIDGETS_HISTORYEDITBOX
//#define WASABI_WIDGETS_OBJECTDIRECTORY
//#define WASABI_WIDGETS_RECTANGLE
//#define WASABI_WIDGETS_PATHPICKER
//#define WASABI_WIDGETS_GRADIENT
//#define WASABI_WIDGETS_MENUBUTTON
//#define WASABI_WIDGETS_MENU
//#define WASABI_WIDGETS_WNDHOLDER
//#define WASABI_WIDGETS_LAYOUTSTATUS
//#define WASABI_WIDGETS_TOOLTIPS

//#define WASABI_COMPILE_STATSWND


//#define WASABI_TOOLOBJECT_HIDEOBJECT
//#define WASABI_TOOLOBJECT_SENDPARAMS
//#define WASABI_TOOLOBJECT_ADDPARAMS

// #endif // WASABI_EXTERNAL_GUIOBJECTS
//#define WASABI_COMPILE_COLORTHEMES

//#define WASABI_SCRIPTOBJECTS_POPUP
//#define WASABI_SCRIPTOBJECTS_LIST
//#define WASABI_SCRIPTOBJECTS_BITLIST
//#define WASABI_SCRIPTOBJECTS_REGION
//#define WASABI_SCRIPTOBJECTS_TIMER
//#define WASABI_SCRIPTOBJECTS_MAP
//#define WASABI_SCRIPTOBJECTS_EMBEDDEDXUI // needed by 3rd+ level objects
//
//#ifndef WASABI_EXTERNAL_GUIOBJECTS
//#define WASABI_SCRIPTOBJECTS_WAC
//#endif // WASABI_EXTERNAL_GUIOBJECTS

//#define WASABI_WNDMGR_ALLCONTAINERSDYNAMIC 0
//#define WASABI_WNDMGR_NORESPAWN
//#define WASABI_WNDMGR_OSMSGBOX
//
//#define WASABI_STATICVARMGR
//#define WASABI_CUSTOM_MODULE_SVCMGR
//
//#define ON_LAYOUT_CHANGED { extern void onLayoutChanged(); onLayoutChanged(); }
//
//#define ON_FATAL_SKIN_ERROR { extern void onFatalSkinError(); onFatalSkinError(); }
//
//#define ON_CREATE_EXTERNAL_WINDOW_GUID(x, y) { extern int onCreateExternalWindowGuid(GUID g); y = onCreateExternalWindowGuid(x); }
//
//#define ON_TOGGLE_DESKTOPALPHA(v) { extern void onToggleDesktopAlpha(int v); onToggleDesktopAlpha(v); }
//#define ON_TWEAK_CONTAINER_NAME(x) { extern const char *onTweakContainerName(const char *name); x = onTweakContainerName(x); }
//#define ON_TWEAK_RENDER_RATIO(x) { extern float onTweakRenderRatio(float v); x = onTweakRenderRatio(x); }
//#define ON_CUSTOM_ALTF4 { extern void onCustomAltF4(); onCustomAltF4(); }
//
//#define WASABI_DEFAULT_STDCONTAINER "resizable_status"
//
//#define SWITCH_SKIN(x) { extern void switchSkin(const char *name); switchSkin(x); }
//#define IS_SKIN_STILL_LOADING(x) { extern int isSkinStillLoading(); x = isSkinStillLoading(); }
//
//#define ON_LOAD_EXTRA_COLORTHEMES() { extern void loadExtraColorThemes(); loadExtraColorThemes(); }
//
//#define LOCALES_CUSTOM_LOAD(x) { extern const char *localesCustomGetFile(); x = localesCustomGetFile(); }
//
//#define DEFAULT_CROSSFADE_ENABLED FALSE
//
//#define CUSTOM_VARS(x, y) { extern const char *getCustomVar(const char *var); y = getCustomVar(x); }
//
//#define WASABI_NO_RELEASEMODE_DEBUGSTRINGS
//
//#define WASABI_CUSTOM_MINIDB(field, buf, len) { extern void getCustomMetaData(const char *f, char *b, int l); getCustomMetaData(field, buf, len); }
//
//#define GET_SONG_INFO_TEXT(ret) { extern const char *getSongInfoText(); ret = getSongInfoText(); }
//
//#define GET_KBDFORWARD_WND(g, wnd) { extern OSWINDOWHANDLE getKeyboardForwardWnd(GUID g); wnd = getKeyboardForwardWnd(g); }
//
//#define WASABI_EDITWND_LISTCOLORS // editwnds use list foreground & background rather than their own colors
//
//#define WASABI_APPBAR_ONDOCKCHANGED(wnd) { extern void onAppBarDockChanged(RootWnd *w); onAppBarDockChanged(wnd); }
//
//#define WASABI_GET_VERSION(cs, n) { extern const char *getVersion(); STRNCPY(cs, getVersion(), n); cs[n] = 0; }
//
//#define WASABI_ON_MAIN_MOVE(hwnd) { extern void onMainLayoutMove(HWND w); onMainLayoutMove(hwnd); }
//
//#define WASABI_ON_REPARENT(hwnd) { extern void onReParent(HWND w); onReParent(hwnd); }
//
//#define WASABI_ON_REINIT( hwnd) { extern void onReInit(HWND w); onReInit(hwnd); }

// config defaults

//#define DEFAULT_DESKTOPALPHA       FALSE
//#define DEFAULT_LINKLAYOUTSCALE    TRUE
//#define DEFAULT_LINKLAYOUTSALPHA   FALSE
//#define DEFAULT_LINKALLALPHA       TRUE
//#define DEFAULT_LINKALLRATIO       FALSE
//adefine DEFAULT_LINKEDALPHA        255
//#define DEFAULT_AUTOOPACITYTIME    2000
//#define DEFAULT_AUTOOPACITYFADEIN  250
//#define DEFAULT_AUTOOPACITYFADEOUT 1000
//#define DEFAULT_AUTOOPACITYTYPE    0
//#define DEFAULT_EXTENDAUTOOPACITY  25
//#define DEFAULT_USERATIOLOCKS      FALSE
//#define DEFAULT_TIMERRESOLUTION    33
//#define DEFAULT_TOOLTIPS           TRUE
//#define DEFAULT_TEXTSPEED          1.0f/3.0f

//#define UTF8 0
//#define WANT_UTF8_WARNINGS

// drop bitmap resources in autoskin bitmap when the ui hasn't been used for a while
//#define DROP_BITMAP_ON_IDLE

#include <api/apiconfig.h>

#endif
