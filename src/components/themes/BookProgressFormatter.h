#pragma once

#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstdio>

namespace BookProgressFormatter {

struct SanitizedProgress {
  float value;
  bool complete;
};

// Only finite values in the closed [0, 100] interval are valid.  In
// particular, an out-of-range value must never be mistaken for completion.
inline SanitizedProgress sanitize(const float progress) {
  if (!std::isfinite(progress) || progress < 0.0f || progress > 100.0f) {
    return {0.0f, false};
  }
  return {progress, progress == 100.0f};
}

inline void format(char* buffer, const size_t bufferSize, const float progress, const uint8_t percentageFormat) {
  const SanitizedProgress sanitized = sanitize(progress);

  if (percentageFormat == 0) {
    if (sanitized.complete) {
      snprintf(buffer, bufferSize, "100%%");
      return;
    }

    int whole = static_cast<int>(sanitized.value + 0.5f);
    if (whole >= 100) whole = 99;
    snprintf(buffer, bufferSize, "%d%%", whole);
    return;
  }

  uint32_t hundredths = sanitized.complete
                            ? 10000U
                            : static_cast<uint32_t>(sanitized.value * 100.0f + 0.5f);
  if (!sanitized.complete && hundredths >= 10000U) hundredths = 9999U;

  if (percentageFormat == 1) {
    uint32_t tenths = (hundredths + 5U) / 10U;
    if (!sanitized.complete && tenths >= 1000U) tenths = 999U;
    snprintf(buffer, bufferSize, "%u.%u%%", static_cast<unsigned>(tenths / 10U),
             static_cast<unsigned>(tenths % 10U));
    return;
  }

  snprintf(buffer, bufferSize, "%u.%02u%%", static_cast<unsigned>(hundredths / 100U),
           static_cast<unsigned>(hundredths % 100U));
}

}  // namespace BookProgressFormatter
