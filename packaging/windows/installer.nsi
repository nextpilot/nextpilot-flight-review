; PlotScope Windows 安装包（NSIS 3）
;
; 由 tools/pack-windows.ps1 调用：
;   makensis /DVERSION=<ver> /DSTAGE=<payload目录> /DOUTFILE=<输出exe> packaging\windows\installer.nsi
;
; STAGE 目录由脚本准备，结构固定：
;   bin\plotscope-gui.exe    GUI 主程序
;   bin\plotscope.exe        headless CLI
;   bin\*.dll                windeployqt 部署的 Qt 运行库 + ads.dll
;   bin\platforms\...        Qt 平台插件

Unicode true
SetCompressor /SOLID lzma

!include "MUI2.nsh"
!include "LogicLib.nsh"
!include "x64.nsh"

!ifndef VERSION
  !define VERSION "0.0.0"
!endif
!ifndef STAGE
  !error "STAGE 未定义：请用 /DSTAGE=<payload目录> 指定"
!endif
!ifndef OUTFILE
  !define OUTFILE "PlotScope-Setup.exe"
!endif

Name "PlotScope"
OutFile "${OUTFILE}"
BrandingText "NextPilot"
InstallDir "$PROGRAMFILES64\PlotScope"
InstallDirRegKey HKLM "Software\PlotScope" "InstallDir"
RequestExecutionLevel admin
ShowInstDetails show
ShowUninstDetails show

VIProductVersion "${VERSION}.0"
VIAddVersionKey /LANG=2052 "ProductName" "PlotScope"
VIAddVersionKey /LANG=2052 "CompanyName" "NextPilot"
VIAddVersionKey /LANG=2052 "FileDescription" "PlotScope 安装程序"
VIAddVersionKey /LANG=2052 "FileVersion" "${VERSION}"
VIAddVersionKey /LANG=2052 "ProductVersion" "${VERSION}"
VIAddVersionKey /LANG=2052 "LegalCopyright" "BSD 3-Clause"

; ${__FILEDIR__} 末尾不带分隔符，必须自己补 '\'，否则 NSIS 会拼成
; packaging\windowsplotscope.ico 并报 "Error while loading icon ... can't open file"。
!define MUI_ICON "${__FILEDIR__}\plotscope.ico"
!define MUI_UNICON "${__FILEDIR__}\plotscope.ico"
!define MUI_ABORTWARNING

!insertmacro MUI_PAGE_WELCOME
!insertmacro MUI_PAGE_DIRECTORY
!insertmacro MUI_PAGE_INSTFILES
!define MUI_FINISHPAGE_RUN "$INSTDIR\bin\plotscope-gui.exe"
!define MUI_FINISHPAGE_RUN_TEXT "立即启动 PlotScope"
!insertmacro MUI_PAGE_FINISH

!insertmacro MUI_UNPAGE_CONFIRM
!insertmacro MUI_UNPAGE_INSTFILES

!insertmacro MUI_LANGUAGE "English"
!insertmacro MUI_LANGUAGE "SimpChinese"

Section "PlotScope（GUI + CLI）" SecMain
  SectionIn RO
  SetOutPath "$INSTDIR"
  File /r "${STAGE}\*.*"

  WriteRegStr HKLM "Software\PlotScope" "InstallDir" "$INSTDIR"
  WriteRegStr HKLM "Software\PlotScope" "Version" "${VERSION}"

  ; 控制面板「程序和功能」条目
  WriteRegStr HKLM "Software\Microsoft\Windows\CurrentVersion\Uninstall\PlotScope" "DisplayName" "PlotScope"
  WriteRegStr HKLM "Software\Microsoft\Windows\CurrentVersion\Uninstall\PlotScope" "DisplayVersion" "${VERSION}"
  WriteRegStr HKLM "Software\Microsoft\Windows\CurrentVersion\Uninstall\PlotScope" "Publisher" "NextPilot"
  WriteRegStr HKLM "Software\Microsoft\Windows\CurrentVersion\Uninstall\PlotScope" "DisplayIcon" "$INSTDIR\bin\plotscope-gui.exe"
  WriteRegStr HKLM "Software\Microsoft\Windows\CurrentVersion\Uninstall\PlotScope" "UninstallString" '"$INSTDIR\uninstall.exe"'
  WriteRegDWORD HKLM "Software\Microsoft\Windows\CurrentVersion\Uninstall\PlotScope" "NoModify" 1
  WriteRegDWORD HKLM "Software\Microsoft\Windows\CurrentVersion\Uninstall\PlotScope" "NoRepair" 1

  WriteUninstaller "$INSTDIR\uninstall.exe"

  CreateDirectory "$SMPROGRAMS\PlotScope"
  CreateShortCut "$SMPROGRAMS\PlotScope\PlotScope.lnk" "$INSTDIR\bin\plotscope-gui.exe" "" "$INSTDIR\bin\plotscope-gui.exe" 0
  ; CLI 给一个命令行窗口入口，不改系统 PATH（避免动用户环境变量）
  CreateShortCut "$SMPROGRAMS\PlotScope\PlotScope CLI.lnk" "$SYSDIR\cmd.exe" "/k cd /d $INSTDIR\bin" "$SYSDIR\cmd.exe" 0
  CreateShortCut "$SMPROGRAMS\PlotScope\卸载 PlotScope.lnk" "$INSTDIR\uninstall.exe"
  CreateShortCut "$DESKTOP\PlotScope.lnk" "$INSTDIR\bin\plotscope-gui.exe" "" "$INSTDIR\bin\plotscope-gui.exe" 0
SectionEnd

Section "Uninstall"
  Delete "$SMPROGRAMS\PlotScope\PlotScope.lnk"
  Delete "$SMPROGRAMS\PlotScope\PlotScope CLI.lnk"
  Delete "$SMPROGRAMS\PlotScope\卸载 PlotScope.lnk"
  RMDir "$SMPROGRAMS\PlotScope"
  Delete "$DESKTOP\PlotScope.lnk"

  DeleteRegKey HKLM "Software\Microsoft\Windows\CurrentVersion\Uninstall\PlotScope"
  DeleteRegKey HKLM "Software\PlotScope"

  RMDir /r "$INSTDIR"
SectionEnd
