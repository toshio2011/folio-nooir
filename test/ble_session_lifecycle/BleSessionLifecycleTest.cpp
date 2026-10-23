#include <gtest/gtest.h>

#include "src/BleInput.h"
#include "src/util/BleMemoryPolicy.h"

TEST(BleSessionLifecycle, SavedOnPreferenceAllowsNormalReconnectAfterWake) {
  EXPECT_TRUE(bleinput::lifecycleReevaluationAllowed(true, false, false));
}

TEST(BleSessionLifecycle, ExplicitOffAndWebExclusiveStillBlockReconnect) {
  EXPECT_FALSE(bleinput::lifecycleReevaluationAllowed(false, false, false));
  EXPECT_FALSE(bleinput::lifecycleReevaluationAllowed(true, true, false));
}
