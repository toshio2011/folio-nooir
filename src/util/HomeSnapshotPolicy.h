#pragma once

#include <cstddef>
#include <cstdint>

namespace HomeSnapshotPolicy {
constexpr size_t THREE_COVER_PAGE_ITEMS = 3;
constexpr size_t FOUR_BY_TWO_PAGE_ITEMS = 8;
constexpr size_t SPINE_ONE_ROW_MAX_PAGE_ITEMS = 10;
constexpr size_t SPINE_TWO_ROW_MAX_PAGE_ITEMS = 28;

enum class RestoreAction : uint8_t { Rebuild, Restore, PatchDynamic };

constexpr bool isSupportedPageSize(const size_t pageItems) {
  return pageItems == THREE_COVER_PAGE_ITEMS || pageItems == FOUR_BY_TWO_PAGE_ITEMS;
}

constexpr bool isSupportedPageCapacity(const size_t pageItems) {
  return pageItems > 0 && pageItems <= SPINE_TWO_ROW_MAX_PAGE_ITEMS;
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

constexpr RestoreAction classifyRestore(const uint64_t savedStructuralKey, const uint64_t currentStructuralKey,
                                         const uint64_t savedDynamicKey, const uint64_t currentDynamicKey) {
  if (savedStructuralKey != currentStructuralKey) return RestoreAction::Rebuild;
  return savedDynamicKey == currentDynamicKey ? RestoreAction::Restore : RestoreAction::PatchDynamic;
}
}  // namespace HomeSnapshotPolicy
