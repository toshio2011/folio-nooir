#pragma once

#include <cstddef>

namespace bluetooth_settings {

constexpr bool preferenceOffTransitionRequiresMenuRebuild(const bool wasEnabled, const bool enabled) {
  return wasEnabled && !enabled;
}

// Preserve a trailing OFF row while the number of discovered or paired
// devices changes asynchronously.
constexpr int selectionAfterDataCountChange(const int selectedIndex, const int previousDataCount,
                                            const int currentDataCount) {
  if (selectedIndex == previousDataCount) return currentDataCount;
  if (selectedIndex < 0) return 0;
  return selectedIndex > currentDataCount ? currentDataCount : selectedIndex;
}

template <typename Rows, typename Action>
int selectionIndexForAction(const Rows& rows, const Action selectedAction, const Action fallbackAction) {
  for (std::size_t index = 0; index < rows.size(); ++index) {
    if (rows[index].action == selectedAction) return static_cast<int>(index);
  }
  for (std::size_t index = 0; index < rows.size(); ++index) {
    if (rows[index].action == fallbackAction) return static_cast<int>(index);
  }
  return 0;
}

// Preparing and failed states share [Connecting/Retry, OFF, Back] when retry is
// available, so an async result does not insert a row above the user's focus.
// After the one permitted retry is used, collapse Connecting and OFF onto the
// only available OFF row while keeping Back addressable.
constexpr int failureSelectionAfterPreparation(const int preparingIndex, const bool retryAvailable) {
  if (preparingIndex <= 0) return 0;
  if (preparingIndex == 1) return retryAvailable ? 1 : 0;
  return retryAvailable ? 2 : 1;
}

}  // namespace bluetooth_settings
