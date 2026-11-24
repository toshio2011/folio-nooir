#pragma once
#include "test_state.h"
class USBClass {
 public:
  bool begin() { ++usb_test::usbBeginCalls; return usb_test::usbBeginResult; }
};
extern USBClass USB;