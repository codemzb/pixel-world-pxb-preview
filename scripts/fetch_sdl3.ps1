# fetch_sdl3.ps1 - Download the official prebuilt SDL3 MinGW devel archive.
#
# SDL3 is the project's ONLY external binary dependency and is intentionally
# NOT committed to the repository (5+ MB of DLLs). This script fetches a
# version-pinned prebuilt from the SDL GitHub releases into external/
# (gitignored) and lays it out as a CMake prefix directory:
#   external/SDL3/{bin,include,lib}
#
# Usage (from anywhere):
#   powershell -File scripts\fetch_sdl3.ps1           # default version below
# Configure afterwards:
#   cmake -S . -B build -G Ninja -DCMAKE_PREFIX_PATH="<repo>/external/SDL3"
#
# The layout matches the official "-mingw" devel archive, so an existing
# local SDL3 install (e.g. E:\SDL3-3.4.14\x86_64-w64-mingw32) works equally
# well via -DCMAKE_PREFIX_PATH without running this script.
param(
    [string]$Version = "3.4.14"
)
$ErrorActionPreference = "Stop"
$ProgressPreference = "SilentlyContinue"   # Invoke-WebRequest progress is slow

$root = (Resolve-Path (Join-Path $PSScriptRoot "..")).Path
$dest = Join-Path $root "external\SDL3"

if (Test-Path (Join-Path $dest "lib\cmake\SDL3")) {
    Write-Host "SDL3 $Version already present at $dest - nothing to do"
    exit 0
}

$name = "SDL3-devel-$Version-mingw.zip"
$url  = "https://github.com/libsdl-org/SDL/releases/download/release-$Version/$name"
$tmp  = Join-Path $env:TEMP "$name"
$extract = Join-Path $env:TEMP "sdl3_extract_$Version"

Write-Host "Downloading $url"
Invoke-WebRequest -Uri $url -OutFile $tmp
if (Test-Path $extract) { Remove-Item $extract -Recurse -Force }
Expand-Archive -Path $tmp -DestinationPath $extract -Force

# Archive layout: SDL3-<version>/x86_64-w64-mingw32/{bin,include,lib,...}
$inner = Get-ChildItem $extract -Directory | Select-Object -First 1
$mingw = Join-Path $inner.FullName "x86_64-w64-mingw32"
if (-not (Test-Path $mingw)) {
    throw "unexpected archive layout: $name has no x86_64-w64-mingw32/ directory"
}

New-Item -ItemType Directory -Force -Path $dest | Out-Null
Copy-Item (Join-Path $mingw "*") $dest -Recurse -Force
Remove-Item $tmp -Force -ErrorAction SilentlyContinue
Remove-Item $extract -Recurse -Force -ErrorAction SilentlyContinue

if (-not (Test-Path (Join-Path $dest "lib\cmake\SDL3"))) {
    throw "downloaded archive lacks lib/cmake/SDL3 - find_package(SDL3) will fail"
}
Write-Host "SDL3 $Version ready: $dest"
Write-Host ("Configure with: cmake -S . -B build -G Ninja " +
            "-DCMAKE_PREFIX_PATH=`"$dest`"")
