#pragma once
#include <cstdint>
#include "test_state.h"
class USBMSC {
 public:
  using StartStopCallback = bool (*)(uint8_t, bool, bool);
  using ReadCallback = int32_t (*)(uint32_t, uint32_t, void*, uint32_t);
  using WriteCallback = int32_t (*)(uint32_t, uint32_t, uint8_t*, uint32_t);

  USBMSC() { current = this; }
  void vendorID(const char*) {}
  void productID(const char*) {}
  void productRevision(const char*) {}
  void mediaPresent(bool) {}
  void isWritable(bool) {}
  void onStartStop(StartStopCallback cb) { startStopCallback = cb; }
  void onRead(ReadCallback cb) { readCallback = cb; }
  void onWrite(WriteCallback cb) { writeCallback = cb; }
  bool begin(uint32_t blocks, uint16_t blockSize) {
    ++usb_test::mscBeginCalls;
    usb_test::lastBlockCount = blocks;
    usb_test::lastBlockSize = blockSize;
    return usb_test::mscBeginResult;
  }
  void end() { ++usb_test::mscEndCalls; }

  StartStopCallback startStopCallback = nullptr;
  ReadCallback readCallback = nullptr;
  WriteCallback writeCallback = nullptr;
  static USBMSC* current;
};
inline USBMSC* USBMSC::current = nullptr;