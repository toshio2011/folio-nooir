#pragma once

#include <cstddef>
#include <string>
#include <utility>
#include <vector>

// Resolve a persisted dictionary folder value against the usable installed
// names. Exact names win; a legacy truncated value is accepted only when it
// identifies one distinct installed name. The helper intentionally does not
// perform case folding or fuzzy matching.
inline bool resolveUniqueDictionaryName(const std::vector<std::string>& installedNames, const char* requestedName,
                                        std::string& resolvedNameOut) {
  resolvedNameOut.clear();
  if (!requestedName || requestedName[0] == '\0') return false;

  for (const auto& installedName : installedNames) {
    if (installedName == requestedName) {
      resolvedNameOut = installedName;
      return true;
    }
  }

  const size_t prefixLength = std::char_traits<char>::length(requestedName);
  std::string candidate;
  for (const auto& installedName : installedNames) {
    if (installedName.size() < prefixLength || installedName.compare(0, prefixLength, requestedName) != 0) continue;
    if (candidate.empty()) {
      candidate = installedName;
    } else if (candidate != installedName) {
      resolvedNameOut.clear();
      return false;
    }
  }

  if (candidate.empty()) return false;
  resolvedNameOut = std::move(candidate);
  return true;
}
