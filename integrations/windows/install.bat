@echo off
setlocal EnableExtensions EnableDelayedExpansion
rem ============================================================
rem   PXB Preview - portable registration (zero external deps)
rem
rem   Auto-locates EXE + DLL by walking up directories using
rem   pushd / cd .. so the trailing-backslash edge case never bites.
rem
rem   The double-click "Open" verb AND the thumbnail handler are
rem   both registered by the DLL's DllRegisterServer (called by
rem   regsvr32), so no Python / .reg / external scripting is needed.
rem
rem   Right-click -> Run as administrator (writes to HKLM).
rem ============================================================

rem --- 1) auto-locate EXE and DLL by walking up from script dir ---
set "INSTALL_DIR="
set "EXE_PATH="
set "DLL_PATH="
set /a "DEPTH=0"
set "TRY_DIR=%~dp0"

:walk_up
set /a "DEPTH+=1"
if %DEPTH% GTR 8 goto :fail_no_install_dir

if not defined EXE_PATH if exist "%TRY_DIR%pxb-preview.exe"              set "EXE_PATH=%TRY_DIR%pxb-preview.exe"
if not defined DLL_PATH if exist "%TRY_DIR%PxbThumbnailHandler.dll"       set "DLL_PATH=%TRY_DIR%PxbThumbnailHandler.dll"
if not defined EXE_PATH if exist "%TRY_DIR%build\pxb-preview.exe"         set "EXE_PATH=%TRY_DIR%build\pxb-preview.exe"
if not defined DLL_PATH if exist "%TRY_DIR%build_thumb\PxbThumbnailHandler.dll" set "DLL_PATH=%TRY_DIR%build_thumb\PxbThumbnailHandler.dll"

if defined EXE_PATH if defined DLL_PATH (
    set "INSTALL_DIR=%TRY_DIR%"
    goto :install_located
)

rem walk up: parent of current TRY_DIR via pushd/cd/popd (trailing-\ safe)
pushd "%TRY_DIR%" >nul 2>&1
if errorlevel 1 goto :fail_no_install_dir
cd .. >nul 2>&1
set "TRY_DIR=%CD%\"
popd >nul 2>&1
goto :walk_up

:install_located
cd /d "%INSTALL_DIR%"

rem --- 2) detect admin via net session ---
set "IS_ADMIN=0"
net session >nul 2>&1
if not errorlevel 1 set "IS_ADMIN=1"

echo.
echo ============================================================
if "%IS_ADMIN%"=="1" (
    echo   PXB Preview install  (ADMIN, all users, HKLM)
) else (
    echo   PXB Preview install  (current user only - regsvr32 needs admin)
    echo   Right-click install.bat -^> "Run as administrator" first.
)
echo   EXE = %EXE_PATH%
echo   DLL = %DLL_PATH%
echo ============================================================
echo.

rem --- 3) register thumbnail + Open verb via regsvr32 ---
echo [1/1] Registering thumbnail handler + Open verb (regsvr32) ...
regsvr32 /s "%DLL_PATH%"
if errorlevel 1 goto :fail_regsvr32
echo       OK

rem --- 4) clear UserChoice cache so our ProgID wins for double-click ---
echo       Clearing cached file-association choice ...
reg delete "HKCU\Software\Microsoft\Windows\CurrentVersion\Explorer\FileExts\.pxb\UserChoice" /f >nul 2>&1

rem --- 5) restart Explorer to apply immediately ---
echo       Restarting Explorer to apply changes ...
taskkill /f /im explorer.exe >nul 2>&1
ping -n 2 127.0.0.1 >nul 2>&1
start explorer.exe >nul 2>&1
echo       OK

echo.
echo ============================================================
echo   Install complete.
echo   - Double-click .pxb opens pxb-preview.exe (full GUI)
echo   - File Explorer shows PXB images as thumbnails
echo.
echo   If thumbnails don't appear, press F5 in Explorer to refresh.
echo ============================================================
echo.
echo Run uninstall.bat (as administrator) to remove these entries.
echo.
pause
exit /b 0

rem ============ error handlers ============

:fail_no_install_dir
echo.
echo [FAIL] Could not locate pxb-preview.exe + PxbThumbnailHandler.dll
echo        Searched %DEPTH% directory level(s) starting at:
echo          %~dp0
echo.
echo        Expected layouts (any of):
echo.
echo        (A) dist folder (recommended):
echo            _ROOT_\pxb-preview.exe
echo            _ROOT_\PxbThumbnailHandler.dll
echo.
echo        (B) source build:
echo            _ROOT_\build\pxb-preview.exe
echo            _ROOT_\build_thumb\PxbThumbnailHandler.dll
echo.
echo        Tip: extract the zip somewhere, then run install.bat
echo             from the extracted _ROOT_ folder (or any subfolder).
echo.
pause
exit /b 1

:fail_regsvr32
echo.
echo [FAIL] regsvr32 failed to register "%DLL_PATH%".
echo        Make sure you ran install.bat "As administrator".
echo.
pause
exit /b 1
