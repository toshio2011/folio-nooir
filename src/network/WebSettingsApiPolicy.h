#pragma once

#include <cstddef>
#include <cstring>

#include "SettingsGroups.h"

namespace WebSettingsApiPolicy {
constexpr size_t kMaxPageItems = 6;
constexpr size_t kMaxOffset = 32;  // One past the largest pre-deferral group (31 Reader settings).
constexpr size_t kMaxGroupRequestBytes = 2048;
constexpr size_t kMaxGroupChanges = 32;  // Largest current stable keyed group is below this bound.

struct PageWindow {
  size_t offset;
  size_t limit;
  size_t end;
};

inline bool validGroup(const SettingsGroupId group) { return group != SettingsGroupId::Unknown; }

inline bool parseGroup(const char* rawGroup, SettingsGroupId& group) {
  group = parseSettingsGroupId(rawGroup);
  return validGroup(group);
}

inline bool parseUnsignedDecimal(const char* raw, const size_t maximum, size_t& value) {
  if (!raw || *raw == '\0') return false;
  size_t digits = 0;
  size_t parsed = 0;
  for (const unsigned char* cursor = reinterpret_cast<const unsigned char*>(raw); *cursor; ++cursor) {
    if (++digits > 3) return false;
    if (*cursor < '0' || *cursor > '9') return false;
    const size_t digit = static_cast<size_t>(*cursor - '0');
    if (parsed > maximum / 10 || (parsed == maximum / 10 && digit > maximum % 10)) return false;
    parsed = parsed * 10 + digit;
  }
  value = parsed;
  return true;
}

inline bool makePageWindow(const char* rawOffset, const char* rawLimit, PageWindow& page) {
  size_t offset = 0;
  size_t limit = kMaxPageItems;
  if (rawOffset && !parseUnsignedDecimal(rawOffset, kMaxOffset, offset)) return false;
  if (rawLimit && !parseUnsignedDecimal(rawLimit, kMaxPageItems, limit)) return false;
  if (limit == 0) return false;
  page.offset = offset;
  page.limit = limit;
  page.end = offset + limit;
  return true;
}

inline bool makePageWindow(const long rawOffset, const long rawLimit, PageWindow& page) {
  if (rawOffset < 0 || static_cast<unsigned long>(rawOffset) > kMaxOffset || rawLimit < 1 ||
      static_cast<unsigned long>(rawLimit) > kMaxPageItems) {
    return false;
  }
  page.offset = static_cast<size_t>(rawOffset);
  page.limit = static_cast<size_t>(rawLimit);
  page.end = page.offset + page.limit;  // Both inputs are capped above, so this cannot overflow.
  return true;
}

inline bool validGroupRequestBytes(const long contentLength) {
  return contentLength > 0 && static_cast<unsigned long>(contentLength) <= kMaxGroupRequestBytes;
}

inline bool validChangeCount(const size_t count) { return count > 0 && count <= kMaxGroupChanges; }

inline bool validSettingsPostShape(const SettingsGroupId group, const long contentLength, const size_t changeCount) {
  return validGroup(group) && validGroupRequestBytes(contentLength) && validChangeCount(changeCount);
}

inline bool isWebWritableKey(const char* key, const SettingsGroupId group) {
  if (!key || !validGroup(group)) return false;
  // These persisted composer fields remain hidden for the 1.6.6 RC even though
  // their implementation and group classification are retained for later.
  if (std::strcmp(key, "sleepBackgroundElement") == 0 || std::strcmp(key, "sleepOverlayAElement") == 0 ||
      std::strcmp(key, "sleepOverlayBElement") == 0) {
    return false;
  }
  return settingsGroupIdForSettingKey(key) == group;
}

template <typename Iterator, typename KeyGetter>
inline bool allKeysBelongToGroup(Iterator begin, Iterator end, const SettingsGroupId group, KeyGetter keyGetter) {
  if (!validGroup(group) || begin == end) return false;
  for (; begin != end; ++begin) {
    if (!isWebWritableKey(keyGetter(*begin), group)) return false;
  }
  return true;
}
}  // namespace WebSettingsApiPolicy
