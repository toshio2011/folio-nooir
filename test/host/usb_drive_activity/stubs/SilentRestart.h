#pragma once
#include "test_state.h"
inline void restartToHomeAfterStorageHandoff() { ++usb_drive_test::restartCalls; usb_drive_test::record("restart.home"); }
