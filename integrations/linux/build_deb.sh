#!/usr/bin/env bash
# build_deb.sh - build a .deb package for PXB Preview (Linux)
set -e
HERE="$(cd "$(dirname "$0")" && pwd)"
ROOT="$HERE/../.."
BUILD="$ROOT/build"
PKG="$HERE/deb/pxb-preview"
# Version: single source of truth src/app/i18n.h (kAppVersion); $VERSION env wins.
VERSION="${VERSION:-$("$ROOT/scripts/app_version.sh")}"

[ -f "$BUILD/pxb-preview" ] || { echo "build the project first (see README)"; exit 1; }

rm -rf "$HERE/deb" && mkdir -p "$PKG/DEBIAN"
mkdir -p "$PKG/usr/bin" "$PKG/usr/share/applications" "$PKG/usr/share/mime/packages" "$PKG/usr/share/thumbnailers"

cp "$BUILD/pxb-preview" "$PKG/usr/bin/"
cp "$HERE/pxb-preview.desktop" "$PKG/usr/share/applications/"
cp "$HERE/application-x-pxb.xml" "$PKG/usr/share/mime/packages/"
cp "$HERE/pxb.thumbnailer" "$PKG/usr/share/thumbnailers/"

cat > "$PKG/DEBIAN/control" <<EOF
Package: pxb-preview
Version: $VERSION
Section: graphics
Priority: optional
Architecture: amd64
Maintainer: PXB Preview <noreply@example.com>
Description: Lightweight cross-platform PXB pixel-art preview tool.
 Reads gzip-compressed PXB files and renders frames, layers and animation.
EOF

# Post-install: refresh MIME/thumbnail caches.
cat > "$PKG/DEBIAN/postinst" <<'EOF'
#!/bin/sh
set -e
update-mime-database /usr/share/mime 2>/dev/null || true
update-desktop-database /usr/share/applications 2>/dev/null || true
EOF
chmod +x "$PKG/DEBIAN/postinst"

dpkg-deb --build "$PKG" "$HERE/pxb-preview_${VERSION}_amd64.deb"
echo "[*] Built $HERE/pxb-preview_${VERSION}_amd64.deb"
