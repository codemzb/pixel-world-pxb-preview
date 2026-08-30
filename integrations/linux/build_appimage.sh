#!/usr/bin/env bash
# build_appimage.sh - package PXB Preview as a single-file AppImage (Linux)
# Requires: linuxdeploy on PATH (https://github.com/linuxdeploy/linuxdeploy)
# Optional: drop a 64x64 pxb-preview.png next to this script for a bundled icon.
set -e
HERE="$(cd "$(dirname "$0")" && pwd)"
ROOT="$HERE/../.."
BUILD="$ROOT/build"
APPDIR="$HERE/AppDir"

[ -f "$BUILD/pxb-preview" ] || { echo "build the project first (see README)"; exit 1; }

# AppImage tooling reads $VERSION for the output name and embedded metadata.
# Single source of truth: src/app/i18n.h (kAppVersion) via the shared helper.
VERSION="$("$ROOT/scripts/app_version.sh")"
export VERSION

rm -rf "$APPDIR" && mkdir -p "$APPDIR/usr/bin" "$APPDIR/usr/share/applications"
cp "$BUILD/pxb-preview" "$APPDIR/usr/bin/"
cp "$HERE/pxb-preview.desktop" "$APPDIR/usr/share/applications/"

if [ -f "$HERE/pxb-preview.png" ]; then
  mkdir -p "$APPDIR/usr/share/icons/hicolor/64x64/apps"
  cp "$HERE/pxb-preview.png" "$APPDIR/usr/share/icons/hicolor/64x64/apps/"
fi

if command -v linuxdeploy >/dev/null 2>&1; then
  linuxdeploy --appdir="$APPDIR" --output appimage \
    --desktop-file="$HERE/pxb-preview.desktop"
  echo "[*] AppImage produced."
else
  echo "[!] linuxdeploy not found. AppDir ready at $APPDIR; install linuxdeploy to finish."
fi
