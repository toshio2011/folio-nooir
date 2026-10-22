#pragma once

#include <algorithm>
#include <cctype>
#include <string>
#include <string_view>

namespace FsHelpers {
inline bool hasExtension(std::string_view value, const char* extension) {
  const size_t extensionLength = std::char_traits<char>::length(extension);
  if (value.size() < extensionLength) return false;
  const size_t offset = value.size() - extensionLength;
  for (size_t index = 0; index < extensionLength; ++index) {
    if (std::tolower(static_cast<unsigned char>(value[offset + index])) !=
        std::tolower(static_cast<unsigned char>(extension[index]))) return false;
  }
  return true;
}
inline bool hasEpubExtension(std::string_view value) { return hasExtension(value, ".epub"); }
inline bool hasXtcExtension(std::string_view value) {
  return hasExtension(value, ".xtc") || hasExtension(value, ".xtch");
}
inline bool hasCbzExtension(std::string_view value) { return hasExtension(value, ".cbz"); }
inline bool hasTxtExtension(std::string_view value) { return hasExtension(value, ".txt"); }
inline bool hasMarkdownExtension(std::string_view value) { return hasExtension(value, ".md"); }
}  // namespace FsHelpers
