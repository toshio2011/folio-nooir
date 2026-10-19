#pragma once

#include <cstdint>

namespace persistable {

enum class SettingsCandidateResult : uint8_t {
  NotChecked,
  Missing,
  Unreadable,
  Malformed,
  Incompatible,
  Valid,
};

constexpr const char* settingsCandidateResultName(const SettingsCandidateResult result) {
  switch (result) {
    case SettingsCandidateResult::NotChecked:
      return "not_checked";
    case SettingsCandidateResult::Missing:
      return "missing";
    case SettingsCandidateResult::Unreadable:
      return "unreadable";
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
