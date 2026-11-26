#pragma once
#include "test_state.h"
inline unsigned long millis() { return usb_drive_test::nowMs; }
inline void delay(const unsigned long ms) { usb_drive_test::nowMs+=ms; usb_drive_test::record("delay"); }
#define LOG_ERR(...) ((void)0)
#define LOG_INF(...) ((void)0)
#define LOG_DBG(...) ((void)0)
