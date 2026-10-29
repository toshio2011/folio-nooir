#include <gtest/gtest.h>

#include <array>
#include <cstdint>
#include <string>

#include <BuildScratch.h>
#include <InflateStream.h>

namespace {
constexpr size_t kFrameBufferBytes = 48 * 1024;

class BuildScratchFixture : public ::testing::Test {
 protected:
  void TearDown() override { buildscratch::reclaim(); }

  std::array<uint8_t, kFrameBufferBytes> framebuffer{};
};
}  // namespace

TEST_F(BuildScratchFixture, StreamingInflaterClaimsAndReleasesFrameBufferScratch) {
  buildscratch::lend(framebuffer.data(), framebuffer.size());

  {
    InflateStream inflate;
    ASSERT_TRUE(inflate.init(true));
    EXPECT_EQ(inflate.initFailure(), InflateStream::InitFailure::None);

    // A second consumer cannot take the same frame while the inflater owns it.
    EXPECT_EQ(buildscratch::claim(1), nullptr);

    constexpr uint8_t compressed[] = {
        0xed, 0xcc, 0xb1, 0x09, 0x80, 0x30, 0x10, 0x05, 0xd0, 0x55, 0xfe, 0x04, 0x0e, 0x21, 0x58, 0xa4,
        0xb3, 0x71, 0x80, 0x23, 0x7e, 0x62, 0x10, 0x93, 0x70, 0x39, 0x45, 0xb7, 0x17, 0xb7, 0xb0, 0xb8,
        0xfe, 0xf1, 0xc2, 0x21, 0x89, 0xe0, 0x6d, 0x2a, 0xd1, 0x72, 0x2d, 0x50, 0xc6, 0x7a, 0x51, 0x1f,
        0xec, 0x64, 0xeb, 0xb0, 0x8d, 0x88, 0xa7, 0x2a, 0x8b, 0x61, 0x9a, 0x97, 0x11, 0xed, 0xf3, 0x52,
        0x56, 0x34, 0xad, 0x49, 0xd9, 0x3b, 0x72, 0x31, 0x89, 0x36, 0x20, 0x78, 0xe5, 0x95, 0x57, 0x5e,
        0x79, 0x85, 0x5f, 0x54, 0x2f,
    };
    constexpr char kPhrase[] = "Image extraction recovery keeps the current EPUB page and progress intact. ";
    const std::string expected = [&] {
      std::string value;
      for (int i = 0; i < 20; ++i) value += kPhrase;
      return value;
    }();
    std::array<uint8_t, 1500> output{};
    inflate.setSource(compressed, sizeof(compressed));
    ASSERT_TRUE(inflate.read(output.data(), output.size()));
    EXPECT_EQ(std::string(output.begin(), output.end()), expected);
    inflate.deinit();
  }

  // Destroying the stream returns the borrowed frame for reuse.
  uint8_t* reclaimed = buildscratch::claim(1);
  ASSERT_NE(reclaimed, nullptr);
  EXPECT_EQ(reclaimed, framebuffer.data());
  buildscratch::release(reclaimed);
}

TEST_F(BuildScratchFixture, UndersizedScratchFallsBackToHeapWithoutClaimingIt) {
  buildscratch::lend(framebuffer.data(), 1024);

  InflateStream inflate;
  ASSERT_TRUE(inflate.init(true));
  EXPECT_EQ(inflate.initFailure(), InflateStream::InitFailure::None);

  // A too-small loan remains available, while the inflater owns its heap
  // backing and must release it through deinit/destruction.
  uint8_t* stillAvailable = buildscratch::claim(1);
  ASSERT_EQ(stillAvailable, framebuffer.data());
  buildscratch::release(stillAvailable);
  inflate.deinit();
}

TEST_F(BuildScratchFixture, WindowAllocationMissCanRetryWithFramebufferScratch) {
  constexpr size_t kLargestBlockShortOfWindow = 32 * 1024 - 12;
  InflateStream firstAttempt;
  EXPECT_FALSE(firstAttempt.init(true, kLargestBlockShortOfWindow));
  EXPECT_EQ(firstAttempt.initFailure(), InflateStream::InitFailure::WindowAllocation);

  // The same 32,756-byte heap measurement does not prevent a retry when the
  // existing 48 KiB framebuffer is safely loaned to this bounded parse.
  buildscratch::lend(framebuffer.data(), framebuffer.size());
  InflateStream retry;
  ASSERT_TRUE(retry.init(true, kLargestBlockShortOfWindow));
  EXPECT_EQ(retry.initFailure(), InflateStream::InitFailure::None);

  constexpr uint8_t compressed[] = {
      0xed, 0xcc, 0xb1, 0x09, 0x80, 0x30, 0x10, 0x05, 0xd0, 0x55, 0xfe, 0x04, 0x0e, 0x21, 0x58, 0xa4,
      0xb3, 0x71, 0x80, 0x23, 0x7e, 0x62, 0x10, 0x93, 0x70, 0x39, 0x45, 0xb7, 0x17, 0xb7, 0xb0, 0xb8,
      0xfe, 0xf1, 0xc2, 0x21, 0x89, 0xe0, 0x6d, 0x2a, 0xd1, 0x72, 0x2d, 0x50, 0xc6, 0x7a, 0x51, 0x1f,
      0xec, 0x64, 0xeb, 0xb0, 0x8d, 0x88, 0xa7, 0x2a, 0x8b, 0x61, 0x9a, 0x97, 0x11, 0xed, 0xf3, 0x52,
      0x56, 0x34, 0xad, 0x49, 0xd9, 0x3b, 0x72, 0x31, 0x89, 0x36, 0x20, 0x78, 0xe5, 0x95, 0x57, 0x5e,
      0x79, 0x85, 0x5f, 0x54, 0x2f,
  };
  constexpr char phrase[] = "Image extraction recovery keeps the current EPUB page and progress intact. ";
  std::string expected;
  for (int i = 0; i < 20; ++i) expected += phrase;
  std::array<uint8_t, 1500> output{};
  retry.setSource(compressed, sizeof(compressed));
  ASSERT_TRUE(retry.read(output.data(), output.size()));
  EXPECT_EQ(std::string(output.begin(), output.end()), expected);
}
