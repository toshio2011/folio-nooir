#pragma once

#include <array>
#include <cstddef>
#include <cstdint>

namespace persistable {

enum class SettingsCandidateResult : uint8_t {
  NotChecked,
  Missing,
  ReadError,
  TooLarge,
  Malformed,
  Incompatible,
  Valid,
};

struct SettingsLoadReport {
  static constexpr size_t NO_SELECTION = 3;
  std::array<SettingsCandidateResult, 3> candidates{};
  size_t selected = NO_SELECTION;
  bool anyExisting = false;

  constexpr bool hasReadError() const {
    for (const auto candidate : candidates) {
      if (candidate == SettingsCandidateResult::ReadError || candidate == SettingsCandidateResult::TooLarge) return true;
    }
    return false;
  }

  constexpr bool hasIncompatibleCandidate() const {
    for (const auto candidate : candidates) {
      if (candidate == SettingsCandidateResult::Incompatible) return true;
    }
    return false;
  }

  // Automatic reset is allowed only when at least one settings candidate was
  // completely read and confirmed malformed, and every other candidate was
  // either missing or malformed. Unknown valid formats and I/O errors block it.
  constexpr bool confirmedCorruptionOnly() const {
    if (!anyExisting || selected != NO_SELECTION || hasReadError() || hasIncompatibleCandidate()) return false;
    bool sawMalformed = false;
    for (const auto candidate : candidates) {
      if (candidate == SettingsCandidateResult::Malformed) sawMalformed = true;
      else if (candidate != SettingsCandidateResult::Missing) return false;
    }
    return sawMalformed;
  }
};

constexpr const char* settingsCandidateResultName(const SettingsCandidateResult result) {
  switch (result) {
    case SettingsCandidateResult::NotChecked:
      return "not_checked";
    case SettingsCandidateResult::Missing:
      return "missing";
    case SettingsCandidateResult::ReadError:
      return "read_error";
    case SettingsCandidateResult::TooLarge:
      return "too_large";
    case SettingsCandidateResult::Malformed:
      return "malformed";
    case SettingsCandidateResult::Incompatible:
      return "incompatible";
    case SettingsCandidateResult::Valid:
      return "valid";
  }
  return "unknown";
}

constexpr bool settingsRecoveryRequired(const bool loadSucceeded, const bool anyCandidateExists) {
  return !loadSucceeded && anyCandidateExists;
}

}  // namespace persistable
