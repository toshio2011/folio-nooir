#pragma once

#include <esp_err.h>
#include <esp_task_wdt.h>

// The physical helper lives in lib/hal, which is intentionally excluded from
// the native simulator. Keep the same no-op-compatible API for host builds.
inline void resetTaskWatchdogIfSubscribed() {
  if (esp_task_wdt_status(nullptr) == ESP_OK) {
    esp_task_wdt_reset();
  }
}
