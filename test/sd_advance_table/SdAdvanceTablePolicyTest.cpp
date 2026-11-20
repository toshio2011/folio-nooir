#include <gtest/gtest.h>

#include <algorithm>
#include <cstdint>
#include <set>
#include <vector>

#include "EpdFont/LowestCodepoints.h"

namespace {
constexpr size_t kAdvanceLimit = 768;
constexpr size_t kScratchCapacity = kAdvanceLimit + 2;

std::vector<uint32_t> retained(const std::vector<uint32_t>& input, const size_t capacity) {
  std::vector<uint32_t> scratch(capacity);
  size_t count = 0;
  for (const uint32_t codepoint : input) {
    SdFontAdvancePolicy::retainLowestUnique(scratch.data(), count, capacity, codepoint);
  }
  scratch.resize(count);
  return scratch;
}

std::vector<uint32_t> lowestMerged(const std::vector<uint32_t>& oldEntries, const std::vector<uint32_t>& newEntries) {
  std::set<uint32_t> merged(oldEntries.begin(), oldEntries.end());
  merged.insert(newEntries.begin(), newEntries.end());
  std::vector<uint32_t> result(merged.begin(), merged.end());
  if (result.size() > kAdvanceLimit) result.resize(kAdvanceLimit);
  return result;
}
}  // namespace

TEST(SdAdvanceTablePolicy, RetainsLowestUniqueEntriesRegardlessOfInputOrder) {
  const std::vector<uint32_t> input = {900, 42, 700, 42, 3, 511, 2, 800, 1};
  EXPECT_EQ(retained(input, 5), (std::vector<uint32_t>{1, 2, 3, 42, 511}));
}

TEST(SdAdvanceTablePolicy, ReplacingLargestEntryKeepsSortedBoundedScratch) {
  std::vector<uint32_t> scratch(kScratchCapacity);
  size_t count = 0;
  for (uint32_t codepoint = 1000; codepoint > 0; --codepoint) {
    SdFontAdvancePolicy::retainLowestUnique(scratch.data(), count, scratch.size(), codepoint);
  }
  ASSERT_EQ(count, kScratchCapacity);
  EXPECT_TRUE(std::is_sorted(scratch.begin(), scratch.end()));
  EXPECT_EQ(scratch.front(), 1U);
  EXPECT_EQ(scratch.back(), static_cast<uint32_t>(kScratchCapacity));
}

TEST(SdAdvanceTablePolicy, TopScratchProducesTheSameCappedAdvanceTable) {
  std::vector<uint32_t> input;
  for (uint32_t codepoint = 0x1000; codepoint < 0x1800; ++codepoint) input.push_back(codepoint);
  for (uint32_t codepoint = 0x1200; codepoint < 0x1400; ++codepoint) input.push_back(codepoint);
  std::reverse(input.begin(), input.end());

  const std::vector<uint32_t> oldEntries = {0x1005, 0x2000, 0x3000};
  const auto scratchCandidates = retained(input, kScratchCapacity);
  const auto reference = lowestMerged(oldEntries, input);
  const auto bounded = lowestMerged(oldEntries, scratchCandidates);
  EXPECT_EQ(bounded, reference);
}

TEST(SdAdvanceTablePolicy, MixedStyleEligibilityAndMissingGlyphsAreIndependent) {
  const std::vector<uint32_t> all = {0x100, 0x101, 0x102, 0x103, 0x104, 0x105};
  const std::vector<uint32_t> regularEligible = {0x101, 0x103, 0x105};
  const std::vector<uint32_t> boldEligible = {0x100, 0x102, 0x104};
  const std::vector<uint32_t> regularCached = {0x101};
  const std::vector<uint32_t> boldCached = {0x100};
  const auto uncached = [](const std::vector<uint32_t>& eligible, const std::vector<uint32_t>& cached) {
    std::vector<uint32_t> result;
    for (const uint32_t codepoint : eligible) {
      if (std::find(cached.begin(), cached.end(), codepoint) == cached.end()) result.push_back(codepoint);
    }
    return result;
  };

  EXPECT_EQ(retained(uncached(regularEligible, regularCached), kScratchCapacity),
            (std::vector<uint32_t>{0x103, 0x105}));
  EXPECT_EQ(retained(uncached(boldEligible, boldCached), kScratchCapacity),
            (std::vector<uint32_t>{0x102, 0x104}));
  EXPECT_EQ(retained(all, kScratchCapacity).size(), all.size());
}

TEST(SdAdvanceTablePolicy, RepeatedScansDoNotDuplicateOrReorderCandidates) {
  const std::vector<uint32_t> firstScan = {0x41, 0x4E00, 0x3042, 0xAC00, 0x41};
  const std::vector<uint32_t> secondScan = {0x3042, 0xAC00, 0x41, 0x4E00};
  EXPECT_EQ(retained(firstScan, kScratchCapacity), retained(secondScan, kScratchCapacity));
}
