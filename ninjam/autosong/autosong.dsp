# Microsoft Developer Studio Project File - Name="autosong" - Package Owner=<4>
# Microsoft Developer Studio Generated Build File, Format Version 6.00
# ** DO NOT EDIT **

# TARGTYPE "Win32 (x86) Console Application" 0x0103

CFG=autosong - Win32 Debug
!MESSAGE This is not a valid makefile. To build this project using NMAKE,
!MESSAGE use the Export Makefile command and run
!MESSAGE 
!MESSAGE NMAKE /f "autosong.mak".
!MESSAGE 
!MESSAGE You can specify a configuration when running NMAKE
!MESSAGE by defining the macro CFG on the command line. For example:
!MESSAGE 
!MESSAGE NMAKE /f "autosong.mak" CFG="autosong - Win32 Debug"
!MESSAGE 
!MESSAGE Possible choices for configuration are:
!MESSAGE 
!MESSAGE "autosong - Win32 Release" (based on "Win32 (x86) Console Application")
!MESSAGE "autosong - Win32 Debug" (based on "Win32 (x86) Console Application")
!MESSAGE 

# Begin Project
# PROP AllowPerConfigDependencies 0
# PROP Scc_ProjName ""
# PROP Scc_LocalPath ""
CPP=cl.exe
RSC=rc.exe

!IF  "$(CFG)" == "autosong - Win32 Release"

# PROP BASE Use_MFC 0
# PROP BASE Use_Debug_Libraries 0
# PROP BASE Output_Dir "Release"
# PROP BASE Intermediate_Dir "Release"
# PROP BASE Target_Dir ""
# PROP Use_MFC 0
# PROP Use_Debug_Libraries 0
# PROP Output_Dir "Release"
# PROP Intermediate_Dir "Release"
# PROP Ignore_Export_Lib 0
# PROP Target_Dir ""
# ADD BASE CPP /nologo /W3 /GX /O2 /D "WIN32" /D "NDEBUG" /D "_CONSOLE" /D "_MBCS" /YX /FD /c
# ADD CPP /nologo /MT /W3 /GX /O2 /I "../../sdks/libvorbis-1.1.2/include" /I "../../sdks/libogg-1.1.3/include" /D "WIN32" /D "NDEBUG" /D "_CONSOLE" /D "_MBCS" /YX /FD /c
# ADD BASE RSC /l 0x409 /d "NDEBUG"
# ADD RSC /l 0x409 /d "NDEBUG"
BSC32=bscmake.exe
# ADD BASE BSC32 /nologo
# ADD BSC32 /nologo
LINK32=link.exe
# ADD BASE LINK32 kernel32.lib user32.lib gdi32.lib winspool.lib comdlg32.lib advapi32.lib shell32.lib ole32.lib oleaut32.lib uuid.lib odbc32.lib odbccp32.lib kernel32.lib user32.lib gdi32.lib winspool.lib comdlg32.lib advapi32.lib shell32.lib ole32.lib oleaut32.lib uuid.lib odbc32.lib odbccp32.lib /nologo /subsystem:console /machine:I386
# ADD LINK32 kernel32.lib user32.lib gdi32.lib winspool.lib comdlg32.lib advapi32.lib shell32.lib ole32.lib oleaut32.lib uuid.lib odbc32.lib odbccp32.lib kernel32.lib user32.lib gdi32.lib winspool.lib comdlg32.lib advapi32.lib shell32.lib ole32.lib oleaut32.lib uuid.lib odbc32.lib odbccp32.lib /nologo /subsystem:console /machine:I386

!ELSEIF  "$(CFG)" == "autosong - Win32 Debug"

# PROP BASE Use_MFC 0
# PROP BASE Use_Debug_Libraries 1
# PROP BASE Output_Dir "Debug"
# PROP BASE Intermediate_Dir "Debug"
# PROP BASE Target_Dir ""
# PROP Use_MFC 0
# PROP Use_Debug_Libraries 1
# PROP Output_Dir "Debug"
# PROP Intermediate_Dir "Debug"
# PROP Target_Dir ""
# ADD BASE CPP /nologo /W3 /Gm /GX /ZI /Od /D "WIN32" /D "_DEBUG" /D "_CONSOLE" /D "_MBCS" /YX /FD /GZ /c
# ADD CPP /nologo /W3 /Gm /GX /ZI /Od /I "../../sdks/oggvorbis-win32sdk-1.0.1/include" /D "WIN32" /D "_DEBUG" /D "_CONSOLE" /D "_MBCS" /YX /FD /GZ /c
# ADD BASE RSC /l 0x409 /d "_DEBUG"
# ADD RSC /l 0x409 /d "_DEBUG"
BSC32=bscmake.exe
# ADD BASE BSC32 /nologo
# ADD BSC32 /nologo
LINK32=link.exe
# ADD BASE LINK32 kernel32.lib user32.lib gdi32.lib winspool.lib comdlg32.lib advapi32.lib shell32.lib ole32.lib oleaut32.lib uuid.lib odbc32.lib odbccp32.lib kernel32.lib user32.lib gdi32.lib winspool.lib comdlg32.lib advapi32.lib shell32.lib ole32.lib oleaut32.lib uuid.lib odbc32.lib odbccp32.lib /nologo /subsystem:console /debug /machine:I386 /pdbtype:sept
# ADD LINK32 kernel32.lib user32.lib gdi32.lib winspool.lib comdlg32.lib advapi32.lib shell32.lib ole32.lib oleaut32.lib uuid.lib odbc32.lib odbccp32.lib kernel32.lib user32.lib gdi32.lib winspool.lib comdlg32.lib advapi32.lib shell32.lib ole32.lib oleaut32.lib uuid.lib odbc32.lib odbccp32.lib /nologo /subsystem:console /debug /machine:I386 /pdbtype:sept

!ENDIF 

# Begin Target

# Name "autosong - Win32 Release"
# Name "autosong - Win32 Debug"
# Begin Group "Source Files"

# PROP Default_Filter "cpp;c;cxx;rc;def;r;odl;idl;hpj;bat"
# Begin Group "ogg"

# PROP Default_Filter ""
# Begin Source File

SOURCE="..\..\sdks\libogg-1.1.3\src\bitwise.c"
# End Source File
# Begin Source File

SOURCE="..\..\sdks\libogg-1.1.3\src\framing.c"
# End Source File
# End Group
# Begin Group "vorbis"

# PROP Default_Filter ""
# Begin Source File

SOURCE="..\..\sdks\libvorbis-1.1.2\lib\analysis.c"
# End Source File
# Begin Source File

SOURCE="..\..\sdks\libvorbis-1.1.2\lib\backends.h"
# End Source File
# Begin Source File

SOURCE="..\..\sdks\libvorbis-1.1.2\lib\bitrate.c"
# End Source File
# Begin Source File

SOURCE="..\..\sdks\libvorbis-1.1.2\lib\bitrate.h"
# End Source File
# Begin Source File

SOURCE="..\..\sdks\libvorbis-1.1.2\lib\block.c"
# End Source File
# Begin Source File

SOURCE="..\..\sdks\libvorbis-1.1.2\lib\codebook.c"
# End Source File
# Begin Source File

SOURCE="..\..\sdks\libvorbis-1.1.2\lib\codebook.h"
# End Source File
# Begin Source File

SOURCE="..\..\sdks\libvorbis-1.1.2\lib\codec_internal.h"
# End Source File
# Begin Source File

SOURCE="..\..\sdks\libvorbis-1.1.2\lib\envelope.c"
# End Source File
# Begin Source File

SOURCE="..\..\sdks\libvorbis-1.1.2\lib\envelope.h"
# End Source File
# Begin Source File

SOURCE="..\..\sdks\libvorbis-1.1.2\lib\floor0.c"
# End Source File
# Begin Source File

SOURCE="..\..\sdks\libvorbis-1.1.2\lib\floor1.c"
# End Source File
# Begin Source File

SOURCE="..\..\sdks\libvorbis-1.1.2\lib\highlevel.h"
# End Source File
# Begin Source File

SOURCE="..\..\sdks\libvorbis-1.1.2\lib\info.c"
# End Source File
# Begin Source File

SOURCE="..\..\sdks\libvorbis-1.1.2\lib\lookup.c"
# End Source File
# Begin Source File

SOURCE="..\..\sdks\libvorbis-1.1.2\lib\lookup.h"
# End Source File
# Begin Source File

SOURCE="..\..\sdks\libvorbis-1.1.2\lib\lookup_data.h"
# End Source File
# Begin Source File

SOURCE="..\..\sdks\libvorbis-1.1.2\lib\lpc.c"
# End Source File
# Begin Source File

SOURCE="..\..\sdks\libvorbis-1.1.2\lib\lpc.h"
# End Source File
# Begin Source File

SOURCE="..\..\sdks\libvorbis-1.1.2\lib\lsp.c"
# End Source File
# Begin Source File

SOURCE="..\..\sdks\libvorbis-1.1.2\lib\lsp.h"
# End Source File
# Begin Source File

SOURCE="..\..\sdks\libvorbis-1.1.2\lib\mapping0.c"
# End Source File
# Begin Source File

SOURCE="..\..\sdks\libvorbis-1.1.2\lib\masking.h"
# End Source File
# Begin Source File

SOURCE="..\..\sdks\libvorbis-1.1.2\lib\mdct.c"
# End Source File
# Begin Source File

SOURCE="..\..\sdks\libvorbis-1.1.2\lib\mdct.h"
# End Source File
# Begin Source File

SOURCE="..\..\sdks\libvorbis-1.1.2\lib\misc.h"
# End Source File
# Begin Source File

SOURCE="..\..\sdks\libvorbis-1.1.2\lib\os.h"
# End Source File
# Begin Source File

SOURCE="..\..\sdks\libvorbis-1.1.2\lib\psy.c"
# End Source File
# Begin Source File

SOURCE="..\..\sdks\libvorbis-1.1.2\lib\psy.h"
# End Source File
# Begin Source File

SOURCE="..\..\sdks\libvorbis-1.1.2\lib\registry.c"
# End Source File
# Begin Source File

SOURCE="..\..\sdks\libvorbis-1.1.2\lib\registry.h"
# End Source File
# Begin Source File

SOURCE="..\..\sdks\libvorbis-1.1.2\lib\res0.c"
# End Source File
# Begin Source File

SOURCE="..\..\sdks\libvorbis-1.1.2\lib\scales.h"
# End Source File
# Begin Source File

SOURCE="..\..\sdks\libvorbis-1.1.2\lib\sharedbook.c"
# End Source File
# Begin Source File

SOURCE="..\..\sdks\libvorbis-1.1.2\lib\smallft.c"
# End Source File
# Begin Source File

SOURCE="..\..\sdks\libvorbis-1.1.2\lib\smallft.h"
# End Source File
# Begin Source File

SOURCE="..\..\sdks\libvorbis-1.1.2\lib\synthesis.c"
# End Source File
# Begin Source File

SOURCE="..\..\sdks\libvorbis-1.1.2\lib\vorbisfile.c"
# End Source File
# Begin Source File

SOURCE="..\..\sdks\libvorbis-1.1.2\lib\window.c"
# End Source File
# Begin Source File

SOURCE="..\..\sdks\libvorbis-1.1.2\lib\window.h"
# End Source File
# End Group
# Begin Source File

SOURCE=.\autosong.cpp
# End Source File
# Begin Source File

SOURCE=..\..\WDL\lameencdec.cpp
# End Source File
# Begin Source File

SOURCE=..\..\WDL\lameencdec.h
# End Source File
# End Group
# Begin Group "Header Files"

# PROP Default_Filter "h;hpp;hxx;hm;inl"
# Begin Source File

SOURCE=..\..\WDL\mp3write.h
# End Source File
# End Group
# Begin Group "Resource Files"

# PROP Default_Filter "ico;cur;bmp;dlg;rc2;rct;bin;rgs;gif;jpg;jpeg;jpe"
# End Group
# End Target
# End Project
