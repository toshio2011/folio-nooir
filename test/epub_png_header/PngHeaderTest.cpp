#include <array>

#include <gtest/gtest.h>

#include "lib/Epub/Epub/converters/PngHeader.h"

namespace {
constexpr std::array<uint8_t, 33> kOneByOnePngHeader = {
    0x89, 0x50, 0x4E, 0x47, 0x0D, 0x0A, 0x1A, 0x0A, 0x00, 0x00, 0x00,
    0x0D, 0x49, 0x48, 0x44, 0x52, 0x00, 0x00, 0x00, 0x01, 0x00, 0x00,
    0x00, 0x01, 0x08, 0x06, 0x00, 0x00, 0x00, 0x1F, 0x15, 0xC4, 0x89};
}  // namespace

TEST(PngHeader, ReadsDimensionsFromValidatedIhdrWithoutDecoderState) {
  int16_t width = -1;
  int16_t height = -1;
  EXPECT_TRUE(pngheader::parseDimensions(kOneByOnePngHeader.data(), kOneByOnePngHeader.size(), width, height));
  EXPECT_EQ(width, 1);
  EXPECT_EQ(height, 1);
}

TEST(PngHeader, RejectsTruncatedPrefixAndPreservesOutputValues) {
  int16_t width = 480;
  int16_t height = 800;
  EXPECT_FALSE(pngheader::parseDimensions(kOneByOnePngHeader.data(), 32, width, height));
  EXPECT_EQ(width, 480);
  EXPECT_EQ(height, 800);
}

TEST(PngHeader, RejectsBadSignatureChunkAndCrc) {
  auto header = kOneByOnePngHeader;
  int16_t width = 0;
  int16_t height = 0;

  header[0] = 0;
  EXPECT_FALSE(pngheader::parseDimensions(header.data(), header.size(), width, height));
  header = kOneByOnePngHeader;
  header[11] = 12;
  EXPECT_FALSE(pngheader::parseDimensions(header.data(), header.size(), width, height));
  header = kOneByOnePngHeader;
  header[32] ^= 1;
  EXPECT_FALSE(pngheader::parseDimensions(header.data(), header.size(), width, height));
}

TEST(PngHeader, RejectsDimensionsThatCannotFitImageDimensions) {
  auto header = kOneByOnePngHeader;
  header[18] = 0x80;  // Width 32768 exceeds ImageDimensions' signed 16-bit range.
  header[19] = 0x00;
  const uint32_t crc = pngheader::crc32(header.data() + 12, 17);
  header[29] = static_cast<uint8_t>(crc >> 24);
  header[30] = static_cast<uint8_t>(crc >> 16);
  header[31] = static_cast<uint8_t>(crc >> 8);
  header[32] = static_cast<uint8_t>(crc);

  int16_t width = 120;
  int16_t height = 180;
  EXPECT_FALSE(pngheader::parseDimensions(header.data(), header.size(), width, height));
  EXPECT_EQ(width, 120);
  EXPECT_EQ(height, 180);
}

TEST(PngHeader, RejectsNullAndZeroSizedInputs) {
  int16_t width = 0;
  int16_t height = 0;
  EXPECT_FALSE(pngheader::parseDimensions(nullptr, kOneByOnePngHeader.size(), width, height));
  EXPECT_FALSE(pngheader::parseDimensions(kOneByOnePngHeader.data(), 0, width, height));
}
