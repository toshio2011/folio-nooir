#pragma once

#include <algorithm>
#include <cctype>
#include <string>

// Small, allocation-bounded shelf preview helper. The complete synopsis stays
// in the metadata cache; this only limits what the feature panel wraps/draws.
namespace SynopsisPreview {

inline std::string firstWords(const std::string& source, const size_t maxWords = 30) {
  if (source.empty() || maxWords == 0) return {};

  std::string result;
  result.reserve(std::min(source.size(), maxWords * 12U + 3U));
  size_t words = 0;
  size_t index = 0;
  bool inWord = false;
  bool pendingSpace = false;
  bool truncated = false;

  while (index < source.size()) {
    // Metadata is normally already plain text, but skip a stray HTML tag so
    // malformed/legacy caches cannot spend shelf time drawing markup.
    if (source[index] == '<') {
      const size_t close = source.find('>', index + 1);
      if (close != std::string::npos) {
        if (!result.empty()) pendingSpace = true;
        inWord = false;
        index = close + 1;
        continue;
      }
    }
    if (source.compare(index, 6, "&nbsp;") == 0) {
      if (!result.empty()) pendingSpace = true;
      inWord = false;
      index += 6;
      continue;
    }

    const unsigned char byte = static_cast<unsigned char>(source[index]);
    if (std::isspace(byte)) {
      if (!result.empty()) pendingSpace = true;
      inWord = false;
      ++index;
      continue;
    }

    if (!inWord) {
      if (words >= maxWords) {
        truncated = true;
        break;
      }
      ++words;
      inWord = true;
    }
    if (pendingSpace && !result.empty() && result.back() != ' ') result.push_back(' ');
    pendingSpace = false;
    result.push_back(source[index++]);
  }

  while (!result.empty() && result.back() == ' ') result.pop_back();
  if (truncated && !result.empty()) result += "...";
  return result;
}

}  // namespace SynopsisPreview
