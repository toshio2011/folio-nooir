#pragma once

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

enum class RestoreAction : uint8_t { Rebuild, Restore, PatchDynamic };

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

// Snapshot pageItems records the layout's capacity. The final page may contain
// fewer visible books, so clamp before validating or indexing its path keys.
constexpr size_t visiblePageItemCount(const size_t pageStart, const size_t pageCapacity,
                                      const size_t visibleBookCount) {
  if (!isSupportedPageCapacity(pageCapacity) || pageStart >= visibleBookCount) return 0;
  const size_t remaining = visibleBookCount - pageStart;
  return pageCapacity < remaining ? pageCapacity : remaining;
}

constexpr RestoreAction classifyRestore(const uint64_t savedStructuralKey, const uint64_t currentStructuralKey,
                                         const uint64_t savedDynamicKey, const uint64_t currentDynamicKey) {
  if (savedStructuralKey != currentStructuralKey) return RestoreAction::Rebuild;
  return savedDynamicKey == currentDynamicKey ? RestoreAction::Restore : RestoreAction::PatchDynamic;
}
}  // namespace HomeSnapshotPolicy
