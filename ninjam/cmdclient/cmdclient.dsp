# Microsoft Developer Studio Project File - Name="cmdclient" - Package Owner=<4>
# Microsoft Developer Studio Generated Build File, Format Version 6.00
# ** DO NOT EDIT **

# TARGTYPE "Win32 (x86) Console Application" 0x0103

CFG=cmdclient - Win32 Debug
!MESSAGE This is not a valid makefile. To build this project using NMAKE,
!MESSAGE use the Export Makefile command and run
!MESSAGE 
!MESSAGE NMAKE /f "cmdclient.mak".
!MESSAGE 
!MESSAGE You can specify a configuration when running NMAKE
!MESSAGE by defining the macro CFG on the command line. For example:
!MESSAGE 
!MESSAGE NMAKE /f "cmdclient.mak" CFG="cmdclient - Win32 Debug"
!MESSAGE 
!MESSAGE Possible choices for configuration are:
!MESSAGE 
!MESSAGE "cmdclient - Win32 Release" (based on "Win32 (x86) Console Application")
!MESSAGE "cmdclient - Win32 Debug" (based on "Win32 (x86) Console Application")
!MESSAGE 

# Begin Project
# PROP AllowPerConfigDependencies 0
# PROP Scc_ProjName ""
# PROP Scc_LocalPath ""
CPP=cl.exe
RSC=rc.exe

!IF  "$(CFG)" == "cmdclient - Win32 Release"

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
# ADD CPP /nologo /MT /W3 /GX /O2 /I "../../sdks/oggvorbis-win32sdk-1.0.1/include" /I "../asio" /I "." /D "WIN32" /D "NDEBUG" /D "_CONSOLE" /D "_MBCS" /YX /FD /c
# ADD BASE RSC /l 0x409 /d "NDEBUG"
# ADD RSC /l 0x409 /d "NDEBUG"
BSC32=bscmake.exe
# ADD BASE BSC32 /nologo
# ADD BSC32 /nologo
LINK32=link.exe
# ADD BASE LINK32 kernel32.lib user32.lib gdi32.lib winspool.lib comdlg32.lib advapi32.lib shell32.lib ole32.lib oleaut32.lib uuid.lib odbc32.lib odbccp32.lib kernel32.lib user32.lib gdi32.lib winspool.lib comdlg32.lib advapi32.lib shell32.lib ole32.lib oleaut32.lib uuid.lib odbc32.lib odbccp32.lib /nologo /subsystem:console /machine:I386
# ADD LINK32 kernel32.lib user32.lib gdi32.lib winspool.lib comdlg32.lib advapi32.lib shell32.lib ole32.lib oleaut32.lib uuid.lib odbc32.lib odbccp32.lib kernel32.lib user32.lib gdi32.lib winspool.lib comdlg32.lib advapi32.lib shell32.lib ole32.lib oleaut32.lib uuid.lib odbc32.lib odbccp32.lib wsock32.lib dsound.lib winmm.lib /nologo /subsystem:console /machine:I386

!ELSEIF  "$(CFG)" == "cmdclient - Win32 Debug"

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
# ADD CPP /nologo /MTd /W3 /Gm /GX /ZI /Od /I "../../sdks/oggvorbis-win32sdk-1.0.1/include" /I "../asio" /I "." /D "WIN32" /D "_DEBUG" /D "_CONSOLE" /D "_MBCS" /YX /FD /GZ /c
# ADD BASE RSC /l 0x409 /d "_DEBUG"
# ADD RSC /l 0x409 /d "_DEBUG"
BSC32=bscmake.exe
# ADD BASE BSC32 /nologo
# ADD BSC32 /nologo
LINK32=link.exe
# ADD BASE LINK32 kernel32.lib user32.lib gdi32.lib winspool.lib comdlg32.lib advapi32.lib shell32.lib ole32.lib oleaut32.lib uuid.lib odbc32.lib odbccp32.lib kernel32.lib user32.lib gdi32.lib winspool.lib comdlg32.lib advapi32.lib shell32.lib ole32.lib oleaut32.lib uuid.lib odbc32.lib odbccp32.lib /nologo /subsystem:console /debug /machine:I386 /pdbtype:sept
# ADD LINK32 kernel32.lib user32.lib gdi32.lib winspool.lib comdlg32.lib advapi32.lib shell32.lib ole32.lib oleaut32.lib uuid.lib odbc32.lib odbccp32.lib kernel32.lib user32.lib gdi32.lib winspool.lib comdlg32.lib advapi32.lib shell32.lib ole32.lib oleaut32.lib uuid.lib odbc32.lib odbccp32.lib dsound.lib winmm.lib wsock32.lib /nologo /subsystem:console /debug /machine:I386 /pdbtype:sept

!ENDIF 

# Begin Target

# Name "cmdclient - Win32 Release"
# Name "cmdclient - Win32 Debug"
# Begin Group "Source Files"

# PROP Default_Filter "cpp;c;cxx;rc;def;r;odl;idl;hpj;bat"
# Begin Group "common"

# PROP Default_Filter ""
# Begin Group "jnetlib"

# PROP Default_Filter ""
# Begin Source File

SOURCE=..\..\WDL\jnetlib\asyncdns.cpp
# End Source File
# Begin Source File

SOURCE=..\..\WDL\jnetlib\asyncdns.h
# End Source File
# Begin Source File

SOURCE=..\..\WDL\jnetlib\connection.cpp
# End Source File
# Begin Source File

SOURCE=..\..\WDL\jnetlib\connection.h
# End Source File
# Begin Source File

SOURCE=..\..\WDL\jnetlib\httpget.cpp
# End Source File
# Begin Source File

SOURCE=..\..\WDL\jnetlib\httpget.h
# End Source File
# Begin Source File

SOURCE=..\..\WDL\jnetlib\httpserv.cpp
# End Source File
# Begin Source File

SOURCE=..\..\WDL\jnetlib\httpserv.h
# End Source File
# Begin Source File

SOURCE=..\..\WDL\jnetlib\jnetlib.h
# End Source File
# Begin Source File

SOURCE=..\..\WDL\jnetlib\listen.cpp
# End Source File
# Begin Source File

SOURCE=..\..\WDL\jnetlib\listen.h
# End Source File
# Begin Source File

SOURCE=..\..\WDL\jnetlib\netinc.h
# End Source File
# Begin Source File

SOURCE=..\..\WDL\jnetlib\util.cpp
# End Source File
# Begin Source File

SOURCE=..\..\WDL\jnetlib\util.h
# End Source File
# End Group
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
# Begin Source File

SOURCE=..\audioconfig.cpp
# End Source File
# Begin Source File

SOURCE=..\audiostream_win32.cpp
# End Source File
# Begin Source File

SOURCE=..\mpb.cpp
# End Source File
# Begin Source File

SOURCE=..\netmsg.cpp
# End Source File
# Begin Source File

SOURCE=..\..\WDL\rng.cpp
# End Source File
# Begin Source File

SOURCE=..\..\WDL\rng.h
# End Source File
# Begin Source File

SOURCE=..\..\WDL\sha.cpp
# End Source File
# Begin Source File

SOURCE=..\..\WDL\sha.h
# End Source File
# Begin Source File

SOURCE="..\..\sdks\oggvorbis-win32sdk-1.0.1\lib\ogg_static.lib"

!IF  "$(CFG)" == "cmdclient - Win32 Release"

!ELSEIF  "$(CFG)" == "cmdclient - Win32 Debug"

# PROP Exclude_From_Build 1

!ENDIF 

# End Source File
# Begin Source File

SOURCE="..\..\sdks\oggvorbis-win32sdk-1.0.1\lib\vorbis_static.lib"

!IF  "$(CFG)" == "cmdclient - Win32 Release"

!ELSEIF  "$(CFG)" == "cmdclient - Win32 Debug"

# PROP Exclude_From_Build 1

!ENDIF 

# End Source File
# Begin Source File

SOURCE="..\..\sdks\oggvorbis-win32sdk-1.0.1\lib\vorbisenc_static.lib"

!IF  "$(CFG)" == "cmdclient - Win32 Release"

!ELSEIF  "$(CFG)" == "cmdclient - Win32 Debug"

# PROP Exclude_From_Build 1

!ENDIF 

# End Source File
# Begin Source File

SOURCE="..\..\sdks\oggvorbis-win32sdk-1.0.1\lib\ogg_static_d.lib"

!IF  "$(CFG)" == "cmdclient - Win32 Release"

# PROP Exclude_From_Build 1

!ELSEIF  "$(CFG)" == "cmdclient - Win32 Debug"

!ENDIF 

# End Source File
# Begin Source File

SOURCE="..\..\sdks\oggvorbis-win32sdk-1.0.1\lib\vorbis_static_d.lib"

!IF  "$(CFG)" == "cmdclient - Win32 Release"

# PROP Exclude_From_Build 1

!ELSEIF  "$(CFG)" == "cmdclient - Win32 Debug"

!ENDIF 

# End Source File
# Begin Source File

SOURCE="..\..\sdks\oggvorbis-win32sdk-1.0.1\lib\vorbisenc_static_d.lib"

!IF  "$(CFG)" == "cmdclient - Win32 Release"

# PROP Exclude_From_Build 1

!ELSEIF  "$(CFG)" == "cmdclient - Win32 Debug"

!ENDIF 

# End Source File
# End Group
# Begin Source File

SOURCE=.\cmdclientres.rc
# End Source File
# Begin Source File

SOURCE=.\ninjamcmdclient.cpp
# End Source File
# End Group
# Begin Group "Header Files"

# PROP Default_Filter "h;hpp;hxx;hm;inl"
# Begin Group "common headers"

# PROP Default_Filter ""
# Begin Source File

SOURCE=..\mpb.h
# End Source File
# Begin Source File

SOURCE=..\netmsg.h
# End Source File
# Begin Source File

SOURCE=..\pcmfmtcvt.h
# End Source File
# Begin Source File

SOURCE=..\vorbisencdec.h
# End Source File
# End Group
# Begin Source File

SOURCE=..\audiostream.h
# End Source File
# End Group
# Begin Group "Resource Files"

# PROP Default_Filter "ico;cur;bmp;dlg;rc2;rct;bin;rgs;gif;jpg;jpeg;jpe"
# End Group
# End Target
# End Project
