#include <gtest/gtest.h>

#include "EpdFont/SharedIntervalTableRegistry.h"

TEST(SharedIntervalTableRegistryTest, ReusesOnlySameCountCandidates) {
  SharedIntervalTableRegistry<4> registry;
  uint32_t bmpTable[3]{};
  uint32_t fullTable[4]{};

  ASSERT_TRUE(registry.add(bmpTable, 3, true));
  ASSERT_TRUE(registry.add(fullTable, 4, false));

  EXPECT_EQ(registry.candidates(3), 0x1u);
  EXPECT_EQ(registry.candidates(4), 0x2u);
  EXPECT_EQ(registry.candidates(2), 0u);
}

TEST(SharedIntervalTableRegistryTest, FiltersByContentAcrossCompactAndFullTables) {
  struct Interval {
    uint32_t first;
    uint32_t last;
    uint32_t offset;
  };
  struct CompactInterval {
    uint16_t first;
    uint16_t last;
    uint16_t offset;
  };

  SharedIntervalTableRegistry<2> registry;
  const Interval full[] = {{'A', 'C', 0}, {'x', 'z', 3}};
  const CompactInterval compact[] = {{'A', 'C', 0}, {'x', 'y', 3}};
  ASSERT_TRUE(registry.add(full, 2, false));
  ASSERT_TRUE(registry.add(compact, 2, true));

  uint32_t candidates = registry.candidates(2);
  for (size_t i = 0; i < 2; ++i) {
    const Interval& input = full[i];
    registry.filterCandidatesAt(candidates, i, [&input](const auto& entry, size_t row) {
      if (entry.bmp16) {
        const auto& prior = static_cast<const CompactInterval*>(entry.table)[row];
        return prior.first == input.first && prior.last == input.last && prior.offset == input.offset;
      }
      const auto& prior = static_cast<const Interval*>(entry.table)[row];
      return prior.first == input.first && prior.last == input.last && prior.offset == input.offset;
    });
  }

  EXPECT_EQ(candidates, 0x1u);
}

TEST(SharedIntervalTableRegistryTest, KeepsTableAliveUntilLastFontReleasesIt) {
  SharedIntervalTableRegistry<2> registry;
  uint32_t table[2]{};

  ASSERT_TRUE(registry.add(table, 2, false));
  ASSERT_TRUE(registry.retain(0));
  EXPECT_EQ(registry.references(table), 2u);
  EXPECT_FALSE(registry.release(table));
  EXPECT_EQ(registry.references(table), 1u);
  EXPECT_EQ(registry.candidates(2), 0x1u);
  EXPECT_TRUE(registry.release(table));
  EXPECT_EQ(registry.references(table), 0u);
  EXPECT_EQ(registry.candidates(2), 0u);
}

TEST(SharedIntervalTableRegistryTest, FullRegistryLeavesAdditionalTablesPrivate) {
  SharedIntervalTableRegistry<1> registry;
  uint32_t registered[1]{};
  uint32_t privateTable[1]{};

  ASSERT_TRUE(registry.add(registered, 1, true));
  EXPECT_FALSE(registry.add(privateTable, 1, true));
  EXPECT_TRUE(registry.release(privateTable));
  EXPECT_EQ(registry.references(registered), 1u);
  EXPECT_TRUE(registry.release(registered));
}

TEST(SharedIntervalTableRegistryTest, RejectsInvalidRetains) {
  SharedIntervalTableRegistry<1> registry;
  uint32_t table[1]{};

  EXPECT_FALSE(registry.retain(0));
  ASSERT_TRUE(registry.add(table, 1, true));
  EXPECT_FALSE(registry.retain(1));
}
