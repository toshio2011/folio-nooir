#include "util/BleKeyMapping.h"
#include "util/BleHidReportMetadata.h"
#include "util/BleHidGenericLearning.h"
#include "util/IineTouchGesture.h"
#include "util/IineTouchButtons.h"
#include "util/BleMappingProfiles.h"
#include "MappedInputManager.h"
#include "util/QuickActions.h"

#include <gtest/gtest.h>

#include <array>
#include <cstdio>
#include <cstring>

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
  EXPECT_EQ(addCapturedIdentity(kinds, values, kSignatureMaxEvents, count, 4, 0x70), CaptureResult::Invalid);
}

TEST(BleInputMapping, LearnAcceptsTypedKeyboardConsumerAndIineButtonUsages) {
  uint8_t kind = 0xFF;
  uint8_t value = 0;
  EXPECT_TRUE(encodeHidUsageForLearning(0x07, 0x4F, kind, value));
  EXPECT_EQ(kind, 2);
  EXPECT_EQ(value, 0x4F);
  EXPECT_TRUE(encodeHidUsageForLearning(0x0C, 0xB5, kind, value));
  EXPECT_EQ(kind, 3);
  EXPECT_EQ(value, 0xB5);
  EXPECT_TRUE(encodeHidUsageForLearning(0x09, 0x01, kind, value));
  EXPECT_EQ(kind, kIineMouseButtonIdentityKind);
  EXPECT_EQ(value, 0x01);
  EXPECT_FALSE(encodeHidUsageForLearning(0xFF, 0x4F, kind, value));
  EXPECT_FALSE(encodeHidUsageForLearning(0x07, 0, kind, value));
}

TEST(BleInputMapping, ReportReferenceControlsIdStrippingAndRejectsMismatch) {
  const uint8_t withId[] = {0x05, 0x01, 0x02};
  const auto stripped = bleinput::hid::payloadWithoutDeclaredReportId(withId, sizeof(withId), 0x05, true);
  ASSERT_NE(stripped.data, nullptr);
  EXPECT_EQ(stripped.length, 2U);
  EXPECT_EQ(stripped.data[0], 0x01);
  EXPECT_FALSE(stripped.reportIdMismatch);

  const auto unknown = bleinput::hid::payloadWithoutDeclaredReportId(withId, sizeof(withId), 0xFF, false);
  ASSERT_NE(unknown.data, nullptr);
  EXPECT_EQ(unknown.length, sizeof(withId));
  EXPECT_EQ(unknown.data[0], 0x05);

  const auto mismatch = bleinput::hid::payloadWithoutDeclaredReportId(withId, sizeof(withId), 0x06, true);
  EXPECT_EQ(mismatch.data, nullptr);
  EXPECT_TRUE(mismatch.reportIdMismatch);
}

TEST(BleInputMapping, DeclaredReportUsesExactGattBodyLengthBeforeOptionalIdPrefix) {
  const uint8_t physicalBody[] = {0x13, 0xC4, 0x09, 0x98, 0x08};
  auto view = bleinput::hid::payloadForDeclaredReport(
      physicalBody, sizeof(physicalBody), 0x06, true, true, sizeof(physicalBody));
  ASSERT_NE(view.data, nullptr);
  EXPECT_EQ(view.length, sizeof(physicalBody));
  EXPECT_EQ(view.data[0], 0x13);  // Status/contact byte is body data, not a mismatched Report ID.
  EXPECT_FALSE(view.reportIdMismatch);

  const uint8_t prefixed[] = {0x06, 0x13, 0xC4, 0x09, 0x98, 0x08};
  view = bleinput::hid::payloadForDeclaredReport(prefixed, sizeof(prefixed), 0x06, true, true, 5);
  ASSERT_NE(view.data, nullptr);
  EXPECT_EQ(view.length, 5U);
  EXPECT_EQ(view.data[0], 0x13);

  const uint8_t wrongExtraPrefix[] = {0x07, 0x13, 0xC4, 0x09, 0x98, 0x08};
  view = bleinput::hid::payloadForDeclaredReport(wrongExtraPrefix, sizeof(wrongExtraPrefix),
                                                 0x06, true, true, 5);
  EXPECT_EQ(view.data, nullptr);
  EXPECT_TRUE(view.reportIdMismatch);

  // With no usable map the Report Reference still determines the ID; keep the
  // entire bounded characteristic value as report body for safe edge learning.
  view = bleinput::hid::payloadForDeclaredReport(physicalBody, sizeof(physicalBody),
                                                 0x06, true, false, 0);
  ASSERT_NE(view.data, nullptr);
  EXPECT_EQ(view.length, sizeof(physicalBody));
}

TEST(BleInputMapping, ReportId255IsValidWhenReportReferenceDeclaresIt) {
  const uint8_t withId[] = {0xFF, 0xB5};
  const auto stripped = bleinput::hid::payloadWithoutDeclaredReportId(
      withId, sizeof(withId), 0xFF, true);
  ASSERT_NE(stripped.data, nullptr);
  EXPECT_EQ(stripped.length, 1U);
  EXPECT_EQ(stripped.data[0], 0xB5);
  EXPECT_FALSE(stripped.reportIdMismatch);
}

TEST(BleInputMapping, ReportMapParserRoutesOnlyDeclaredSafeInputReports) {
  const uint8_t map[] = {
      0x05, 0x01,        // Usage Page (Generic Desktop)
      0xA1, 0x01,        // Collection (Application)
      0x85, 0xFF,        // Report ID 255
      0x05, 0x0C,        // Usage Page (Consumer)
      0x81, 0x00,        // Input
      0x85, 0x01,        // Report ID 1
      0x05, 0x07,        // Usage Page (Keyboard)
      0x81, 0x00,        // Input
      0xC0};             // End Collection
  bleinput::hid::ReportMapHints hints;
  ASSERT_TRUE(bleinput::hid::parseReportMap(map, sizeof(map), hints));
  EXPECT_TRUE(hints.valid);
  EXPECT_TRUE(hints.hasReportIds);
  EXPECT_TRUE(bleinput::hid::reportMapAllowsUsage(hints, 0xFF, true, 1, 0x0C));
  EXPECT_TRUE(bleinput::hid::reportMapAllowsUsage(hints, 0x01, true, 1, 0x07));
  EXPECT_FALSE(bleinput::hid::reportMapAllowsUsage(hints, 0x02, true, 1, 0x0C));
  EXPECT_FALSE(bleinput::hid::genericInputReportAllowed(hints, 0x02, true, 1));
  EXPECT_FALSE(bleinput::hid::genericInputReportAllowed(hints, 0xFF, true, 2));
}

TEST(BleInputMapping, ReportMapParserRejectsMixedUnknownAndMalformedReports) {
  const uint8_t mixed[] = {
      0x85, 0x03, 0x05, 0x0C, 0x81, 0x00,  // Consumer input, ID 3.
      0x05, 0x01, 0x81, 0x00};              // Unsupported input, same ID.
  bleinput::hid::ReportMapHints hints;
  ASSERT_TRUE(bleinput::hid::parseReportMap(mixed, sizeof(mixed), hints));
  EXPECT_FALSE(bleinput::hid::reportMapAllowsUsage(hints, 0x03, true, 1, 0x0C));

  const uint8_t malformed[] = {0x85};  // Truncated Report ID item.
  EXPECT_FALSE(bleinput::hid::parseReportMap(malformed, sizeof(malformed), hints));
  EXPECT_TRUE(hints.available);
  EXPECT_FALSE(hints.valid);
  EXPECT_FALSE(bleinput::hid::genericInputReportAllowed(hints, 0xFF, false, 1));
}

TEST(BleInputMapping, ReportMapParserBoundsReportIdTablesAndPreservesLegacyNoMapPath) {
  uint8_t map[17 * 7] = {};
  size_t offset = 0;
  for (uint8_t id = 1; id <= 17; ++id) {
    map[offset++] = 0x85; map[offset++] = id;  // Report ID.
    map[offset++] = 0x05; map[offset++] = 0x0C;  // Consumer page.
    map[offset++] = 0x81; map[offset++] = 0x00;  // Input.
  }
  bleinput::hid::ReportMapHints hints;
  EXPECT_FALSE(bleinput::hid::parseReportMap(map, offset, hints));
  EXPECT_TRUE(hints.overflow);
  EXPECT_FALSE(hints.valid);

  bleinput::hid::ReportMapHints noMap;
  EXPECT_TRUE(bleinput::hid::genericInputReportAllowed(noMap, 0xFF, false, 0xFF));
}

TEST(BleInputMapping, GenericLearningDecodesUnknownVendorButtonsByDescriptorField) {
  using namespace bleinput::hid::learning;
  const uint8_t descriptor[] = {
      0x06, 0x00, 0xFF, 0x09, 0x01, 0xA1, 0x01,
      0x85, 0x2A, 0x09, 0x10, 0x15, 0x00, 0x25, 0x01,
      0x75, 0x01, 0x95, 0x02, 0x19, 0x10, 0x29, 0x11,
      0x81, 0x02, 0xC0};
  Layout layout{};
  ASSERT_TRUE(parseLayout(descriptor, sizeof(descriptor), layout));
  ASSERT_TRUE(layout.valid);
  ASSERT_EQ(layout.fieldCount, 2);
  EXPECT_EQ(layout.fields[0].reportId, 0x2A);
  EXPECT_EQ(layout.fields[0].bitOffset, 0);
  EXPECT_EQ(layout.fields[0].usagePage, 0xFF00);
  EXPECT_EQ(layout.fields[0].usage, 0x10);
  EXPECT_EQ(layout.fields[1].bitOffset, 1);

  Decoder decoder{};
  Edge edges[kMaxEdges]{};
  uint8_t count = 0;
  const uint8_t baseline[] = {0x00};
  const uint8_t button1[] = {0x01};
  const uint8_t twoButtons[] = {0x03};
  const uint8_t button2[] = {0x02};
  EXPECT_TRUE(decode(layout, decoder, 0x2A, baseline, 1, edges, kMaxEdges, count));
  EXPECT_EQ(count, 0);
  ASSERT_TRUE(decode(layout, decoder, 0x2A, button1, 1, edges, kMaxEdges, count));
  ASSERT_EQ(count, 1);
  EXPECT_EQ(edges[0].fieldToken, 0);
  EXPECT_TRUE(edges[0].pressed);
  ASSERT_TRUE(decode(layout, decoder, 0x2A, twoButtons, 1, edges, kMaxEdges, count));
  ASSERT_EQ(count, 1);
  EXPECT_EQ(edges[0].fieldToken, 1);
  ASSERT_TRUE(decode(layout, decoder, 0x2A, button2, 1, edges, kMaxEdges, count));
  ASSERT_EQ(count, 1);
  EXPECT_EQ(edges[0].fieldToken, 0);
  EXPECT_FALSE(edges[0].pressed);
  EXPECT_FALSE(decode(layout, decoder, 0x2A, twoButtons, 2, edges, kMaxEdges, count));
}

TEST(BleInputMapping, GenericLearningAcceptsUnknownUsagePagesByFieldAndReportIdentity) {
  using namespace bleinput::hid::learning;
  const uint8_t descriptor[] = {
      0x06, 0x59, 0x00, 0x09, 0x01, 0xA1, 0x01,
      0x85, 0x31, 0x15, 0x00, 0x25, 0x01, 0x75, 0x01, 0x95, 0x01,
      0x09, 0x02, 0x81, 0x02, 0xC0};
  Layout layout{};
  ASSERT_TRUE(parseLayout(descriptor, sizeof(descriptor), layout));
  ASSERT_EQ(layout.fieldCount, 1);
  EXPECT_EQ(layout.fields[0].usagePage, 0x0059);
  EXPECT_EQ(layout.fields[0].reportId, 0x31);
  Decoder decoder{};
  Edge edges[kMaxEdges]{};
  uint8_t count = 0;
  const uint8_t baseline[] = {0};
  const uint8_t pressed[] = {1};
  EXPECT_TRUE(decode(layout, decoder, 0x31, baseline, 1, edges, kMaxEdges, count));
  EXPECT_EQ(count, 0);
  ASSERT_TRUE(decode(layout, decoder, 0x31, pressed, 1, edges, kMaxEdges, count));
  ASSERT_EQ(count, 1);
  EXPECT_TRUE(edges[0].pressed);
  EXPECT_EQ(edges[0].fieldToken, 0);
}

TEST(BleInputMapping, GenericLearnedIdentitySequenceIsBoundedAndPersistable) {
  using namespace bleinput::hid::learning;
  IdentityPart parts[kIdentityParts]{};
  ASSERT_TRUE(identityParts(0x12345678, 0x2A, 3, 1, parts, kIdentityParts));
  EXPECT_FALSE(identityParts(0x12345678, 0x2A, 3, 0, parts, kIdentityParts));
  EXPECT_EQ(parts[0].kind, 6);
  EXPECT_EQ(parts[0].value, 0x78);
  EXPECT_EQ(parts[3].kind, 9);
  EXPECT_EQ(parts[3].value, 0x12);
  EXPECT_EQ(parts[4].kind, 10);
  EXPECT_EQ(parts[4].value, 0x2A);
  EXPECT_EQ(parts[5].kind, 11);
  EXPECT_EQ(parts[5].value, 3);
  EXPECT_EQ(parts[6].kind, 12);
  EXPECT_EQ(parts[6].value, 1);

  CapturedIdentity signature[kIdentityParts]{};
  for (uint8_t i = 0; i < kIdentityParts; ++i) signature[i] = {parts[i].kind, parts[i].value};
  KeyMapEntry entries[kMapCapacity]{};
  ASSERT_EQ(assign(entries, signature, kIdentityParts, kButtonAction, 3), AssignResult::Added);
  const PersistedEntry saved = toPersisted(entries[0]);
  KeyMapEntry restored{};
  ASSERT_TRUE(fromPersisted(saved, restored));
  EXPECT_TRUE(sameCaptured(restored, signature, kIdentityParts));
  EXPECT_TRUE(validIdentity(2, 0x04));  // Existing keyboard identities remain valid.
}

TEST(BleInputMapping, GenericLearningIgnoresMouseAxesAndTracksGamepadHatRelease) {
  using namespace bleinput::hid::learning;
  const uint8_t mouseDescriptor[] = {
      0x05, 0x01, 0x09, 0x02, 0xA1, 0x01, 0x09, 0x30, 0x15, 0x00, 0x26, 0xFF, 0x00,
      0x75, 0x08, 0x95, 0x01, 0x81, 0x06, 0xC0};
  Layout mouse{};
  ASSERT_TRUE(parseLayout(mouseDescriptor, sizeof(mouseDescriptor), mouse));
  EXPECT_EQ(mouse.fieldCount, 0);

  const uint8_t hatDescriptor[] = {
      0x05, 0x01, 0x09, 0x04, 0xA1, 0x01, 0x85, 0x07, 0x09, 0x39,
      0x15, 0x00, 0x25, 0x07, 0x75, 0x04, 0x95, 0x01, 0x81, 0x42, 0xC0};
  Layout hat{};
  ASSERT_TRUE(parseLayout(hatDescriptor, sizeof(hatDescriptor), hat));
  ASSERT_EQ(hat.fieldCount, 1);
  EXPECT_EQ(hat.fields[0].kind, FieldKind::Hat);
  Decoder decoder{};
  Edge edges[kMaxEdges]{};
  uint8_t count = 0;
  const uint8_t neutral[] = {0x08};
  const uint8_t north[] = {0x00};
  EXPECT_TRUE(decode(hat, decoder, 0x07, neutral, 1, edges, kMaxEdges, count));
  ASSERT_TRUE(decode(hat, decoder, 0x07, north, 1, edges, kMaxEdges, count));
  ASSERT_EQ(count, 1);
  EXPECT_EQ(edges[0].state, 1);
  EXPECT_TRUE(edges[0].pressed);
  ASSERT_TRUE(decode(hat, decoder, 0x07, neutral, 1, edges, kMaxEdges, count));
  ASSERT_EQ(count, 1);
  EXPECT_FALSE(edges[0].pressed);
}

TEST(BleInputMapping, GenericTouchNeedsDeclaredTouchCollectionAndEmitsOneReleaseTimeGesture) {
  using namespace bleinput::hid::learning;
  const uint8_t descriptor[] = {
      0x05, 0x0D, 0x09, 0x04, 0xA1, 0x01, 0x85, 0x06,
      0x09, 0x22, 0xA1, 0x02, 0x09, 0x42, 0x15, 0x00, 0x25, 0x01,
      0x75, 0x01, 0x95, 0x01, 0x81, 0x02, 0x75, 0x07, 0x95, 0x01, 0x81, 0x03,
      0x05, 0x01, 0x15, 0x00, 0x26, 0xFF, 0x0F, 0x75, 0x10, 0x95, 0x02,
      0x09, 0x30, 0x09, 0x31, 0x81, 0x02, 0xC0, 0xC0};
  Layout layout{};
  ASSERT_TRUE(parseLayout(descriptor, sizeof(descriptor), layout));
  ASSERT_EQ(layout.fieldCount, 3);
  EXPECT_EQ(layout.fields[0].kind, FieldKind::TouchContact);
  EXPECT_EQ(layout.fields[1].kind, FieldKind::TouchX);
  EXPECT_EQ(layout.fields[2].kind, FieldKind::TouchY);

  Decoder decoder{};
  Edge edges[kMaxEdges]{};
  uint8_t count = 0;
  const uint8_t neutral[] = {0x00, 0xE8, 0x03, 0xE8, 0x03};   // 1000, 1000
  const uint8_t downStart[] = {0x01, 0xE8, 0x03, 0xE8, 0x03};
  const uint8_t moveRight[] = {0x01, 0xD0, 0x07, 0xE8, 0x03}; // 2000, 1000
  EXPECT_TRUE(decode(layout, decoder, 6, neutral, sizeof(neutral), edges, kMaxEdges, count, 10));
  EXPECT_TRUE(decode(layout, decoder, 6, downStart, sizeof(downStart), edges, kMaxEdges, count, 20));
  EXPECT_EQ(count, 0);
  EXPECT_TRUE(decode(layout, decoder, 6, moveRight, sizeof(moveRight), edges, kMaxEdges, count, 40));
  const uint8_t release[] = {0x00, 0xD0, 0x07, 0xE8, 0x03};
  ASSERT_TRUE(decode(layout, decoder, 6, release, sizeof(release), edges, kMaxEdges, count, 60));
  ASSERT_EQ(count, 2);
  EXPECT_EQ(edges[0].fieldToken, 0);
  EXPECT_EQ(edges[0].state, 3);  // one Right gesture token
  EXPECT_TRUE(edges[0].pressed);
  EXPECT_FALSE(edges[1].pressed);

  const uint8_t tapStart[] = {0x01, 0xD0, 0x07, 0xE8, 0x03};
  ASSERT_TRUE(decode(layout, decoder, 6, tapStart, sizeof(tapStart), edges, kMaxEdges, count, 100));
  const uint8_t tapRelease[] = {0x00, 0xD0, 0x07, 0xE8, 0x03};
  ASSERT_TRUE(decode(layout, decoder, 6, tapRelease, sizeof(tapRelease), edges, kMaxEdges, count, 120));
  ASSERT_EQ(count, 2);
  EXPECT_EQ(edges[0].state, 1);
  EXPECT_FALSE(decode(layout, decoder, 6, tapRelease, 4, edges, kMaxEdges, count, 130));
}

TEST(BleInputMapping, GenericTouchLearnsPhysicalTwentyByteReportsFromDescriptorWithoutDeviceName) {
  using namespace bleinput::hid::learning;
  // A bounded 20-byte Report ID 6/Input layout matching the physical touch
  // field order: Tip Switch, In Range, padding/contact count, X/Y, then reserved
  // bytes. No device-name predicate participates in this decoder.
  const uint8_t descriptor[] = {
      0x05,0x0D,0x09,0x04,0xA1,0x01,0x85,0x06,0x05,0x0D,0x09,0x22,0xA1,0x02,
      0x09,0x42,0x15,0x00,0x25,0x01,0x75,0x01,0x95,0x01,0x81,0x02,
      0x09,0x32,0x81,0x02,0x75,0x02,0x81,0x03,0x75,0x04,0x25,0x0F,0x09,0x54,0x81,0x02,
      0x05,0x01,0x16,0x01,0x80,0x26,0xFF,0x7F,0x75,0x10,0x95,0x02,
      0x09,0x30,0x09,0x31,0x81,0x06,0x75,0x08,0x95,0x0F,0x81,0x01,0xC0,0xC0};
  Layout layout{};
  ASSERT_TRUE(parseLayout(descriptor, sizeof(descriptor), layout));
  ASSERT_TRUE(reportHasTouchGestureFields(layout, 6));
  bool sawSignedX = false;
  for (uint8_t i = 0; i < layout.fieldCount; ++i) {
    if (layout.fields[i].kind == FieldKind::TouchX) {
      sawSignedX = true;
      EXPECT_EQ(layout.fields[i].logicalMin, -32767);
      EXPECT_EQ(layout.fields[i].logicalMax, 32767);
    }
  }
  EXPECT_TRUE(sawSignedX);
  size_t expectedBytes = 0;
  ASSERT_TRUE(expectedInputBytes(layout, 6, expectedBytes));
  EXPECT_EQ(expectedBytes, 20U);

  auto report = [](uint8_t status, uint16_t x, uint16_t y) {
    std::array<uint8_t,20> bytes{};
    bytes[0] = status;
    bytes[1] = static_cast<uint8_t>(x);
    bytes[2] = static_cast<uint8_t>(x >> 8);
    bytes[3] = static_cast<uint8_t>(y);
    bytes[4] = static_cast<uint8_t>(y >> 8);
    return bytes;
  };
  Decoder decoder{};
  Edge edges[kMaxEdges]{};
  uint8_t count = 0;
  auto idle = report(0x12, 0x07D0, 0x07D0);
  auto leftStart = report(0x13, 0x09C4, 0x0898);
  auto leftMiddle = report(0x13, 0x031A, 0x0883);
  auto leftEnd = report(0x13, 0x0000, 0x0878);
  auto release = report(0x12, 0x0000, 0x0878);
  EXPECT_TRUE(decode(layout, decoder, 6, idle.data(), idle.size(), edges, kMaxEdges, count, 10));
  ASSERT_TRUE(decode(layout, decoder, 6, leftStart.data(), leftStart.size(), edges, kMaxEdges, count, 20));
  EXPECT_EQ(count, 0);
  ASSERT_TRUE(decode(layout, decoder, 6, leftMiddle.data(), leftMiddle.size(), edges, kMaxEdges, count, 40));
  EXPECT_EQ(count, 0);
  ASSERT_TRUE(decode(layout, decoder, 6, leftEnd.data(), leftEnd.size(), edges, kMaxEdges, count, 60));
  EXPECT_EQ(count, 0);
  ASSERT_TRUE(decode(layout, decoder, 6, release.data(), release.size(), edges, kMaxEdges, count, 80));
  ASSERT_EQ(count, 2);
  EXPECT_EQ(edges[0].state, 2);  // Same synthetic swipe identity as legacy IINE left.
  EXPECT_TRUE(edges[0].pressed);
  EXPECT_FALSE(edges[1].pressed);
  IdentityPart signature[kIdentityParts]{};
  ASSERT_TRUE(identityParts(layout.descriptorHash, 6, edges[0].fieldToken, edges[0].state,
                            signature, kIdentityParts));
  const auto learnedLeft = signature[kIdentityParts - 1].value;

  // Repeated coordinates produce no second completed action; another physical
  // control state has a different learned identity independent of coordinates.
  EXPECT_TRUE(decode(layout, decoder, 6, release.data(), release.size(), edges, kMaxEdges, count, 90));
  EXPECT_EQ(count, 0);
  EXPECT_TRUE(identityParts(layout.descriptorHash, 6, edges[0].fieldToken, 3,
                            signature, kIdentityParts));
  EXPECT_NE(learnedLeft, signature[kIdentityParts - 1].value);
  auto truncated = report(0x13, 1000, 1000);
  EXPECT_FALSE(decode(layout, decoder, 6, truncated.data(), truncated.size() - 1,
                      edges, kMaxEdges, count, 100));

  Decoder resetProbe{};
  EXPECT_TRUE(decode(layout, resetProbe, 6, leftStart.data(), leftStart.size(), edges, kMaxEdges, count, 200));
  EXPECT_EQ(count, 0);
  resetProbe.resetTouch();
  EXPECT_TRUE(decode(layout, resetProbe, 6, release.data(), release.size(), edges, kMaxEdges, count, 220));
  EXPECT_EQ(count, 0);  // A Reader boundary cannot complete an old contact.
}

TEST(BleInputMapping, GenericRawFallbackRequiresOneBoundedPressReleaseBit) {
  using namespace bleinput::hid::learning;
  RawBitFallback fallback{};
  Edge press{}, release{};
  const uint8_t neutral[] = {0x00, 0x00};
  const uint8_t pressed[] = {0x04, 0x00};
  const uint8_t ambiguous[] = {0x07, 0x00};
  EXPECT_FALSE(decodeRawBitFallbackCycle(fallback, 0x1234, neutral, 2, 10, press, release));
  EXPECT_FALSE(decodeRawBitFallbackCycle(fallback, 0x1234, pressed, 2, 70, press, release));
  ASSERT_TRUE(decodeRawBitFallbackCycle(fallback, 0x1234, neutral, 2, 120, press, release));
  EXPECT_EQ(press.fieldToken, 2);
  EXPECT_TRUE(press.pressed);
  EXPECT_EQ(release.fieldToken, 2);
  EXPECT_FALSE(release.pressed);
  // A second matching cycle is independently learnable; every cycle yields one pair.
  EXPECT_FALSE(decodeRawBitFallbackCycle(fallback, 0x1234, pressed, 2, 180, press, release));
  ASSERT_TRUE(decodeRawBitFallbackCycle(fallback, 0x1234, neutral, 2, 240, press, release));
  EXPECT_FALSE(decodeRawBitFallbackCycle(fallback, 0x1234, ambiguous, 2, 300, press, release));
  EXPECT_FALSE(decodeRawBitFallbackCycle(fallback, 0x1234, neutral, 2, 360, press, release));
  EXPECT_FALSE(decodeRawBitFallbackCycle(fallback, 0x5678, pressed, 2, 420, press, release));  // New peer/report baseline.
  EXPECT_FALSE(decodeRawBitFallbackCycle(fallback, 0x5678, neutral, 2, 2000, press, release));  // Stale baseline reset.
  const uint8_t oversized[17] = {};
  EXPECT_FALSE(decodeRawBitFallbackCycle(fallback, 0x5678, oversized, sizeof(oversized), 2050, press, release));

  fallback.reset();
  EXPECT_FALSE(decodeRawBitFallbackCycle(fallback, 0x1234, neutral, 2, 2100, press, release));
  EXPECT_FALSE(decodeRawBitFallbackCycle(fallback, 0x1234, pressed, 2, 2140, press, release));
  fallback.reset();
  EXPECT_FALSE(decodeRawBitFallbackCycle(fallback, 0x1234, neutral, 2, 2180, press, release));
  EXPECT_FALSE(decodeRawBitFallbackCycle(fallback, 0x1234, pressed, 2, 2220, press, release));
  ASSERT_TRUE(decodeRawBitFallbackCycle(fallback, 0x1234, neutral, 2, 2260, press, release));
}

TEST(BleInputMapping, LearnedInputLabelPersistsWithoutChangingIdentityOrAction) {
  using namespace bleinput::mapping;
  const CapturedIdentity signature[] = {{6, 0x12}, {7, 0x34}, {8, 0x56}, {9, 0x78},
                                        {10, 0x2A}, {11, 0x03}, {12, 0x01}};
  KeyMapEntry entries[kMapCapacity]{};
  ASSERT_EQ(assign(entries, signature, 7, kButtonAction, 3), AssignResult::Added);
  ASSERT_TRUE(setInputLabel(entries[0], "IINE Side Key"));
  EXPECT_TRUE(sameCaptured(entries[0], signature, 7));
  EXPECT_EQ(assign(entries, signature, 7, kButtonAction, 4), AssignResult::Replaced);
  EXPECT_STREQ(entries[0].inputLabel, "IINE Side Key");
  EXPECT_EQ(resolve(entries[0]).target, 4);
  const PersistedEntry saved = toPersisted(entries[0]);
  KeyMapEntry restored{};
  ASSERT_TRUE(fromPersisted(saved, restored));
  EXPECT_STREQ(restored.inputLabel, "IINE Side Key");
  EXPECT_TRUE(sameCaptured(restored, signature, 7));
  EXPECT_EQ(sizeof(KeyMapEntry), 38u);
  EXPECT_FALSE(setInputLabel(restored, "0123456789abcdef"));
}

TEST(BleInputMapping, EveryConfiguredQuickActionHasABoundedBleTarget) {
  EXPECT_EQ(kReaderQuickActionCount, QuickActions::ACTION_COUNT);
  for (uint8_t action = 0; action < kReaderQuickActionCount; ++action) {
    const uint8_t target = readerShortcutTarget(action);
    EXPECT_NE(target, 0xFF);
    EXPECT_TRUE(validTarget(kShortcutAction, target));
  }
  EXPECT_TRUE(validTarget(kShortcutAction, readerShortcutTarget(kReaderExtraActionNextChapter)));
  EXPECT_TRUE(validTarget(kShortcutAction, readerShortcutTarget(kReaderExtraActionPreviousChapter)));
  EXPECT_TRUE(validTarget(kShortcutAction, readerShortcutTarget(kReaderExtraActionFootnotes)));
}

TEST(BleInputMapping, ReaderActionsHaveBoundedNonOverlappingShortcutTargets) {
  EXPECT_EQ(kReaderShortcutBase, kGlobalShortcutCount);
  EXPECT_TRUE(isReaderActionShortcut(readerShortcutTarget(0)));
  EXPECT_TRUE(isReaderActionShortcut(readerShortcutTarget(kReaderExtraActionFootnotes)));
  EXPECT_FALSE(isReaderActionShortcut(kShortcutRecent));
  EXPECT_EQ(readerShortcutTarget(kReaderShortcutCount), 0xFF);
  EXPECT_LT(readerShortcutTarget(kReaderExtraActionFootnotes), kShortcutCount);
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

TEST(BleInputMapping, StaleProfileSnapshotCannotDropSavedIdentities) {
  KeyMapEntry saved[kMapCapacity]{};
  KeyMapEntry candidate[kMapCapacity]{};
  const CapturedIdentity first[] = {{1, 0x4F}};
  const CapturedIdentity second[] = {{1, 0x50}};
  ASSERT_EQ(assign(saved, first, 1, kButtonAction, 1), AssignResult::Added);
  ASSERT_EQ(assign(saved, second, 1, kButtonAction, 2), AssignResult::Added);
  for (uint8_t i = 0; i < kMapCapacity; ++i) candidate[i] = saved[i];
  EXPECT_TRUE(preservesExistingMappings(saved, candidate));

  EXPECT_EQ(remove(candidate, second, 1), AssignResult::Removed);
  EXPECT_FALSE(preservesExistingMappings(saved, candidate));

  // Updating an existing action does not change the learned HID identity.
  for (uint8_t i = 0; i < kMapCapacity; ++i) candidate[i] = saved[i];
  ASSERT_EQ(assign(candidate, first, 1, kButtonAction, 3), AssignResult::Replaced);
  EXPECT_TRUE(preservesExistingMappings(saved, candidate));
}

TEST(BleInputMapping, EmptyFortyEightByteProfileSnapshotCannotReplacePopulatedMap) {
  // This is the compact JSON emitted for schema 2 with an empty mapping list.
  static constexpr char kEmptyProfileJson[] =
      "{\"schema\":2,\"controllerPreset\":0,\"bleKeyMap\":[]}";
  EXPECT_EQ(sizeof(kEmptyProfileJson) - 1, 48U);

  KeyMapEntry persisted[kMapCapacity]{};
  KeyMapEntry emptyCandidate[kMapCapacity]{};
  const CapturedIdentity identities[] = {
      {1, 0x4F}, {1, 0x50}, {1, 0x51}, {1, 0x52}, {1, 0x53},
  };
  ASSERT_LE(sizeof(identities) / sizeof(identities[0]), kMapCapacity);
  for (size_t i = 0; i < sizeof(identities) / sizeof(identities[0]); ++i) {
    ASSERT_EQ(assign(persisted, &identities[i], 1, kButtonAction, 3), AssignResult::Added);
  }

  EXPECT_FALSE(preservesExistingMappings(persisted, emptyCandidate));
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



using bleinput::iine_touch::Gesture;
using bleinput::iine_touch::GestureRecognizer;

std::array<uint8_t, 20> iineReport(const uint8_t status, const uint16_t x, const uint16_t y) {
  std::array<uint8_t, 20> report{};
  report[0] = status;
  report[1] = static_cast<uint8_t>(x & 0xFF);
  report[2] = static_cast<uint8_t>(x >> 8);
  report[3] = static_cast<uint8_t>(y & 0xFF);
  report[4] = static_cast<uint8_t>(y >> 8);
  return report;
}

std::array<uint8_t, 5> iineZeroCompanion() { return {}; }

TEST(IineTouchGesture, CapturedSwipeLeftEmitsOnlyOnceAtReleaseAndIgnoresCompanion) {
  GestureRecognizer recognizer;
  auto first = iineReport(0x13, 2500, 2200);  // 13 C4 09 98 08
  auto second = iineReport(0x13, 794, 2179);  // 13 1A 03 83 08
  auto third = iineReport(0x13, 0, 2168);     // 13 00 00 78 08
  auto release = iineReport(0x12, 0, 2168);   // 12 00 00 78 08
  auto companion = iineZeroCompanion();
  EXPECT_EQ(recognizer.consume(first.data(), first.size(), 100), Gesture::None);
  EXPECT_EQ(recognizer.consume(companion.data(), companion.size(), 110), Gesture::None);
  EXPECT_EQ(recognizer.consume(second.data(), second.size(), 130), Gesture::None);
  EXPECT_EQ(recognizer.consume(third.data(), third.size(), 150), Gesture::None);
  EXPECT_EQ(recognizer.consume(release.data(), release.size(), 170), Gesture::SwipeLeft);
  EXPECT_EQ(recognizer.consume(release.data(), release.size(), 180), Gesture::None);
}

TEST(IineTouchGesture, CapturedSwipeRightUpAndDownUseRelativeDisplacement) {
  GestureRecognizer right;
  auto r1 = iineReport(0x13, 1500, 2200);
  auto r2 = iineReport(0x13, 3206, 2200);
  auto r3 = iineReport(0x13, 4060, 2200);
  auto rr = iineReport(0x12, 4060, 2200);
  right.consume(r1.data(), r1.size(), 0);
  right.consume(r2.data(), r2.size(), 25);
  right.consume(r3.data(), r3.size(), 50);
  EXPECT_EQ(right.consume(rr.data(), rr.size(), 75), Gesture::SwipeRight);

  GestureRecognizer up;
  auto u1 = iineReport(0x13, 2000, 2500);
  auto u2 = iineReport(0x13, 2000, 1700);
  auto u3 = iineReport(0x13, 2000, 900);
  auto ur = iineReport(0x12, 2000, 900);
  up.consume(u1.data(), u1.size(), 100);
  up.consume(u2.data(), u2.size(), 120);
  up.consume(u3.data(), u3.size(), 140);
  EXPECT_EQ(up.consume(ur.data(), ur.size(), 160), Gesture::SwipeUp);

  GestureRecognizer down;
  auto d1 = iineReport(0x13, 2000, 2000);
  auto d2 = iineReport(0x13, 2000, 2800);
  auto d3 = iineReport(0x13, 2000, 3600);
  auto dr = iineReport(0x12, 2000, 3600);
  down.consume(d1.data(), d1.size(), 200);
  down.consume(d2.data(), d2.size(), 220);
  down.consume(d3.data(), d3.size(), 240);
  EXPECT_EQ(down.consume(dr.data(), dr.size(), 260), Gesture::SwipeDown);
}

TEST(IineTouchGesture, PhysicalDirectionLabelsInvertSyntheticSwipeWithoutChangingEventIdentity) {
  using bleinput::iine_touch::Gesture;
  using bleinput::iine_touch::PhysicalDirection;
  using bleinput::iine_touch::physicalDirectionForGesture;
  EXPECT_EQ(physicalDirectionForGesture(Gesture::SwipeLeft), PhysicalDirection::Right);
  EXPECT_EQ(physicalDirectionForGesture(Gesture::SwipeRight), PhysicalDirection::Left);
  EXPECT_EQ(physicalDirectionForGesture(Gesture::SwipeUp), PhysicalDirection::Down);
  EXPECT_EQ(physicalDirectionForGesture(Gesture::SwipeDown), PhysicalDirection::Up);
  EXPECT_STREQ(bleinput::iine_touch::physicalDirectionName(physicalDirectionForGesture(Gesture::SwipeLeft)), "Right");
  EXPECT_STREQ(bleinput::iine_touch::physicalDirectionName(physicalDirectionForGesture(Gesture::SwipeRight)), "Left");
  EXPECT_STREQ(bleinput::iine_touch::physicalDirectionName(physicalDirectionForGesture(Gesture::SwipeUp)), "Down");
  EXPECT_STREQ(bleinput::iine_touch::physicalDirectionName(physicalDirectionForGesture(Gesture::SwipeDown)), "Up");
  EXPECT_EQ(static_cast<uint8_t>(Gesture::SwipeLeft), bleinput::mapping::kIineGestureSwipeLeft);
  EXPECT_EQ(static_cast<uint8_t>(Gesture::SwipeRight), bleinput::mapping::kIineGestureSwipeRight);
  EXPECT_EQ(static_cast<uint8_t>(Gesture::SwipeUp), bleinput::mapping::kIineGestureSwipeUp);
  EXPECT_EQ(static_cast<uint8_t>(Gesture::SwipeDown), bleinput::mapping::kIineGestureSwipeDown);
}

TEST(IineTouchButtons, FiveDescriptorButtonUsagesProduceIndependentEdgesAndIgnorePointerMotion) {
  using bleinput::iine_touch::ButtonDecoder;
  ButtonDecoder decoder;
  const uint8_t button1[] = {0x01, 0, 0, 0, 0, 0};
  auto edges = decoder.consume(button1, sizeof(button1));
  ASSERT_TRUE(edges.valid);
  EXPECT_EQ(edges.pressed, 0x01);
  EXPECT_EQ(edges.released, 0);

  // X/Y and Wheel can change while the five-bit button state stays held.
  const uint8_t motionOnly[] = {0x01, 0x23, 0xD0, 0x07, 0xD0, 0x01};
  edges = decoder.consume(motionOnly, sizeof(motionOnly));
  EXPECT_TRUE(edges.valid);
  EXPECT_EQ(edges.pressed, 0);
  EXPECT_EQ(edges.released, 0);

  const uint8_t button1And5[] = {0x11, 0, 0, 0, 0, 0};
  edges = decoder.consume(button1And5, sizeof(button1And5));
  EXPECT_EQ(edges.pressed, 0x10);
  EXPECT_EQ(edges.released, 0);
  const uint8_t button5[] = {0x10, 0, 0, 0, 0, 0};
  edges = decoder.consume(button5, sizeof(button5));
  EXPECT_EQ(edges.pressed, 0);
  EXPECT_EQ(edges.released, 0x01);
  const uint8_t allUp[] = {0, 0, 0, 0, 0, 0};
  edges = decoder.consume(allUp, sizeof(allUp));
  EXPECT_EQ(edges.released, 0x10);

  decoder.consume(button1, sizeof(button1));
  edges = decoder.consume(button1, sizeof(button1) - 1);
  EXPECT_FALSE(edges.valid);
  EXPECT_EQ(edges.released, 0x01);
  EXPECT_EQ(decoder.heldMask(), 0);
  decoder.consume(button1, sizeof(button1));
  const uint8_t invalidPadding[] = {0x81, 0, 0, 0, 0, 0};
  edges = decoder.consume(invalidPadding, sizeof(invalidPadding));
  EXPECT_FALSE(edges.valid);
  EXPECT_EQ(edges.released, 0x01);
}

TEST(IineTouchButtons, DescriptorGatesButtonAndTouchReportsByTheirOwnReportIds) {
  // Captured physical X4 Report Map prefix: ID 4 is five one-bit Button
  // usages followed by constant padding and pointer fields; ID 6 is the
  // Digitizer contact report with declared X/Y coordinates.
  const uint8_t mapPrefix[] = {
      0x05,0x0D,0x09,0x02,0xA1,0x01,0x85,0x04,0x05,0x01,0x09,0x01,0xA1,0x00,0x05,0x09,
      0x19,0x01,0x29,0x05,0x15,0x00,0x25,0x01,0x75,0x01,0x95,0x05,0x81,0x02,0x95,0x03,
      0x81,0x01,0x05,0x01,0x09,0x30,0x09,0x31,0x16,0x01,0x80,0x26,0xFF,0x7F,0x75,0x10,
      0x95,0x02,0x81,0x06,0x09,0x38,0x15,0x81,0x25,0x7F,0x75,0x08,0x95,0x01,0x81,0x06,
      0xC0,0xC0,0x05,0x0D,0x09,0x04,0xA1,0x01,0x85,0x06,0x05,0x0D,0x09,0x22,0xA1,0x02,
      0x09,0x42,0x15,0x00,0x25,0x01,0x75,0x01,0x95,0x01,0x81,0x02,0x09,0x32,0x81,0x02,
      0x75,0x02,0x81,0x03,0x75,0x04,0x25,0x0F,0x09,0x51,0x81,0x02,0x05,0x01,0x26,0x00,
      0x10,0x75,0x10,0x55,0x00,0x65,0x00,0x09,0x30,0x35,0x00,0x46,0x00,0x00,0x81,0x02};
  bleinput::hid::ReportMapHints hints;
  ASSERT_TRUE(bleinput::hid::parseReportMap(mapPrefix, sizeof(mapPrefix), hints));
  EXPECT_EQ(bleinput::hid::reportIdIineButtonMask(4, hints), 0x1F);
  EXPECT_TRUE(bleinput::hid::reportMapAllowsIineFiveButtonReport(hints, 4, true, 1));
  EXPECT_FALSE(bleinput::hid::reportMapAllowsIineFiveButtonReport(hints, 6, true, 1));
  // The captured 128-byte prefix ends after the X field; it does not establish
  // that Y is declared in the same report, so the decoder must fail closed.
  EXPECT_FALSE(bleinput::hid::reportMapAllowsDeviceScopedDigitizerCoordinates(hints, 6, true, 1));
  EXPECT_FALSE(bleinput::hid::reportMapAllowsDeviceScopedDigitizerCoordinates(hints, 4, true, 1));

  uint8_t kind = 0, value = 0;
  for (uint8_t button = 1; button <= 5; ++button) {
    EXPECT_TRUE(bleinput::mapping::encodeHidUsageForLearning(0x09, button, kind, value));
    EXPECT_EQ(kind, bleinput::mapping::kIineMouseButtonIdentityKind);
    EXPECT_EQ(value, button);
    EXPECT_TRUE(bleinput::mapping::validIdentity(kind, value));
  }
  EXPECT_TRUE(bleinput::mapping::encodeHidUsageForLearning(0x09, 6, kind, value));
  EXPECT_EQ(kind, bleinput::mapping::kIineMouseButtonIdentityKind);
  EXPECT_EQ(value, 6);
}

TEST(IineTouchButtons, ReportIdPrefixIsStrippedOnlyWhenItMatchesDescriptor) {
  using namespace bleinput::iine_touch;
  ButtonDecoder decoder;
  const uint8_t pressWithId[] = {kButtonReportId, 0x04, 0, 0, 0, 0, 0};
  const auto press = decoder.consume(pressWithId, sizeof(pressWithId));
  ASSERT_TRUE(press.valid);
  EXPECT_EQ(press.pressed, 0x04);
  const uint8_t wrongId[] = {0x06, 0, 0, 0, 0, 0, 0};
  EXPECT_FALSE(decoder.consume(wrongId, sizeof(wrongId)).valid);
  EXPECT_EQ(decoder.heldMask(), 0);
}

TEST(IineTouchGesture, DeviceScopedFallbackRequiresIineCompositeReportMap) {
  const uint8_t composite[] = {
      0x85,0x04, 0x05,0x09, 0x19,0x01, 0x29,0x05, 0x75,0x01, 0x95,0x05, 0x81,0x02,
      0x85,0x06, 0x05,0x0D, 0x09,0x22, 0x81,0x02,
      0x05,0x01, 0x09,0x30, 0x09,0x31, 0x75,0x10, 0x95,0x02, 0x81,0x02};
  bleinput::hid::ReportMapHints hints;
  ASSERT_TRUE(bleinput::hid::parseReportMap(composite, sizeof(composite), hints));
  EXPECT_TRUE(bleinput::hid::reportMapMatchesIineTouchComposite(hints));
  EXPECT_TRUE(bleinput::hid::reportMapAllowsDigitizerCoordinates(hints, 6, true, 1));

  const uint8_t touchOnly[] = {
      0x85,0x06, 0x05,0x0D, 0x09,0x22, 0x81,0x02,
      0x05,0x01, 0x09,0x30, 0x09,0x31, 0x75,0x10, 0x95,0x02, 0x81,0x02};
  ASSERT_TRUE(bleinput::hid::parseReportMap(touchOnly, sizeof(touchOnly), hints));
  EXPECT_FALSE(bleinput::hid::reportMapMatchesIineTouchComposite(hints));
}

TEST(IineTouchGesture, StationaryCapturedCoordinatesNormalizeToTapWithoutZoneIdentity) {
  const uint16_t points[][2] = {{2000, 2000}, {3000, 2000}, {3000, 2500}};
  for (const auto& point : points) {
    GestureRecognizer recognizer;
    auto press = iineReport(0x13, point[0], point[1]);
    auto release = iineReport(0x12, point[0], point[1]);
    EXPECT_EQ(recognizer.consume(press.data(), press.size(), 20), Gesture::None);
    EXPECT_EQ(recognizer.consume(release.data(), release.size(), 40), Gesture::Tap);
  }
}

TEST(IineTouchGesture, InvalidTruncatedOutOfOrderAndTimedOutSequencesFailClosed) {
  GestureRecognizer recognizer;
  auto release = iineReport(0x12, 0, 0);
  EXPECT_EQ(recognizer.consume(release.data(), release.size(), 1), Gesture::None);
  auto press = iineReport(0x13, 2500, 2200);
  recognizer.consume(press.data(), press.size(), 100);
  EXPECT_EQ(recognizer.consume(press.data(), 5, 120), Gesture::None);
  EXPECT_FALSE(recognizer.active());

  recognizer.consume(press.data(), press.size(), 200);
  auto far = iineReport(0x13, 0, 2200);
  recognizer.consume(far.data(), far.size(), 600);  // gap beyond the bounded window
  EXPECT_FALSE(recognizer.active());
  EXPECT_EQ(recognizer.consume(release.data(), release.size(), 620), Gesture::None);

  recognizer.consume(press.data(), press.size(), 1000);
  auto moved = iineReport(0x13, 0, 2200);
  EXPECT_EQ(recognizer.consume(moved.data(), moved.size(), 1400), Gesture::None);
  EXPECT_FALSE(recognizer.active());
  EXPECT_EQ(recognizer.consume(release.data(), release.size(), 1420), Gesture::None);
}

TEST(IineTouchGesture, MalformedCoordinatesAndNonzeroReservedBytesResetState) {
  GestureRecognizer recognizer;
  auto press = iineReport(0x13, 2000, 2000);
  recognizer.consume(press.data(), press.size(), 100);
  auto malformed = iineReport(0x13, 5000, 2000);
  EXPECT_EQ(recognizer.consume(malformed.data(), malformed.size(), 120), Gesture::None);
  EXPECT_FALSE(recognizer.active());

  recognizer.consume(press.data(), press.size(), 200);
  malformed = iineReport(0x13, 2800, 2000);
  malformed[19] = 1;
  EXPECT_EQ(recognizer.consume(malformed.data(), malformed.size(), 220), Gesture::None);
  EXPECT_FALSE(recognizer.active());
}

TEST(IineTouchGesture, QueuePairIsAtomicAndDropsOnOverflow) {
  struct Event { uint8_t phase; uint8_t identity; };
  Event ring[16]{};
  uint8_t head = 14;
  uint8_t tail = 0;  // only one free slot remains
  const Event press{0, 2};
  const Event release{1, 2};
  EXPECT_FALSE(bleinput::iine_touch::enqueueGesturePair(ring, head, tail, press, release));
  EXPECT_EQ(head, 14);

  tail = 1;  // exactly two free slots, including wraparound
  ASSERT_TRUE(bleinput::iine_touch::enqueueGesturePair(ring, head, tail, press, release));
  EXPECT_EQ(head, 0);
  EXPECT_EQ(ring[14].identity, 2);
  EXPECT_EQ(ring[15].phase, 1);
}

TEST(IineTouchGesture, DescriptorMustDeclareDedicatedNumberedDigitizerReport) {
  const uint8_t digitizerMap[] = {0x05, 0x0D, 0x85, 0x06, 0x81, 0x00};
  bleinput::hid::ReportMapHints hints;
  ASSERT_TRUE(bleinput::hid::parseReportMap(digitizerMap, sizeof(digitizerMap), hints));
  EXPECT_TRUE(bleinput::hid::reportMapAllowsDigitizerInput(hints, 6, true, 1));
  EXPECT_FALSE(bleinput::hid::reportMapAllowsDigitizerInput(hints, 6, true, 2));
  EXPECT_FALSE(bleinput::hid::reportMapAllowsDigitizerInput(hints, 6, false, 1));
  EXPECT_FALSE(bleinput::iine_touch::isSupportedDeviceName("Other BLE HID"));
  EXPECT_TRUE(bleinput::iine_touch::isSupportedDeviceName("IG05 Gamepad(BLE)"));
  EXPECT_TRUE(bleinput::iine_touch::isSupportedDeviceName("IINE_control"));
  EXPECT_FALSE(bleinput::iine_touch::isSupportedDeviceName("iine_control"));

  // Physical IG05 descriptor: Digitizer fields and Generic Desktop X/Y are
  // both carried by Report ID 6. Coordinate usages are safe only on this
  // device-scoped opt-in path.
  const uint8_t ig05Map[] = {0x05,0x0D,0x85,0x06,0x81,0x00,
                             0x05,0x01,0x09,0x30,0x81,0x02,
                             0x05,0x01,0x09,0x31,0x81,0x02};
  ASSERT_TRUE(bleinput::hid::parseReportMap(ig05Map, sizeof(ig05Map), hints));
  EXPECT_TRUE(bleinput::hid::reportMapAllowsDigitizerCoordinates(hints, 6, true, 1));
  EXPECT_FALSE(bleinput::hid::reportMapAllowsDigitizerCoordinates(hints, 6, true, 2));
  const uint8_t unrelatedDesktop[] = {0x05,0x0D,0x85,0x06,0x81,0x00,
                                      0x05,0x01,0x09,0x38,0x81,0x02};
  ASSERT_TRUE(bleinput::hid::parseReportMap(unrelatedDesktop, sizeof(unrelatedDesktop), hints));
  EXPECT_FALSE(bleinput::hid::reportMapAllowsDigitizerCoordinates(hints, 6, true, 1));

  const uint8_t mixedMap[] = {0x05, 0x0D, 0x85, 0x06, 0x81, 0x00,
                              0x05, 0x07, 0x81, 0x00};
  ASSERT_TRUE(bleinput::hid::parseReportMap(mixedMap, sizeof(mixedMap), hints));
  EXPECT_FALSE(bleinput::hid::reportMapAllowsDigitizerInput(hints, 6, true, 1));
}

TEST(IineTouchGesture, IineControlNameStillRequiresAValidatedDigitizerReport) {
  using namespace bleinput::iine_touch;
  const uint8_t digitizerCoordinates[] = {
      0x05,0x0D,0x85,0x06,0x81,0x00,
      0x05,0x01,0x09,0x30,0x81,0x02,
      0x09,0x31,0x81,0x02};
  bleinput::hid::ReportMapHints hints;
  ASSERT_TRUE(bleinput::hid::parseReportMap(digitizerCoordinates, sizeof(digitizerCoordinates), hints));
  EXPECT_TRUE(isSupportedDeviceName("IINE_control"));
  EXPECT_TRUE(bleinput::hid::reportMapAllowsDigitizerCoordinates(hints, kReportId, true, kInputReportType));
  GestureRecognizer recognizer;
  auto start = iineReport(0x13, 1000, 1000);
  auto release = iineReport(0x12, 2000, 1000);
  EXPECT_EQ(recognizer.consume(start.data(), start.size(), 10), Gesture::None);
  EXPECT_EQ(recognizer.consume(release.data(), release.size(), 50), Gesture::SwipeRight);

  const uint8_t unrelated[] = {0x05,0x01,0x85,0x06,0x09,0x30,0x81,0x02};
  ASSERT_TRUE(bleinput::hid::parseReportMap(unrelated, sizeof(unrelated), hints));
  EXPECT_FALSE(bleinput::hid::reportMapAllowsDigitizerCoordinates(hints, kReportId, true, kInputReportType));
}

TEST(IineTouchGesture, PhysicalTwentyByteCapturesProduceOneGestureAndIgnoreCompanion) {
  using namespace bleinput::iine_touch;
  const std::array<uint8_t,20> leftStart = {0x13,0xC4,0x09,0x98,0x08,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0};
  const std::array<uint8_t,20> leftMiddle = {0x13,0x1A,0x03,0x83,0x08,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0};
  const std::array<uint8_t,20> leftEnd = {0x13,0x00,0x00,0x78,0x08,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0};
  const std::array<uint8_t,20> leftRelease = {0x12,0x00,0x00,0x78,0x08,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0};
  GestureRecognizer recognizer;
  EXPECT_EQ(recognizer.consume(leftStart.data(), 20, 10), Gesture::None);
  const uint8_t companion[5] = {0,0,0,0,0};
  EXPECT_EQ(recognizer.consume(companion, sizeof(companion), 20), Gesture::None);
  EXPECT_EQ(recognizer.consume(leftMiddle.data(), 20, 30), Gesture::None);
  EXPECT_EQ(recognizer.consume(leftEnd.data(), 20, 40), Gesture::None);
  EXPECT_EQ(recognizer.consume(leftRelease.data(), 20, 50), Gesture::SwipeLeft);
  EXPECT_EQ(recognizer.consume(leftRelease.data(), 20, 60), Gesture::None);

  // Exact physical multi-contact sample; the secondary contact must not
  // cancel the primary swipe or create an additional event.
  const std::array<uint8_t,20> multitouch = {0x13,0x86,0x0C,0x98,0x08,0x23,0xD0,0x07,0xD0,0x07,0,0,0,0,0,0,0,0,0,0};
  EXPECT_TRUE(bleinput::iine_touch::hasSupportedContactSlots(multitouch.data(), 20));
  auto bad = multitouch;
  bad[5] = 0x44;
  EXPECT_FALSE(bleinput::iine_touch::hasSupportedContactSlots(bad.data(), 20));
}

TEST(IineTouchGesture, CapturedReportMapAssignsXyToDigitizerIdSix) {
  // Exact first 128 bytes emitted by the physical X4 Report Map diagnostic;
  // capture records that the full descriptor was 395 bytes (remaining tail
  // was not printed by that firmware).
  const uint8_t mapPrefix[] = {
      0x05,0x0D,0x09,0x02,0xA1,0x01,0x85,0x04,0x05,0x01,0x09,0x01,0xA1,0x00,0x05,0x09,
      0x19,0x01,0x29,0x05,0x15,0x00,0x25,0x01,0x75,0x01,0x95,0x05,0x81,0x02,0x95,0x03,
      0x81,0x01,0x05,0x01,0x09,0x30,0x09,0x31,0x16,0x01,0x80,0x26,0xFF,0x7F,0x75,0x10,
      0x95,0x02,0x81,0x06,0x09,0x38,0x15,0x81,0x25,0x7F,0x75,0x08,0x95,0x01,0x81,0x06,
      0xC0,0xC0,0x05,0x0D,0x09,0x04,0xA1,0x01,0x85,0x06,0x05,0x0D,0x09,0x22,0xA1,0x02,
      0x09,0x42,0x15,0x00,0x25,0x01,0x75,0x01,0x95,0x01,0x81,0x02,0x09,0x32,0x81,0x02,
      0x75,0x02,0x81,0x03,0x75,0x04,0x25,0x0F,0x09,0x51,0x81,0x02,0x05,0x01,0x26,0x00,
      0x10,0x75,0x10,0x55,0x00,0x65,0x00,0x09,0x30,0x35,0x00,0x46,0x00,0x00,0x81,0x02};
  bleinput::hid::ReportMapHints hints;
  ASSERT_TRUE(bleinput::hid::parseReportMap(mapPrefix, sizeof(mapPrefix), hints));
  EXPECT_FALSE(bleinput::hid::reportMapAllowsDigitizerCoordinates(hints, 6, true, 1));
  EXPECT_FALSE(bleinput::hid::reportMapAllowsDigitizerCoordinates(hints, 4, true, 1));
}

TEST(IineTouchGesture, PhysicalIineControlTapProducesOneLearnedIdentityAndAction) {
  using bleinput::iine_touch::Gesture;
  using bleinput::iine_touch::GestureRecognizer;

  // Byte-for-byte 20-byte reports from the 2026-11-12 COM3 capture.
  const std::array<uint8_t, 20> press = {0x13,0x1A,0x06,0x68,0x08,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0};
  const std::array<uint8_t, 20> release = {0x12,0x1A,0x06,0x68,0x08,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0};
  const std::array<uint8_t, 5> companion = {0,0,0,0,0};
  ASSERT_TRUE(bleinput::iine_touch::isValidatedContactReport(press.data(), press.size()));
  ASSERT_TRUE(bleinput::iine_touch::isValidatedContactReport(release.data(), release.size()));

  GestureRecognizer recognizer;
  EXPECT_EQ(recognizer.consume(press.data(), press.size(), 100), Gesture::None);
  EXPECT_EQ(recognizer.consume(companion.data(), companion.size(), 101), Gesture::None);
  const Gesture learned = recognizer.consume(release.data(), release.size(), 280);
  EXPECT_EQ(learned, Gesture::Tap);
  EXPECT_EQ(recognizer.consume(release.data(), release.size(), 281), Gesture::None);

  uint8_t identityKind = 0xFF;
  uint8_t identityValue = 0;
  ASSERT_TRUE(encodeHidUsageForLearning(0x0D, static_cast<uint8_t>(learned), identityKind, identityValue));
  EXPECT_EQ(identityKind, kIineGestureIdentityKind);
  EXPECT_EQ(identityValue, kIineGestureTap);

  KeyMapEntry entries[kMapCapacity]{};
  const CapturedIdentity captured[] = {{identityKind, identityValue}};
  ASSERT_EQ(assign(entries, captured, 1, kButtonAction, 3), AssignResult::Added);
  EXPECT_TRUE(matchesAt(entries[0], identityKind, identityValue, 0));
  EXPECT_EQ(resolve(entries[0]).target, 3);
}

TEST(IineTouchGesture, SameMetadataAndBodyAreRejectedWithoutDescriptorDigitizerCoordinates) {
  using namespace bleinput::iine_touch;
  // Reuse a byte-for-byte IINE contact while presenting an unrelated Report
  // Map that happens to reuse ID 6. This is the dangerous case for the raw
  // length-exception path: metadata and contact bytes alone look plausible.
  const std::array<uint8_t, 20> contact = {
      0x13,0x1A,0x06,0x68,0x08,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0};
  ASSERT_TRUE(isValidatedContactReport(contact.data(), contact.size()));
  const bool hasReportReference = true;
  const bool sameMetadata = kReportCharacteristic == 0x2A4D && kReportId == 6 &&
      kInputReportType == 1 && hasReportReference;
  ASSERT_TRUE(sameMetadata);

  const uint8_t unrelatedGamepad[] = {
      0x05,0x01,0x09,0x05,0xA1,0x01,0x85,0x06,
      0x05,0x09,0x19,0x01,0x29,0x08,0x15,0x00,0x25,0x01,
      0x75,0x01,0x95,0x08,0x81,0x02,0xC0};
  bleinput::hid::ReportMapHints hints;
  ASSERT_TRUE(bleinput::hid::parseReportMap(unrelatedGamepad, sizeof(unrelatedGamepad), hints));
  const bool descriptorConfirmsTouch = bleinput::hid::reportMapAllowsDigitizerCoordinates(
      hints, kReportId, hasReportReference, kInputReportType);
  EXPECT_FALSE(descriptorConfirmsTouch);
  // The legacy metadata+payload-only predicate accepts this false positive;
  // the required descriptor-scoped predicate must reject it.
  EXPECT_TRUE(sameMetadata && isValidatedContactReport(contact.data(), contact.size()));
  EXPECT_FALSE(sameMetadata && descriptorConfirmsTouch &&
               isValidatedContactReport(contact.data(), contact.size()));

  bleinput::hid::learning::Layout unrelatedLayout{};
  ASSERT_TRUE(bleinput::hid::learning::parseLayout(
      unrelatedGamepad, sizeof(unrelatedGamepad), unrelatedLayout));
  size_t declaredBytes = 0;
  ASSERT_TRUE(bleinput::hid::learning::expectedInputBytes(
      unrelatedLayout, kReportId, declaredBytes));
  EXPECT_EQ(declaredBytes, 1U);
  const auto strictPayload = bleinput::hid::payloadForDeclaredReport(
      contact.data(), contact.size(), kReportId, hasReportReference, true, declaredBytes);
  EXPECT_FALSE(strictPayload.data);
}

TEST(BleMappingProfiles, BoundedCatalogSupportsCreateRenameDuplicateAndProtectedDefault) {
  using namespace bleinput::profiles;
  ProfileRecord records[kMaxProfiles]{};
  records[0].id = kDefaultProfileId;
  std::strncpy(records[0].name, "Default", sizeof(records[0].name) - 1);
  uint8_t count = 1;
  EXPECT_EQ(add(records, count, 1, "EPUB Reading"), CatalogResult::Added);
  EXPECT_EQ(add(records, count, 2, "EPUB Reading"), CatalogResult::DuplicateName);
  EXPECT_EQ(rename(records, count, 0, "Renamed Default"), CatalogResult::Protected);
  EXPECT_EQ(rename(records, count, 1, "Manga RTL"), CatalogResult::Renamed);
  EXPECT_STREQ(records[1].name, "Manga RTL");
  EXPECT_EQ(remove(records, count, 0, 1), CatalogResult::Protected);
  EXPECT_EQ(remove(records, count, 1, 1), CatalogResult::Protected);
  EXPECT_EQ(remove(records, count, 1, 0), CatalogResult::Removed);
  EXPECT_EQ(count, 1);
  EXPECT_EQ(nextId(records, count, 1), 1);
}

TEST(BleMappingProfiles, NamesAndCatalogCapacityAreBounded) {
  using namespace bleinput::profiles;
  char name[kProfileNameBytes]{};
  EXPECT_TRUE(sanitizeName("  one hand  ", name));
  EXPECT_STREQ(name, "one hand");
  EXPECT_FALSE(sanitizeName("   ", name));
  ProfileRecord records[kMaxProfiles]{};
  records[0].id = kDefaultProfileId;
  std::strncpy(records[0].name, "Default", sizeof(records[0].name) - 1);
  uint8_t count = 1;
  for (uint8_t id = 1; id < kMaxProfiles; ++id) {
    char item[12];
    std::snprintf(item, sizeof(item), "Profile %u", static_cast<unsigned>(id));
    EXPECT_EQ(add(records, count, id, item), CatalogResult::Added);
  }
  EXPECT_EQ(add(records, count, 9, "Full"), CatalogResult::Full);
}

TEST(BleMappingProfiles, VirtualFactoryPresetsDoNotConsumeCustomSlotsOrIds) {
  using namespace bleinput::profiles;
  EXPECT_TRUE(isFactoryProfile(kFactoryYiserId));
  EXPECT_TRUE(isFactoryProfile(kFactoryFree2Id));
  EXPECT_TRUE(isFactoryProfile(kFactoryFree3Id));
  ProfileRecord records[kMaxProfiles]{};
  records[0].id = 0;
  uint8_t count = 1;
  EXPECT_EQ(add(records, count, 1, "Custom"), CatalogResult::Added);
  EXPECT_EQ(nextId(records, count, kFactoryYiserId), 2);
  EXPECT_EQ(count, 2);  // Factory presets are virtual and outside the 8 stored slots.
}

TEST(IineTouchGesture, GestureIdentitiesCanBeLearnedAndPersistedAsTypedDigitizerKeys) {
  KeyMapEntry entries[kMapCapacity]{};
  const CapturedIdentity swipeLeft[] = {{kIineGestureIdentityKind, kIineGestureSwipeLeft}};
  ASSERT_EQ(assign(entries, swipeLeft, 1, kButtonAction, 8), AssignResult::Added);
  EXPECT_TRUE(validIdentity(entries[0].keyKind, entries[0].keyValue));
  EXPECT_TRUE(matchesAt(entries[0], kIineGestureIdentityKind, kIineGestureSwipeLeft, 0));
  const PersistedEntry encoded = toPersisted(entries[0]);
  KeyMapEntry restored{};
  ASSERT_TRUE(fromPersisted(encoded, restored));
  EXPECT_EQ(restored.keyKind, kIineGestureIdentityKind);
  EXPECT_EQ(restored.keyValue, kIineGestureSwipeLeft);
  EXPECT_EQ(resolve(restored).target, 8);
  EXPECT_FALSE(validIdentity(kIineGestureIdentityKind, 0x70));
}

}  // namespace
