#include <gtest/gtest.h>

#include "Epub/PageCacheValidation.h"

namespace {

TEST(SectionCacheValidation, EmptyPageStillRequiresFootnoteCount) {
  EXPECT_TRUE(page_cache_validation::elementCountFits(/*count=*/0, /*remaining=*/sizeof(uint16_t)));
  EXPECT_FALSE(page_cache_validation::elementCountFits(/*count=*/0, /*remaining=*/0));
}

TEST(SectionCacheValidation, ElementCountUsesMinimumWireSize) {
  constexpr size_t minimum = page_cache_validation::MIN_SERIALIZED_ELEMENT_BYTES;

  EXPECT_TRUE(page_cache_validation::elementCountFits(/*count=*/1, minimum + sizeof(uint16_t)));
  EXPECT_TRUE(page_cache_validation::elementCountFits(/*count=*/2, minimum * 2 + sizeof(uint16_t)));
  EXPECT_FALSE(page_cache_validation::elementCountFits(/*count=*/2, minimum + sizeof(uint16_t)));
}

TEST(SectionCacheValidation, TruncatedCountCannotPass) {
  EXPECT_FALSE(page_cache_validation::elementCountFits(/*count=*/1, sizeof(uint8_t)));
  EXPECT_FALSE(page_cache_validation::elementCountFits(/*count=*/0, sizeof(uint8_t)));
}

TEST(SectionCacheValidation, LengthPrefixedStringMustLeaveTrailingFields) {
  EXPECT_TRUE(page_cache_validation::stringLengthFits(/*length=*/4, /*remaining=*/12, /*trailing=*/8));
  EXPECT_FALSE(page_cache_validation::stringLengthFits(/*length=*/5, /*remaining=*/12, /*trailing=*/8));
  EXPECT_FALSE(page_cache_validation::stringLengthFits(/*length=*/1, /*remaining=*/7, /*trailing=*/8));
}

TEST(SectionCacheValidation, MaximumLengthIsBoundedByRemainingPageBytes) {
  EXPECT_FALSE(page_cache_validation::stringLengthFits(UINT32_MAX, /*remaining=*/64, /*trailing=*/8));
}

}  // namespace
