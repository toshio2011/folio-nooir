#include "util/QuickActions.h"

#include <gtest/gtest.h>

TEST(QuickActions, DefaultsAreBoundedAndContextFiltered) {
  QuickActions::resetSlots();

  std::array<QuickActions::ActionId, QuickActions::SLOT_COUNT> available{};
  const QuickActions::Context noDictionary{true, false, false};
  EXPECT_EQ(QuickActions::collectAvailable(noDictionary, available), 3u);
  EXPECT_EQ(available[0], QuickActions::ActionId::ToggleBookmark);
  EXPECT_EQ(available[1], QuickActions::ActionId::ToggleDarkMode);
  EXPECT_EQ(available[2], QuickActions::ActionId::RefreshScreen);

  const QuickActions::Context fullContext{true, true, true};
  EXPECT_EQ(QuickActions::collectAvailable(fullContext, available), QuickActions::SLOT_COUNT);
  EXPECT_EQ(available[1], QuickActions::ActionId::Lookup);
}

TEST(QuickActions, SettingsBoundaryRejectsDuplicatesAndSupportsDisabledSlots) {
  QuickActions::resetSlots();

  EXPECT_FALSE(QuickActions::setSlot(0, QuickActions::ActionId::Lookup));
  EXPECT_TRUE(QuickActions::setSlot(0, QuickActions::ActionId::None));
  EXPECT_EQ(QuickActions::getSlot(0), QuickActions::ActionId::None);
  EXPECT_FALSE(QuickActions::setSlot(QuickActions::SLOT_COUNT, QuickActions::ActionId::Home));

  QuickActions::resetSlots();
}

TEST(QuickActions, AvailabilityKeepsReaderOnlyActionsBounded) {
  const QuickActions::Context noSection{false, true, true};
  EXPECT_FALSE(QuickActions::isAvailable(QuickActions::ActionId::Lookup, noSection));
  EXPECT_FALSE(QuickActions::isAvailable(QuickActions::ActionId::ToggleBookmark, noSection));
  EXPECT_TRUE(QuickActions::isAvailable(QuickActions::ActionId::Home, noSection));
  EXPECT_TRUE(QuickActions::isAvailable(QuickActions::ActionId::Sleep, noSection));
  EXPECT_FALSE(QuickActions::isAvailable(QuickActions::ActionId::Sync,
                                         QuickActions::Context{true, true, false}));
}
