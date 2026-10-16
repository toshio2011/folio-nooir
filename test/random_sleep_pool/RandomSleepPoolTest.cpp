#include "util/RandomSleepPreferences.h"

#include <gtest/gtest.h>

#include <cstdint>
#include <initializer_list>
#include <limits>
#include <map>
#include <string>
#include <utility>

namespace {

class FakeJsonDocument;

class FakeJsonVariant {
 public:
  FakeJsonVariant(FakeJsonDocument& document, std::string key) : document_(document), key_(std::move(key)) {}

  bool isNull() const;

  template <typename T>
  bool is() const {
    return !isNull() && value() <= static_cast<uint32_t>(std::numeric_limits<T>::max());
  }

  template <typename T>
  T as() const {
    return static_cast<T>(value());
  }

  template <typename T>
  FakeJsonVariant& operator=(const T value) {
    set(static_cast<uint32_t>(value));
    return *this;
  }

 private:
  uint32_t value() const;
  void set(uint32_t value);

  FakeJsonDocument& document_;
  std::string key_;
};

class FakeJsonDocument {
 public:
  FakeJsonVariant operator[](const char* key) { return {*this, key}; }
  FakeJsonVariant operator[](const char* key) const { return {const_cast<FakeJsonDocument&>(*this), key}; }

 private:
  friend class FakeJsonVariant;
  std::map<std::string, uint32_t> values_;
};

bool FakeJsonVariant::isNull() const { return document_.values_.find(key_) == document_.values_.end(); }

uint32_t FakeJsonVariant::value() const {
  const auto found = document_.values_.find(key_);
  return found == document_.values_.end() ? 0 : found->second;
}

void FakeJsonVariant::set(const uint32_t value) { document_.values_[key_] = value; }

constexpr uint16_t maskFor(std::initializer_list<uint8_t> modes) {
  uint16_t mask = 0;
  for (const auto mode : modes) mask |= RandomSleepPool::bitForMode(mode);
  return mask;
}

TEST(RandomSleepPool, PersistedRandomAndShufflePreferencesRoundTrip) {
  FakeJsonDocument stored;
  constexpr uint16_t expectedMask = maskFor({SleepScreenModeValue::COVER, SleepScreenModeValue::CUSTOM,
                                              SleepScreenModeValue::READING_SUMMARY_SLEEP});
  constexpr uint8_t expectedOrder = static_cast<uint8_t>(RandomSleepPool::Order::Shuffle);
  RandomSleepPool::writePreferences(stored, expectedMask, expectedOrder);

  uint16_t loadedMask = 0;
  uint8_t loadedOrder = static_cast<uint8_t>(RandomSleepPool::Order::Random);
  EXPECT_FALSE(RandomSleepPool::readPreferences(stored, loadedMask, loadedOrder, false));
  EXPECT_EQ(loadedMask, expectedMask);
  EXPECT_EQ(loadedOrder, expectedOrder);

  FakeJsonDocument randomStored;
  constexpr uint8_t randomOrder = static_cast<uint8_t>(RandomSleepPool::Order::Random);
  RandomSleepPool::writePreferences(randomStored, expectedMask, randomOrder);
  loadedMask = 0;
  loadedOrder = expectedOrder;
  EXPECT_FALSE(RandomSleepPool::readPreferences(randomStored, loadedMask, loadedOrder, false));
  EXPECT_EQ(loadedMask, expectedMask);
  EXPECT_EQ(loadedOrder, randomOrder);
}

TEST(RandomSleepPool, OldSavedSettingsRetainThePreviousFullRandomPoolAndOrder) {
  FakeJsonDocument oldSettings;
  oldSettings["sleepScreen"] = SleepScreenModeValue::RANDOM;
  oldSettings["sleepModeLayoutVersion"] = 1;

  uint16_t mask = RandomSleepPool::kNewInstallDefaultMask;
  uint8_t order = static_cast<uint8_t>(RandomSleepPool::Order::Shuffle);
  EXPECT_TRUE(RandomSleepPool::readPreferences(oldSettings, mask, order, false));
  EXPECT_EQ(mask, RandomSleepPool::kAllStableModesMask);
  EXPECT_EQ(order, static_cast<uint8_t>(RandomSleepPool::Order::Random));
}

TEST(RandomSleepPool, NewInstallDefaultsRemainSmallAndShuffleWithoutMigration) {
  FakeJsonDocument newSettings;
  uint16_t mask = RandomSleepPool::kNewInstallDefaultMask;
  uint8_t order = static_cast<uint8_t>(RandomSleepPool::Order::Shuffle);
  EXPECT_FALSE(RandomSleepPool::readPreferences(newSettings, mask, order, false));
  EXPECT_EQ(mask, RandomSleepPool::kNewInstallDefaultMask);
  EXPECT_EQ(order, static_cast<uint8_t>(RandomSleepPool::Order::Shuffle));
}

TEST(RandomSleepPool, OldProfileWithoutKeysKeepsCurrentPoolAndOrder) {
  FakeJsonDocument oldProfile;
  uint16_t mask = maskFor({SleepScreenModeValue::LIGHT, SleepScreenModeValue::COVER_PROGRESS});
  uint8_t order = static_cast<uint8_t>(RandomSleepPool::Order::Shuffle);
  EXPECT_FALSE(RandomSleepPool::readPreferences(oldProfile, mask, order, true));
  EXPECT_EQ(mask, maskFor({SleepScreenModeValue::LIGHT, SleepScreenModeValue::COVER_PROGRESS}));
  EXPECT_EQ(order, static_cast<uint8_t>(RandomSleepPool::Order::Shuffle));
}

TEST(RandomSleepPool, ZeroMaskFallsBackToAllStableModesAndFlexibleIsNeverSelectable) {
  EXPECT_EQ(RandomSleepPool::normalizeMask(0), RandomSleepPool::kAllStableModesMask);
  EXPECT_EQ(RandomSleepPool::bitForMode(SleepScreenModeValue::FLEXIBLE_COMPOSITE), 0);
  EXPECT_EQ(RandomSleepPool::bitForMode(SleepScreenModeValue::RANDOM), 0);
  EXPECT_EQ(RandomSleepPool::kStableModes.size(), 15U);
}

TEST(RandomSleepPool, OneEnabledModeIsSelectedDirectly) {
  RandomSleepPool::SequenceState sequence{0xFFFF, 0xFFFF, 0};
  const uint16_t mask = RandomSleepPool::bitForMode(SleepScreenModeValue::COVER);
  EXPECT_EQ(RandomSleepPool::choose(mask, RandomSleepPool::Order::Shuffle, sequence, 123),
            SleepScreenModeValue::COVER);
  EXPECT_EQ(sequence.randomState, 0U);
  EXPECT_EQ(sequence.remainingMask, 0U);
}

TEST(RandomSleepPool, ShuffleVisitsEveryEnabledModeExactlyOncePerCycle) {
  const uint16_t mask = maskFor({SleepScreenModeValue::DARK, SleepScreenModeValue::COVER,
                                 SleepScreenModeValue::READING_SUMMARY_SLEEP,
                                 SleepScreenModeValue::COVER_PROGRESS});
  RandomSleepPool::SequenceState sequence{0, 0, 0x7654321u};
  uint16_t firstCycle = 0;
  for (int i = 0; i < 4; ++i) {
    const uint8_t mode = RandomSleepPool::choose(mask, RandomSleepPool::Order::Shuffle, sequence, 1);
    EXPECT_EQ(mask & RandomSleepPool::bitForMode(mode), RandomSleepPool::bitForMode(mode));
    EXPECT_EQ(firstCycle & RandomSleepPool::bitForMode(mode), 0);
    firstCycle |= RandomSleepPool::bitForMode(mode);
  }
  EXPECT_EQ(firstCycle, mask);

  uint16_t secondCycle = 0;
  for (int i = 0; i < 4; ++i) {
    const uint8_t mode = RandomSleepPool::choose(mask, RandomSleepPool::Order::Shuffle, sequence, 1);
    EXPECT_EQ(secondCycle & RandomSleepPool::bitForMode(mode), 0);
    secondCycle |= RandomSleepPool::bitForMode(mode);
  }
  EXPECT_EQ(secondCycle, mask);
}

TEST(RandomSleepPool, PoolChangeStartsANewBoundedShuffleCycle) {
  const uint16_t originalMask = maskFor({SleepScreenModeValue::COVER, SleepScreenModeValue::CUSTOM});
  const uint16_t changedMask = maskFor({SleepScreenModeValue::COVER, SleepScreenModeValue::LIGHT});
  RandomSleepPool::SequenceState sequence{RandomSleepPool::bitForMode(SleepScreenModeValue::CUSTOM), originalMask,
                                           0x12345678u};
  const uint8_t mode = RandomSleepPool::choose(changedMask, RandomSleepPool::Order::Shuffle, sequence, 1);
  EXPECT_TRUE(changedMask & RandomSleepPool::bitForMode(mode));
  EXPECT_EQ(sequence.activePoolMask, changedMask);
  EXPECT_EQ(sequence.remainingMask, changedMask & ~RandomSleepPool::bitForMode(mode));
}

TEST(RandomSleepPool, RandomCanRepeatImmediatelyAndNeverLeavesEnabledPool) {
  const uint16_t mask = maskFor({SleepScreenModeValue::LIGHT, SleepScreenModeValue::COVER,
                                 SleepScreenModeValue::COVER_PROGRESS});
  bool observedRepeat = false;
  for (uint32_t seed = 1; seed < 200 && !observedRepeat; ++seed) {
    RandomSleepPool::SequenceState sequence{0, 0, seed};
    const uint8_t first = RandomSleepPool::choose(mask, RandomSleepPool::Order::Random, sequence, 1);
    const uint8_t second = RandomSleepPool::choose(mask, RandomSleepPool::Order::Random, sequence, 1);
    EXPECT_TRUE(mask & RandomSleepPool::bitForMode(first));
    EXPECT_TRUE(mask & RandomSleepPool::bitForMode(second));
    observedRepeat = first == second;
  }
  EXPECT_TRUE(observedRepeat);
}

TEST(RandomSleepPool, PreviewSequenceDoesNotAdvanceActualShuffleState) {
  const uint16_t mask = maskFor({SleepScreenModeValue::DARK, SleepScreenModeValue::COVER,
                                 SleepScreenModeValue::READING_SUMMARY_SLEEP});
  RandomSleepPool::SequenceState actual{RandomSleepPool::bitForMode(SleepScreenModeValue::COVER) |
                                             RandomSleepPool::bitForMode(SleepScreenModeValue::READING_SUMMARY_SLEEP),
                                         mask, 0x87654321u};
  RandomSleepPool::SequenceState control = actual;
  RandomSleepPool::SequenceState preview = actual;
  for (int i = 0; i < 8; ++i) {
    RandomSleepPool::choose(mask, RandomSleepPool::Order::Shuffle, preview, 777);
  }
  EXPECT_EQ(actual.remainingMask, control.remainingMask);
  EXPECT_EQ(actual.activePoolMask, control.activePoolMask);
  EXPECT_EQ(actual.randomState, control.randomState);
  EXPECT_EQ(RandomSleepPool::choose(mask, RandomSleepPool::Order::Shuffle, actual, 777),
            RandomSleepPool::choose(mask, RandomSleepPool::Order::Shuffle, control, 777));
}

TEST(RandomSleepPool, PersistedSleepModeValuesRemainStable) {
  EXPECT_EQ(SleepScreenModeValue::DARK, 0);
  EXPECT_EQ(SleepScreenModeValue::COVER, 3);
  EXPECT_EQ(SleepScreenModeValue::BLANK, 5);
  EXPECT_EQ(SleepScreenModeValue::COVER_PROGRESS, 15);
  EXPECT_EQ(SleepScreenModeValue::RANDOM, 16);
  EXPECT_EQ(SleepScreenModeValue::FLEXIBLE_COMPOSITE, 17);
}

}  // namespace
