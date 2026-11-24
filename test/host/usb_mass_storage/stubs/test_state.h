#pragma once
#include <cstdint>
namespace usb_test {
inline bool mounted = false;
inline bool suspended = false;
inline bool disconnectResult = true;
inline bool mscBeginResult = true;
inline bool usbBeginResult = true;
inline bool watchdogSubscribed = false;
inline unsigned watchdogResets = 0;
inline unsigned yields = 0;
inline unsigned delays = 0;
inline unsigned disconnectCalls = 0;
inline unsigned usbBeginCalls = 0;
inline unsigned mscBeginCalls = 0;
inline unsigned mscEndCalls = 0;
inline uint32_t lastBlockCount = 0;
inline uint16_t lastBlockSize = 0;
}