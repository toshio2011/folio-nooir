#include "network/WebExitLifecyclePolicy.h"
#include "network/WebSettingsApiPolicy.h"
#include "network/WebSettingsMemoryPolicy.h"

#include <gtest/gtest.h>

#include <limits>
#include <string>
#include <vector>

TEST(WebExitLifecyclePolicy, RetriesUntilServerWifiAndBothNetifsAreGone) {
  const WebExitLifecyclePolicy::Resources complete{true, true, true, true};
  EXPECT_TRUE(WebExitLifecyclePolicy::complete(complete));
  EXPECT_FALSE(WebExitLifecyclePolicy::maySkipCleanup(false, complete));
  EXPECT_TRUE(WebExitLifecyclePolicy::maySkipCleanup(true, complete));

  const WebExitLifecyclePolicy::Resources liveServer{false, true, true, true};
  const WebExitLifecyclePolicy::Resources liveWifi{true, false, true, true};
  const WebExitLifecyclePolicy::Resources liveStationNetif{true, true, false, true};
  const WebExitLifecyclePolicy::Resources liveApNetif{true, true, true, false};
  EXPECT_FALSE(WebExitLifecyclePolicy::maySkipCleanup(true, liveServer));
  EXPECT_FALSE(WebExitLifecyclePolicy::maySkipCleanup(true, liveWifi));
  EXPECT_FALSE(WebExitLifecyclePolicy::maySkipCleanup(true, liveStationNetif));
  EXPECT_FALSE(WebExitLifecyclePolicy::maySkipCleanup(true, liveApNetif));
}

TEST(WebSettingsMemoryPolicy, KeepsExistingNearFloorAndResponseSizedAdmission) {
  using namespace WebSettingsMemoryPolicy;
  EXPECT_EQ(kMinFreeHeap, 20u * 1024u);
  EXPECT_EQ(kMinLargestBlock, 8u * 1024u);
  EXPECT_EQ(requiredFreeHeap(0), kMinFreeHeap);
  EXPECT_FALSE(admitted(kMinFreeHeap, kMinLargestBlock - 1, 0));
  EXPECT_TRUE(admitted(kMinFreeHeap, kMinLargestBlock, 0));

  // A response just beyond the fixed free-heap floor must be rejected until
  // the 3x measured-pressure allowance plus 12 KiB reserve is available.
  constexpr size_t responseBytes = 2731;
  EXPECT_EQ(requiredFreeHeap(responseBytes), 20u * 1024u + 1u);
  EXPECT_FALSE(admitted(20u * 1024u, kMinLargestBlock, responseBytes));
  EXPECT_TRUE(admitted(20u * 1024u + 1u, kMinLargestBlock, responseBytes));
}

TEST(WebSettingsMemoryPolicy, SaturatesRequiredHeapInsteadOfWrapping) {
  using namespace WebSettingsMemoryPolicy;
  EXPECT_EQ(requiredFreeHeap(std::numeric_limits<size_t>::max()), std::numeric_limits<size_t>::max());
  EXPECT_FALSE(admitted(std::numeric_limits<size_t>::max() - 1, kMinLargestBlock,
                        std::numeric_limits<size_t>::max()));
}

TEST(WebSettingsApiPolicy, RequiresOneKnownGroupAndNeverUsesAnImplicitCatalog) {
  using namespace WebSettingsApiPolicy;
  SettingsGroupId group = SettingsGroupId::Unknown;
  EXPECT_FALSE(parseGroup(nullptr, group));
  EXPECT_FALSE(parseGroup("", group));
  EXPECT_FALSE(parseGroup("unknown", group));
  EXPECT_TRUE(parseGroup("home_appearance", group));
  EXPECT_EQ(group, SettingsGroupId::HomeAppearance);
  EXPECT_TRUE(parseGroup("sleep", group));
  EXPECT_EQ(group, SettingsGroupId::Sleep);
}

TEST(WebSettingsApiPolicy, ParsesOnlyBoundedPagesOfOneToSixRows) {
  using namespace WebSettingsApiPolicy;
  PageWindow page{};
  ASSERT_TRUE(makePageWindow(nullptr, nullptr, page));
  EXPECT_EQ(page.offset, 0u);
  EXPECT_EQ(page.limit, 6u);
  EXPECT_EQ(page.end, 6u);

  ASSERT_TRUE(makePageWindow("31", "6", page));
  EXPECT_EQ(page.offset, 31u);
  EXPECT_EQ(page.limit, 6u);
  EXPECT_FALSE(makePageWindow("33", "6", page));
  EXPECT_FALSE(makePageWindow("-1", "6", page));
  EXPECT_FALSE(makePageWindow("2x", "6", page));
  EXPECT_FALSE(makePageWindow("0", "0", page));
  EXPECT_FALSE(makePageWindow("0", "7", page));
  EXPECT_FALSE(makePageWindow("999999999999999999999", "6", page));
}

TEST(WebSettingsApiPolicy, CapsGroupRequestBodyAndChangeCount) {
  using namespace WebSettingsApiPolicy;
  EXPECT_FALSE(validGroupRequestBytes(0));
  EXPECT_TRUE(validGroupRequestBytes(1));
  EXPECT_TRUE(validGroupRequestBytes(2048));
  EXPECT_FALSE(validGroupRequestBytes(2049));
  EXPECT_FALSE(validSettingsPostShape(SettingsGroupId::Unknown, 128, 1));
  EXPECT_FALSE(validSettingsPostShape(SettingsGroupId::Sleep, 2049, 1));
  EXPECT_FALSE(validSettingsPostShape(SettingsGroupId::Sleep, 128, 0));
  EXPECT_TRUE(validSettingsPostShape(SettingsGroupId::Reading, 2048, 31));
  EXPECT_FALSE(validSettingsPostShape(SettingsGroupId::Reading, 128, 33));
}

TEST(WebSettingsApiPolicy, RejectsUnknownCrossGroupAndHiddenComposerKeysBeforeApply) {
  using namespace WebSettingsApiPolicy;
  const std::vector<std::string> valid = {"fontSize", "fontFamily"};
  const auto key = [](const std::string& value) { return value.c_str(); };
  EXPECT_TRUE(allKeysBelongToGroup(valid.begin(), valid.end(), SettingsGroupId::Reading, key));

  const std::vector<std::string> mixed = {"fontSize", "uiTheme"};
  bool applied = false;
  if (allKeysBelongToGroup(mixed.begin(), mixed.end(), SettingsGroupId::Reading, key)) applied = true;
  EXPECT_FALSE(applied);

  const std::vector<std::string> unknown = {"fontSize", "mysteryKey"};
  EXPECT_FALSE(allKeysBelongToGroup(unknown.begin(), unknown.end(), SettingsGroupId::Reading, key));

  const std::vector<std::string> hidden = {"sleepScreen", "sleepOverlayAElement"};
  EXPECT_FALSE(allKeysBelongToGroup(hidden.begin(), hidden.end(), SettingsGroupId::Sleep, key));
  EXPECT_FALSE(allKeysBelongToGroup(valid.begin(), valid.end(), SettingsGroupId::Unknown, key));
}
