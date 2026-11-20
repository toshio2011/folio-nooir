#pragma once

#include <cstddef>
#include <cstdint>
#include <cstring>

namespace bleinput::profiles {

inline constexpr uint8_t kDefaultProfileId = 0;
inline constexpr uint8_t kMaxProfiles = 8;
inline constexpr uint8_t kFactoryYiserId = 0xFC;
inline constexpr uint8_t kFactoryFree2Id = 0xFD;
inline constexpr uint8_t kFactoryFree3Id = 0xFE;
inline constexpr bool isFactoryProfile(uint8_t id) {
  return id == kFactoryYiserId || id == kFactoryFree2Id || id == kFactoryFree3Id;
}
inline constexpr size_t kProfileNameBytes = 24;  // 23 visible ASCII bytes plus NUL.

struct ProfileRecord {
  uint8_t id = 0;
  char name[kProfileNameBytes] = {};
};

enum class CatalogResult : uint8_t { Added, Renamed, Removed, Full, DuplicateName, Invalid, Protected, NotFound };

inline bool sanitizeName(const char* input, char (&output)[kProfileNameBytes]) {
  output[0] = '\0';
  if (!input) return false;
  size_t out = 0;
  bool previousSpace = true;
  for (size_t i = 0; input[i] && out + 1 < kProfileNameBytes; ++i) {
    const unsigned char c = static_cast<unsigned char>(input[i]);
    const bool allowed = (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') ||
                         (c >= '0' && c <= '9') || c == '_' || c == '-';
    if (allowed) {
      output[out++] = static_cast<char>(c);
      previousSpace = false;
    } else if (c == ' ' && !previousSpace) {
      output[out++] = ' ';
      previousSpace = true;
    }
  }
  if (out > 0 && output[out - 1] == ' ') --out;
  output[out] = '\0';
  return out > 0;
}

inline int8_t find(const ProfileRecord* records, const uint8_t count, const uint8_t id) {
  if (!records || count > kMaxProfiles) return -1;
  for (uint8_t i = 0; i < count; ++i) if (records[i].id == id) return static_cast<int8_t>(i);
  return -1;
}

inline bool nameExists(const ProfileRecord* records, const uint8_t count, const char* name,
                       const int8_t exceptIndex = -1) {
  if (!records || count > kMaxProfiles || !name) return true;
  for (uint8_t i = 0; i < count; ++i) {
    if (static_cast<int8_t>(i) == exceptIndex) continue;
    if (std::strcmp(records[i].name, name) == 0) return true;
  }
  return false;
}

inline CatalogResult add(ProfileRecord* records, uint8_t& count, const uint8_t id, const char* name) {
  if (!records || count > kMaxProfiles || id == kDefaultProfileId) return CatalogResult::Invalid;
  char safe[kProfileNameBytes] = {};
  if (!sanitizeName(name, safe)) return CatalogResult::Invalid;
  if (count >= kMaxProfiles) return CatalogResult::Full;
  if (find(records, count, id) >= 0 || nameExists(records, count, safe)) return CatalogResult::DuplicateName;
  records[count].id = id;
  std::memcpy(records[count].name, safe, sizeof(safe));
  ++count;
  return CatalogResult::Added;
}

inline CatalogResult rename(ProfileRecord* records, const uint8_t count, const uint8_t id, const char* name) {
  const int8_t index = find(records, count, id);
  if (index < 0) return CatalogResult::NotFound;
  if (id == kDefaultProfileId) return CatalogResult::Protected;
  char safe[kProfileNameBytes] = {};
  if (!sanitizeName(name, safe)) return CatalogResult::Invalid;
  if (nameExists(records, count, safe, index)) return CatalogResult::DuplicateName;
  std::memcpy(records[index].name, safe, sizeof(safe));
  return CatalogResult::Renamed;
}

inline CatalogResult remove(ProfileRecord* records, uint8_t& count, const uint8_t id,
                            const uint8_t activeId) {
  const int8_t index = find(records, count, id);
  if (index < 0) return CatalogResult::NotFound;
  if (id == kDefaultProfileId || id == activeId) return CatalogResult::Protected;
  for (uint8_t i = static_cast<uint8_t>(index + 1); i < count; ++i) records[i - 1] = records[i];
  records[--count] = ProfileRecord{};
  return CatalogResult::Removed;
}

inline uint8_t nextId(const ProfileRecord* records, const uint8_t count, const uint8_t seed) {
  if (!records || count > kMaxProfiles) return 0xFF;
  uint8_t candidate = seed == kDefaultProfileId ? 1 : seed;
  for (uint16_t tries = 0; tries < 255; ++tries) {
    if (candidate != 0xFF && !isFactoryProfile(candidate) && find(records, count, candidate) < 0) return candidate;
    candidate = candidate == 0xFF ? 1 : static_cast<uint8_t>(candidate + 1);
  }
  return 0xFF;
}

}  // namespace bleinput::profiles
