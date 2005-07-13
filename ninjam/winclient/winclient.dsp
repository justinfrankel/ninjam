# Microsoft Developer Studio Project File - Name="winclient" - Package Owner=<4>
# Microsoft Developer Studio Generated Build File, Format Version 6.00
# ** DO NOT EDIT **

# TARGTYPE "Win32 (x86) Console Application" 0x0103

CFG=winclient - Win32 Debug
!MESSAGE This is not a valid makefile. To build this project using NMAKE,
!MESSAGE use the Export Makefile command and run
!MESSAGE 
!MESSAGE NMAKE /f "winclient.mak".
!MESSAGE 
!MESSAGE You can specify a configuration when running NMAKE
!MESSAGE by defining the macro CFG on the command line. For example:
!MESSAGE 
!MESSAGE NMAKE /f "winclient.mak" CFG="winclient - Win32 Debug"
!MESSAGE 
!MESSAGE Possible choices for configuration are:
!MESSAGE 
!MESSAGE "winclient - Win32 Release" (based on "Win32 (x86) Console Application")
!MESSAGE "winclient - Win32 Debug" (based on "Win32 (x86) Console Application")
!MESSAGE 

# Begin Project
# PROP AllowPerConfigDependencies 0
# PROP Scc_ProjName ""
# PROP Scc_LocalPath ""
CPP=cl.exe
RSC=rc.exe

!IF  "$(CFG)" == "winclient - Win32 Release"

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
# ADD CPP /nologo /MT /W3 /GX /O2 /I "C:\DXSDK\include" /I "../../sdks/oggvorbis-win32sdk-1.0.1/include" /I "." /D "WIN32" /D "NDEBUG" /D "_WINDOWS" /D "_MBCS" /YX /FD /c
# ADD BASE RSC /l 0x409 /d "NDEBUG"
# ADD RSC /l 0x409 /d "NDEBUG"
BSC32=bscmake.exe
# ADD BASE BSC32 /nologo
# ADD BSC32 /nologo
LINK32=link.exe
# ADD BASE LINK32 kernel32.lib user32.lib gdi32.lib winspool.lib comdlg32.lib advapi32.lib shell32.lib ole32.lib oleaut32.lib uuid.lib odbc32.lib odbccp32.lib kernel32.lib user32.lib gdi32.lib winspool.lib comdlg32.lib advapi32.lib shell32.lib ole32.lib oleaut32.lib uuid.lib odbc32.lib odbccp32.lib /nologo /subsystem:console /machine:I386
# ADD LINK32 kernel32.lib user32.lib gdi32.lib winspool.lib comdlg32.lib advapi32.lib shell32.lib ole32.lib oleaut32.lib uuid.lib odbc32.lib odbccp32.lib kernel32.lib user32.lib gdi32.lib winspool.lib comdlg32.lib advapi32.lib shell32.lib ole32.lib oleaut32.lib uuid.lib odbc32.lib odbccp32.lib wsock32.lib setupapi.lib winmm.lib dsound.lib comctl32.lib /nologo /subsystem:windows /machine:I386 /out:"Release/ninjam.exe" /opt:nowin98
# SUBTRACT LINK32 /pdb:none

!ELSEIF  "$(CFG)" == "winclient - Win32 Debug"

# PROP BASE Use_MFC 0
# PROP BASE Use_Debug_Libraries 1
# PROP BASE Output_Dir "Debug"
# PROP BASE Intermediate_Dir "Debug"
# PROP BASE Target_Dir ""
# PROP Use_MFC 0
# PROP Use_Debug_Libraries 1
# PROP Output_Dir "Debug"
# PROP Intermediate_Dir "Debug"
# PROP Ignore_Export_Lib 0
# PROP Target_Dir ""
# ADD BASE CPP /nologo /W3 /Gm /GX /ZI /Od /D "WIN32" /D "_DEBUG" /D "_CONSOLE" /D "_MBCS" /YX /FD /GZ /c
# ADD CPP /nologo /MTd /W3 /Gm /GX /ZI /Od /I "../../sdks/oggvorbis-win32sdk-1.0.1/include" /I "." /D "WIN32" /D "_DEBUG" /D "_CONSOLE" /D "_MBCS" /YX /FD /GZ /c
# ADD BASE RSC /l 0x409 /d "_DEBUG"
# ADD RSC /l 0x409 /d "_DEBUG"
BSC32=bscmake.exe
# ADD BASE BSC32 /nologo
# ADD BSC32 /nologo
LINK32=link.exe
# ADD BASE LINK32 kernel32.lib user32.lib gdi32.lib winspool.lib comdlg32.lib advapi32.lib shell32.lib ole32.lib oleaut32.lib uuid.lib odbc32.lib odbccp32.lib kernel32.lib user32.lib gdi32.lib winspool.lib comdlg32.lib advapi32.lib shell32.lib ole32.lib oleaut32.lib uuid.lib odbc32.lib odbccp32.lib /nologo /subsystem:console /debug /machine:I386 /pdbtype:sept
# ADD LINK32 kernel32.lib user32.lib gdi32.lib winspool.lib comdlg32.lib advapi32.lib shell32.lib ole32.lib oleaut32.lib uuid.lib odbc32.lib odbccp32.lib kernel32.lib user32.lib gdi32.lib winspool.lib comdlg32.lib advapi32.lib shell32.lib ole32.lib oleaut32.lib uuid.lib odbc32.lib odbccp32.lib wsock32.lib /nologo /subsystem:console /debug /machine:I386 /out:"Debug/ninjam.exe" /pdbtype:sept

!ENDIF 

# Begin Target

# Name "winclient - Win32 Release"
# Name "winclient - Win32 Debug"
# Begin Group "Source Files"

# PROP Default_Filter "cpp;c;cxx;rc;def;r;odl;idl;hpj;bat"
# Begin Group "common"

# PROP Default_Filter ""
# Begin Group "asio"

# PROP Default_Filter ""
# Begin Source File

SOURCE=..\asio\asio.cpp
# End Source File
# Begin Source File

SOURCE=..\asio\asiodrivers.cpp
# End Source File
# Begin Source File

SOURCE=..\asio\asiolist.cpp
# End Source File
# End Group
# Begin Group "wdl src"

# PROP Default_Filter ""
# Begin Group "jnetlib"

# PROP Default_Filter ""
# Begin Source File

SOURCE=..\..\WDL\jnetlib\asyncdns.cpp
# End Source File
# Begin Source File

SOURCE=..\..\WDL\jnetlib\connection.cpp
# End Source File
# Begin Source File

SOURCE=..\..\WDL\jnetlib\listen.cpp
# End Source File
# Begin Source File

SOURCE=..\..\WDL\jnetlib\util.cpp
# End Source File
# End Group
# Begin Source File

SOURCE=..\..\WDL\wingui\richeditctrl.h
# End Source File
# Begin Source File

SOURCE=..\..\WDL\rng.cpp
# End Source File
# Begin Source File

SOURCE=..\..\WDL\sha.cpp
# End Source File
# Begin Source File

SOURCE=..\..\WDL\wingui\wndsize.cpp
# End Source File
# Begin Source File

SOURCE=..\..\WDL\wingui\wndsize.h
# End Source File
# End Group
# Begin Group "ks"

# PROP Default_Filter ""
# Begin Source File

SOURCE=..\ks\audfilter.cpp
# End Source File
# Begin Source File

SOURCE=..\ks\audfilter.h
# End Source File
# Begin Source File

SOURCE=..\ks\audpin.cpp
# End Source File
# Begin Source File

SOURCE=..\ks\audpin.h
# End Source File
# Begin Source File

SOURCE=..\ks\enum.cpp
# End Source File
# Begin Source File

SOURCE=..\ks\enum.h
# End Source File
# Begin Source File

SOURCE=..\ks\filter.cpp
# End Source File
# Begin Source File

SOURCE=..\ks\filter.h
# End Source File
# Begin Source File

SOURCE=..\ks\irptgt.cpp
# End Source File
# Begin Source File

SOURCE=..\ks\irptgt.h
# End Source File
# Begin Source File

SOURCE=..\ks\kssample.h
# End Source File
# Begin Source File

SOURCE=..\ks\node.cpp
# End Source File
# Begin Source File

SOURCE=..\ks\node.h
# End Source File
# Begin Source File

SOURCE=..\ks\pin.cpp
# End Source File
# Begin Source File

SOURCE=..\ks\pin.h
# End Source File
# Begin Source File

SOURCE=..\ks\tlist.h
# End Source File
# Begin Source File

SOURCE="..\ks\util-ks.cpp"
# End Source File
# End Group
# Begin Source File

SOURCE=..\audioconfig.cpp
# End Source File
# Begin Source File

SOURCE=..\audiostream_asio.cpp
# End Source File
# Begin Source File

SOURCE=..\audiostream_ks.cpp
# End Source File
# Begin Source File

SOURCE=..\audiostream_win32.cpp

!IF  "$(CFG)" == "winclient - Win32 Release"

# ADD CPP /O2

!ELSEIF  "$(CFG)" == "winclient - Win32 Debug"

!ENDIF 

# End Source File
# Begin Source File

SOURCE=..\chanmix.cpp
# End Source File
# Begin Source File

SOURCE=..\chanmix.h
# End Source File
# Begin Source File

SOURCE=..\mpb.cpp
# End Source File
# Begin Source File

SOURCE=..\netmsg.cpp
# End Source File
# Begin Source File

SOURCE=..\njclient.cpp
# End Source File
# Begin Source File

SOURCE=..\njclient.h
# End Source File
# Begin Source File

SOURCE=..\njmisc.cpp
# End Source File
# Begin Source File

SOURCE=..\njmisc.h
# End Source File
# Begin Source File

SOURCE="..\..\sdks\oggvorbis-win32sdk-1.0.1\lib\vorbisenc_static.lib"
# End Source File
# Begin Source File

SOURCE="..\..\sdks\oggvorbis-win32sdk-1.0.1\lib\ogg_static.lib"
# End Source File
# Begin Source File

SOURCE="..\..\sdks\oggvorbis-win32sdk-1.0.1\lib\vorbis_static.lib"
# End Source File
# End Group
# Begin Source File

SOURCE=.\chat.cpp
# End Source File
# Begin Source File

SOURCE=.\license.cpp
# End Source File
# Begin Source File

SOURCE=.\locchn.cpp
# End Source File
# Begin Source File

SOURCE=.\remchn.cpp
# End Source File
# Begin Source File

SOURCE=.\winclient.cpp
# End Source File
# Begin Source File

SOURCE=.\winclientres.rc
# End Source File
# End Group
# Begin Group "Header Files"

# PROP Default_Filter "h;hpp;hxx;hm;inl"
# Begin Source File

SOURCE=..\audiostream_asio.h
# End Source File
# Begin Source File

SOURCE=..\mpb.h
# End Source File
# Begin Source File

SOURCE=..\..\WDL\mutex.h
# End Source File
# Begin Source File

SOURCE=..\netmsg.h
# End Source File
# Begin Source File

SOURCE=.\winclient.h
# End Source File
# End Group
# Begin Group "Resource Files"

# PROP Default_Filter "ico;cur;bmp;dlg;rc2;rct;bin;rgs;gif;jpg;jpeg;jpe"
# Begin Source File

SOURCE=.\bitmap1.bmp
# End Source File
# Begin Source File

SOURCE=.\ninjam_gui_win.ico
# End Source File
# End Group
# End Target
# End Project
