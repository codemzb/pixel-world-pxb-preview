# package_windows.ps1 - One-shot Windows packaging: assemble the portable dist
# folder and build the NSIS installer (+ portable zip).
#
# Single entry point for local AND CI (.github/workflows/ci.yml). Every makensis
# input is passed as an ABSOLUTE /D define, so package.nsi's relative defaults
# can never drift again (the 2026-08-30 "can't open icon.ico" packaging failure
# was exactly such a drift). The version is parsed from src/app/i18n.h
# (kAppVersion — the single source of truth) and injected as /DAPP_VERSION, so
# the installer metadata can never fall out of sync either.
#
# Usage (repo root, after building both targets):
#   powershell -File scripts\package_windows.ps1
# Optional: -AppBuild -ThumbBuild -SDL3Dir -MingwBin -OutDir
#
# Output layout — every artifact lands under dist/<os>/ with a versioned,
# arch-tagged name:
#   dist/windows/pxb-preview-<ver>-win64/                portable folder (exe + dlls)
#   dist/windows/pxb-preview-<ver>-win64-setup.exe       NSIS installer
#   dist/windows/pxb-preview-<ver>-win64-portable.zip    zip (contains the folder)
# The exe inside keeps its stable name pxb-preview.exe: the file association,
# DefaultIcon registry value and shortcuts all reference that name.
param(
    [string]$AppBuild   = "build",
    [string]$ThumbBuild = "build_thumb",
    [string]$SDL3Dir    = "",    # CMake prefix containing bin\SDL3.dll
    [string]$MingwBin   = "",    # dir with libwinpthread-1.dll (also auto-probes
                                 # C:\msys64 etc. for CI runners)
    [string]$OutDir     = "dist\windows"
)
$ErrorActionPreference = "Stop"
$root = (Resolve-Path (Join-Path $PSScriptRoot "..")).Path

function Resolve-First {
    param([string[]]$Paths, [string]$What)
    foreach ($p in $Paths) {
        if ($p -and (Test-Path $p)) { return (Resolve-Path $p).Path }
    }
    throw "cannot locate $What; tried: $($Paths -join ', ')"
}

# ---- inputs -------------------------------------------------------------------
$appExe = Resolve-First @((Join-Path $AppBuild "pxb-preview.exe")) `
    "pxb-preview.exe (cmake --build $AppBuild first)"
$thumb = Resolve-First @((Join-Path $ThumbBuild "PxbThumbnailHandler.dll")) `
    "PxbThumbnailHandler.dll (cmake --build $ThumbBuild first)"

$sdl3Candidates = @()
if ($SDL3Dir) {
    $sdl3Candidates += (Join-Path $SDL3Dir "bin\SDL3.dll")
    $sdl3Candidates += (Join-Path $SDL3Dir "SDL3.dll")
}
$sdl3Candidates += (Join-Path $root "external\SDL3\bin\SDL3.dll")
$sdl3dll = Resolve-First $sdl3Candidates `
    "SDL3.dll (run scripts/fetch_sdl3.ps1 or pass -SDL3Dir)"

# libwinpthread-1.dll is REQUIRED at runtime: both pxb-preview.exe and the
# thumbnail DLL import it (verified via objdump -p; the -Bstatic link option
# does not fully resolve it). It ships with the MinGW toolchain — on CI that
# is the preinstalled MSYS2, locally pass -MingwBin.
$pthreadCandidates = @()
if ($MingwBin) {
    $pthreadCandidates += (Join-Path $MingwBin "libwinpthread-1.dll")
}
$pthreadCandidates += (Join-Path $root "external\mingw-runtime\libwinpthread-1.dll")
$pthreadCandidates += "C:\msys64\mingw64\bin\libwinpthread-1.dll"
$pthreadCandidates += "C:\tools\msys64\mingw64\bin\libwinpthread-1.dll"
if ($env:RUNNER_TEMP) {
    $pthreadCandidates += (Join-Path $env:RUNNER_TEMP "msys64\mingw64\bin\libwinpthread-1.dll")
}
$pthread = Resolve-First $pthreadCandidates `
    "libwinpthread-1.dll (pass -MingwBin pointing at the MinGW toolchain's bin dir)"

$makensis = (Get-Command makensis -ErrorAction SilentlyContinue).Source
if (-not $makensis) {
    $makensis = Resolve-First @(
        (Join-Path ${env:ProgramFiles(x86)} "NSIS\makensis.exe"),
        (Join-Path $env:ProgramFiles "NSIS\makensis.exe")) "NSIS (makensis.exe)"
}

# ---- version: single source of truth is src/app/i18n.h --------------------------
$version = "0.0.0"
$i18n = Get-Content (Join-Path $root "src\app\i18n.h") -Raw
if ($i18n -match 'kAppVersion\s*=\s*"([^"]+)"') { $version = $Matches[1] }
Write-Host "Packaging PXB Preview v$version"

# ---- assemble the portable dist folder ------------------------------------------
$outBase = Join-Path $root $OutDir
$dist = Join-Path $outBase "pxb-preview-$version-win64"
New-Item -ItemType Directory -Force -Path $dist | Out-Null
Copy-Item $appExe $dist -Force
Copy-Item $thumb $dist -Force
Copy-Item $sdl3dll $dist -Force
if ($pthread) { Copy-Item $pthread $dist -Force }Copy-Item (Join-Path $root "assets\icon.png") $dist -Force
Copy-Item (Join-Path $root "README.md") $dist -Force
Remove-Item (Join-Path $dist "pxb-preview.log") -ErrorAction SilentlyContinue

# ---- NSIS installer --------------------------------------------------------------
$nsi = Join-Path $root "integrations\windows\package.nsi"
$setup = Join-Path $outBase "pxb-preview-$version-win64-setup.exe"
& $makensis /DSOURCE_DIR="$dist" `
            /DASSETS_DIR="$(Join-Path $root 'assets')" `
            /DPROJECT_ROOT="$root" `
            /DOUT_FILE="$setup" `
            /DAPP_VERSION="$version" `
            $nsi
if ($LASTEXITCODE -ne 0) { throw "makensis failed with exit code $LASTEXITCODE" }

# ---- portable zip (contains the folder itself, so extraction stays tidy) ---------
$zip = Join-Path $outBase "pxb-preview-$version-win64-portable.zip"
Compress-Archive -Path $dist -DestinationPath $zip -Force

Write-Host ""
Write-Host "Done:"
Write-Host "  dist      : $dist"
Write-Host "  installer : $setup"
Write-Host "  portable  : $zip"
