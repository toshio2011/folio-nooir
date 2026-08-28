#pragma once

#include <cstdint>

struct TestEsp {
  uint32_t getFreeHeap() const { return 1024U * 1024U; }
};

inline TestEsp ESP;
