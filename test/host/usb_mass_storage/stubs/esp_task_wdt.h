#pragma once
#include "test_state.h"
constexpr int ESP_OK = 0;
inline int esp_task_wdt_status(void*) { return usb_test::watchdogSubscribed ? ESP_OK : -1; }
inline int esp_task_wdt_reset() { ++usb_test::watchdogResets; return ESP_OK; }