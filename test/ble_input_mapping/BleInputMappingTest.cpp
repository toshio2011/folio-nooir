#include "util/BleKeyMapping.h"
#include "MappedInputManager.h"

#include <gtest/gtest.h>

namespace {
using namespace bleinput::mapping;

TEST(BleInputMapping, DefaultPhysicalActionsResolveToVirtualButtons) {
  KeyMapEntry entries[kMapCapacity] = {};
  const DefaultBinding defaults[] = {
      {{0, 1}, kButtonAction, 3},  // remote Next -> Right
      {{0, 2}, kButtonAction, 2},  // remote Previous -> Left
      {{0, 3}, kButtonAction, 1},  // center -> Confirm
      {{0, 4}, kButtonAction, 0},  // back -> Back
      {{0, 5}, kButtonAction, 3},  // PageDown -> Right
      {{0, 6}, kButtonAction, 2},  // PageUp -> Left
  };
  ASSERT_TRUE(resetToDefaults(entries, defaults, 6));

  for (uint8_t i = 0; i < 6; ++i) {
    ASSERT_TRUE(matchesAt(entries[i], defaults[i].identity.kind, defaults[i].identity.value, 0));
    const auto action = resolve(entries[i]);
    EXPECT_EQ(action.type, ResolvedActionType::VirtualButton);
    EXPECT_EQ(action.target, defaults[i].target);
  }
}

TEST(BleInputMapping, LearningOverwritesByIdentityAndCanRemove) {
  KeyMapEntry entries[kMapCapacity] = {};
  const CapturedIdentity captured[] = {{1, 0x4F}};

  EXPECT_EQ(assign(entries, captured, 1, kButtonAction, 3), AssignResult::Added);
  ASSERT_EQ(resolve(entries[0]).type, ResolvedActionType::VirtualButton);
  EXPECT_EQ(resolve(entries[0]).target, 3);  // Right reaches the normal input interface.
  EXPECT_EQ(assign(entries, captured, 1, kButtonAction, 5), AssignResult::Replaced);
  EXPECT_EQ(resolve(entries[0]).target, 5);
  EXPECT_EQ(assign(entries, captured, 1, kShortcutAction, kShortcutClippings), AssignResult::Replaced);
  EXPECT_EQ(resolve(entries[0]).type, ResolvedActionType::Shortcut);
  EXPECT_EQ(resolve(entries[0]).target, kShortcutClippings);
  EXPECT_EQ(remove(entries, captured, 1), AssignResult::Removed);
  EXPECT_FALSE(isOccupied(entries[0]));
  EXPECT_EQ(remove(entries, captured, 1), AssignResult::NotFound);
}

TEST(BleInputMapping, ReaderPageActionsRemainAvailableForNewMappings) {
  KeyMapEntry entries[kMapCapacity] = {};
  const CapturedIdentity nextPage[] = {{0, 0x4F}};
  const CapturedIdentity previousPage[] = {{0, 0x50}};
  const uint8_t pageBack = static_cast<uint8_t>(MappedInputManager::Button::PageBack);
  const uint8_t pageForward = static_cast<uint8_t>(MappedInputManager::Button::PageForward);

  ASSERT_EQ(assign(entries, nextPage, 1, kButtonAction, pageForward), AssignResult::Added);
  ASSERT_EQ(assign(entries, previousPage, 1, kButtonAction, pageBack), AssignResult::Added);
  EXPECT_EQ(resolve(entries[0]).target, pageForward);
  EXPECT_EQ(resolve(entries[1]).target, pageBack);
  EXPECT_EQ(pageBack, 7);
  EXPECT_EQ(pageForward, 8);
}

TEST(BleInputMapping, TypedKeyboardUsageDoesNotConflateWithLegacyUntypedCode) {
  KeyMapEntry entries[kMapCapacity] = {};
  const CapturedIdentity oldIdentity[] = {{1, 0x04}};
  const CapturedIdentity typedIdentity[] = {{2, 0x04}};
  ASSERT_EQ(assign(entries, oldIdentity, 1, kButtonAction, 3), AssignResult::Added);
  EXPECT_EQ(assign(entries, typedIdentity, 1, kShortcutAction, kShortcutHome), AssignResult::Added);
  EXPECT_EQ(entries[0].keyKind, 1);
  EXPECT_EQ(entries[0].actionType, kButtonAction);
  EXPECT_EQ(entries[1].keyKind, 2);
  EXPECT_EQ(entries[1].actionType, kShortcutAction);
  EXPECT_EQ(entries[1].shortcut, kShortcutHome);
  EXPECT_EQ(findFirstMatch(entries, 2, 0x04, 1, 0x04, 0), 1);
}

TEST(BleInputMapping, LegacyPhysicalSettingsMigrateWithoutChangingTargetsOrSignatures) {
  PersistedEntry old{};
  old.keyKind = 0;
  old.keyValue = 9;
  old.button = 8;  // Legacy PageForward binding remains valid and unchanged.
  old.actionType = 0xFF;  // No type field existed before this worker.
  old.signatureCount = 2;
  old.signature[0] = 9;
  old.signature[1] = static_cast<uint16_t>((1U << 8) | 0x4F);

  KeyMapEntry restored{};
  ASSERT_TRUE(fromPersisted(old, restored));
  EXPECT_EQ(restored.actionType, kButtonAction);
  EXPECT_EQ(restored.button, 8);
  ASSERT_EQ(restored.signatureCount, 2);
  EXPECT_EQ(restored.signature[1], old.signature[1]);

  const PersistedEntry saved = toPersisted(restored);
  EXPECT_EQ(saved.keyKind, old.keyKind);
  EXPECT_EQ(saved.keyValue, old.keyValue);
  EXPECT_EQ(saved.button, old.button);
  EXPECT_EQ(saved.signatureCount, old.signatureCount);
  EXPECT_EQ(saved.signature[1], old.signature[1]);
}

TEST(BleInputMapping, ShortcutSettingsRoundTripAndRejectUnsafeTargets) {
  PersistedEntry stored{};
  stored.keyKind = 1;
  stored.keyValue = 0x2C;
  stored.actionType = kShortcutAction;
  stored.button = 0xFF;
  stored.shortcut = kShortcutRecent;

  KeyMapEntry restored{};
  ASSERT_TRUE(fromPersisted(stored, restored));
  EXPECT_EQ(resolve(restored).type, ResolvedActionType::Shortcut);
  EXPECT_EQ(resolve(restored).target, kShortcutRecent);
  EXPECT_EQ(toPersisted(restored).shortcut, kShortcutRecent);
  EXPECT_FALSE(validTarget(kShortcutAction, kShortcutCount));  // No sleep/deep-sleep target exists.

  stored.shortcut = kShortcutCount;
  EXPECT_FALSE(fromPersisted(stored, restored));
}

TEST(BleInputMapping, CaptureDeduplicatesAndBoundsInput) {
  uint8_t kinds[kSignatureMaxEvents] = {};
  uint8_t values[kSignatureMaxEvents] = {};
  uint8_t count = 0;
  EXPECT_EQ(addCapturedIdentity(kinds, values, kSignatureMaxEvents, count, 1, 0x4F), CaptureResult::Added);
  EXPECT_EQ(addCapturedIdentity(kinds, values, kSignatureMaxEvents, count, 1, 0x4F), CaptureResult::Duplicate);
  EXPECT_EQ(addCapturedIdentity(kinds, values, kSignatureMaxEvents, count, 0, 7), CaptureResult::Added);
  EXPECT_EQ(count, 2);
  for (uint8_t i = count; i < kSignatureMaxEvents; ++i) {
    EXPECT_EQ(addCapturedIdentity(kinds, values, kSignatureMaxEvents, count, 1, i), CaptureResult::Added);
  }
  EXPECT_EQ(count, kSignatureMaxEvents);
  EXPECT_EQ(addCapturedIdentity(kinds, values, kSignatureMaxEvents, count, 1, 0x70), CaptureResult::Full);
  EXPECT_EQ(addCapturedIdentity(kinds, values, kSignatureMaxEvents, count, 3, 0x70), CaptureResult::Invalid);
}

TEST(BleInputMapping, LearnAcceptsOnlyExplicitlyNormalizedKeyboardUsages) {
  uint8_t kind = 0xFF;
  uint8_t value = 0;
  EXPECT_TRUE(encodeKeyboardUsageForLearning(0x07, 0x4F, kind, value));
  EXPECT_EQ(kind, 2);
  EXPECT_EQ(value, 0x4F);
  EXPECT_FALSE(encodeKeyboardUsageForLearning(0x0C, 0x4F, kind, value));
  EXPECT_FALSE(encodeKeyboardUsageForLearning(0xFF, 0x4F, kind, value));
  EXPECT_FALSE(encodeKeyboardUsageForLearning(0x07, 0, kind, value));
}

TEST(BleInputMapping, LearnAcceptsOnlyHostNormalizedSpecialKeys) {
  uint8_t kind = 0xFF;
  uint8_t value = 0;
  EXPECT_TRUE(encodeNormalizedSpecialForLearning(true, 4, kind, value));
  EXPECT_EQ(kind, 0);
  EXPECT_EQ(value, 4);
  EXPECT_FALSE(encodeNormalizedSpecialForLearning(false, 4, kind, value));
  EXPECT_FALSE(encodeNormalizedSpecialForLearning(true, 0, kind, value));
}

TEST(BleInputMapping, ExactTypedUsageWinsBeforeLegacyFallback) {
  KeyMapEntry entries[kMapCapacity] = {};
  const CapturedIdentity legacy[] = {{1, 0x4F}};
  const CapturedIdentity typed[] = {{2, 0x4F}};
  ASSERT_EQ(assign(entries, legacy, 1, kButtonAction, 2), AssignResult::Added);
  ASSERT_EQ(assign(entries, typed, 1, kShortcutAction, kShortcutLibrary), AssignResult::Added);
  EXPECT_EQ(findFirstMatch(entries, 2, 0x4F, 1, 0x4F, 0), 1);
  EXPECT_EQ(findFirstMatch(entries, 2, 0x50, 1, 0x4F, 0), 0);
}

TEST(BleInputMapping, SignatureReleaseDoesNotCancelPendingSequence) {
  EXPECT_TRUE(releaseKeepsPendingSignature(InputPhase::Release));
  EXPECT_FALSE(releaseKeepsPendingSignature(InputPhase::Press));
  EXPECT_FALSE(releaseKeepsPendingSignature(InputPhase::Repeat));
}

TEST(BleInputMapping, DirectionalBindingsNavigateReaderMenusWithOrientationMapping) {
  // A learned Right/Down mapping is both a reader page-turn edge and a Next
  // action while ButtonNavigator owns a menu or list.
  EXPECT_TRUE(navigationAliasForMenu(true, false, false, true, false, false, false, false));
  EXPECT_TRUE(navigationAliasForMenu(true, false, false, false, false, true, false, false));
  EXPECT_TRUE(navigationAliasForMenu(false, false, true, false, false, false, false, false));
  EXPECT_TRUE(navigationAliasForMenu(false, false, false, false, true, false, false, false));

  // Orientation-following navigation keeps the same axis swap as GPIO input.
  EXPECT_TRUE(navigationAliasForMenu(true, true, true, false, false, false, false, false));
  EXPECT_TRUE(navigationAliasForMenu(true, true, false, false, true, false, false, false));
  EXPECT_TRUE(navigationAliasForMenu(false, true, false, true, false, false, false, false));
  EXPECT_TRUE(navigationAliasForMenu(false, true, false, false, false, true, false, false));
  EXPECT_FALSE(navigationAliasForMenu(true, false, true, false, false, false, false, false));
  EXPECT_FALSE(navigationAliasForMenu(false, false, false, true, false, false, false, false));

  // PageForward/PageBack can exist in saved settings created by older Learn
  // screens and continue to move a menu in their original direction.
  EXPECT_TRUE(navigationAliasForMenu(true, false, false, false, false, false, false, true));
  EXPECT_TRUE(navigationAliasForMenu(false, false, false, false, false, false, true, false));
  EXPECT_FALSE(navigationAliasForMenu(true, false, false, false, false, false, true, false));
  EXPECT_FALSE(navigationAliasForMenu(false, false, false, false, false, false, false, true));
}

TEST(BleInputMapping, ShortcutDebounceHandlesQuickTapAndMillisWraparound) {
  EXPECT_FALSE(debounceElapsed(1200, 1000, 280));
  EXPECT_TRUE(debounceElapsed(1280, 1000, 280));
  EXPECT_FALSE(debounceElapsed(0x0000001FUL, 0xFFFFFFF0UL, 48));
  EXPECT_TRUE(debounceElapsed(0x00000020UL, 0xFFFFFFF0UL, 48));
  EXPECT_TRUE(debounceElapsed(0x00000021UL, 0xFFFFFFF0UL, 48));
}

TEST(BleInputMapping, DuplicateIdentityWithinOneSignatureIsRejected) {
  KeyMapEntry entries[kMapCapacity] = {};
  const CapturedIdentity duplicate[] = {{1, 0x4F}, {1, 0x4F}};
  EXPECT_EQ(assign(entries, duplicate, 2, kButtonAction, 3), AssignResult::Invalid);
  EXPECT_FALSE(isOccupied(entries[0]));
}

TEST(BleInputMapping, VirtualButtonPreservesPressRepeatReleaseAndDebounceSemantics) {
  EXPECT_EQ(transition(InputPhase::Press, false, false, 1000, 0, 280), ButtonTransition::Pressed);
  EXPECT_EQ(transition(InputPhase::Repeat, true, true, 1200, 1000, 280), ButtonTransition::Repeated);
  EXPECT_EQ(transition(InputPhase::Release, true, true, 1300, 1000, 280), ButtonTransition::Released);
  EXPECT_EQ(transition(InputPhase::Repeat, false, false, 1400, 0, 280), ButtonTransition::Ignored);
  EXPECT_EQ(transition(InputPhase::Press, true, true, 1400, 1000, 280), ButtonTransition::Debounced);
  EXPECT_EQ(transition(InputPhase::Press, false, false, 1200, 1000, 280), ButtonTransition::Debounced);
  EXPECT_EQ(transition(InputPhase::Press, false, false, 1400, 1000, 280), ButtonTransition::Pressed);
}

TEST(BleInputMapping, HeldDurationSurvivesReleaseFrameAndUsesWrapSafeElapsedTime) {
  mappedinput::BleHeldDuration duration;
  duration.press(1000);
  EXPECT_EQ(duration.duration(1125), 125);

  duration.release(1375);
  EXPECT_EQ(duration.duration(1375), 375);
  // A reader may ask for the held time after processing the release edge.
  EXPECT_EQ(duration.duration(1400), 375);

  duration.beginFrame();
  EXPECT_EQ(duration.duration(1401), 0);

  duration.press(0xFFFFFFF0UL);
  EXPECT_EQ(duration.duration(0x00000010UL), 32);
  duration.release(0x00000020UL);
  EXPECT_EQ(duration.duration(0x00000020UL), 48);
}

TEST(BleInputMapping, HeldDurationIsIsolatedPerButtonAndNewPressClearsReleasedTime) {
  mappedinput::BleHeldDuration firstButton;
  mappedinput::BleHeldDuration secondButton;
  firstButton.press(100);
  secondButton.press(120);

  firstButton.release(150);
  EXPECT_EQ(firstButton.duration(160), 50);
  EXPECT_EQ(secondButton.duration(160), 40);

  // Starting another press on one button discards only that button's old
  // release-frame duration and starts counting from the new timestamp.
  firstButton.press(200);
  EXPECT_EQ(firstButton.duration(225), 25);
  EXPECT_EQ(secondButton.duration(225), 105);

  firstButton.beginFrame();
  secondButton.beginFrame();
  EXPECT_EQ(firstButton.duration(226), 26);
  EXPECT_EQ(secondButton.duration(226), 106);
  secondButton.release(230);
  EXPECT_EQ(secondButton.duration(231), 110);
  firstButton.release(240);
  EXPECT_EQ(firstButton.duration(241), 40);
}

TEST(BleInputMapping, LearnTimeoutHandlesMillisWraparound) {
  EXPECT_FALSE(learnTimedOut(100, 30099, 30000));
  EXPECT_TRUE(learnTimedOut(100, 30100, 30000));
  EXPECT_TRUE(learnTimedOut(0xFFFFFFF0UL, 0x00000020UL, 48));
}

TEST(BleInputMapping, FixedMappingTableRejectsOverflow) {
  KeyMapEntry entries[kMapCapacity] = {};
  for (uint8_t i = 0; i < kMapCapacity; ++i) {
    const CapturedIdentity identity[] = {{1, static_cast<uint8_t>(i + 4)}};
    EXPECT_EQ(assign(entries, identity, 1, kButtonAction, static_cast<uint8_t>(i % kPhysicalButtonCount)),
              AssignResult::Added);
  }
  const CapturedIdentity extra[] = {{1, 0xF0}};
  EXPECT_EQ(assign(entries, extra, 1, kButtonAction, 3), AssignResult::Full);
}

}  // namespace
