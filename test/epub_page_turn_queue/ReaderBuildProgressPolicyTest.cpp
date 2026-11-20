#include <gtest/gtest.h>

#include "util/ReaderBuildProgressPolicy.h"

TEST(ReaderBuildProgressPolicy, ZeroPageForegroundBuildShowsErrorAfterSafeRecoverySuspends) {
  EXPECT_TRUE(reader_build_policy::shouldShowFirstPageHeapError(0, true));
}

TEST(ReaderBuildProgressPolicy, ExistingReadablePagesRemainSuspendableAndResumable) {
  EXPECT_FALSE(reader_build_policy::shouldShowFirstPageHeapError(1, true));
  EXPECT_FALSE(reader_build_policy::shouldShowFirstPageHeapError(8, true));
}

TEST(ReaderBuildProgressPolicy, DoesNotReportBeforeHeapRecoveryHasSuspendedBuild) {
  EXPECT_FALSE(reader_build_policy::shouldShowFirstPageHeapError(0, false));
}

TEST(ReaderBuildProgressPolicy, RepeatedZeroPageSuspensionsTerminateOnTheFirstRetryCycle) {
  constexpr size_t kObservedNoProgressSuspensions = 7;
  size_t attempts = 0;
  bool terminalError = false;
  while (attempts < kObservedNoProgressSuspensions && !terminalError) {
    ++attempts;
    terminalError = reader_build_policy::shouldShowFirstPageHeapError(0, true);
  }
  EXPECT_TRUE(terminalError);
  EXPECT_EQ(attempts, 1U);
}
