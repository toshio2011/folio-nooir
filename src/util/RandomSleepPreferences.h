#pragma once

#include <cstdint>

#include "SleepScreenMode.h"

namespace RandomSleepPool {

// Keep the tiny persistence adapter generic so its migration and round-trip
// behavior can be covered by the host suite without pulling in the firmware's
// ArduinoJson runtime.
template <typename JsonDocument>
void writePreferences(JsonDocument& doc, const uint16_t enabledMask, const uint8_t order) {
  doc["randomSleepModeMask"] = enabledMask;
  doc["randomSleepOrder"] = order;
}

template <typename JsonDocument>
bool readPreferences(const JsonDocument& doc, uint16_t& enabledMask, uint8_t& order,
                     const bool preserveMissingManualFields) {
  bool needsResave = false;
  const auto storedMask = doc["randomSleepModeMask"];
  const bool legacySettings = !doc["sleepScreen"].isNull() || !doc["sleepModeLayoutVersion"].isNull();

  if (storedMask.template is<uint32_t>() && storedMask.template as<uint32_t>() <= UINT16_MAX) {
    enabledMask = static_cast<uint16_t>(storedMask.template as<uint32_t>());
  } else if (storedMask.isNull()) {
    if (!preserveMissingManualFields) {
      enabledMask = legacySettings ? kAllStableModesMask : kNewInstallDefaultMask;
      needsResave = legacySettings;
    }
  } else {
    enabledMask = kAllStableModesMask;
    needsResave = true;
  }

  const auto storedOrder = doc["randomSleepOrder"];
  if (storedOrder.template is<uint8_t>() && storedOrder.template as<uint8_t>() < 2) {
    order = storedOrder.template as<uint8_t>();
  } else if (storedOrder.isNull()) {
    if (!preserveMissingManualFields) {
      order = legacySettings ? static_cast<uint8_t>(Order::Random) : static_cast<uint8_t>(Order::Shuffle);
      needsResave = needsResave || legacySettings;
    }
  } else {
    order = static_cast<uint8_t>(Order::Random);
    needsResave = true;
  }
  return needsResave;
}

}  // namespace RandomSleepPool
