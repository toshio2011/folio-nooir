#include "network/WebExitLifecyclePolicy.h"
#include "network/WebSettingsMemoryPolicy.h"

#include <gtest/gtest.h>

#include <limits>

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
