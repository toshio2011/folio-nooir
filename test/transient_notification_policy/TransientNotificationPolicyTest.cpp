#include <gtest/gtest.h>

#include "src/util/TransientNotificationPolicy.h"

TEST(TransientNotificationPolicy, DefersToastsBehindModalActivityUi) {
  EXPECT_FALSE(TransientNotificationPolicy::canPaint(false, true));
  EXPECT_FALSE(TransientNotificationPolicy::canPaint(true, true));
  EXPECT_TRUE(TransientNotificationPolicy::canPaint(false, false));
  EXPECT_FALSE(TransientNotificationPolicy::canPaint(true, false));
}
