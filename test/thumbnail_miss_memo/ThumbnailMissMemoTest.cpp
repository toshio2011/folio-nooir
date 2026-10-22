#include <gtest/gtest.h>

#include "src/util/ThumbnailMissMemo.h"

TEST(ThumbnailMissMemo, RemembersEntriesAcrossPackedWordBoundaries) {
  ThumbnailMissMemo memo;
  memo.reset(130);

  memo.markMissing(0);
  memo.markMissing(63);
  memo.markMissing(64);
  memo.markMissing(129);

  EXPECT_TRUE(memo.contains(0));
  EXPECT_TRUE(memo.contains(63));
  EXPECT_TRUE(memo.contains(64));
  EXPECT_TRUE(memo.contains(129));
  EXPECT_FALSE(memo.contains(1));
  EXPECT_FALSE(memo.contains(128));
}

TEST(ThumbnailMissMemo, IgnoresOutOfRangeEntriesAndClearsForNextSession) {
  ThumbnailMissMemo memo;
  memo.reset(2);
  memo.markMissing(2);
  memo.markMissing(1000);

  EXPECT_FALSE(memo.contains(2));
  EXPECT_FALSE(memo.contains(1000));

  memo.markMissing(1);
  memo.reset(130);
  EXPECT_FALSE(memo.contains(1));
}

TEST(ThumbnailMissMemo, SupportsAnEmptyShelf) {
  ThumbnailMissMemo memo;
  memo.reset(0);
  memo.markMissing(0);

  EXPECT_FALSE(memo.contains(0));
}

TEST(ThumbnailPathMissMemo, PersistsPathMissesAcrossActivityInstances) {
  ThumbnailPathMissMemo processMemo;
  processMemo.markMissing("/.crosspoint/epub_1515163688/thumb_220.bmp");

  // RecentBooksActivity can be destroyed and recreated while this small memo
  // remains alive for the process.
  EXPECT_TRUE(processMemo.contains("/.crosspoint/epub_1515163688/thumb_220.bmp"));
  EXPECT_FALSE(processMemo.contains("/.crosspoint/epub_1515163689/thumb_220.bmp"));
}

TEST(ThumbnailPathMissMemo, ExplicitInvalidationAllowsARefreshedThumbnailToBeProbed) {
  ThumbnailPathMissMemo processMemo;
  processMemo.markMissing("/.crosspoint/book/thumb_220.bmp");
  EXPECT_TRUE(processMemo.contains("/.crosspoint/book/thumb_220.bmp"));

  processMemo.clear();
  EXPECT_FALSE(processMemo.contains("/.crosspoint/book/thumb_220.bmp"));
}
