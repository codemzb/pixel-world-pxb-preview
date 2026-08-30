#!/usr/bin/env bash
# install.sh - install PXB Preview + register .pxb association on Linux
# Run as root (sudo). Requires the built binaries: pxb-preview, pxb-thumbnailer
set -e
HERE="$(cd "$(dirname "$0")" && pwd)"
BIN_DIR=${BIN_DIR:-/usr/local/bin}
SHARE=${SHARE:-/usr/share}

echo "[*] Installing binaries to $BIN_DIR"
install -m 0755 "$HERE/pxb-preview"        "$BIN_DIR/pxb-preview"
install -m 0755 "$HERE/pxb-thumbnailer"    "$BIN_DIR/pxb-thumbnailer"

echo "[*] Installing desktop entry"
install -m 0644 "$HERE/pxb-preview.desktop" "$SHARE/applications/pxb-preview.desktop"

echo "[*] Installing MIME type"
install -m 0644 "$HERE/application-x-pxb.xml" "$SHARE/mime/packages/application-x-pxb.xml"
update-mime-database "$SHARE/mime" || true

echo "[*] Installing thumbnailer"
install -m 0644 "$HERE/pxb.thumbnailer" "$SHARE/thumbnailers/pxb.thumbnailer"
update-desktop-database "$SHARE/applications" || true

echo "[*] Setting .pxb default application"
xdg-mime default pxb-preview.desktop application/x-pxb 2>/dev/null || true
gio mime application/x-pxb pxb-preview.desktop 2>/dev/null || true

echo "[*] Done. Log out/in (or run 'nautilus -q') to refresh thumbnails."
