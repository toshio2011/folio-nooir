#include "util/BookCacheUtils.h"

#include <gtest/gtest.h>

#include <array>
#include <string>
#include <vector>

#include <HalStorage.h>
#include <BookCacheStub.h>

namespace {

constexpr char kBookPath[] = "/books/Detective.xtc";
constexpr char kCachePath[] = "/.crosspoint/xtc_/books/Detective.xtc";
constexpr char kBookmarkPath[] = "/.crosspoint/bookmarks/Detective.json";
constexpr char kStatsPath[] = "/.crosspoint/statistics.json";
constexpr char kCacheCoverPath[] = "/.crosspoint/xtc_/books/Detective.xtc/cover.bmp";
constexpr char kCachePagePath[] = "/.crosspoint/xtc_/books/Detective.xtc/page_0.bmp";

const std::string kBookmark = R"({"page":17,"note":"keep"})";
const std::string kStats = R"({"minutes":52,"streak":3})";
const std::string kCover = "cover bytes";
const std::string kPage = "cached page bytes";
const std::array<uint8_t, 4> kCanonical = {0x12, 0x34, 0x56, 0x78};
const std::array<uint8_t, 4> kBackup = {0x87, 0x65, 0x43, 0x21};
const std::array<uint8_t, 4> kTemporary = {0x11, 0x22, 0x33, 0x44};
const std::array<uint8_t, 6> kEpubProgress = {0x01, 0x02, 0x03, 0x04, 0xA5, 0x5A};

std::string bytes(const uint8_t* data, size_t length) {
  return std::string(reinterpret_cast<const char*>(data), length);
}

template <size_t N>
std::string bytes(const std::array<uint8_t, N>& data) {
  return bytes(data.data(), data.size());
}

void addBookCache() {
  Storage.mkdir(kCachePath);
  Storage.files[kCacheCoverPath] = kCover;
  Storage.files[kCachePagePath] = kPage;
  Storage.files[kBookmarkPath] = kBookmark;
  Storage.files[kStatsPath] = kStats;
}

void expectUnchangedAfterAbort(const std::map<std::string, std::string>& originalFiles) {
  clearBookCache(kBookPath);
  EXPECT_EQ(Storage.files, originalFiles);
  EXPECT_TRUE(Storage.directories.contains(kCachePath));
}

class BookCacheUtilsTest : public testing::Test {
 protected:
  void SetUp() override { Storage.reset(); }
};

TEST_F(BookCacheUtilsTest, CorruptOrShortCandidatesAbortWithoutRemovingAnyCacheOrBookData) {
  const std::vector<std::pair<std::string, std::string>> cases = {
      {"/progress.bin", "\x01\x02"},
      {"/progress.bin.bak", "\x01\x02\x03"},
      {"/progress.bin.tmp", "\x01"},
  };

  for (const auto& [suffix, corruptBytes] : cases) {
    Storage.reset();
    addBookCache();
    Storage.files[std::string(kCachePath) + suffix] = corruptBytes;
    const auto before = Storage.files;

    SCOPED_TRACE(suffix);
    expectUnchangedAfterAbort(before);
    EXPECT_EQ(Storage.files.at(kBookmarkPath), kBookmark);
    EXPECT_EQ(Storage.files.at(kStatsPath), kStats);
  }
}

TEST_F(BookCacheUtilsTest, UnreadableOrShortReadCandidateAbortsAndPreservesEverything) {
  addBookCache();
  const std::string progressPath = std::string(kCachePath) + "/progress.bin";
  Storage.files[progressPath] = bytes(kCanonical);
  Storage.unreadablePaths.insert(progressPath);
  const auto unreadableBefore = Storage.files;
  expectUnchangedAfterAbort(unreadableBefore);

  Storage.reset();
  addBookCache();
  Storage.files[progressPath] = bytes(kCanonical);
  Storage.readLimits[progressPath] = 2;
  const auto shortReadBefore = Storage.files;
  expectUnchangedAfterAbort(shortReadBefore);
}

TEST_F(BookCacheUtilsTest, InvalidBackupDoesNotCauseValidCanonicalOrBackupToBeDestroyed) {
  addBookCache();
  const std::string canonicalPath = std::string(kCachePath) + "/progress.bin";
  const std::string backupPath = canonicalPath + ".bak";
  Storage.files[canonicalPath] = bytes(kCanonical);
  Storage.files[backupPath] = "bad";
  const auto before = Storage.files;

  expectUnchangedAfterAbort(before);
}

TEST_F(BookCacheUtilsTest, ValidCanonicalProgressAndRecoveryBackupSurviveCacheClear) {
  addBookCache();
  const std::string epubPath = "/books/Detective.epub";
  const std::string cachePath = testCachePath("epub", epubPath);
  Storage.mkdir(cachePath.c_str());
  Storage.files[cachePath + "/cover.bmp"] = kCover;
  Storage.files[cachePath + "/progress.bin"] = bytes(kEpubProgress);
  const std::string bookmarkPath = "/.crosspoint/bookmarks/Detective.epub.json";
  Storage.files[bookmarkPath] = kBookmark;
  const std::string statsPath = "/.crosspoint/statistics.json";
  Storage.files[statsPath] = kStats;

  clearBookCache(epubPath);

  EXPECT_FALSE(Storage.exists((cachePath + "/cover.bmp").c_str()));
  EXPECT_EQ(Storage.files.at(cachePath + "/progress.bin"), bytes(kEpubProgress));
  EXPECT_EQ(Storage.files.at(cachePath + "/progress.bin.bak"), bytes(kEpubProgress));
  EXPECT_EQ(Storage.files.at(bookmarkPath), kBookmark);
  EXPECT_EQ(Storage.files.at(statsPath), kStats);
}

TEST_F(BookCacheUtilsTest, BackupOnlyIsRecoveredAsCanonicalAndRetainedAsFallback) {
  addBookCache();
  const std::string backupPath = std::string(kCachePath) + "/progress.bin.bak";
  Storage.files[backupPath] = bytes(kBackup);

  clearBookCache(kBookPath);

  EXPECT_FALSE(Storage.exists(kCacheCoverPath));
  EXPECT_EQ(Storage.files.at(std::string(kCachePath) + "/progress.bin"), bytes(kBackup));
  EXPECT_EQ(Storage.files.at(backupPath), bytes(kBackup));
  EXPECT_EQ(Storage.files.at(kBookmarkPath), kBookmark);
  EXPECT_EQ(Storage.files.at(kStatsPath), kStats);
}

TEST_F(BookCacheUtilsTest, TemporaryOnlyIsRecoveredAndOriginalTemporaryCandidateIsPreserved) {
  addBookCache();
  const std::string tempPath = std::string(kCachePath) + "/progress.bin.tmp";
  Storage.files[tempPath] = bytes(kTemporary);

  clearBookCache(kBookPath);

  EXPECT_FALSE(Storage.exists(kCacheCoverPath));
  EXPECT_EQ(Storage.files.at(std::string(kCachePath) + "/progress.bin"), bytes(kTemporary));
  EXPECT_EQ(Storage.files.at(std::string(kCachePath) + "/progress.bin.bak"), bytes(kTemporary));
  EXPECT_EQ(Storage.files.at(tempPath), bytes(kTemporary));
  EXPECT_EQ(Storage.files.at(kBookmarkPath), kBookmark);
  EXPECT_EQ(Storage.files.at(kStatsPath), kStats);
}

TEST_F(BookCacheUtilsTest, DistinctValidCanonicalBackupAndTemporaryCandidatesAreAllPreserved) {
  addBookCache();
  const std::string canonicalPath = std::string(kCachePath) + "/progress.bin";
  const std::string backupPath = canonicalPath + ".bak";
  const std::string tempPath = canonicalPath + ".tmp";
  Storage.files[canonicalPath] = bytes(kCanonical);
  Storage.files[backupPath] = bytes(kBackup);
  Storage.files[tempPath] = bytes(kTemporary);

  clearBookCache(kBookPath);

  EXPECT_EQ(Storage.files.at(canonicalPath), bytes(kCanonical));
  EXPECT_EQ(Storage.files.at(backupPath), bytes(kBackup));
  EXPECT_EQ(Storage.files.at(tempPath), bytes(kTemporary));
  EXPECT_FALSE(Storage.exists(kCacheCoverPath));
  EXPECT_EQ(Storage.files.at(kBookmarkPath), kBookmark);
  EXPECT_EQ(Storage.files.at(kStatsPath), kStats);
}

TEST_F(BookCacheUtilsTest, MissingProgressDoesNotTurnCacheClearIntoHistoryDeletion) {
  addBookCache();
  const auto before = Storage.files;

  expectUnchangedAfterAbort(before);
}

TEST_F(BookCacheUtilsTest, TransientMissingCacheDirectorySkipsTheClearOperation) {
  addBookCache();
  Storage.files[std::string(kCachePath) + "/progress.bin"] = bytes(kCanonical);
  const auto before = Storage.files;
  Storage.existsFalsePaths.insert(kCachePath);

  clearBookCache(kBookPath);

  EXPECT_EQ(Storage.removeDirCalls, 0u);
  EXPECT_EQ(Storage.files, before);
  EXPECT_TRUE(Storage.directories.contains(kCachePath));
}

TEST_F(BookCacheUtilsTest, PartialDirectoryClearFailureRestoresEveryValidatedProgressCandidate) {
  addBookCache();
  const std::string canonicalPath = std::string(kCachePath) + "/progress.bin";
  const std::string backupPath = canonicalPath + ".bak";
  const std::string temporaryPath = canonicalPath + ".tmp";
  Storage.files[canonicalPath] = bytes(kCanonical);
  Storage.files[backupPath] = bytes(kBackup);
  Storage.files[temporaryPath] = bytes(kTemporary);
  Storage.partialRemoveDirFailAfterPath = temporaryPath;

  clearBookCache(kBookPath);

  EXPECT_EQ(Storage.removeDirCalls, 1u);
  EXPECT_TRUE(Storage.directories.contains(kCachePath));
  EXPECT_FALSE(Storage.exists(kCacheCoverPath));
  EXPECT_FALSE(Storage.exists(kCachePagePath));
  EXPECT_EQ(Storage.files.at(canonicalPath), bytes(kCanonical));
  EXPECT_EQ(Storage.files.at(backupPath), bytes(kBackup));
  EXPECT_EQ(Storage.files.at(temporaryPath), bytes(kTemporary));
  EXPECT_EQ(Storage.files.at(kBookmarkPath), kBookmark);
  EXPECT_EQ(Storage.files.at(kStatsPath), kStats);
}

}  // namespace
