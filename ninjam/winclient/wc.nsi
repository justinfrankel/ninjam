;Include Modern UI


!define MUI_COMPONENTSPAGE_NODESC

  !include "MUI.nsh"

;--------------------------------
;General

!define VER_MAJOR 0
!define VER_MINOR 06

SetCompressor lzma


  ;Name and file
  Name "NINJAM ${VER_MAJOR}.${VER_MINOR}"
  OutFile "ninjam${VER_MAJOR}${VER_MINOR}-install.exe"

  ;Default installation folder
  InstallDir "$PROGRAMFILES\NINJAM"
  
  ;Get installation folder from registry if available
  InstallDirRegKey HKLM "Software\NINJAM" ""

;--------------------------------
;Interface Settings

  !define MUI_ABORTWARNING

;--------------------------------
;Pages

  !insertmacro MUI_PAGE_LICENSE "license.txt"
  !insertmacro MUI_PAGE_COMPONENTS
  !insertmacro MUI_PAGE_DIRECTORY
  !insertmacro MUI_PAGE_INSTFILES
  
  !insertmacro MUI_UNPAGE_CONFIRM
  !insertmacro MUI_UNPAGE_INSTFILES
  
;--------------------------------
;Languages
 
  !insertmacro MUI_LANGUAGE "English"




;--------------------------------
;Installer Sections


Section "NINJAM"

  SectionIn RO
  SetOutPath "$INSTDIR"


  File release\ninjam.exe
    
  ;Store installation folder
  WriteRegStr HKLM "Software\NINJAM" "" $INSTDIR
  
  ;Create uninstaller
  WriteUninstaller "$INSTDIR\Uninstall.exe"

  File license.txt
  File whatsnew.txt

SectionEnd

Section "Effect Processing and Drum Machine support"

  File jesus.dll

  File /r release\Effects
  File /r release\Data
  CreateDirectory $INSTDIR\Presets

SectionEnd

Section "ASIO Input/Output support"

  File Release\njasiodrv.dll

SectionEnd

Section "NINJAM Session Converter"
  File ..\cliplogcvt\release\cliplogcvt.exe
SectionEnd


Section "Start Menu Shortcuts"

  SetOutPath $SMPROGRAMS\NINJAM
  CreateShortcut "$OUTDIR\NINJAM.lnk" "$INSTDIR\ninjam.exe"
  CreateShortcut "$OUTDIR\NINJAM License.lnk" "$INSTDIR\license.txt"
  CreateShortcut "$OUTDIR\Whatsnew.txt.lnk" "$INSTDIR\whatsnew.txt"
  CreateShortcut "$OUTDIR\NINJAM web site.lnk" "http://www.ninjam.com"
  CreateShortcut "$OUTDIR\Uninstall NINJAM.lnk" "$INSTDIR\uninstall.exe"

  SetOutPath $INSTDIR

SectionEnd


;--------------------------------
;Uninstaller Section

Section "Uninstall"

  Delete "$INSTDIR\ninjam.exe"
  Delete "$INSTDIR\cliplogcvt.exe"

  Delete "$INSTDIR\jesus.dll"
  Delete "$INSTDIR\njasiodrv.dll"

  Delete "$INSTDIR\ninjam.ini"
  Delete "$INSTDIR\license.txt"
  Delete "$INSTDIR\whatsnew.txt"

  Delete "$INSTDIR\Uninstall.exe"
  Delete "$SMPROGRAMS\NINJAM\*.lnk"
  RMDir $SMPROGRAMS\NINJAM

  RMDir "$INSTDIR\Presets"
  RMDir "$INSTDIR\Effects"
  RMDir "$INSTDIR\Packages"
  RMDir "$INSTDIR\Data"
  RMDir "$INSTDIR"

  DeleteRegKey HKLM "Software\NINJAM"

 IfFileExists $INSTDIR\*.* "" skip

  MessageBox MB_OK|MB_YESNO "This uninstaller did not want to risk removing your NINJAM directory (sessions or custom effects may remain).$\r$\nWould you like to view the NINJAM directory for manual removal?" IDNO skip

   ExecShell "open" "$INSTDIR"

 skip:

SectionEnd
