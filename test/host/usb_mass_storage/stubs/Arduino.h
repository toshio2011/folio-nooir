#pragma once
#include <cstdint>
#include "test_state.h"
inline void delay(uint32_t ms) { usb_test::delays += ms; }