#!/usr/bin/env bash
# build_dmg.sh - build the PXB Quick Look generator and wrap it in a .dmg
#
# Uses the shared pxb parsing core (gzip + PNG via miniz/stb). Requires Xcode
# command line tools (clang, hdiutil).
set -e

HERE="$(cd "$(dirname "$0")" && pwd)"
ROOT="$HERE/../../.."              # pxb-preview/
CORE="$ROOT/src/core"
APP_VERSION="$("$ROOT/scripts/app_version.sh")"   # single source: i18n.h
QLGEN="$HERE/PxbQuickLook.qlgenerator"
MACOS_DIR="$QLGEN/Contents/MacOS"
OUT_DMG="$HERE/PxbQuickLook-${APP_VERSION}.dmg"

# Need stb + miniz sources. Prefer a local vendor dir, else fetch.
STB_H="$(find "$ROOT/thirdparty" -name 'stb_image.h' 2>/dev/null | head -1)"
MINIZ_H="$(find "$ROOT/thirdparty" -name 'miniz.h' 2>/dev/null | head -1)"
MINIZ_C="$(find "$ROOT/thirdparty" -name 'miniz.c' 2>/dev/null | head -1)"
if [ -z "$STB_H" ] || [ -z "$MINIZ_H" ]; then
  echo "[*] Fetching stb + miniz into thirdparty/ ..."
  mkdir -p "$ROOT/thirdparty"
  [ -z "$STB_H" ] && curl -fsSL https://raw.githubusercontent.com/nothings/stb/master/stb_image.h -o "$ROOT/thirdparty/stb_image.h"
  [ -z "$MINIZ_H" ] && curl -fsSL https://raw.githubusercontent.com/richgel999/miniz/master/miniz.h -o "$ROOT/thirdparty/miniz.h"
  [ -z "$MINIZ_C" ] && curl -fsSL https://raw.githubusercontent.com/richgel999/miniz/master/miniz.c -o "$ROOT/thirdparty/miniz.c"
  STB_H="$ROOT/thirdparty/stb_image.h"
  MINIZ_H="$ROOT/thirdparty/miniz.h"
  MINIZ_C="$ROOT/thirdparty/miniz.c"
fi

mkdir -p "$MACOS_DIR"
echo "[*] Compiling PxbQL.mm + pxb core ..."
clang++ -std=c++17 -O2 -fobjc-arc \
  -I"$CORE" -I"$(dirname "$STB_H")" -I"$(dirname "$MINIZ_H")" \
  "$HERE/PxbQL.mm" \
  "$CORE/gzip.cpp" "$CORE/image.cpp" "$CORE/fileutil.cpp" "$CORE/pxb_reader.cpp" \
  "$MINIZ_C" \
  -framework QuickLook -framework Cocoa -framework CoreGraphics \
  -o "$MACOS_DIR/PxbQL"

# Fill the __PXB_VERSION__ placeholder so the bundle version tracks i18n.h.
sed -e "s/__PXB_VERSION__/${APP_VERSION}/g" "$HERE/Info.plist" \
    > "$QLGEN/Contents/Info.plist"

echo "[*] Ad-hoc signing ..."
codesign --force --deep --sign - "$QLGEN" 2>/dev/null || echo "(codesign optional / skipped)"

echo "[*] Building $OUT_DMG ..."
[ -f "$OUT_DMG" ] && rm -f "$OUT_DMG"
hdiutil create -format UDZO -srcfolder "$QLGEN" -volname "PxbQuickLook" "$OUT_DMG"

echo "[*] Done: $OUT_DMG"
echo "    Install: cp -r '$QLGEN' ~/Library/QuickLook/ && qlmanage -r"
