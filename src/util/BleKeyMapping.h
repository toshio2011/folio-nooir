#pragma once

#include <cstddef>
#include <cstdint>

namespace bleinput::mapping {

// Persisted kind 0 is a normalized SpecialKey and kind 1 is the pre-existing
// untyped usage identity. New learned keyboard usages use kind 2 so they cannot
// collide with synthetic/unclassified generic report codes.
constexpr uint8_t kButtonAction = 0;
constexpr uint8_t kShortcutAction = 1;
constexpr uint8_t kShortcutHome = 0;
constexpr uint8_t kShortcutLibrary = 1;
constexpr uint8_t kShortcutRecent = 2;
constexpr uint8_t kShortcutClippings = 3;
constexpr uint8_t kShortcutCount = 4;
constexpr uint8_t kPhysicalButtonCount = 6;
constexpr uint8_t kLegacyButtonCount = 11;
constexpr uint8_t kSignatureMaxEvents = 8;
constexpr uint8_t kMapCapacity = 10;

enum class InputPhase : uint8_t { Press, Repeat, Release };
enum class ButtonTransition : uint8_t { Ignored, Pressed, Repeated, Released, Debounced };
enum class ResolvedActionType : uint8_t { Invalid, VirtualButton, Shortcut };
enum class CaptureResult : uint8_t { Added, Duplicate, Full, Invalid };

constexpr bool debounceElapsed(const uint32_t now, const uint32_t lastDispatch, const uint32_t debounceMs) {
  return static_cast<uint32_t>(now - lastDispatch) >= debounceMs;
}

constexpr bool releaseKeepsPendingSignature(const InputPhase phase) {
  return phase == InputPhase::Release;
}

struct ResolvedAction {
  ResolvedActionType type = ResolvedActionType::Invalid;
  uint8_t target = 0xFF;
};

inline ButtonTransition transition(const InputPhase phase, const bool held, const bool active,
                                   const unsigned long now, const unsigned long lastDispatch,
                                   const unsigned long debounceMs) {
  if (phase == InputPhase::Release) return held ? ButtonTransition::Released : ButtonTransition::Ignored;
  if (phase == InputPhase::Repeat) return active ? ButtonTransition::Repeated : ButtonTransition::Ignored;
  if ((lastDispatch != 0 && !debounceElapsed(now, lastDispatch, debounceMs)) || held)
    return ButtonTransition::Debounced;
  return ButtonTransition::Pressed;
}

inline CaptureResult addCapturedIdentity(uint8_t* kinds, uint8_t* values, const uint8_t capacity, uint8_t& count,
                                         const uint8_t kind, const uint8_t value) {
  if (!kinds || !values || capacity == 0 || capacity > kSignatureMaxEvents || count > capacity || kind > 2)
    return CaptureResult::Invalid;
  for (uint8_t i = 0; i < count; ++i) {
    if (kinds[i] == kind && values[i] == value) return CaptureResult::Duplicate;
  }
  if (count >= capacity) return CaptureResult::Full;
  kinds[count] = kind;
  values[count] = value;
  ++count;
  return CaptureResult::Added;
}

inline bool encodeKeyboardUsageForLearning(const uint8_t usagePage, const uint8_t usage, uint8_t& kind,
                                           uint8_t& value) {
  if (usage == 0 || usagePage != 0x07) return false;
  kind = 2;
  value = usage;
  return true;
}

inline bool encodeNormalizedSpecialForLearning(const bool normalized, const uint8_t special, uint8_t& kind,
                                               uint8_t& value) {
  if (!normalized || special == 0) return false;
  kind = 0;
  value = special;
  return true;
}

constexpr bool learnTimedOut(const uint32_t startedAt, const uint32_t now, const uint32_t timeoutMs) {
  return static_cast<uint32_t>(now - startedAt) >= timeoutMs;
}

struct KeyMapEntry {
  uint8_t keyKind = 0xFF;
  uint8_t keyValue = 0;
  // Physical target for legacy/button entries. 0xFF for shortcut entries.
  uint8_t button = 0xFF;
  uint8_t actionType = kButtonAction;
  uint8_t shortcut = 0xFF;
  uint8_t signatureCount = 0;
  uint16_t signature[kSignatureMaxEvents] = {};
};

struct CapturedIdentity {
  uint8_t kind = 0xFF;
  uint8_t value = 0;
};

struct DefaultBinding {
  CapturedIdentity identity;
  uint8_t actionType = kButtonAction;
  uint8_t target = 0xFF;
};

struct PersistedEntry {
  uint8_t keyKind = 0xFF;
  uint8_t keyValue = 0;
  uint8_t button = 0xFF;
  // 0xFF means the old persisted shape, which always represented a button.
  uint8_t actionType = 0xFF;
  uint8_t shortcut = 0xFF;
  uint8_t signatureCount = 0;
  uint16_t signature[kSignatureMaxEvents] = {};
};

enum class AssignResult : uint8_t { Added, Replaced, Removed, Full, Invalid, NotFound };

constexpr bool validTarget(const uint8_t actionType, const uint8_t target) {
  return actionType == kButtonAction ? target < kLegacyButtonCount
                                     : actionType == kShortcutAction && target < kShortcutCount;
}

constexpr uint8_t entryCount(const KeyMapEntry& entry) {
  return entry.signatureCount > 0 && entry.signatureCount <= kSignatureMaxEvents ? entry.signatureCount : 1;
}

constexpr bool sameCodeIdentity(const uint8_t leftKind, const uint8_t leftValue, const uint8_t rightKind,
                                const uint8_t rightValue) {
  // Kind 1 is an old untyped value; kind 2 is a page-7 keyboard usage. Equal
  // byte values do not make those identities interchangeable.
  return leftKind == rightKind && leftValue == rightValue;
}

constexpr bool entryIsValid(const KeyMapEntry& entry) {
  if (entry.keyKind > 2 || !validTarget(entry.actionType, entry.actionType == kButtonAction ? entry.button
                                                                                           : entry.shortcut))
    return false;
  if (entry.signatureCount > kSignatureMaxEvents) return false;
  for (uint8_t i = 0; i < entry.signatureCount; ++i) {
    if ((entry.signature[i] >> 8) > 2) return false;
  }
  return entry.signatureCount == 0 ||
         (static_cast<uint8_t>(entry.signature[0] >> 8) == entry.keyKind &&
          static_cast<uint8_t>(entry.signature[0] & 0xFF) == entry.keyValue);
}

constexpr ResolvedAction resolve(const KeyMapEntry& entry) {
  if (!entryIsValid(entry)) return {};
  return entry.actionType == kShortcutAction
             ? ResolvedAction{ResolvedActionType::Shortcut, entry.shortcut}
             : ResolvedAction{ResolvedActionType::VirtualButton, entry.button};
}

constexpr bool isOccupied(const KeyMapEntry& entry) { return entryIsValid(entry); }

constexpr bool matchesAt(const KeyMapEntry& entry, const uint8_t kind, const uint8_t value,
                         const uint8_t signatureIndex) {
  if (!entryIsValid(entry) || signatureIndex >= entryCount(entry)) return false;
  const uint16_t encoded = entry.signatureCount ? entry.signature[signatureIndex]
                                                 : (static_cast<uint16_t>(entry.keyKind) << 8) | entry.keyValue;
  return static_cast<uint8_t>(encoded >> 8) == kind && static_cast<uint8_t>(encoded & 0xFF) == value;
}

template <size_t Capacity>
int8_t findFirstMatch(const KeyMapEntry (&entries)[Capacity], const uint8_t primaryKind,
                      const uint8_t primaryValue, const uint8_t fallbackKind, const uint8_t fallbackValue,
                      const uint8_t signatureIndex) {
  for (uint8_t i = 0; i < Capacity; ++i) {
    if (matchesAt(entries[i], primaryKind, primaryValue, signatureIndex)) return static_cast<int8_t>(i);
  }
  if (primaryKind == fallbackKind && primaryValue == fallbackValue) return -1;
  for (uint8_t i = 0; i < Capacity; ++i) {
    if (matchesAt(entries[i], fallbackKind, fallbackValue, signatureIndex)) return static_cast<int8_t>(i);
  }
  return -1;
}

inline bool sameCaptured(const KeyMapEntry& entry, const CapturedIdentity* captured, const uint8_t count) {
  if (!captured || count == 0 || count > kSignatureMaxEvents || entryCount(entry) != count || !entryIsValid(entry))
    return false;
  for (uint8_t i = 0; i < count; ++i) {
    const uint16_t encoded = entry.signatureCount ? entry.signature[i]
                                                   : (static_cast<uint16_t>(entry.keyKind) << 8) | entry.keyValue;
    if (!sameCodeIdentity(static_cast<uint8_t>(encoded >> 8), static_cast<uint8_t>(encoded & 0xFF),
                          captured[i].kind, captured[i].value))
      return false;
  }
  return true;
}

template <size_t Capacity>
AssignResult assign(KeyMapEntry (&entries)[Capacity], const CapturedIdentity* captured, const uint8_t count,
                    const uint8_t actionType, const uint8_t target) {
  if (!captured || count == 0 || count > kSignatureMaxEvents || actionType > kShortcutAction ||
      !validTarget(actionType, target))
    return AssignResult::Invalid;
  for (uint8_t i = 0; i < count; ++i) {
    if (captured[i].kind > 2) return AssignResult::Invalid;
    for (uint8_t j = 0; j < i; ++j) {
      if (captured[i].kind == captured[j].kind && captured[i].value == captured[j].value)
        return AssignResult::Invalid;
    }
  }

  KeyMapEntry* destination = nullptr;
  bool replaced = false;
  for (auto& entry : entries) {
    if (sameCaptured(entry, captured, count)) {
      destination = &entry;
      replaced = true;
      break;
    }
  }
  if (!destination) {
    for (auto& entry : entries) {
      if (!isOccupied(entry)) {
        destination = &entry;
        break;
      }
    }
  }
  if (!destination) return AssignResult::Full;

  *destination = KeyMapEntry{};
  destination->keyKind = captured[0].kind;
  destination->keyValue = captured[0].value;
  destination->actionType = actionType;
  destination->button = actionType == kButtonAction ? target : 0xFF;
  destination->shortcut = actionType == kShortcutAction ? target : 0xFF;
  destination->signatureCount = count > 1 ? count : 0;
  for (uint8_t i = 0; i < count; ++i) {
    destination->signature[i] = (static_cast<uint16_t>(captured[i].kind) << 8) | captured[i].value;
  }
  return replaced ? AssignResult::Replaced : AssignResult::Added;
}

template <size_t Capacity>
AssignResult remove(KeyMapEntry (&entries)[Capacity], const CapturedIdentity* captured, const uint8_t count) {
  if (!captured || count == 0 || count > kSignatureMaxEvents) return AssignResult::Invalid;
  for (auto& entry : entries) {
    if (!sameCaptured(entry, captured, count)) continue;
    entry = KeyMapEntry{};
    return AssignResult::Removed;
  }
  return AssignResult::NotFound;
}

template <size_t Capacity>
bool resetToDefaults(KeyMapEntry (&entries)[Capacity], const DefaultBinding* defaults, const uint8_t count) {
  if ((!defaults && count != 0) || count > Capacity) return false;
  for (uint8_t i = 0; i < count; ++i) {
    if (defaults[i].identity.kind > 2 || defaults[i].actionType > kShortcutAction ||
        !validTarget(defaults[i].actionType, defaults[i].target))
      return false;
  }
  for (auto& entry : entries) entry = KeyMapEntry{};
  for (uint8_t i = 0; i < count; ++i) {
    if (assign(entries, &defaults[i].identity, 1, defaults[i].actionType, defaults[i].target) != AssignResult::Added)
      return false;
  }
  return true;
}

inline bool fromPersisted(const PersistedEntry& source, KeyMapEntry& output) {
  output = KeyMapEntry{};
  const uint8_t actionType = source.actionType == 0xFF ? kButtonAction : source.actionType;
  if (source.keyKind > 2 || source.signatureCount > kSignatureMaxEvents || actionType > kShortcutAction) return false;
  const uint8_t target = actionType == kButtonAction ? source.button : source.shortcut;
  if (!validTarget(actionType, target)) return false;

  output.keyKind = source.keyKind;
  output.keyValue = source.keyValue;
  output.actionType = actionType;
  output.button = actionType == kButtonAction ? source.button : 0xFF;
  output.shortcut = actionType == kShortcutAction ? source.shortcut : 0xFF;
  if (source.signatureCount > 0) {
    if (source.signatureCount < 2) return false;
    for (uint8_t i = 0; i < source.signatureCount; ++i) {
      const uint16_t encoded = source.signature[i];
      if ((encoded >> 8) > 2) return false;
      output.signature[i] = encoded;
    }
    if (static_cast<uint8_t>(output.signature[0] >> 8) != output.keyKind ||
        static_cast<uint8_t>(output.signature[0] & 0xFF) != output.keyValue)
      return false;
    output.signatureCount = source.signatureCount;
  }
  return entryIsValid(output);
}

inline PersistedEntry toPersisted(const KeyMapEntry& source) {
  PersistedEntry output{};
  if (!entryIsValid(source)) return output;
  output.keyKind = source.keyKind;
  output.keyValue = source.keyValue;
  output.button = source.button;
  output.actionType = source.actionType;
  output.shortcut = source.shortcut;
  output.signatureCount = source.signatureCount;
  for (uint8_t i = 0; i < source.signatureCount; ++i) output.signature[i] = source.signature[i];
  return output;
}

}  // namespace bleinput::mapping
