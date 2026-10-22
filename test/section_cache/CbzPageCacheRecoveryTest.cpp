#include <algorithm>

#include <gtest/gtest.h>

#include "HalStorage.h"
#include "util/CbzPageCache.h"

namespace {

bool queriedPath(const std::string& path) {
  return std::find(Storage.existenceQueries.begin(), Storage.existenceQueries.end(), path) !=
         Storage.existenceQueries.end();
}

void writeTemporaryPage(const std::string& path) {
  HalFile file;
  ASSERT_TRUE(Storage.openFileForWrite("CBZCACHE", path, file));
  const uint8_t payload[] = {0x50, 0x58, 0x43, 0x01};
  ASSERT_EQ(file.write(payload, sizeof(payload)), static_cast<int>(sizeof(payload)));
  file.close();
}

CbzPageCache::PageInfo validPageInfo() {
  CbzPageCache::PageInfo info;
  info.sourceWidth = 600;
  info.sourceHeight = 800;
  info.targetWidth = 480;
  info.targetHeight = 640;
  info.viewMode = 1;
  info.zoomLevel = 0;
  return info;
}

TEST(CbzPageCacheRecovery, MissingManifestLeavesPageFilesForLazyReplacement) {
  Storage.reset();
  Storage.files["/book.cbz"] = {0x43, 0x42, 0x5A};
  Storage.files["/cache/page_000000.pxc"] = {0x50, 0x58, 0x43};
  Storage.files["/cache/page_000000.pxc.tmp"] = {0x50, 0x58};

  CbzPageCache cache("/cache", "/book.cbz", 2);
  ASSERT_TRUE(cache.open());

  EXPECT_TRUE(Storage.files.count("/cache/page_000000.pxc"));
  EXPECT_TRUE(Storage.files.count("/cache/page_000000.pxc.tmp"));
  EXPECT_FALSE(queriedPath("/cache/page_000000.pxc"));
  CbzPageCache::PageInfo info;
  EXPECT_FALSE(cache.isReady(0, 1, 0, info));
}

TEST(CbzPageCacheRecovery, ExistingManifestChecksPagePayloadOnlyWhenRequested) {
  Storage.reset();
  Storage.files["/book.cbz"] = {0x43, 0x42, 0x5A};

  CbzPageCache cache("/cache", "/book.cbz", 2);
  ASSERT_TRUE(cache.open());
  const std::string temporaryPage = cache.temporaryPagePath(0);
  writeTemporaryPage(temporaryPage);
  ASSERT_TRUE(cache.publish(0, validPageInfo(), temporaryPage));

  Storage.existenceQueries.clear();
  CbzPageCache reopened("/cache", "/book.cbz", 2);
  ASSERT_TRUE(reopened.open());
  EXPECT_FALSE(queriedPath(reopened.pagePath(0)));

  CbzPageCache::PageInfo info;
  EXPECT_TRUE(reopened.isReady(0, 1, 0, info));
  EXPECT_TRUE(queriedPath(reopened.pagePath(0)));

  EXPECT_TRUE(Storage.remove(reopened.pagePath(0).c_str()));
  Storage.existenceQueries.clear();
  EXPECT_FALSE(reopened.isReady(0, 1, 0, info));
  EXPECT_EQ(reopened.completedCount(), 0U);
}

TEST(CbzPageCacheRecovery, UnreadableSourceDoesNotResetCachedManifestOrPages) {
  Storage.reset();
  Storage.files["/book.cbz"] = {0x43, 0x42, 0x5A};

  CbzPageCache cache("/cache", "/book.cbz", 1);
  ASSERT_TRUE(cache.open());
  const std::string temporaryPage = cache.temporaryPagePath(0);
  writeTemporaryPage(temporaryPage);
  ASSERT_TRUE(cache.publish(0, validPageInfo(), temporaryPage));

  const std::string manifest = "/cache/page_cache.bin";
  const size_t manifestSize = Storage.files[manifest].size();
  Storage.unreadablePaths.insert("/book.cbz");
  CbzPageCache retry("/cache", "/book.cbz", 1);

  EXPECT_FALSE(retry.open());
  EXPECT_EQ(Storage.files[manifest].size(), manifestSize);
  EXPECT_TRUE(Storage.files.count("/cache/page_000000.pxc"));
}

TEST(CbzPageCacheRecovery, ExcessivePageCountDisablesCacheWithoutUnboundedAllocation) {
  Storage.reset();
  CbzPageCache cache("/cache", "/book.cbz", 1000000);

  EXPECT_EQ(cache.pageCount(), 0U);
  EXPECT_FALSE(cache.open());
  EXPECT_TRUE(Storage.files.empty());
}

}  // namespace
