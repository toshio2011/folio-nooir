#pragma once

#include <cstdint>
#include <functional>
#include <string>
#include <vector>

class Epub;

// A small, font-independent page map for the optional Stable Pages reader mode.
// It deliberately counts visible EPUB text only: no CSS layout, image decode, or
// whole-book RAM buffer is involved. The cache is independent from book.bin and
// the rendered section caches.
class StablePageCache final {
 public:
  static constexpr uint32_t MAGIC = 0x53504731;  // "SPG1"
  // Version 2 adds the reference page at which each spine item starts.  This
  // lets imported CrossInk maps preserve page boundaries that span chapters.
  static constexpr uint16_t VERSION = 2;
  static constexpr uint16_t DEFAULT_CHARS_PER_PAGE = 1500;

  struct Entry {
    uint32_t textUnits = 0;
    uint32_t firstPage = 0;
    uint32_t cumulativePages = 0;
  };

  struct Index {
    uint16_t charsPerPage = DEFAULT_CHARS_PER_PAGE;
    uint32_t sourceSize = 0;
    uint64_t sourceFingerprint = 0;
    uint32_t totalPages = 0;
    std::vector<Entry> entries;

    bool valid() const { return !entries.empty() && totalPages > 0; }
  };

  enum class BuildResult : uint8_t { Built, AlreadyCached, Cancelled, Failed };

  // Return false when the cache is absent, stale, truncated, or from a different
  // source/algorithm version.
  static bool load(const Epub& epub, Index& out);

  // Build to a temporary file and atomically rename only after the complete map
  // is written. The callback is called once per spine item and may return false
  // to stop without leaving a partial stable cache behind.
  using ProgressCallback = std::function<bool(uint16_t processed, uint16_t total, const std::string& href,
                                              uint32_t totalPagesSoFar)>;
  static BuildResult build(const Epub& epub, Index& out, const ProgressCallback& progress = {});

  // Convert the current rendered chapter/page position to a stable 1-based page.
  // The result is deliberately approximate and remains unchanged when font/layout
  // settings change.
  static uint32_t pageFor(const Index& index, int spineIndex, int currentPage, int chapterPageCount);

  static const char* getFileName() { return "stable_pages.bin"; }
};
