#include <gtest/gtest.h>

#include "src/util/LibraryBookPreparationPolicy.h"

using library_book_preparation::RetrieveAllSummary;
using library_book_preparation::ThumbnailOutcome;

TEST(LibraryBookPreparation, MetadataCacheWithMissingThumbnailRemainsIncomplete) {
  EXPECT_FALSE(library_book_preparation::cacheIsComplete(true, false, false));
  EXPECT_TRUE(library_book_preparation::cacheIsComplete(true, true, false));
  EXPECT_TRUE(library_book_preparation::cacheIsComplete(true, false, true));
  EXPECT_TRUE(library_book_preparation::cacheIsComplete(true, false, false, true));
  EXPECT_FALSE(library_book_preparation::cacheIsComplete(false, true, false));
}

TEST(LibraryBookPreparation, FailedHoverAttemptIsSuppressedOnlyForTheCurrentActivity) {
  EXPECT_TRUE(library_book_preparation::shouldStartHoverWork(false, true, false));
  EXPECT_FALSE(library_book_preparation::shouldStartHoverWork(true, true, false));
  EXPECT_TRUE(library_book_preparation::shouldStartHoverWork(false, true, false));  // next activity
  EXPECT_FALSE(library_book_preparation::shouldStartHoverWork(false, false, false));
}

TEST(LibraryBookPreparation, CoverQueueSeparatesValidNoCoverAndUnsupportedSources) {
  using library_book_preparation::shouldQueueThumbnail;
  EXPECT_FALSE(shouldQueueThumbnail(true, false, true));
  EXPECT_FALSE(shouldQueueThumbnail(false, true, true));
  EXPECT_FALSE(shouldQueueThumbnail(false, false, false));
  EXPECT_FALSE(shouldQueueThumbnail(false, false, true, true));
  EXPECT_TRUE(shouldQueueThumbnail(false, false, true));
}

TEST(LibraryBookPreparation, ThreeMegabyteSourceLimitAndBulkOutcomesAreReported) {
  EXPECT_EQ(library_book_preparation::MAX_COVER_SOURCE_BYTES, 3u * 1024u * 1024u);

  RetrieveAllSummary summary;
  summary.noteMetadataCache(true);
  summary.noteMetadataCache(false);
  summary.noteMetadataResult(true);
  summary.noteMetadataResult(false);
  summary.noteThumbnailCache(true);
  summary.noteThumbnailCache(false);
  summary.noteThumbnailOutcome(ThumbnailOutcome::Generated);
  summary.noteThumbnailOutcome(ThumbnailOutcome::SkippedNoSource);
  summary.noteThumbnailOutcome(ThumbnailOutcome::SkippedUnsupported);
  summary.noteThumbnailOutcome(ThumbnailOutcome::SkippedOversize);
  summary.noteThumbnailOutcome(ThumbnailOutcome::Failed);
  ++summary.queueFailures;

  EXPECT_EQ(summary.metadataCacheHits, 1u);
  EXPECT_EQ(summary.metadataCacheMisses, 1u);
  EXPECT_EQ(summary.metadataPrepared, 1u);
  EXPECT_EQ(summary.metadataFailed, 1u);
  EXPECT_EQ(summary.thumbnailCacheHits, 1u);
  EXPECT_EQ(summary.thumbnailsGenerated, 1u);
  EXPECT_EQ(summary.thumbnailsSkipped, 3u);
  EXPECT_EQ(summary.thumbnailsFailed, 1u);
  EXPECT_NE(summary.completionMessage().find("1 hit/1 miss"), std::string::npos);
  EXPECT_NE(summary.completionMessage().find("1 made/3 skipped/2 failed"), std::string::npos);
}
