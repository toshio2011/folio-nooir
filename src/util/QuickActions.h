#pragma once

#include <array>
#include <cstddef>
#include <cstdint>

// Small, reader-facing action registry.  The settings screen may configure the
// four slots through this interface; persistence and presentation remain owned
// by the settings activity.
namespace QuickActions {

constexpr std::size_t SLOT_COUNT = 4;
constexpr std::size_t ACTION_COUNT = 11;

enum class ActionId : uint8_t {
  RefreshScreen = 0,
  ToggleDarkMode,
  Lookup,
  ReadingStats,
  ToggleBookmark,
  ReaderOptions,
  Orientation,
  Sync,
  Screenshot,
  Home,
  Sleep,
  None = 0xFF,
};

struct Context {
  bool hasSection = false;
  bool hasDictionary = false;
  bool hasKoReaderCredentials = false;
};

// Defaults favor actions already exposed by the reader's configurable
// shortcuts.  None disables a slot without changing the array size.
inline constexpr std::array<ActionId, SLOT_COUNT> DEFAULT_SLOTS = {
    ActionId::ToggleBookmark,
    ActionId::Lookup,
    ActionId::ToggleDarkMode,
    ActionId::RefreshScreen,
};

inline constexpr std::array<ActionId, ACTION_COUNT> CONFIGURABLE_ACTIONS = {
    ActionId::RefreshScreen, ActionId::ToggleDarkMode, ActionId::Lookup,      ActionId::ReadingStats,
    ActionId::ToggleBookmark, ActionId::ReaderOptions, ActionId::Orientation, ActionId::Sync,
    ActionId::Screenshot,     ActionId::Home,          ActionId::Sleep,
};

// Inline storage keeps the registry allocation-free and makes the settings
// boundary usable without constructing a permanent manager object. Keep the
// mutable array behind the small setter/getter API below.
namespace detail {
inline std::array<ActionId, SLOT_COUNT> configuredSlots = DEFAULT_SLOTS;
}  // namespace detail

inline const std::array<ActionId, SLOT_COUNT>& slots() { return detail::configuredSlots; }

inline bool isKnown(ActionId action) {
  switch (action) {
    case ActionId::RefreshScreen:
    case ActionId::ToggleDarkMode:
    case ActionId::Lookup:
    case ActionId::ReadingStats:
    case ActionId::ToggleBookmark:
    case ActionId::ReaderOptions:
    case ActionId::Orientation:
    case ActionId::Sync:
    case ActionId::Screenshot:
    case ActionId::Home:
    case ActionId::Sleep:
      return true;
    case ActionId::None:
      return false;
  }
  return false;
}

inline bool isAvailable(const ActionId action, const Context& context) {
  switch (action) {
    case ActionId::Lookup:
      return context.hasSection && context.hasDictionary;
    case ActionId::ToggleBookmark:
      return context.hasSection;
    case ActionId::Sync:
      return context.hasKoReaderCredentials;
    case ActionId::None:
      return false;
    case ActionId::RefreshScreen:
    case ActionId::ToggleDarkMode:
    case ActionId::ReadingStats:
    case ActionId::ReaderOptions:
    case ActionId::Orientation:
    case ActionId::Screenshot:
    case ActionId::Home:
    case ActionId::Sleep:
      return true;
  }
  return false;
}

inline bool setSlot(const std::size_t slot, const ActionId action) {
  if (slot >= SLOT_COUNT || (action != ActionId::None && !isKnown(action))) return false;
  if (action != ActionId::None) {
    for (std::size_t i = 0; i < SLOT_COUNT; ++i) {
      if (i != slot && detail::configuredSlots[i] == action) return false;
    }
  }
  detail::configuredSlots[slot] = action;
  return true;
}

inline ActionId getSlot(const std::size_t slot) {
  return slot < SLOT_COUNT ? detail::configuredSlots[slot] : ActionId::None;
}

inline void resetSlots() { detail::configuredSlots = DEFAULT_SLOTS; }

// Load persisted slots without treating the built-in defaults as occupied.
// The caller normalises malformed values before calling this allocation-free
// helper; duplicate entries are left disabled.
inline void loadSlots(const std::array<ActionId, SLOT_COUNT>& requested) {
  detail::configuredSlots.fill(ActionId::None);
  for (std::size_t i = 0; i < SLOT_COUNT; ++i) {
    const ActionId action = requested[i];
    if (action == ActionId::None || !isKnown(action)) continue;
    bool duplicate = false;
    for (std::size_t j = 0; j < i; ++j) {
      if (detail::configuredSlots[j] == action) {
        duplicate = true;
        break;
      }
    }
    if (!duplicate) detail::configuredSlots[i] = action;
  }
}

// Copy only configured actions that are valid in the current context.  The
// output is caller-owned fixed storage; no vector or heap allocation is used.
inline std::size_t collectAvailable(const Context& context, std::array<ActionId, SLOT_COUNT>& output) {
  std::size_t count = 0;
  for (const auto action : detail::configuredSlots) {
    if (count >= SLOT_COUNT || !isAvailable(action, context)) continue;
    output[count++] = action;
  }
  return count;
}

// Localized labels are resolved lazily when the popup opens.
const char* label(ActionId action);

}  // namespace QuickActions
