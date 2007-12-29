# Microsoft Developer Studio Project File - Name="reaninjam" - Package Owner=<4>
# Microsoft Developer Studio Generated Build File, Format Version 6.00
# ** DO NOT EDIT **

# TARGTYPE "Win32 (x86) Dynamic-Link Library" 0x0102

CFG=reaninjam - Win32 Debug
!MESSAGE This is not a valid makefile. To build this project using NMAKE,
!MESSAGE use the Export Makefile command and run
!MESSAGE 
!MESSAGE NMAKE /f "reaninjam.mak".
!MESSAGE 
!MESSAGE You can specify a configuration when running NMAKE
!MESSAGE by defining the macro CFG on the command line. For example:
!MESSAGE 
!MESSAGE NMAKE /f "reaninjam.mak" CFG="reaninjam - Win32 Debug"
!MESSAGE 
!MESSAGE Possible choices for configuration are:
!MESSAGE 
!MESSAGE "reaninjam - Win32 Release" (based on "Win32 (x86) Dynamic-Link Library")
!MESSAGE "reaninjam - Win32 Debug" (based on "Win32 (x86) Dynamic-Link Library")
!MESSAGE 

# Begin Project
# PROP AllowPerConfigDependencies 0
# PROP Scc_ProjName ""
# PROP Scc_LocalPath ""
CPP=xicl6.exe
MTL=midl.exe
RSC=rc.exe

!IF  "$(CFG)" == "reaninjam - Win32 Release"

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
# ADD BASE CPP /nologo /MT /W3 /GX /O2 /D "WIN32" /D "NDEBUG" /D "_WINDOWS" /D "_MBCS" /D "_USRDLL" /D "REAGATE_EXPORTS" /YX /FD /c
# ADD CPP /nologo /MD /W3 /GX- /O2 /I "../../../sdks/libvorbis-1.1.2/include" /I "../../../sdks/libogg-1.1.3/include" /D "WIN32" /D "NDEBUG" /D "_WINDOWS" /D "_MBCS" /D "_USRDLL" /D "REAGATE_EXPORTS" /D "REANINJAM" /YX /FD /c
# ADD BASE MTL /nologo /D "NDEBUG" /mktyplib203 /win32
# ADD MTL /nologo /D "NDEBUG" /mktyplib203 /win32
# ADD BASE RSC /l 0x409 /d "NDEBUG"
# ADD RSC /l 0x409 /d "NDEBUG"
BSC32=bscmake.exe
# ADD BASE BSC32 /nologo
# ADD BSC32 /nologo
LINK32=xilink6.exe
# ADD BASE LINK32 kernel32.lib user32.lib gdi32.lib winspool.lib comdlg32.lib advapi32.lib shell32.lib ole32.lib oleaut32.lib uuid.lib odbc32.lib odbccp32.lib /nologo /dll /machine:I386
# ADD LINK32 kernel32.lib user32.lib gdi32.lib winspool.lib comdlg32.lib advapi32.lib shell32.lib ole32.lib oleaut32.lib uuid.lib odbc32.lib odbccp32.lib winmm.lib wsock32.lib comctl32.lib /nologo /dll /machine:I386 /out:"../../Release/plugins/fx/reaninjam.dll" /opt:nowin98
# SUBTRACT LINK32 /pdb:none

!ELSEIF  "$(CFG)" == "reaninjam - Win32 Debug"

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
# ADD BASE CPP /nologo /MTd /W3 /Gm /GX /ZI /Od /D "WIN32" /D "_DEBUG" /D "_WINDOWS" /D "_MBCS" /D "_USRDLL" /D "REAGATE_EXPORTS" /YX /FD /GZ /c
# ADD CPP /nologo /MTd /W3 /Gm /GX /ZI /Od /I "../../../sdks/libvorbis-1.1.2/include" /I "../../../sdks/libogg-1.1.3/include" /D "WIN32" /D "_DEBUG" /D "_WINDOWS" /D "_MBCS" /D "_USRDLL" /D "REAGATE_EXPORTS" /D "REANINJAM" /YX /FD /GZ /c
# ADD BASE MTL /nologo /D "_DEBUG" /mktyplib203 /win32
# ADD MTL /nologo /D "_DEBUG" /mktyplib203 /win32
# ADD BASE RSC /l 0x409 /d "_DEBUG"
# ADD RSC /l 0x409 /d "_DEBUG"
BSC32=bscmake.exe
# ADD BASE BSC32 /nologo
# ADD BSC32 /nologo
LINK32=xilink6.exe
# ADD BASE LINK32 kernel32.lib user32.lib gdi32.lib winspool.lib comdlg32.lib advapi32.lib shell32.lib ole32.lib oleaut32.lib uuid.lib odbc32.lib odbccp32.lib /nologo /dll /debug /machine:I386 /pdbtype:sept
# ADD LINK32 kernel32.lib user32.lib gdi32.lib winspool.lib comdlg32.lib advapi32.lib shell32.lib ole32.lib oleaut32.lib uuid.lib odbc32.lib odbccp32.lib winmm.lib wsock32.lib comctl32.lib /nologo /dll /debug /machine:I386 /out:"../../Debug/plugins/fx/reaninjam.dll" /pdbtype:sept

!ENDIF 

# Begin Target

# Name "reaninjam - Win32 Release"
# Name "reaninjam - Win32 Debug"
# Begin Group "Source Files"

# PROP Default_Filter "cpp;c;cxx;rc;def;r;odl;idl;hpj;bat"
# Begin Group "ninjam"

# PROP Default_Filter ""
# Begin Group "common"

# PROP Default_Filter ""
# Begin Source File

SOURCE=..\..\..\ninjam\mpb.cpp
# End Source File
# Begin Source File

SOURCE=..\..\..\ninjam\netmsg.cpp
# End Source File
# Begin Source File

SOURCE=..\..\..\ninjam\njclient.cpp

!IF  "$(CFG)" == "reaninjam - Win32 Release"

# ADD CPP /D "USE_ICC"

!ELSEIF  "$(CFG)" == "reaninjam - Win32 Debug"

!ENDIF 

# End Source File
# End Group
# Begin Group "jnetlib"

# PROP Default_Filter ""
# Begin Source File

SOURCE=..\..\..\WDL\jnetlib\asyncdns.cpp
# End Source File
# Begin Source File

SOURCE=..\..\..\WDL\jnetlib\asyncdns.h
# End Source File
# Begin Source File

SOURCE=..\..\..\WDL\jnetlib\connection.cpp
# End Source File
# Begin Source File

SOURCE=..\..\..\WDL\jnetlib\connection.h
# End Source File
# Begin Source File

SOURCE=..\..\..\WDL\jnetlib\httpget.cpp
# End Source File
# Begin Source File

SOURCE=..\..\..\WDL\jnetlib\jnetlib.h
# End Source File
# Begin Source File

SOURCE=..\..\..\WDL\jnetlib\netinc.h
# End Source File
# Begin Source File

SOURCE=..\..\..\WDL\jnetlib\util.cpp
# End Source File
# Begin Source File

SOURCE=..\..\..\WDL\jnetlib\util.h
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

SOURCE=.\winclient.h
# End Source File
# End Group
# Begin Group "wdl"

# PROP Default_Filter ""
# Begin Source File

SOURCE=..\..\..\WDL\rng.cpp
# End Source File
# Begin Source File

SOURCE=..\..\..\WDL\rng.h
# End Source File
# Begin Source File

SOURCE=..\..\..\WDL\sha.cpp
# End Source File
# Begin Source File

SOURCE=..\..\..\WDL\sha.h
# End Source File
# Begin Source File

SOURCE=..\..\..\WDL\wingui\wndsize.cpp
# End Source File
# Begin Source File

SOURCE=..\..\..\WDL\wingui\wndsize.h
# End Source File
# End Group
# Begin Source File

SOURCE=.\res.rc
# End Source File
# Begin Source File

SOURCE=.\vstframe.cpp
# End Source File
# End Group
# Begin Group "Header Files"

# PROP Default_Filter "h;hpp;hxx;hm;inl"
# Begin Source File

SOURCE=..\..\sdks\vstsdk2.3\source\common\AEffect.h
# End Source File
# Begin Source File

SOURCE=..\..\sdks\vstsdk2.3\source\common\aeffectx.h
# End Source File
# Begin Source File

SOURCE=.\resource.h
# End Source File
# End Group
# Begin Group "Resource Files"

# PROP Default_Filter "ico;cur;bmp;dlg;rc2;rct;bin;rgs;gif;jpg;jpeg;jpe"
# End Group
# End Target
# End Project
