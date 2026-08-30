; package.nsi - NSIS installer for PXB Preview
;
; Flexible, path-agnostic installer using environment-driven file sourcing.
; Supports command-line parameter override for source directory.
;
; Usage:
;   makensis package.nsi                  # uses default SOURCE_DIR (../../../dist/...)
;   makensis /DSOURCE_DIR=E:\path\to\artifacts package.nsi   # custom source dir
;
; The Python build.py script automatically sets SOURCE_DIR correctly.

!include "MUI2.nsh"
!include "x64.nsh"
!include "FileFunc.nsh"
!include "LogicLib.nsh"   ; ${If}/${EndIf} used by the uninstall .pxb cleanup

; ============================================================
; Configuration & Versioning
; ============================================================

Name "PXB Preview"
InstallDir "$PROGRAMFILES\PxbPreview"
RequestExecutionLevel admin

; Version: injected by scripts/package_windows.ps1 from src/app/i18n.h
; (kAppVersion — the single source of truth) via /DAPP_VERSION. The default
; below is a fallback for manual makensis runs.
!ifndef APP_VERSION
  !define APP_VERSION "0.0.1"
!endif
!define APP_NAME "PXB Preview"
!define APP_PUBLISHER "pxb"

; ============================================================
; File-association contract values
; ============================================================
; MUST match src/core/pxb_reg_contract.h (the single source of truth).
; Packaging automation can inject them from the contract header instead of
; letting them drift here:
;   makensis /DPXB_PROGID=PxbPreview.pxbfile /DPXB_CLSID="{A1B2...}" \
;            /DPXB_SHELLEX_KEY="{E357FCCD-...}" package.nsi
!ifndef PXB_PROGID
  !define PXB_PROGID "PxbPreview.pxbfile"
!endif
!ifndef PXB_CLSID
  !define PXB_CLSID "{A1B2C3D4-0001-4E5F-8A9B-112233445566}"
!endif
!ifndef PXB_SHELLEX_KEY
  !define PXB_SHELLEX_KEY "{E357FCCD-A995-4576-B01F-234630154E96}"
!endif

; VIProductVersion must be "x.x.x.x"
VIProductVersion "${APP_VERSION}.0"
VIAddVersionKey "ProductName" "${APP_NAME}"
VIAddVersionKey "FileDescription" "PXB pixel-art previewer"
VIAddVersionKey "FileVersion" "${APP_VERSION}"
VIAddVersionKey "ProductVersion" "${APP_VERSION}"
VIAddVersionKey "LegalCopyright" "(c) mzb.one"
VIAddVersionKey "CompanyName" "${APP_PUBLISHER}"

; ============================================================
; Path Resolution: SOURCE_DIR can be passed via command-line
; or defaults to relative path (../../dist/pxb-preview-windows-x64/)
; ============================================================

; Fallbacks for MANUAL makensis runs only — scripts/package_windows.ps1 always
; overrides all of these with absolute /D defines. Keep the fallback roughly in
; step with the real layout (dist/windows/pxb-preview-<ver>-win64).
!ifndef SOURCE_DIR
  !define SOURCE_DIR "..\..\dist\windows\pxb-preview-0.0.1-win64"
!endif

!ifndef ASSETS_DIR
  !define ASSETS_DIR "..\..\assets"
!endif
!ifndef PROJECT_ROOT
  !define PROJECT_ROOT "..\..\."
!endif

; ============================================================
; Icon Configuration
; ============================================================

!define MUI_ICON "${ASSETS_DIR}\icon.ico"
!define MUI_UNICON "${ASSETS_DIR}\icon.ico"

; ============================================================
; NSIS Installer Pages & Language
; ============================================================

!insertmacro MUI_PAGE_WELCOME
!insertmacro MUI_PAGE_DIRECTORY
!insertmacro MUI_PAGE_INSTFILES
!insertmacro MUI_UNPAGE_CONFIRM
!insertmacro MUI_UNPAGE_INSTFILES
!insertmacro MUI_UNPAGE_FINISH

!insertmacro MUI_LANGUAGE "SimpChinese"
!insertmacro MUI_LANGUAGE "English"

; Language strings
LangString SectionInstall ${LANG_SIMPCHINESE} "安装"
LangString SectionInstall ${LANG_ENGLISH}      "Install"
LangString SectionUninstall ${LANG_SIMPCHINESE} "卸载"
LangString SectionUninstall ${LANG_ENGLISH}      "Uninstall"

LangString MissingFiles ${LANG_SIMPCHINESE} "错误：找不到必要的文件。请确保从正确的位置运行 build.py，或检查源文件目录。"
LangString MissingFiles ${LANG_ENGLISH}      "Error: Required files not found. Ensure build.py was run correctly, or check the source directory."

; Show language picker at startup
Function .onInit
  !insertmacro MUI_LANGDLL_DISPLAY
FunctionEnd

; ============================================================
; Installation Logic
; ============================================================

Section "$(SectionInstall)"
  SetOutPath "$INSTDIR"

  ; Verify required files exist before proceeding
  IfFileExists "${SOURCE_DIR}\pxb-preview.exe" files_exist
  MessageBox MB_ICONSTOP "$(MissingFiles)$\nMissing: ${SOURCE_DIR}\pxb-preview.exe"
  Abort

files_exist:

  ; Copy main executable
  File "${SOURCE_DIR}\pxb-preview.exe"
  
  ; Copy Windows runtime dependencies (if present)
  IfFileExists "${SOURCE_DIR}\SDL3.dll" have_sdl
  DetailPrint "Note: SDL3.dll not found (expected in portable distribution)"
  Goto skip_sdl
have_sdl:
  File "${SOURCE_DIR}\SDL3.dll"
skip_sdl:

  IfFileExists "${SOURCE_DIR}\libwinpthread-1.dll" have_pthread
  DetailPrint "Note: libwinpthread-1.dll not found"
  Goto skip_pthread
have_pthread:
  File "${SOURCE_DIR}\libwinpthread-1.dll"
skip_pthread:

  ; Copy thumbnail handler DLL (crucial for file preview)
  IfFileExists "${SOURCE_DIR}\PxbThumbnailHandler.dll" have_thumb
  MessageBox MB_ICONEXCLAMATION "Warning: PxbThumbnailHandler.dll not found.$\nFile thumbnails will not appear in Explorer."
  Goto skip_thumb
have_thumb:
  File "${SOURCE_DIR}\PxbThumbnailHandler.dll"
  
  ; Register COM thumbnail handler (writes CLSID + shellex hook + ProgID)
  RegDLL "$INSTDIR\PxbThumbnailHandler.dll"

skip_thumb:

  ; Copy icon and documentation
  IfFileExists "${SOURCE_DIR}\icon.png" have_icon
  Goto skip_icon
have_icon:
  File "${SOURCE_DIR}\icon.png"
skip_icon:

  IfFileExists "${PROJECT_ROOT}\README.md" have_readme
  Goto skip_readme
have_readme:
  File "${PROJECT_ROOT}\README.md"
skip_readme:

  ; ============================================================
  ; File Association & Registry Setup
  ; ============================================================

  ; Values come from the PXB_* defines at the top (single source: the
  ; contract header, injectable via /D at makensis time).
  WriteRegStr HKLM "Software\Classes\.pxb" "" "${PXB_PROGID}"
  WriteRegStr HKLM "Software\Classes\${PXB_PROGID}" "" "PXB Pixel Art"
  WriteRegStr HKLM "Software\Classes\${PXB_PROGID}\DefaultIcon" "" "$INSTDIR\pxb-preview.exe,0"
  WriteRegStr HKLM "Software\Classes\${PXB_PROGID}\shell\open\command" "" '"$INSTDIR\pxb-preview.exe" "%1"'

  ; Thumbnail handler shellex hook (CLSID matches DLL's DllRegisterServer contract)
  WriteRegStr HKLM "Software\Classes\SystemFileAssociations\.pxb\shellex\${PXB_SHELLEX_KEY}" "" "${PXB_CLSID}"
  WriteRegStr HKLM "Software\Classes\${PXB_PROGID}\shellex\${PXB_SHELLEX_KEY}" "" "${PXB_CLSID}"

  ; Clear stale per-user UserChoice override (would silently beat ProgID)
  DeleteRegKey HKCU "Software\Microsoft\Windows\CurrentVersion\Explorer\FileExts\.pxb\UserChoice"

  ; Refresh shell association cache (SHCNE_ASSOCCHANGED = 0x08000000)
  System::Call 'shell32::SHChangeNotify(i 0x08000000, i 0, i 0, i 0) v'

  ; ============================================================
  ; Uninstall Registration ("Apps & Features")
  ; ============================================================

  WriteUninstaller "$INSTDIR\uninstall.exe"
  WriteRegStr HKLM "Software\Microsoft\Windows\CurrentVersion\Uninstall\PxbPreview" \
                   "DisplayName" "${APP_NAME}"
  WriteRegStr HKLM "Software\Microsoft\Windows\CurrentVersion\Uninstall\PxbPreview" \
                   "DisplayVersion" "${APP_VERSION}"
  WriteRegStr HKLM "Software\Microsoft\Windows\CurrentVersion\Uninstall\PxbPreview" \
                   "Publisher" "${APP_PUBLISHER}"
  WriteRegStr HKLM "Software\Microsoft\Windows\CurrentVersion\Uninstall\PxbPreview" \
                   "DisplayIcon" "$INSTDIR\pxb-preview.exe"
  WriteRegStr HKLM "Software\Microsoft\Windows\CurrentVersion\Uninstall\PxbPreview" \
                   "UninstallString" '"$INSTDIR\uninstall.exe"'
  WriteRegStr HKLM "Software\Microsoft\Windows\CurrentVersion\Uninstall\PxbPreview" \
                   "InstallLocation" "$INSTDIR"
  WriteRegDWORD HKLM "Software\Microsoft\Windows\CurrentVersion\Uninstall\PxbPreview" \
                   "NoModify" 1
  WriteRegDWORD HKLM "Software\Microsoft\Windows\CurrentVersion\Uninstall\PxbPreview" \
                   "NoRepair" 1

SectionEnd

; ============================================================
; Uninstall Logic
; ============================================================

Section "$(SectionUninstall)"
  
  ; DllUnregisterServer removes CLSID + shellex + ProgID (single source of truth)
  UnRegDLL "$INSTDIR\PxbThumbnailHandler.dll"

  ; Belt-and-braces: also explicitly delete the keys this installer wrote,
  ; in case UnRegDLL failed. Standard NSIS DeleteRegKey has no /r switch and
  ; refuses keys that still hold subkeys, so remove deepest-first.
  DeleteRegKey HKLM "Software\Classes\${PXB_PROGID}\DefaultIcon"
  DeleteRegKey HKLM "Software\Classes\${PXB_PROGID}\shell\open\command"
  DeleteRegKey HKLM "Software\Classes\${PXB_PROGID}\shell\open"
  DeleteRegKey HKLM "Software\Classes\${PXB_PROGID}\shell"
  DeleteRegKey HKLM "Software\Classes\${PXB_PROGID}\shellex\${PXB_SHELLEX_KEY}"
  DeleteRegKey HKLM "Software\Classes\${PXB_PROGID}\shellex"
  DeleteRegKey HKLM "Software\Classes\${PXB_PROGID}"
  DeleteRegKey HKLM "Software\Classes\SystemFileAssociations\.pxb\shellex\${PXB_SHELLEX_KEY}"

  ; Only drop the .pxb -> ProgID mapping if it still points at us — the same
  ; courtesy the app's unregister applies — so we never clobber another
  ; program's .pxb association that happened to be installed over ours.
  ReadRegStr $0 HKLM "Software\Classes\.pxb" ""
  ${If} $0 == "${PXB_PROGID}"
    DeleteRegKey HKLM "Software\Classes\.pxb"
  ${EndIf}

  DeleteRegKey HKCU "Software\Microsoft\Windows\CurrentVersion\Explorer\FileExts\.pxb\UserChoice"

  ; Uninstall entry from "Apps & Features"
  DeleteRegKey HKLM "Software\Microsoft\Windows\CurrentVersion\Uninstall\PxbPreview"

  ; Delete installed files
  Delete "$INSTDIR\pxb-preview.exe"
  Delete "$INSTDIR\SDL3.dll"
  Delete "$INSTDIR\libwinpthread-1.dll"
  Delete "$INSTDIR\PxbThumbnailHandler.dll"
  Delete "$INSTDIR\icon.png"
  Delete "$INSTDIR\README.md"
  Delete "$INSTDIR\uninstall.exe"
  
  ; Remove installation directory if empty
  RMDir "$INSTDIR"

  ; Refresh shell association cache
  System::Call 'shell32::SHChangeNotify(i 0x08000000, i 0, i 0, i 0) v'

SectionEnd

; ============================================================
; Output File Configuration
; ============================================================

; Allow command-line override: makensis /DOUT_FILE=myinstaller.exe package.nsi
!ifndef OUT_FILE
  !define OUT_FILE "..\..\dist\windows\pxb-preview-setup.exe"
!endif

OutFile "${OUT_FILE}"
