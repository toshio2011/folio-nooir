#include <cstdint>
#include <cstring>
#include <string>
#include <vector>

#include <gtest/gtest.h>

#include "Xtc/XtcParser.h"
#include "Xtc/XtcTypes.h"
#include "XtcMetadataCache.h"

namespace {
constexpr const char* kFixturePath = "/manga/fixture.xtc";
constexpr uint64_t kChapterOffset = 312;
constexpr uint64_t kPageTableOffset = 504;
constexpr uint64_t kPageOffset = 520;
constexpr size_t kPageDataBytes = 2;

template <typename T>
void store(std::vector<uint8_t>& bytes, const size_t offset, const T& value) {
  ASSERT_LE(offset + sizeof(T), bytes.size());
  std::memcpy(bytes.data() + offset, &value, sizeof(T));
}

void writeChapter(std::vector<uint8_t>& bytes, const size_t offset, const char* name, const uint16_t start,
                  const uint16_t end) {
  if (name) std::memcpy(bytes.data() + offset, name, std::strlen(name));
  store(bytes, offset + 80, start);
  store(bytes, offset + 82, end);
}

std::vector<uint8_t> makeFixture(const std::string* json = nullptr, const std::string& marker = "NOOIRM01",
                                 const uint32_t lengthOverride = UINT32_MAX,
                                 const uint32_t containerMagic = xtc::XTC_MAGIC) {
  constexpr size_t pageBlockBytes = sizeof(xtc::XtgPageHeader) + kPageDataBytes;
  std::vector<uint8_t> bytes(kPageOffset + pageBlockBytes, 0);

  xtc::XtcHeader header{};
  header.magic = containerMagic;
  header.versionMajor = 1;
  header.versionMinor = 0;
  header.pageCount = 1;
  header.readDirection = 1;
  header.hasMetadata = 1;
  header.hasChapters = 1;
  header.currentPage = 1;
  header.metadataOffset = sizeof(xtc::XtcHeader);
  header.pageTableOffset = kPageTableOffset;
  header.dataOffset = kPageOffset;
  header.chapterOffset = kChapterOffset;
  std::memcpy(bytes.data(), &header, sizeof(header));
  constexpr char title[] = "Synthetic Manga";
  constexpr char author[] = "Test Artist";
  std::memcpy(bytes.data() + 0x38, title, sizeof(title));
  std::memcpy(bytes.data() + 0xB8, author, sizeof(author));

  writeChapter(bytes, kChapterOffset, "Chapter One", 1, 1);

  xtc::PageTableEntry entry{};
  entry.dataOffset = kPageOffset;
  entry.dataSize = pageBlockBytes;
  entry.width = 4;
  entry.height = 2;
  std::memcpy(bytes.data() + kPageTableOffset, &entry, sizeof(entry));

  xtc::XtgPageHeader page{};
  page.magic = containerMagic == xtc::XTCH_MAGIC ? xtc::XTH_MAGIC : xtc::XTG_MAGIC;
  page.width = 4;
  page.height = 2;
  page.colorMode = 0;
  page.compression = 0;
  page.dataSize = kPageDataBytes;
  std::memcpy(bytes.data() + kPageOffset, &page, sizeof(page));
  bytes[kPageOffset + sizeof(page)] = 0xA5;
  bytes[kPageOffset + sizeof(page) + 1u] = 0x5A;

  if (json) {
    bytes.insert(bytes.end(), json->begin(), json->end());
    bytes.insert(bytes.end(), marker.begin(), marker.end());
    const uint32_t payloadLength = lengthOverride == UINT32_MAX ? static_cast<uint32_t>(json->size()) : lengthOverride;
    for (uint8_t i = 0; i < 4; ++i) bytes.push_back(static_cast<uint8_t>(payloadLength >> (i * 8u)));
  }
  return bytes;
}

xtc::XtcParser openFixture(std::vector<uint8_t> bytes) {
  XtcMetadataTestStorage::reset();
  Storage.addFile(kFixturePath, std::move(bytes));
  xtc::XtcParser parser;
  EXPECT_EQ(parser.open(kFixturePath), xtc::XtcError::OK);
  return parser;
}
}  // namespace

TEST(XtcMetadata, LazilyParsesNooirDescriptionAndSeriesEscapes) {
  const std::string json =
      R"json({"title":"Synthetic Manga","description":"A quote: \"yes\"\nA path \\ caf\u00e9","series":"Manga \u30EF\u30F3 \uD83D\uDE80","genres":["Action",2,true,null]})json";
  auto parser = openFixture(makeFixture(&json));
  ASSERT_TRUE(parser.isOpen());
  EXPECT_FALSE(parser.hasLoadedExtendedMetadata());
  EXPECT_EQ(parser.getTitle(), "Synthetic Manga");
  EXPECT_EQ(parser.getAuthor(), "Test Artist");
  EXPECT_EQ(XtcMetadataTestStorage::openCalls, 1u);
  EXPECT_EQ(XtcMetadataTestStorage::openHandles, 0u);

  EXPECT_EQ(parser.getDescription(), "A quote: \"yes\"\nA path \\ caf\xC3\xA9");
  EXPECT_EQ(parser.getSeries(), std::string("Manga ") + "\xE3\x83\xAF\xE3\x83\xB3" + " \xF0\x9F\x9A\x80");
  EXPECT_EQ(XtcMetadataTestStorage::openCalls, 2u);
  EXPECT_EQ(XtcMetadataTestStorage::openHandles, 0u);
}

TEST(XtcMetadata, RetriesAfterTransientTrailerReadFailure) {
  const std::string json = R"json({"description":"retry synopsis","series":"Retry Series"})json";
  auto parser = openFixture(makeFixture(&json));
  XtcMetadataTestStorage::failNextRead = true;

  EXPECT_TRUE(parser.getDescription().empty());
  EXPECT_FALSE(parser.hasLoadedExtendedMetadata());
  EXPECT_EQ(parser.getExtendedMetadataState(), xtc::ExtendedMetadataState::NotChecked);
  EXPECT_EQ(XtcMetadataTestStorage::openCalls, 2u);
  EXPECT_EQ(XtcMetadataTestStorage::openHandles, 0u);

  EXPECT_EQ(parser.getDescription(), "retry synopsis");
  EXPECT_EQ(parser.getSeries(), "Retry Series");
  EXPECT_TRUE(parser.hasLoadedExtendedMetadata());
  EXPECT_EQ(parser.getExtendedMetadataState(), xtc::ExtendedMetadataState::Parsed);
  EXPECT_EQ(XtcMetadataTestStorage::openCalls, 3u);
  EXPECT_EQ(XtcMetadataTestStorage::openHandles, 0u);
}

TEST(XtcMetadata, MissingOrWrongMarkerIsIgnoredOnce) {
  const std::string json = R"json({"description":"ignored","series":"ignored"})json";
  auto parser = openFixture(makeFixture(&json, "NOOIRX01"));
  EXPECT_TRUE(parser.getDescription().empty());
  EXPECT_TRUE(parser.getSeries().empty());
  EXPECT_EQ(parser.getExtendedMetadataState(), xtc::ExtendedMetadataState::NotPresent);
  EXPECT_EQ(XtcMetadataTestStorage::openCalls, 2u);
  EXPECT_EQ(XtcMetadataTestStorage::openHandles, 0u);

  auto legacy = openFixture(makeFixture());
  EXPECT_TRUE(legacy.getSeries().empty());
  EXPECT_TRUE(legacy.getDescription().empty());
  EXPECT_EQ(XtcMetadataTestStorage::openCalls, 2u);
  EXPECT_EQ(XtcMetadataTestStorage::openHandles, 0u);
}

TEST(XtcMetadata, RejectsInvalidFooterLengthsAndPayloadBounds) {
  const std::string json = R"json({"series":"invalid length"})json";
  auto mismatch = openFixture(makeFixture(&json, "NOOIRM01", static_cast<uint32_t>(json.size() - 1u)));
  EXPECT_TRUE(mismatch.getSeries().empty());
  EXPECT_EQ(mismatch.getExtendedMetadataState(), xtc::ExtendedMetadataState::Unsupported);

  auto tooLarge = openFixture(makeFixture(&json, "NOOIRM01", 4097));
  EXPECT_TRUE(tooLarge.getSeries().empty());

  auto underflow = openFixture(makeFixture(&json, "NOOIRM01", UINT32_MAX - 1u));
  EXPECT_TRUE(underflow.getSeries().empty());
  EXPECT_EQ(XtcMetadataTestStorage::openHandles, 0u);
}

TEST(XtcMetadata, RejectsInvalidJsonUtf8AndTargetFieldTypesWithoutPartialData) {
  const std::string malformed = R"json({"series":"partial","description":]})json";
  auto badJson = openFixture(makeFixture(&malformed));
  EXPECT_TRUE(badJson.getSeries().empty());
  EXPECT_TRUE(badJson.getDescription().empty());

  const std::string wrongType = R"json({"description":"partial","series":42})json";
  auto badType = openFixture(makeFixture(&wrongType));
  EXPECT_TRUE(badType.getDescription().empty());
  EXPECT_TRUE(badType.getSeries().empty());

  std::string invalidUtf8 = R"json({"series":"bad)json";
  invalidUtf8.push_back(static_cast<char>(0xFF));
  invalidUtf8 += R"json("})json";
  auto badUtf8 = openFixture(makeFixture(&invalidUtf8));
  EXPECT_TRUE(badUtf8.getSeries().empty());
  EXPECT_EQ(XtcMetadataTestStorage::openHandles, 0u);
}

TEST(XtcMetadata, RejectsPayloadOverTheBoundedLimit) {
  std::string oversized = R"json({"description":")json";
  oversized.append(4100, 'x');
  oversized += R"json(","series":"too large"})json";
  ASSERT_GT(oversized.size(), 4096u);
  auto parser = openFixture(makeFixture(&oversized));
  EXPECT_TRUE(parser.getDescription().empty());
  EXPECT_TRUE(parser.getSeries().empty());
  EXPECT_EQ(XtcMetadataTestStorage::openHandles, 0u);
}

TEST(XtcMetadata, PreservesPageAndChapterParsingAroundTheTrailer) {
  const std::string json = R"json({"description":"synopsis","series":"series"})json";
  auto parser = openFixture(makeFixture(&json, "NOOIRM01", UINT32_MAX, xtc::XTCH_MAGIC));
  xtc::PageInfo info{};
  ASSERT_TRUE(parser.getPageInfo(0, info));
  EXPECT_EQ(info.width, 4u);
  EXPECT_EQ(info.height, 2u);
  EXPECT_EQ(info.bitDepth, 2u);
  uint8_t page[kPageDataBytes]{};
  EXPECT_EQ(parser.loadPage(0, page, sizeof(page)), kPageDataBytes);
  EXPECT_EQ(page[0], 0xA5);
  EXPECT_EQ(page[1], 0x5A);
  ASSERT_TRUE(parser.hasChapters());
  const auto& chapters = parser.getChapters();
  ASSERT_EQ(chapters.size(), 1u);
  EXPECT_EQ(chapters[0].name, "Chapter One");

  EXPECT_EQ(parser.getDescription(), "synopsis");
  EXPECT_EQ(parser.getSeries(), "series");
  EXPECT_EQ(XtcMetadataTestStorage::openHandles, 0u);
  EXPECT_EQ(parser.loadPage(0, page, sizeof(page)), kPageDataBytes);
  EXPECT_EQ(page[0], 0xA5);
  EXPECT_EQ(XtcMetadataTestStorage::openHandles, 1u);
}

TEST(XtcMetadata, BoundsChapterAllocationForCorruptOffsets) {
  constexpr uint32_t corruptChapterOffset = 1024;
  constexpr size_t maxChapterRecords = 128;
  constexpr size_t extraChapterRecords = 24;
  constexpr size_t chapterRecordSize = 96;

  auto bytes = makeFixture();
  bytes.resize(corruptChapterOffset + (maxChapterRecords + extraChapterRecords) * chapterRecordSize, 0);
  store(bytes, 0x30, corruptChapterOffset);
  for (size_t i = 0; i < maxChapterRecords + extraChapterRecords; ++i) {
    writeChapter(bytes, corruptChapterOffset + i * chapterRecordSize, "Chapter beyond bounded table", 1, 1);
  }

  auto parser = openFixture(std::move(bytes));
  const auto& chapters = parser.getChapters();
  EXPECT_EQ(chapters.size(), maxChapterRecords);
  EXPECT_EQ(XtcMetadataTestStorage::openHandles, 0u);
}

TEST(XtcMetadataCache, PersistsCompleteMetadataWithoutDependingOnRecentBooks) {
  XtcMetadataTestStorage::reset();
  constexpr const char* sourcePath = "/manga/cache-test.xtc";
  constexpr const char* cachePath = "/.crosspoint/xtc-test/metadata.bin";
  Storage.addFile(sourcePath, {0x58, 0x54, 0x43, 0x01});

  xtc_metadata_cache::SourceFingerprint fingerprint;
  ASSERT_TRUE(xtc_metadata_cache::readSourceFingerprint(sourcePath, fingerprint));
  xtc_metadata_cache::Record original;
  original.title = "Synthetic Title";
  original.author = "Synthetic Artist";
  original.synopsis = "A synopsis stored outside RecentBooksStore.";
  original.series = "Synthetic Series";
  original.extendedMetadata = xtc_metadata_cache::ExtendedMetadataState::Parsed;
  original.cover = xtc_metadata_cache::CoverState::Unknown;

  ASSERT_TRUE(xtc_metadata_cache::store(cachePath, fingerprint, original));
  xtc_metadata_cache::Record loaded;
  EXPECT_EQ(xtc_metadata_cache::load(cachePath, fingerprint, loaded), xtc_metadata_cache::LoadResult::Hit);
  EXPECT_EQ(loaded.title, original.title);
  EXPECT_EQ(loaded.author, original.author);
  EXPECT_EQ(loaded.synopsis, original.synopsis);
  EXPECT_EQ(loaded.series, original.series);
  EXPECT_EQ(loaded.extendedMetadata, xtc_metadata_cache::ExtendedMetadataState::Parsed);
  EXPECT_EQ(loaded.cover, xtc_metadata_cache::CoverState::Unknown);
  original.cover = xtc_metadata_cache::CoverState::Unsupported;
  ASSERT_TRUE(xtc_metadata_cache::store(cachePath, fingerprint, original));
  EXPECT_EQ(xtc_metadata_cache::load(cachePath, fingerprint, loaded), xtc_metadata_cache::LoadResult::Hit);
  EXPECT_EQ(loaded.cover, xtc_metadata_cache::CoverState::Unsupported);
  EXPECT_EQ(XtcMetadataTestStorage::openHandles, 0u);
}

TEST(XtcMetadataCache, RejectsReplacementAndCorruptRecords) {
  XtcMetadataTestStorage::reset();
  constexpr const char* sourcePath = "/manga/cache-test.xtc";
  constexpr const char* cachePath = "/.crosspoint/xtc-test/metadata.bin";
  Storage.addFile(sourcePath, {0x58, 0x54, 0x43, 0x01});

  xtc_metadata_cache::SourceFingerprint fingerprint;
  ASSERT_TRUE(xtc_metadata_cache::readSourceFingerprint(sourcePath, fingerprint));
  xtc_metadata_cache::Record original;
  original.title = "Replacement Test";
  original.extendedMetadata = xtc_metadata_cache::ExtendedMetadataState::NotPresent;
  original.cover = xtc_metadata_cache::CoverState::NoCover;
  ASSERT_TRUE(xtc_metadata_cache::store(cachePath, fingerprint, original));
  xtc_metadata_cache::Record noCover;
  EXPECT_EQ(xtc_metadata_cache::load(cachePath, fingerprint, noCover), xtc_metadata_cache::LoadResult::Hit);
  EXPECT_EQ(noCover.cover, xtc_metadata_cache::CoverState::NoCover);

  XtcMetadataTestStorage::files[sourcePath].push_back(0x02);
  xtc_metadata_cache::SourceFingerprint replacedSize;
  ASSERT_TRUE(xtc_metadata_cache::readSourceFingerprint(sourcePath, replacedSize));
  xtc_metadata_cache::Record ignored;
  EXPECT_EQ(xtc_metadata_cache::load(cachePath, replacedSize, ignored), xtc_metadata_cache::LoadResult::Stale);

  XtcMetadataTestStorage::files[sourcePath].pop_back();
  ++XtcMetadataTestStorage::modifiedTime;
  xtc_metadata_cache::SourceFingerprint replacedTimestamp;
  ASSERT_TRUE(xtc_metadata_cache::readSourceFingerprint(sourcePath, replacedTimestamp));
  EXPECT_EQ(xtc_metadata_cache::load(cachePath, replacedTimestamp, ignored), xtc_metadata_cache::LoadResult::Stale);

  --XtcMetadataTestStorage::modifiedTime;
  XtcMetadataTestStorage::files[cachePath].back() ^= 0x01u;
  EXPECT_EQ(xtc_metadata_cache::load(cachePath, fingerprint, ignored), xtc_metadata_cache::LoadResult::Invalid);
  EXPECT_EQ(xtc_metadata_cache::load("/missing/metadata.bin", fingerprint, ignored),
            xtc_metadata_cache::LoadResult::Missing);
  EXPECT_EQ(XtcMetadataTestStorage::openHandles, 0u);
}
