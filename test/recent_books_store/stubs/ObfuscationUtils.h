#pragma once

#include <string>

namespace obfuscation {
inline std::string deobfuscateFromBase64(const char*, bool* ok) {
  if (ok) *ok = false;
  return {};
}
inline std::string obfuscateToBase64(const char*) { return {}; }
}  // namespace obfuscation
