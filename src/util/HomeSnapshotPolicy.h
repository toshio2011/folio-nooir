#pragma once

#include <array>
#include <cstddef>
#include <cstdint>

namespace HomeSnapshotPolicy {
constexpr size_t THREE_COVER_PAGE_ITEMS = 3;
constexpr size_t FOUR_BY_TWO_PAGE_ITEMS = 8;
constexpr size_t CAROUSEL_PAGE_ITEMS = 1;
constexpr size_t SPINE_ONE_ROW_MAX_PAGE_ITEMS = 10;
constexpr size_t SPINE_TWO_ROW_MAX_PAGE_ITEMS = 28;
constexpr size_t SPINE_THREE_ROW_MAX_PAGE_ITEMS = 42;

struct SnapshotIdentity {
  uint8_t theme = 0;
  uint8_t layout = 0;
};

// These are the settings that can change pixels in a Home framebuffer. System
// preferences such as Bluetooth/Wi-Fi and reader-only typography deliberately
// do not participate in the signature.
using VisualSettingsSignature = std::array<uint8_t, 20>;

constexpr bool visualSettingsMatch(const VisualSettingsSignature& saved,
                                   const VisualSettingsSignature& current) {
  return saved == current;
}

struct SnapshotRect {
  int16_t x = 0;
  int16_t y = 0;
  int16_t width = 0;
  int16_t height = 0;

  constexpr bool valid() const { return width > 0 && height > 0; }
};

constexpr bool rectMatches(const SnapshotRect& saved, const SnapshotRect& current) {
  return saved.x == current.x && saved.y == current.y && saved.width == current.width &&
         saved.height == current.height;
}

enum class RefreshWaveform : uint8_t { Fast, Full };
enum class RefreshReason : uint8_t { Ordinary, PeriodicGhostCleanup, OrientationResync };

struct RefreshState {
  uint16_t completedFastUpdates = 0;
  bool periodicCleanupPending = false;
};

struct RefreshDecision {
  RefreshWaveform waveform = RefreshWaveform::Fast;
  RefreshReason reason = RefreshReason::Ordinary;
};

constexpr uint8_t refreshIntervalForFrequency(const uint8_t frequency) {
  switch (frequency) {
    case 0: return 1;
    case 1: return 5;
    case 2: return 10;
    case 3: return 15;
    case 4: return 30;
    default: return 15;
  }
}

constexpr RefreshDecision chooseRefresh(const bool orientationChanged = false) {
  if (orientationChanged) return {RefreshWaveform::Full, RefreshReason::OrientationResync};
  return {RefreshWaveform::Fast, RefreshReason::Ordinary};
}

constexpr RefreshDecision chooseMaintenanceRefresh(const RefreshState& state, const bool safeBoundary) {
  if (safeBoundary && state.periodicCleanupPending) {
    return {RefreshWaveform::Full, RefreshReason::PeriodicGhostCleanup};
  }
  return {RefreshWaveform::Fast, RefreshReason::Ordinary};
}

constexpr void recordRefresh(RefreshState& state, const RefreshDecision decision, const uint8_t interval) {
  if (decision.waveform == RefreshWaveform::Full) {
    state.completedFastUpdates = 0;
    state.periodicCleanupPending = false;
  } else if (state.completedFastUpdates < UINT16_MAX) {
    ++state.completedFastUpdates;
    const uint8_t boundedInterval = interval == 0 ? 1 : interval;
    if (state.completedFastUpdates >= boundedInterval) state.periodicCleanupPending = true;
  }
}

inline RefreshState& sharedRefreshState() {
  static RefreshState state{};
  return state;
}

constexpr const char* refreshReasonName(const RefreshReason reason) {
  switch (reason) {
    case RefreshReason::PeriodicGhostCleanup: return "periodic_ghost_cleanup";
    case RefreshReason::OrientationResync: return "orientation_resync";
    case RefreshReason::Ordinary:
    default: return "ordinary";
  }
}

enum class RestoreAction : uint8_t { Rebuild, Restore, PatchDynamic };
enum class SnapshotInvalidationReason : uint8_t {
  None,
  VisualOrStructuralKeyMismatch,
  VisiblePageNotPatchable,
  SlotGeometryUnavailable,
  SavedSelectionOutOfRange,
  CurrentPageGeometryMismatch,
};

constexpr SnapshotInvalidationReason classifySnapshotInvalidation(
    const bool keyMatches, const bool pagePatchable, const bool slotGeometryRequired,
    const bool slotGeometryAvailable, const bool selectionInRange) {
  if (!keyMatches) return SnapshotInvalidationReason::VisualOrStructuralKeyMismatch;
  if (!pagePatchable) return SnapshotInvalidationReason::VisiblePageNotPatchable;
  if (slotGeometryRequired && !slotGeometryAvailable) return SnapshotInvalidationReason::SlotGeometryUnavailable;
  if (!selectionInRange) return SnapshotInvalidationReason::SavedSelectionOutOfRange;
  return SnapshotInvalidationReason::None;
}

constexpr const char* snapshotInvalidationReasonName(const SnapshotInvalidationReason reason) {
  switch (reason) {
    case SnapshotInvalidationReason::VisualOrStructuralKeyMismatch: return "visual_or_structural_key_mismatch";
    case SnapshotInvalidationReason::VisiblePageNotPatchable: return "visible_page_not_patchable";
    case SnapshotInvalidationReason::SlotGeometryUnavailable: return "slot_geometry_unavailable";
    case SnapshotInvalidationReason::SavedSelectionOutOfRange: return "saved_selection_out_of_range";
    case SnapshotInvalidationReason::CurrentPageGeometryMismatch: return "current_page_geometry_mismatch";
    case SnapshotInvalidationReason::None:
    default: return "none";
  }
}

constexpr bool isSupportedPageSize(const size_t pageItems) {
  return pageItems == CAROUSEL_PAGE_ITEMS || pageItems == THREE_COVER_PAGE_ITEMS ||
         pageItems == FOUR_BY_TWO_PAGE_ITEMS;
}

constexpr bool isSupportedPageCapacity(const size_t pageItems) {
  return pageItems > 0 && pageItems <= SPINE_THREE_ROW_MAX_PAGE_ITEMS;
}

constexpr bool identityMatches(const SnapshotIdentity saved, const SnapshotIdentity current) {
  return saved.theme == current.theme && saved.layout == current.layout;
}

constexpr bool pageSizeMatches(const size_t savedPageItems, const size_t currentPageItems) {
  return isSupportedPageSize(savedPageItems) && savedPageItems == currentPageItems;
}

constexpr size_t pageStartForSelection(const size_t selectorIndex, const size_t pageItems) {
  return isSupportedPageSize(pageItems) ? selectorIndex - selectorIndex % pageItems : SIZE_MAX;
}

constexpr bool pageSelectionMatches(const size_t selectorIndex, const size_t pageStart, const size_t pageItems,
                                    const size_t visibleBookCount) {
  return isSupportedPageSize(pageItems) && selectorIndex < visibleBookCount &&
         pageStartForSelection(selectorIndex, pageItems) == pageStart;
}

constexpr bool pageRangeMatches(const size_t selectorIndex, const size_t pageStart, const size_t pageItems,
                                const size_t visibleBookCount) {
  return isSupportedPageCapacity(pageItems) && selectorIndex < visibleBookCount && selectorIndex >= pageStart &&
         selectorIndex - pageStart < pageItems;
}

constexpr bool threeRowProgressIsDynamic(const bool liteTheme, const bool threeRowLayout) {
  return liteTheme && threeRowLayout;
}

// Snapshot pageItems records the layout's capacity. The final page may contain
// fewer visible books, so clamp before validating or indexing its path keys.
constexpr size_t visiblePageItemCount(const size_t pageStart, const size_t pageCapacity,
                                      const size_t visibleBookCount) {
  if (!isSupportedPageCapacity(pageCapacity) || pageStart >= visibleBookCount) return 0;
  const size_t remaining = visibleBookCount - pageStart;
  return pageCapacity < remaining ? pageCapacity : remaining;
}

// Lite three-row spines can repair changed book faces one slot at a time.
// Reuse is safe only when the saved and current frame contain the same number
// of occupied slots, so no old face or separator is left in an empty position.
constexpr bool canPatchLiteThreeRowVisibleSlots(const bool liteThreeRowLayout,
                                                 const size_t savedVisibleSlots,
                                                 const size_t currentVisibleSlots) {
  return liteThreeRowLayout && savedVisibleSlots > 0 && savedVisibleSlots == currentVisibleSlots;
}

constexpr bool canPatchLiteThreeRowVisibleSlot(const bool liteThreeRowLayout,
                                                const size_t savedVisibleSlots,
                                                const size_t currentVisibleSlots,
                                                const bool sameBookInSlot) {
  return canPatchLiteThreeRowVisibleSlots(liteThreeRowLayout, savedVisibleSlots, currentVisibleSlots) &&
         sameBookInSlot;
}

constexpr bool canPatchLiteThreeRowPage(const bool liteThreeRowLayout, const bool savedGeometryAvailable,
                                        const size_t savedSlots, const size_t currentSlots) {
  return liteThreeRowLayout && savedGeometryAvailable && savedSlots > 0 &&
         savedSlots <= SPINE_THREE_ROW_MAX_PAGE_ITEMS && currentSlots > 0 &&
         currentSlots <= SPINE_THREE_ROW_MAX_PAGE_ITEMS;
}

// Face-only updates are safe only when the packed page structure is unchanged.
// A moved identity, page start, slot count, or rectangle can also move centered
// row groups and shelf furniture, which requires a full framebuffer rebuild.
template <size_t Capacity>
constexpr bool threeRowPageStructureMatches(
    const bool liteThreeRowLayout, const bool samePageStart, const size_t savedSlots,
    const size_t currentSlots, const std::array<uint64_t, Capacity>& savedPathKeys,
    const std::array<uint64_t, Capacity>& currentPathKeys,
    const std::array<SnapshotRect, Capacity>& savedRects,
    const std::array<SnapshotRect, Capacity>& currentRects) {
  if (!liteThreeRowLayout || !samePageStart || savedSlots == 0 || savedSlots != currentSlots ||
      currentSlots > Capacity) {
    return false;
  }
  for (size_t slot = 0; slot < currentSlots; ++slot) {
    if (savedPathKeys[slot] == 0 || savedPathKeys[slot] != currentPathKeys[slot] ||
        !savedRects[slot].valid() || !currentRects[slot].valid() ||
        !rectMatches(savedRects[slot], currentRects[slot])) {
      return false;
    }
  }
  return true;
}

constexpr bool slotNeedsPatch(const uint64_t savedPath, const uint64_t currentPath,
                              const uint64_t savedVisual, const uint64_t currentVisual,
                              const SnapshotRect& savedRect, const SnapshotRect& currentRect) {
  return savedPath != currentPath || savedVisual != currentVisual || !rectMatches(savedRect, currentRect);
}

constexpr RestoreAction classifyRestore(const uint64_t savedStructuralKey, const uint64_t currentStructuralKey,
                                         const uint64_t savedDynamicKey, const uint64_t currentDynamicKey) {
  if (savedStructuralKey != currentStructuralKey) return RestoreAction::Rebuild;
  return savedDynamicKey == currentDynamicKey ? RestoreAction::Restore : RestoreAction::PatchDynamic;
}

// A dynamic difference can reuse the saved shelf pixels only when the caller
// can patch every changed shelf detail in place. Structural changes always
// require a full rebuild.
constexpr bool canUseRestoredSnapshotFastPath(const RestoreAction action, const bool pageMatches,
                                              const bool snapshotWritePending, const bool blockedByOverlayOrWork,
                                              const bool dynamicPatchCanBeDrawnInPlace) {
  return action != RestoreAction::Rebuild &&
         (action != RestoreAction::PatchDynamic || dynamicPatchCanBeDrawnInPlace) && pageMatches &&
         !snapshotWritePending && !blockedByOverlayOrWork;
}
}  // namespace HomeSnapshotPolicy
