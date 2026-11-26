#pragma once
#include <string>
#include <vector>
namespace usb_drive_test {
inline unsigned long nowMs=0;
inline int restartCalls=0;
inline std::vector<std::string> events;
inline void record(const char* event) { events.emplace_back(event); }
inline void resetTrace() { nowMs=0; restartCalls=0; events.clear(); }
}
