#include <gtest/gtest.h>

#include <array>

#include "src/util/HomeSnapshotPolicy.h"

TEST(HomeSnapshotPolicy, SupportsBothPersistentGridSizes) {
  EXPECT_TRUE(HomeSnapshotPolicy::isSupportedPageSize(3));
  EXPECT_TRUE(HomeSnapshotPolicy::isSupportedPageSize(8));
  EXPECT_TRUE(HomeSnapshotPolicy::isSupportedPageSize(1));
  EXPECT_FALSE(HomeSnapshotPolicy::isSupportedPageSize(4));
  EXPECT_FALSE(HomeSnapshotPolicy::isSupportedPageCapacity(0));
  EXPECT_TRUE(HomeSnapshotPolicy::isSupportedPageCapacity(HomeSnapshotPolicy::SPINE_ONE_ROW_MAX_PAGE_ITEMS));
  EXPECT_TRUE(HomeSnapshotPolicy::isSupportedPageCapacity(HomeSnapshotPolicy::SPINE_TWO_ROW_MAX_PAGE_ITEMS));
  EXPECT_TRUE(HomeSnapshotPolicy::isSupportedPageCapacity(HomeSnapshotPolicy::SPINE_THREE_ROW_MAX_PAGE_ITEMS));
  EXPECT_FALSE(HomeSnapshotPolicy::isSupportedPageCapacity(HomeSnapshotPolicy::SPINE_THREE_ROW_MAX_PAGE_ITEMS + 1));
  EXPECT_TRUE(HomeSnapshotPolicy::pageSizeMatches(3, 3));
  EXPECT_TRUE(HomeSnapshotPolicy::pageSizeMatches(8, 8));
  EXPECT_FALSE(HomeSnapshotPolicy::pageSizeMatches(3, 8));
  EXPECT_FALSE(HomeSnapshotPolicy::pageSizeMatches(8, 3));
}

TEST(HomeSnapshotPolicy, PagesSelectionByTheActiveGridSize) {
  EXPECT_EQ(HomeSnapshotPolicy::pageStartForSelection(0, HomeSnapshotPolicy::CAROUSEL_PAGE_ITEMS), 0u);
  EXPECT_EQ(HomeSnapshotPolicy::pageStartForSelection(17, HomeSnapshotPolicy::CAROUSEL_PAGE_ITEMS), 17u);
  EXPECT_TRUE(HomeSnapshotPolicy::pageSelectionMatches(17, 17, HomeSnapshotPolicy::CAROUSEL_PAGE_ITEMS, 18));
  EXPECT_FALSE(HomeSnapshotPolicy::pageSelectionMatches(17, 16, HomeSnapshotPolicy::CAROUSEL_PAGE_ITEMS, 18));
  EXPECT_EQ(HomeSnapshotPolicy::pageStartForSelection(0, 3), 0u);
  EXPECT_EQ(HomeSnapshotPolicy::pageStartForSelection(2, 3), 0u);
  EXPECT_EQ(HomeSnapshotPolicy::pageStartForSelection(3, 3), 3u);
  EXPECT_EQ(HomeSnapshotPolicy::pageStartForSelection(7, 8), 0u);
  EXPECT_EQ(HomeSnapshotPolicy::pageStartForSelection(8, 8), 8u);
  EXPECT_FALSE(HomeSnapshotPolicy::pageSelectionMatches(3, 0, 3, 6));
  EXPECT_TRUE(HomeSnapshotPolicy::pageSelectionMatches(5, 3, 3, 6));
  EXPECT_TRUE(HomeSnapshotPolicy::pageSelectionMatches(7, 0, 8, 9));
  EXPECT_FALSE(HomeSnapshotPolicy::pageSelectionMatches(9, 8, 8, 9));
  EXPECT_TRUE(HomeSnapshotPolicy::pageRangeMatches(8, 5, 10, 20));
  EXPECT_TRUE(HomeSnapshotPolicy::pageRangeMatches(27, 18, 10, 40));
  EXPECT_FALSE(HomeSnapshotPolicy::pageRangeMatches(28, 18, 10, 40));
  EXPECT_TRUE(HomeSnapshotPolicy::pageRangeMatches(41, 0, HomeSnapshotPolicy::SPINE_THREE_ROW_MAX_PAGE_ITEMS, 42));
  EXPECT_FALSE(HomeSnapshotPolicy::pageRangeMatches(42, 0, HomeSnapshotPolicy::SPINE_THREE_ROW_MAX_PAGE_ITEMS, 43));
  EXPECT_FALSE(HomeSnapshotPolicy::pageRangeMatches(42, 0, HomeSnapshotPolicy::SPINE_THREE_ROW_MAX_PAGE_ITEMS + 1, 43));
}

TEST(HomeSnapshotPolicy, RequiresExactThemeAndLayoutIdentity) {
  using HomeSnapshotPolicy::SnapshotIdentity;
  EXPECT_TRUE(HomeSnapshotPolicy::identityMatches(SnapshotIdentity{4, 0}, SnapshotIdentity{4, 0}));
  EXPECT_FALSE(HomeSnapshotPolicy::identityMatches(SnapshotIdentity{4, 0}, SnapshotIdentity{5, 0}));
  EXPECT_FALSE(HomeSnapshotPolicy::identityMatches(SnapshotIdentity{4, 0}, SnapshotIdentity{4, 1}));
}

TEST(HomeSnapshotPolicy, ClampsPartialSnapshotPagesToVisibleBooks) {
  EXPECT_EQ(HomeSnapshotPolicy::visiblePageItemCount(0, 8, 5), 5u);
  EXPECT_EQ(HomeSnapshotPolicy::visiblePageItemCount(8, 8, 15), 7u);
  EXPECT_EQ(HomeSnapshotPolicy::visiblePageItemCount(0, 3, 2), 2u);
  EXPECT_EQ(HomeSnapshotPolicy::visiblePageItemCount(0, HomeSnapshotPolicy::SPINE_THREE_ROW_MAX_PAGE_ITEMS, 42),
            42u);
  EXPECT_EQ(HomeSnapshotPolicy::visiblePageItemCount(15, 8, 15), 0u);
  EXPECT_EQ(HomeSnapshotPolicy::visiblePageItemCount(16, 8, 15), 0u);
  EXPECT_EQ(HomeSnapshotPolicy::visiblePageItemCount(0, 0, 15), 0u);
}

TEST(HomeSnapshotPolicy, SeparatesStructuralRebuildFromDynamicPatch) {
  using HomeSnapshotPolicy::RestoreAction;
  constexpr std::array<size_t, 2> pageSizes{HomeSnapshotPolicy::THREE_COVER_PAGE_ITEMS,
                                            HomeSnapshotPolicy::FOUR_BY_TWO_PAGE_ITEMS};
  for (const size_t pageItems : pageSizes) {
    ASSERT_TRUE(HomeSnapshotPolicy::isSupportedPageSize(pageItems));
    EXPECT_EQ(HomeSnapshotPolicy::classifyRestore(10, 11, 20, 20), RestoreAction::Rebuild);
    EXPECT_EQ(HomeSnapshotPolicy::classifyRestore(10, 10, 20, 21), RestoreAction::PatchDynamic);
    EXPECT_EQ(HomeSnapshotPolicy::classifyRestore(10, 10, 20, 20), RestoreAction::Restore);
  }
}

TEST(HomeSnapshotPolicy, OnlyLiteThreeRowProgressIsDynamic) {
  EXPECT_TRUE(HomeSnapshotPolicy::threeRowProgressIsDynamic(true, true));
  EXPECT_FALSE(HomeSnapshotPolicy::threeRowProgressIsDynamic(true, false));
  EXPECT_FALSE(HomeSnapshotPolicy::threeRowProgressIsDynamic(false, true));
}

TEST(HomeSnapshotPolicy, PatchesLiteThreeRowChangesOnlyWhenOccupiedSlotCountMatches) {
  EXPECT_TRUE(HomeSnapshotPolicy::canPatchLiteThreeRowVisibleSlots(true, 21, 21));
  EXPECT_FALSE(HomeSnapshotPolicy::canPatchLiteThreeRowVisibleSlots(false, 21, 21));
  EXPECT_FALSE(HomeSnapshotPolicy::canPatchLiteThreeRowVisibleSlots(true, 21, 20));
  EXPECT_FALSE(HomeSnapshotPolicy::canPatchLiteThreeRowVisibleSlots(true, 0, 0));
  EXPECT_TRUE(HomeSnapshotPolicy::canPatchLiteThreeRowVisibleSlot(true, 21, 21, true));
  EXPECT_FALSE(HomeSnapshotPolicy::canPatchLiteThreeRowVisibleSlot(true, 21, 21, false));
  EXPECT_TRUE(HomeSnapshotPolicy::canPatchLiteThreeRowPage(true, true, 21, 20));
  EXPECT_FALSE(HomeSnapshotPolicy::canPatchLiteThreeRowPage(true, false, 21, 21));
  EXPECT_FALSE(HomeSnapshotPolicy::canPatchLiteThreeRowPage(false, true, 21, 21));
}

TEST(HomeSnapshotPolicy, PatchesChangedAndReorderedSlotsWhenGeometryIsSaved) {
  using HomeSnapshotPolicy::SnapshotRect;
  constexpr SnapshotRect oldRect{10, 20, 30, 40};
  constexpr SnapshotRect movedRect{44, 20, 30, 40};
  EXPECT_FALSE(HomeSnapshotPolicy::slotNeedsPatch(101, 101, 201, 201, oldRect, oldRect));
  EXPECT_TRUE(HomeSnapshotPolicy::slotNeedsPatch(101, 102, 201, 201, oldRect, oldRect));
  EXPECT_TRUE(HomeSnapshotPolicy::slotNeedsPatch(101, 101, 201, 202, oldRect, oldRect));
  EXPECT_TRUE(HomeSnapshotPolicy::slotNeedsPatch(101, 101, 201, 201, oldRect, movedRect));
  // A Recent front-move changes several position-to-book mappings; each moved
  // face and any slot removed by a Finished filter is independently patched.
  constexpr std::array<uint64_t, 3> savedPaths{11, 22, 33};
  constexpr std::array<uint64_t, 3> currentPaths{99, 11, 22};
  EXPECT_TRUE(HomeSnapshotPolicy::slotNeedsPatch(savedPaths[0], currentPaths[0], 1, 1, oldRect, oldRect));
  EXPECT_TRUE(HomeSnapshotPolicy::slotNeedsPatch(savedPaths[1], currentPaths[1], 2, 2, oldRect, oldRect));
  EXPECT_TRUE(HomeSnapshotPolicy::slotNeedsPatch(savedPaths[2], currentPaths[2], 3, 3, oldRect, oldRect));
  EXPECT_TRUE(HomeSnapshotPolicy::slotNeedsPatch(savedPaths[2], 0, 3, 0, oldRect, SnapshotRect{}));
}

TEST(HomeSnapshotPolicy, SeparatesVisualSettingsAndIgnoresUnrepresentedSystemPreferences) {
  HomeSnapshotPolicy::VisualSettingsSignature saved{};
  auto same = saved;
  EXPECT_TRUE(HomeSnapshotPolicy::visualSettingsMatch(saved, same));
  auto visualChange = saved;
  visualChange[0] = 1;
  EXPECT_FALSE(HomeSnapshotPolicy::visualSettingsMatch(saved, visualChange));
  auto nonVisualPreferenceChange = saved;
  EXPECT_TRUE(HomeSnapshotPolicy::visualSettingsMatch(saved, nonVisualPreferenceChange));
}

TEST(HomeSnapshotPolicy, ClassifiesSnapshotInvalidationCauses) {
  using HomeSnapshotPolicy::SnapshotInvalidationReason;
  EXPECT_EQ(HomeSnapshotPolicy::classifySnapshotInvalidation(false, true, false, true, true),
            SnapshotInvalidationReason::VisualOrStructuralKeyMismatch);
  EXPECT_EQ(HomeSnapshotPolicy::classifySnapshotInvalidation(true, false, false, true, true),
            SnapshotInvalidationReason::VisiblePageNotPatchable);
  EXPECT_EQ(HomeSnapshotPolicy::classifySnapshotInvalidation(true, true, true, false, true),
            SnapshotInvalidationReason::SlotGeometryUnavailable);
  EXPECT_EQ(HomeSnapshotPolicy::classifySnapshotInvalidation(true, true, false, true, false),
            SnapshotInvalidationReason::SavedSelectionOutOfRange);
  EXPECT_EQ(HomeSnapshotPolicy::classifySnapshotInvalidation(true, true, false, true, true),
            SnapshotInvalidationReason::None);
  EXPECT_STREQ(HomeSnapshotPolicy::snapshotInvalidationReasonName(
                   SnapshotInvalidationReason::CurrentPageGeometryMismatch),
               "current_page_geometry_mismatch");
}

TEST(HomeSnapshotPolicy, DefersPeriodicGhostCleanupUntilSafeMaintenanceBoundary) {
  using HomeSnapshotPolicy::RefreshReason;
  using HomeSnapshotPolicy::RefreshWaveform;
  HomeSnapshotPolicy::RefreshState state{};
  EXPECT_EQ(HomeSnapshotPolicy::refreshIntervalForFrequency(0), 1);
  EXPECT_EQ(HomeSnapshotPolicy::refreshIntervalForFrequency(1), 5);
  EXPECT_EQ(HomeSnapshotPolicy::refreshIntervalForFrequency(2), 10);
  EXPECT_EQ(HomeSnapshotPolicy::refreshIntervalForFrequency(3), 15);
  EXPECT_EQ(HomeSnapshotPolicy::refreshIntervalForFrequency(4), 30);
  for (int i = 0; i < 14; ++i) {
    const auto decision = HomeSnapshotPolicy::chooseRefresh();
    EXPECT_EQ(decision.waveform, RefreshWaveform::Fast);
    EXPECT_EQ(decision.reason, RefreshReason::Ordinary);
    HomeSnapshotPolicy::recordRefresh(state, decision, 15);
  }
  EXPECT_EQ(state.completedFastUpdates, 14);
  EXPECT_FALSE(state.periodicCleanupPending);

  // Reaching the configured cadence requests a cleanup but active Home
  // selection updates continue to use the fast waveform.
  const auto cadenceUpdate = HomeSnapshotPolicy::chooseRefresh();
  EXPECT_EQ(cadenceUpdate.waveform, RefreshWaveform::Fast);
  HomeSnapshotPolicy::recordRefresh(state, cadenceUpdate, 15);
  EXPECT_TRUE(state.periodicCleanupPending);
  const auto nextSelection = HomeSnapshotPolicy::chooseRefresh();
  EXPECT_EQ(nextSelection.waveform, RefreshWaveform::Fast);
  HomeSnapshotPolicy::recordRefresh(state, nextSelection, 15);
  EXPECT_EQ(state.completedFastUpdates, 16);

  // No cleanup occurs at a boundary that is not known to be safe.
  const auto activeBoundary = HomeSnapshotPolicy::chooseMaintenanceRefresh(state, false);
  EXPECT_EQ(activeBoundary.waveform, RefreshWaveform::Fast);
  EXPECT_EQ(state.completedFastUpdates, 16);
  EXPECT_TRUE(state.periodicCleanupPending);

  // A real sleep-screen boundary consumes the accumulated cleanup exactly once.
  const auto cleanup = HomeSnapshotPolicy::chooseMaintenanceRefresh(state, true);
  EXPECT_EQ(cleanup.waveform, RefreshWaveform::Full);
  EXPECT_EQ(cleanup.reason, RefreshReason::PeriodicGhostCleanup);
  HomeSnapshotPolicy::recordRefresh(state, cleanup, 15);
  EXPECT_EQ(state.completedFastUpdates, 0);
  EXPECT_FALSE(state.periodicCleanupPending);
  EXPECT_EQ(HomeSnapshotPolicy::chooseMaintenanceRefresh(state, true).waveform, RefreshWaveform::Fast);

  // Orientation is an explicit full-resync request and can satisfy pending
  // ghost-clean debt immediately. It remains independent of the cadence.
  HomeSnapshotPolicy::recordRefresh(state, cadenceUpdate, 15);
  const auto orientation = HomeSnapshotPolicy::chooseRefresh(true);
  EXPECT_EQ(orientation.waveform, RefreshWaveform::Full);
  EXPECT_EQ(orientation.reason, RefreshReason::OrientationResync);
  HomeSnapshotPolicy::recordRefresh(state, orientation, 15);
  EXPECT_FALSE(state.periodicCleanupPending);
  EXPECT_EQ(state.completedFastUpdates, 0);
}

TEST(HomeSnapshotPolicy, AccumulatesHomeUpdatesAndBoundsTheCounter) {
  using HomeSnapshotPolicy::RefreshWaveform;
  HomeSnapshotPolicy::RefreshState state{};
  for (uint32_t i = 0; i < static_cast<uint32_t>(UINT16_MAX) + 20; ++i) {
    const auto decision = HomeSnapshotPolicy::chooseRefresh();
    EXPECT_EQ(decision.waveform, RefreshWaveform::Fast);
    HomeSnapshotPolicy::recordRefresh(state, decision, 30);
  }
  EXPECT_EQ(state.completedFastUpdates, UINT16_MAX);
  EXPECT_TRUE(state.periodicCleanupPending);
}

TEST(HomeSnapshotPolicy, EveryConfiguredFrequencyDefersUntilTheSameSafeBoundary) {
  using HomeSnapshotPolicy::RefreshWaveform;
  for (uint8_t frequency = 0; frequency <= 4; ++frequency) {
    HomeSnapshotPolicy::RefreshState state{};
    const uint8_t interval = HomeSnapshotPolicy::refreshIntervalForFrequency(frequency);
    for (uint8_t update = 0; update < interval; ++update) {
      const auto decision = HomeSnapshotPolicy::chooseRefresh();
      EXPECT_EQ(decision.waveform, RefreshWaveform::Fast);
      HomeSnapshotPolicy::recordRefresh(state, decision, interval);
    }
    EXPECT_TRUE(state.periodicCleanupPending);
    EXPECT_EQ(HomeSnapshotPolicy::chooseMaintenanceRefresh(state, false).waveform, RefreshWaveform::Fast);
    EXPECT_EQ(HomeSnapshotPolicy::chooseMaintenanceRefresh(state, true).waveform, RefreshWaveform::Full);
  }
}

TEST(HomeSnapshotPolicy, ReusesStableShelfPixelsWhilePatchingDynamicDetails) {
  using HomeSnapshotPolicy::RestoreAction;
  EXPECT_TRUE(HomeSnapshotPolicy::canUseRestoredSnapshotFastPath(RestoreAction::Restore, true, false, false, false));
  EXPECT_TRUE(HomeSnapshotPolicy::canUseRestoredSnapshotFastPath(RestoreAction::PatchDynamic, true, false, false, true));
  EXPECT_FALSE(HomeSnapshotPolicy::canUseRestoredSnapshotFastPath(RestoreAction::PatchDynamic, true, false, false, false));
  EXPECT_FALSE(HomeSnapshotPolicy::canUseRestoredSnapshotFastPath(RestoreAction::Rebuild, true, false, false, true));
  EXPECT_FALSE(HomeSnapshotPolicy::canUseRestoredSnapshotFastPath(RestoreAction::PatchDynamic, false, false, false, true));
  EXPECT_FALSE(HomeSnapshotPolicy::canUseRestoredSnapshotFastPath(RestoreAction::PatchDynamic, true, true, false, true));
  EXPECT_FALSE(HomeSnapshotPolicy::canUseRestoredSnapshotFastPath(RestoreAction::Restore, true, false, true, true));
}
