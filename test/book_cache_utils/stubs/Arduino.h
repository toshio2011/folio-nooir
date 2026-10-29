#pragma once

#include <cstdint>

inline uint32_t micros() {
  static uint32_t ticks = 0;
  return ++ticks;
}
