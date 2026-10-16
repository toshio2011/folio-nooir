#pragma once

#include <array>
#include <cstdint>

#include "SleepComposer.h"

namespace RandomSleepPool {

// The bit positions match persisted sleep-mode numbers. Values 4, 16, and 17
// are deliberately absent: legacy Cover + Custom, Random itself, and Flexible.
inline constexpr std::array<uint8_t, 15> kStableModes = {
    SleepScreenModeValue::DARK,
    SleepScreenModeValue::LIGHT,
    SleepScreenModeValue::CUSTOM,
    SleepScreenModeValue::COVER,
    SleepScreenModeValue::BLANK,
    SleepScreenModeValue::QUICK_RESUME,
    SleepScreenModeValue::OVERLAY,
    SleepScreenModeValue::COVER_OVERLAY,
    SleepScreenModeValue::READING_STATS_SLEEP,
    SleepScreenModeValue::MINIMAL_STATS,
    SleepScreenModeValue::CLIPPING_COVER,
    SleepScreenModeValue::TODO_LIST,
    SleepScreenModeValue::READING_CALENDAR_SLEEP,
    SleepScreenModeValue::READING_SUMMARY_SLEEP,
    SleepScreenModeValue::COVER_PROGRESS,
};

constexpr uint16_t bitForMode(const uint8_t mode) {
  return mode < 16 ? static_cast<uint16_t>(1U << mode) : 0;
}

constexpr uint16_t stableModeMask() {
  uint16_t mask = 0;
  for (const auto mode : kStableModes) mask |= bitForMode(mode);
  return mask;
}

inline constexpr uint16_t kAllStableModesMask = stableModeMask();
inline constexpr uint16_t kNewInstallDefaultMask =
    bitForMode(SleepScreenModeValue::COVER) | bitForMode(SleepScreenModeValue::CUSTOM);

struct SequenceState {
  uint16_t remainingMask = 0;
  uint16_t activePoolMask = 0;
  uint32_t randomState = 0;
};

enum class Order : uint8_t { Random = 0, Shuffle = 1 };

constexpr uint16_t normalizeMask(const uint16_t mask) {
  const uint16_t enabledStable = mask & kAllStableModesMask;
  return enabledStable ? enabledStable : kAllStableModesMask;
}

inline uint32_t nextRandom(SequenceState& state, const uint32_t initialSeed) {
  uint32_t value = state.randomState;
  if (value == 0) value = initialSeed ? initialSeed : 0x9E3779B9u;
  // xorshift32: fixed-cost, no global Arduino RNG state and no allocation.
  value ^= value << 13;
  value ^= value >> 17;
  value ^= value << 5;
  if (value == 0) value = 0x9E3779B9u;
  state.randomState = value;
  return value;
}

inline uint8_t choose(const uint16_t configuredMask, const Order order, SequenceState& state,
                      const uint32_t initialSeed) {
  const uint16_t poolMask = normalizeMask(configuredMask);
  if (poolMask != state.activePoolMask) {
    state.activePoolMask = poolMask;
    state.remainingMask = order == Order::Shuffle ? poolMask : 0;
  }

  // With one mode enabled, avoid advancing sequence state or invoking a PRNG.
  if ((poolMask & static_cast<uint16_t>(poolMask - 1)) == 0) {
    state.remainingMask = 0;
    for (const auto mode : kStableModes) {
      if (poolMask & bitForMode(mode)) return mode;
    }
  }

  if (order == Order::Shuffle) {
    state.remainingMask &= poolMask;
    if (state.remainingMask == 0) state.remainingMask = poolMask;
  } else {
    state.remainingMask = 0;
  }

  const uint16_t candidates = order == Order::Shuffle ? state.remainingMask : poolMask;
  unsigned count = 0;
  for (const auto mode : kStableModes) count += (candidates & bitForMode(mode)) != 0;
  if (count <= 1) {
    for (const auto mode : kStableModes) {
      const uint16_t modeBit = bitForMode(mode);
      if (candidates & modeBit) {
        if (order == Order::Shuffle) state.remainingMask &= static_cast<uint16_t>(~modeBit);
        return mode;
      }
    }
  }

  unsigned selectedOffset = nextRandom(state, initialSeed) % count;
  for (const auto mode : kStableModes) {
    const uint16_t modeBit = bitForMode(mode);
    if ((candidates & modeBit) == 0) continue;
    if (selectedOffset-- == 0) {
      if (order == Order::Shuffle) state.remainingMask &= static_cast<uint16_t>(~modeBit);
      return mode;
    }
  }
  return kStableModes.front();
}

}  // namespace RandomSleepPool
