#include <gtest/gtest.h>

#include "src/activities/reader/EpubPageTurnQueue.h"

TEST(EpubPageTurnQueue, RepeatedTurnsAreBoundedAndConsumedOnce) {
  EpubPageTurnQueue queue;
  for (int i = 0; i < EpubPageTurnQueue::MAX_PENDING_TURNS; ++i) {
    EXPECT_TRUE(queue.enqueue(true));
  }
  EXPECT_FALSE(queue.enqueue(true));

  bool forward = false;
  for (int i = 0; i < EpubPageTurnQueue::MAX_PENDING_TURNS; ++i) {
    ASSERT_TRUE(queue.take(forward));
    EXPECT_TRUE(forward);
  }
  EXPECT_FALSE(queue.take(forward));
}

TEST(EpubPageTurnQueue, OppositeTurnsCancelPendingInput) {
  EpubPageTurnQueue queue;
  ASSERT_TRUE(queue.enqueue(true));
  ASSERT_TRUE(queue.enqueue(true));
  ASSERT_TRUE(queue.enqueue(false));

  bool forward = false;
  ASSERT_TRUE(queue.take(forward));
  EXPECT_TRUE(forward);
  EXPECT_FALSE(queue.take(forward));
}

TEST(EpubPageTurnQueue, ReversalAfterAnExecutedTurnIsPreserved) {
  EpubPageTurnQueue queue;
  bool forward = false;

  ASSERT_TRUE(queue.enqueue(true));
  ASSERT_TRUE(queue.take(forward));
  EXPECT_TRUE(forward);

  ASSERT_TRUE(queue.enqueue(false));
  ASSERT_TRUE(queue.take(forward));
  EXPECT_FALSE(forward);
}

TEST(EpubPageTurnQueue, ClearCancelsStaleNavigation) {
  EpubPageTurnQueue queue;
  ASSERT_TRUE(queue.enqueue(true));
  queue.clear();

  bool forward = false;
  EXPECT_FALSE(queue.take(forward));
  EXPECT_FALSE(queue.hasPending());
}
