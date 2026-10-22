#pragma once

#include <cstdint>

class TestHalClock {
 public:
  uint32_t getDateKey() const { return 20261022; }
};

inline TestHalClock halClock;
