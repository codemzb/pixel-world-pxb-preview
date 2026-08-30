@echo off
setlocal EnableExtensions EnableDelayedExpansion
rem ============================================================
rem   PXB Preview - uninstall (no external deps)
rem
rem   Auto-locates PxbThumbnailHandler.dll by walking up via
rem   pushd / cd .. (trailing-backslash safe).
rem
rem   Right-click -> Run as administrator.
rem ============================================================

set "INSTALL_DIR="
set "DLL_PATH="
set /a "DEPTH=0"
set "TRY_DIR=%~dp0"

:walk_up
set /a "DEPTH+=1"
if %DEPTH% GTR 8 goto :fail_no_install_dir

if not defined DLL_PATH if exist "%TRY_DIR%PxbThumbnailHandler.dll"       set "DLL_PATH=%TRY_DIR%PxbThumbnailHandler.dll"
if not defined DLL_PATH if exist "%TRY_DIR%build_thumb\PxbThumbnailHandler.dll" set "DLL_PATH=%TRY_DIR%build_thumb\PxbThumbnailHandler.dll"

if defined DLL_PATH goto :uninstall_ready

pushd "%TRY_DIR%" >nul 2>&1
if errorlevel 1 goto :fail_no_install_dir
cd .. >nul 2>&1
set "TRY_DIR=%CD%\"
popd >nul 2>&1
goto :walk_up

:uninstall_ready
set "IS_ADMIN=0"
net session >nul 2>&1
if not errorlevel 1 set "IS_ADMIN=1"

echo.
echo ============================================================
echo   PXB Preview uninstall
echo   DLL = %DLL_PATH%
if not "%IS_ADMIN%"=="1" echo   Right-click this file -^> "Run as administrator" first.
echo ============================================================
echo.

echo [1/3] Unregistering COM handler ...
regsvr32 /u /s "%DLL_PATH%"
if errorlevel 1 echo       WARN: regsvr32 returned %errorlevel% (continuing)

echo [2/3] Removing registry keys ...
reg delete "HKCU\Software\Microsoft\Windows\CurrentVersion\Explorer\FileExts\.pxb\UserChoice" /f >nul 2>&1
reg delete "HKCU\Software\Classes\.pxb" /f >nul 2>&1
reg delete "HKCU\Software\Classes\PxbPreview.pxbfile" /f >nul 2>&1
reg delete "HKLM\Software\Classes\.pxb" /f >nul 2>&1
reg delete "HKLM\Software\Classes\PxbPreview.pxbfile" /f >nul 2>&1
echo       OK

echo [3/3] Restarting Explorer ...
taskkill /f /im explorer.exe >nul 2>&1
ping -n 2 127.0.0.1 >nul 2>&1
start explorer.exe >nul 2>&1
echo       OK

echo.
echo ============================================================
echo   Uninstall complete. The .pxb-preview.exe itself was left
echo   in place - you can delete the folder manually if desired.
echo ============================================================
echo.
pause
exit /b 0

:fail_no_install_dir
echo.
echo [FAIL] Could not locate PxbThumbnailHandler.dll.
echo        Searched %DEPTH% directory level(s) starting at:
echo          %~dp0
echo.
pause
exit /b 1
