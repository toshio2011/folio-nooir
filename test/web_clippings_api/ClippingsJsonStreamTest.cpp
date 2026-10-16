#include "util/ClippingsJsonStream.h"

#include <gtest/gtest.h>

#include <algorithm>
#include <string>
#include <vector>

namespace {

class StringReader {
 public:
  explicit StringReader(const std::string& source) : source(source) {}

  int peek() const { return position < source.size() ? static_cast<unsigned char>(source[position]) : -1; }

  int read() {
    const int value = peek();
    if (value >= 0) ++position;
    return value;
  }

 private:
  const std::string& source;
  size_t position = 0;
};

std::string makeLargeFixture(const size_t count) {
  const std::string text(1024, 'h');
  const std::string note(256, 'n');
  std::string fixture = "{\"version\":1,\"clippings\":[";
  for (size_t index = 0; index < count; ++index) {
    if (index != 0) fixture += ',';
    fixture += "{\"text\":\"" + text + "\",\"note\":\"" + note + "\",\"page\":" +
               std::to_string(index) + "}";
  }
  fixture += "]}";
  return fixture;
}

}  // namespace

TEST(ClippingsJsonStream, LargeBookEmitsOneBoundedRecordAtATimeAndRetainsOnlyRequestedPage) {
  constexpr size_t kMaxTextBytes = 1024;
  constexpr size_t kMaxNoteBytes = 256;
  constexpr size_t kMaxRecords = 32;
  constexpr size_t kPageSize = 12;
  constexpr size_t kRecordBufferBytes = 6 * (kMaxTextBytes + kMaxNoteBytes) + 1024;

  const std::string fixture = makeLargeFixture(kMaxRecords);
  StringReader reader(fixture);
  std::vector<char> recordBuffer(kRecordBufferBytes);
  std::vector<std::string> returnedPage;
  std::vector<std::string> returnedNotes;
  size_t recordsFound = 0;
  size_t largestRecord = 0;
  char* firstRecordBuffer = nullptr;
  const auto result = ClippingsJsonStream::parse(
      reader, recordBuffer.data(), recordBuffer.size(), [&](char* record, const size_t length) {
        if (!firstRecordBuffer) firstRecordBuffer = record;
        EXPECT_EQ(record, firstRecordBuffer);
        largestRecord = std::max(largestRecord, length);
        EXPECT_EQ(record[length], '\0');
        const std::string json(record, length);
        EXPECT_NE(json.find("\"text\":\""), std::string::npos);
        if (recordsFound < kMaxRecords && recordsFound >= 12 && returnedPage.size() < kPageSize) {
          returnedPage.emplace_back(kMaxTextBytes, 'h');
          returnedNotes.emplace_back(kMaxNoteBytes, 'n');
          EXPECT_LE(returnedPage.back().size(), kMaxTextBytes);
          EXPECT_LE(returnedNotes.back().size(), kMaxNoteBytes);
        }
        ++recordsFound;
        return ClippingsJsonStream::RecordStatus::accepted;
      });

  ASSERT_TRUE(result.valid);
  EXPECT_TRUE(result.hasClippingsArray);
  EXPECT_EQ(recordsFound, kMaxRecords);
  EXPECT_EQ(returnedPage.size(), kPageSize);
  EXPECT_EQ(returnedNotes.size(), kPageSize);
  EXPECT_LE(largestRecord, kRecordBufferBytes - 1);
  EXPECT_LE(returnedPage.size(), kPageSize);
}

TEST(ClippingsJsonStream, HandlesEscapedDelimitersAndRejectsTruncatedRecords) {
  constexpr size_t kRecordBufferBytes = 9000;
  std::vector<char> recordBuffer(kRecordBufferBytes);
  const std::string valid = R"({"other":{"nested":[1,true]},"clippings":[{"text":"quote \" and brace } and bracket ]"}]})";
  StringReader validReader(valid);
  size_t recordsFound = 0;
  const auto validResult = ClippingsJsonStream::parse(
      validReader, recordBuffer.data(), recordBuffer.size(), [&](char*, size_t) {
        ++recordsFound;
        return ClippingsJsonStream::RecordStatus::accepted;
      });
  EXPECT_TRUE(validResult.valid);
  EXPECT_EQ(recordsFound, 1u);

  const std::string truncated = R"({"clippings":[{"text":"unfinished"})";
  StringReader truncatedReader(truncated);
  const auto truncatedResult = ClippingsJsonStream::parse(
      truncatedReader, recordBuffer.data(), recordBuffer.size(), [](char*, size_t) {
        return ClippingsJsonStream::RecordStatus::accepted;
      });
  EXPECT_FALSE(truncatedResult.valid);
}

TEST(ClippingsJsonStream, RejectsARecordThatExceedsTheFixedBuffer) {
  const std::string fixture = R"({"clippings":[{"text":"this record cannot fit"}]})";
  StringReader reader(fixture);
  char recordBuffer[8]{};
  size_t callbackCount = 0;
  const auto result = ClippingsJsonStream::parse(
      reader, recordBuffer, sizeof(recordBuffer), [&](char*, size_t) {
        ++callbackCount;
        return ClippingsJsonStream::RecordStatus::accepted;
      });
  EXPECT_FALSE(result.valid);
  EXPECT_EQ(callbackCount, 0u);
}

