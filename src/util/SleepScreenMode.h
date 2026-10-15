#pragma once

#include <Arduino.h>

#include <cstdint>

#include "CrossPointSettings.h"

// Random mode picks among the existing selectable sleep screens, including
// the compact cover/progress screen. Flexible remains opt-in so existing
// users do not see a behavior change unless they select it directly.
inline uint8_t chooseRandomSleepScreenMode() {
  static constexpr uint8_t SELECTABLE_MODES[] = {
      CrossPointSettings::SLEEP_SCREEN_MODE::DARK,
      CrossPointSettings::SLEEP_SCREEN_MODE::LIGHT,
      CrossPointSettings::SLEEP_SCREEN_MODE::CUSTOM,
      CrossPointSettings::SLEEP_SCREEN_MODE::COVER,
      CrossPointSettings::SLEEP_SCREEN_MODE::BLANK,
      CrossPointSettings::SLEEP_SCREEN_MODE::QUICK_RESUME,
      CrossPointSettings::SLEEP_SCREEN_MODE::OVERLAY,
      CrossPointSettings::SLEEP_SCREEN_MODE::COVER_OVERLAY,
      CrossPointSettings::SLEEP_SCREEN_MODE::READING_STATS_SLEEP,
      CrossPointSettings::SLEEP_SCREEN_MODE::MINIMAL_STATS,
      CrossPointSettings::SLEEP_SCREEN_MODE::CLIPPING_COVER,
      CrossPointSettings::SLEEP_SCREEN_MODE::TODO_LIST,
      CrossPointSettings::SLEEP_SCREEN_MODE::READING_CALENDAR_SLEEP,
      CrossPointSettings::SLEEP_SCREEN_MODE::READING_SUMMARY_SLEEP,
      CrossPointSettings::SLEEP_SCREEN_MODE::COVER_PROGRESS,
  };
  const long modeIndex = random(static_cast<long>(sizeof(SELECTABLE_MODES) / sizeof(SELECTABLE_MODES[0])));
  return SELECTABLE_MODES[modeIndex];
}
