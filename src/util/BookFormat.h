#pragma once

#include <cstddef>
#include <cstdint>
#include <string_view>

namespace BookFormat {

// These are the book formats currently accepted by the Folio library and
// reader. Keep this resolver independent of storage so a badge never needs an
// additional SD probe or metadata read.
enum class Kind : uint8_t {
  Unknown = 0,
  Epub,
  Cbz,
  Xtc,
  Xtch,
  Txt,
  Markdown,
};

struct Entry {
  std::string_view extension;
  Kind kind;
  const char* label;
};

inline constexpr Entry SUPPORTED[] = {
    {"epub", Kind::Epub, "EPUB"}, {"cbz", Kind::Cbz, "CBZ"},
    {"xtc", Kind::Xtc, "XTC"},   {"xtch", Kind::Xtch, "XTCH"},
    {"txt", Kind::Txt, "TXT"},   {"md", Kind::Markdown, "MD"},
};

inline bool asciiEqualsIgnoreCase(const std::string_view left, const std::string_view right) noexcept {
  if (left.size() != right.size()) return false;
  for (std::size_t i = 0; i < left.size(); ++i) {
    const auto lower = [](const char value) {
      return value >= 'A' && value <= 'Z' ? static_cast<char>(value + ('a' - 'A')) : value;
    };
    if (lower(left[i]) != lower(right[i])) return false;
  }
  return true;
}

inline Kind kindForPath(const std::string_view path) noexcept {
  const std::size_t separator = path.find_last_of("/\\");
  const std::size_t nameStart = separator == std::string_view::npos ? 0 : separator + 1;
  const std::size_t dot = path.find_last_of('.');
  if (dot == std::string_view::npos || dot <= nameStart || dot + 1 >= path.size()) return Kind::Unknown;

  const std::string_view extension = path.substr(dot + 1);
  for (const auto& entry : SUPPORTED) {
    if (asciiEqualsIgnoreCase(extension, entry.extension)) return entry.kind;
  }
  return Kind::Unknown;
}

inline Kind kindForPath(const char* path) noexcept {
  return path == nullptr ? Kind::Unknown : kindForPath(std::string_view(path));
}

inline const char* labelForKind(const Kind kind) noexcept {
  for (const auto& entry : SUPPORTED) {
    if (entry.kind == kind) return entry.label;
  }
  return nullptr;
}

inline const char* labelForPath(const std::string_view path) noexcept {
  return labelForKind(kindForPath(path));
}

inline const char* labelForPath(const char* path) noexcept {
  return labelForKind(kindForPath(path));
}

}  // namespace BookFormat
