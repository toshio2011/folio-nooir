#!/bin/sh
set -eu
HERE=$(CDPATH= cd -- "$(dirname "$0")" && pwd)
ROOT=$(CDPATH= cd -- "$HERE/../../.." && pwd)
BUILD_DIR="${TMPDIR:-/tmp}/nooir-usb-drive-activity"
mkdir -p "$BUILD_DIR"
c++ -std=c++17 -Wall -Wextra -Werror \
  -I"$HERE/stubs" -I"$ROOT/src" \
  "$HERE/test_usb_drive_activity.cpp" \
  "$ROOT/src/activities/network/UsbDriveActivity.cpp" \
  -o "$BUILD_DIR/test_usb_drive_activity"
"$BUILD_DIR/test_usb_drive_activity"
