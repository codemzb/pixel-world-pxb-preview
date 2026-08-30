#!/bin/sh
# app_version.sh - print the app version.
#
# Single source of truth: src/app/i18n.h (kAppVersion). All POSIX-side
# packaging scripts (deb / AppImage / dmg) source the version through this so
# a version bump is one edit that propagates everywhere. CMake does the same
# parse natively (see CMakeLists.txt, PXB_APP_VERSION).
#
# Usage: VERSION="$(..."$(dirname "$0")/../scripts/app_version.sh")"
set -e

root=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
v=$(sed -n 's/.*kAppVersion[[:space:]]*=[[:space:]]*"\([^"]*\)".*/\1/p' "$root/src/app/i18n.h")
if [ -z "$v" ]; then
  echo "app_version.sh: cannot parse kAppVersion from $root/src/app/i18n.h" >&2
  exit 1
fi
echo "$v"
