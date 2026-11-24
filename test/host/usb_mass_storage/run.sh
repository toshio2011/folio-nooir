#!/bin/sh
set -eu

HERE=$(CDPATH= cd -- "$(dirname "$0")" && pwd)
ROOT=$(CDPATH= cd -- "$HERE/../../.." && pwd)
SDK="$ROOT/freeink-sdk-s3/libs/hardware/UsbMassStorage"
BUILD_DIR="${TMPDIR:-/tmp}/nooir-usb-msc-host"
mkdir -p "$BUILD_DIR"

python3 "$ROOT/scripts/patch_nooir_usb_msc_watchdog.py" \
  "$SDK/src/UsbMassStorage.cpp" "$BUILD_DIR/UsbMassStorage.cpp"

c++ -std=c++17 -Wall -Wextra -Werror \
  -I"$HERE/stubs" -I"$SDK/include" \
  "$HERE/test_usb_mass_storage.cpp" "$BUILD_DIR/UsbMassStorage.cpp" \
  -o "$BUILD_DIR/test_usb_mass_storage"
"$BUILD_DIR/test_usb_mass_storage"