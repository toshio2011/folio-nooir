#pragma once
#include "GfxRenderer.h"
#include "test_state.h"
class TestSdCardFontSystem { public:
  int releaseCalls=0;
  void releaseForStorage(GfxRenderer&) { ++releaseCalls; usb_drive_test::record("fonts.release"); }
};
extern TestSdCardFontSystem sdFontSystem;
