#include "StablePageCache.h"

#include <Epub.h>
#include <HalStorage.h>
#include <StreamingJsonParser.h>
#include <Logging.h>

#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <cstring>
#include <string_view>
#include <utility>

namespace {

constexpr size_t STREAM_BUFFER_SIZE = 512;
// x-locations is metadata, not rendered XHTML. A larger transient buffer
// reduces SD/ZIP callbacks without affecting the normal low-memory scan path.
constexpr size_t CROSSINK_STREAM_BUFFER_SIZE = 2048;
// Keep malformed or pathological EPUB metadata from reserving an unbounded
// vector on the X3. Normal books are far below this limit.
constexpr size_t MAX_CROSSINK_SPINE_ROWS = 2048;
constexpr uint32_t MAX_TEXT_UNITS = 0x7FFFFFFFu;

struct CacheHeader {
  uint32_t magic = StablePageCache::MAGIC;
  uint16_t version = StablePageCache::VERSION;
  uint16_t charsPerPage = StablePageCache::DEFAULT_CHARS_PER_PAGE;
  uint32_t sourceSize = 0;
  uint64_t sourceFingerprint = 0;
  uint16_t spineCount = 0;
  uint16_t reserved = 0;
  uint32_t totalPages = 0;
};

// CrossInk stores its font-independent page map as a small streaming JSON
// file.  Keep only one spine row at a time; the complete JSON is never copied
// into RAM (important on the X3/X4 boards).
class CrossInkLocationsParser final {
 public:
  struct Row {
    std::string href;
    uint32_t characterCount = 0;
    uint32_t startPage = 0;
    uint32_t endPage = 0;
    bool hasHref = false;
    bool hasPages = false;
  };

  explicit CrossInkLocationsParser(const size_t maxRows)
      : parser({this, onKey, onString, onNumber, onBool, onNull, onObjectStart, onObjectEnd, onArrayStart, onArrayEnd}),
        maxRows(maxRows) {
    rows.reserve(maxRows);
  }

  void feed(const char* data, const size_t length) { parser.feed(data, length); }
  bool valid() const {
    return !parser.hasError() && !overflowed && formatValid && version == 1 && charsPerReferencePage > 0 &&
           totalReferencePages > 0 && !rows.empty();
  }
  uint32_t charsPerReferencePage = 0;
  uint32_t totalReferencePages = 0;
  std::vector<Row> rows;

 private:
  enum class Field : uint8_t { None, Format, Version, CharsPerPage, TotalPages, Href, CharacterCount, StartPage,
                                EndPage };

  StreamingJsonParser parser;
  Field field = Field::None;
  bool formatValid = false;
  uint32_t version = 0;
  uint8_t depth = 0;
  uint8_t spineArrayDepth = 0;
  uint8_t rowDepth = 0;
  size_t maxRows = 0;
  bool waitingForSpineArray = false;
  bool inRow = false;
  bool overflowed = false;
  Row row;

  static CrossInkLocationsParser* self(void* ctx) { return static_cast<CrossInkLocationsParser*>(ctx); }

  static void onKey(void* ctx, const char* key, const size_t len) {
    auto* self = CrossInkLocationsParser::self(ctx);
    const std::string_view name(key, len);
    if (name == "spine") {
      self->waitingForSpineArray = true;
      self->field = Field::None;
    } else if (name == "format") {
      self->field = Field::Format;
    } else if (name == "version") {
      self->field = Field::Version;
    } else if (name == "charactersPerReferencePage") {
      self->field = Field::CharsPerPage;
    } else if (name == "totalReferencePages") {
      self->field = Field::TotalPages;
    } else if (self->inRow) {
      if (name == "href") self->field = Field::Href;
      else if (name == "characterCount") self->field = Field::CharacterCount;
      else if (name == "startReferencePage") self->field = Field::StartPage;
      else if (name == "endReferencePage") self->field = Field::EndPage;
      else self->field = Field::None;
    } else {
      self->field = Field::None;
    }
  }

  static void onString(void* ctx, const char* value, const size_t len) {
    auto* self = CrossInkLocationsParser::self(ctx);
    if (self->field == Field::Format) {
      self->formatValid = std::string_view(value, len) == "x-locations";
    } else if (self->inRow && self->field == Field::Href) {
      self->row.href.assign(value, len);
      self->row.hasHref = !self->row.href.empty();
    }
    self->field = Field::None;
  }

  static void onNumber(void* ctx, const char* value, const size_t len) {
    auto* self = CrossInkLocationsParser::self(ctx);
    char buffer[32] = {};
    const size_t copyLen = std::min(len, sizeof(buffer) - 1);
    memcpy(buffer, value, copyLen);
    const uint32_t number = static_cast<uint32_t>(strtoul(buffer, nullptr, 10));
    switch (self->field) {
      case Field::Version: self->version = number; break;
      case Field::CharsPerPage: self->charsPerReferencePage = number; break;
      case Field::TotalPages: self->totalReferencePages = number; break;
      case Field::CharacterCount:
        if (self->inRow) self->row.characterCount = number;
        break;
      case Field::StartPage:
        if (self->inRow) {
          self->row.startPage = number;
          self->row.hasPages = self->row.startPage > 0 && self->row.endPage >= self->row.startPage;
        }
        break;
      case Field::EndPage:
        if (self->inRow) {
          self->row.endPage = number;
          self->row.hasPages = self->row.startPage > 0 && self->row.endPage >= self->row.startPage;
        }
        break;
      default: break;
    }
    self->field = Field::None;
  }

  static void onBool(void* /*ctx*/, bool /*value*/) {}
  static void onNull(void* /*ctx*/) {}

  static void onObjectStart(void* ctx) {
    auto* self = CrossInkLocationsParser::self(ctx);
    ++self->depth;
    if (self->spineArrayDepth != 0 && self->depth == self->spineArrayDepth + 1) {
      self->inRow = true;
      self->rowDepth = self->depth;
      self->row = {};
    }
  }

  static void onObjectEnd(void* ctx) {
    auto* self = CrossInkLocationsParser::self(ctx);
    if (self->inRow && self->depth == self->rowDepth) {
      if (self->row.hasHref && self->row.hasPages) {
        if (self->rows.size() < self->maxRows) self->rows.push_back(std::move(self->row));
        else self->overflowed = true;
      }
      self->row = {};
      self->inRow = false;
      self->rowDepth = 0;
    }
    if (self->depth > 0) --self->depth;
  }

  static void onArrayStart(void* ctx) {
    auto* self = CrossInkLocationsParser::self(ctx);
    ++self->depth;
    if (self->waitingForSpineArray) {
      self->spineArrayDepth = self->depth;
      self->waitingForSpineArray = false;
    }
  }

  static void onArrayEnd(void* ctx) {
    auto* self = CrossInkLocationsParser::self(ctx);
    if (self->spineArrayDepth == self->depth) self->spineArrayDepth = 0;
    if (self->depth > 0) --self->depth;
  }
};

class JsonParserSink final : public Print {
 public:
  explicit JsonParserSink(CrossInkLocationsParser& parser) : parser(parser) {}
  size_t write(const uint8_t* data, const size_t length) override {
    parser.feed(reinterpret_cast<const char*>(data), length);
    return length;
  }
  size_t write(const uint8_t value) override {
    parser.feed(reinterpret_cast<const char*>(&value), 1);
    return 1;
  }

 private:
  CrossInkLocationsParser& parser;
};

bool sameHref(const std::string& spineHref, const std::string& locationHref) {
  if (spineHref == locationHref) return true;
  if (locationHref.size() >= spineHref.size()) return false;
  const size_t suffixStart = spineHref.size() - locationHref.size();
  return spineHref.compare(suffixStart, locationHref.size(), locationHref) == 0 &&
         suffixStart > 0 && spineHref[suffixStart - 1] == '/';
}

bool loadCrossInkLocations(const Epub& epub, StablePageCache::Index& out) {
  const int spineCount = epub.getSpineItemsCount();
  if (spineCount <= 0 || static_cast<size_t>(spineCount) > MAX_CROSSINK_SPINE_ROWS) return false;

  CrossInkLocationsParser parser(static_cast<size_t>(spineCount));
  JsonParserSink sink(parser);
  if (!epub.readItemContentsToStream("META-INF/x-locations.json", sink, CROSSINK_STREAM_BUFFER_SIZE) ||
      !parser.valid()) {
    return false;
  }

  if (parser.rows.size() != static_cast<size_t>(spineCount)) return false;

  StablePageCache::Index imported;
  imported.charsPerPage = static_cast<uint16_t>(std::min<uint32_t>(UINT16_MAX, parser.charsPerReferencePage));
  imported.totalPages = parser.totalReferencePages;
  imported.entries.reserve(parser.rows.size());
  for (int i = 0; i < spineCount; ++i) {
    const auto& row = parser.rows[static_cast<size_t>(i)];
    if (!row.hasHref || !row.hasPages || row.startPage == 0 || row.endPage < row.startPage ||
        !sameHref(epub.getSpineItem(i).href, row.href)) {
      return false;
    }
    imported.entries.push_back({row.characterCount, row.startPage, row.endPage});
  }
  if (!imported.valid() || imported.entries.back().cumulativePages != imported.totalPages) return false;
  out = std::move(imported);
  return true;
}

uint64_t fnv1a(const std::string& value) {
  uint64_t hash = 1469598103934665603ULL;
  for (const unsigned char c : value) {
    hash ^= c;
    hash *= 1099511628211ULL;
  }
  return hash;
}

uint32_t sourceSize(const Epub& epub) {
  HalFile file;
  if (!Storage.openFileForRead("SPG", epub.getPath(), file)) return 0;
  const uint64_t size = file.fileSize64();
  return size > UINT32_MAX ? UINT32_MAX : static_cast<uint32_t>(size);
}

uint64_t sourceFingerprint(const Epub& epub, const uint32_t size) {
  uint64_t hash = fnv1a(epub.getPath());
  hash ^= static_cast<uint64_t>(size) * 1099511628211ULL;
  hash ^= static_cast<uint64_t>(epub.getBookSize()) * 0x9E3779B97F4A7C15ULL;
  hash ^= static_cast<uint64_t>(epub.getSpineItemsCount()) << 32;
  return hash;
}

class VisibleTextCounter final : public Print {
 public:
  explicit VisibleTextCounter(std::function<bool()> pulse = {}) : pulse_(std::move(pulse)) {}

  size_t write(const uint8_t* data, const size_t length) override {
    for (size_t i = 0; i < length; ++i) {
      consume(data[i]);
      if (cancelled_) return 0;
    }
    return length;
  }

  size_t write(const uint8_t value) override {
    consume(value);
    return cancelled_ ? 0 : 1;
  }

  uint32_t count() const { return units_; }
  bool cancelled() const { return cancelled_; }

 private:
  bool inTag_ = false;
  bool inComment_ = false;
  bool inScript_ = false;
  bool inStyle_ = false;
  bool tagClosing_ = false;
  char tagName_[16] = {};
  uint8_t tagNameLength_ = 0;
  char commentTail_[2] = {};
  uint8_t commentTailLength_ = 0;
  uint32_t units_ = 0;
  std::function<bool()> pulse_;
  uint32_t bytesSincePulse_ = 0;
  bool cancelled_ = false;

  void beginTag() {
    inTag_ = true;
    tagClosing_ = false;
    tagNameLength_ = 0;
  }

  void finishTag() {
    tagName_[tagNameLength_] = '\0';
    if (tagNameLength_ > 0) {
      const bool closing = tagClosing_;
      const bool script = std::strcmp(tagName_, "script") == 0;
      const bool style = std::strcmp(tagName_, "style") == 0;
      if (script) inScript_ = !closing;
      if (style) inStyle_ = !closing;
    }
    inTag_ = false;
  }

  void consume(const uint8_t byte) {
    if (pulse_ && ++bytesSincePulse_ >= 32768) {
      bytesSincePulse_ = 0;
      if (!pulse_()) {
        cancelled_ = true;
        return;
      }
    }

    if (inComment_) {
      commentTail_[commentTailLength_++ % 2] = static_cast<char>(byte);
      if (commentTailLength_ >= 2 && commentTail_[0] == '-' && commentTail_[1] == '-' && byte == '>') {
        inComment_ = false;
        commentTailLength_ = 0;
      }
      return;
    }

    if (inTag_) {
      if (byte == '>') {
        finishTag();
      } else if (tagNameLength_ == 0 && byte == '/') {
        tagClosing_ = true;
      } else if (tagNameLength_ < sizeof(tagName_) - 1 && std::isalpha(byte)) {
        tagName_[tagNameLength_++] = static_cast<char>(std::tolower(byte));
      }
      return;
    }

    if (byte == '<') {
      beginTag();
      return;
    }
    if (inScript_ || inStyle_) return;
    // Count UTF-8 codepoint starts. ASCII text and whitespace count one unit;
    // continuation bytes are ignored so non-ASCII text is not over-counted.
    if ((byte & 0xC0u) == 0x80u) return;
    if (units_ < MAX_TEXT_UNITS) ++units_;
  }
};

bool writeAll(HalFile& file, const void* data, const size_t size) {
  return file.write(data, size) == size;
}

bool readAll(HalFile& file, void* data, const size_t size) {
  return file.read(data, size) == size;
}

}  // namespace

bool StablePageCache::load(const Epub& epub, Index& out) {
  out = {};
  const std::string path = epub.getCachePath() + "/" + getFileName();
  HalFile file;
  if (!Storage.openFileForRead("SPG", path, file)) return false;

  CacheHeader header;
  const uint32_t size = sourceSize(epub);
  if (!readAll(file, &header, sizeof(header)) || header.magic != MAGIC || header.version != VERSION ||
      header.charsPerPage == 0 || header.spineCount == 0 || header.totalPages == 0 ||
      header.sourceSize != size || header.sourceFingerprint != sourceFingerprint(epub, size) ||
      header.spineCount != epub.getSpineItemsCount()) {
    return false;
  }

  out.charsPerPage = header.charsPerPage;
  out.sourceSize = header.sourceSize;
  out.sourceFingerprint = header.sourceFingerprint;
  out.totalPages = header.totalPages;
  out.entries.reserve(header.spineCount);
  uint32_t previous = 0;
  for (uint16_t i = 0; i < header.spineCount; ++i) {
    Entry entry;
    if (!readAll(file, &entry, sizeof(entry)) || entry.cumulativePages < previous ||
        (entry.firstPage != 0 && entry.firstPage > entry.cumulativePages)) {
      out = {};
      return false;
    }
    previous = entry.cumulativePages;
    out.entries.push_back(entry);
  }
  return out.valid() && out.entries.back().cumulativePages == out.totalPages;
}

StablePageCache::BuildResult StablePageCache::build(const Epub& epub, Index& out,
                                                    const ProgressCallback& progress) {
  out = {};
  const int spineCount = epub.getSpineItemsCount();
  if (spineCount <= 0 || spineCount > UINT16_MAX) return BuildResult::Failed;

  Index cached;
  if (load(epub, cached)) {
    out = std::move(cached);
    return BuildResult::AlreadyCached;
  }

  // CrossInk's optimizer already did the expensive whole-book pass. Import
  // its compact reference-page map instead of inflating every XHTML chapter a
  // second time. The imported map is written to our normal cache below, so
  // subsequent opens use the same fast local path as a Nooir-built map.
  Index crossInk;
  if (loadCrossInkLocations(epub, crossInk)) {
    crossInk.sourceSize = sourceSize(epub);
    crossInk.sourceFingerprint = sourceFingerprint(epub, crossInk.sourceSize);
    const std::string path = epub.getCachePath() + "/" + getFileName();
    const std::string tempPath = path + ".tmp";
    Storage.remove(tempPath.c_str());
    HalFile file;
    if (Storage.openFileForWrite("SPG", tempPath, file)) {
      CacheHeader header;
      header.charsPerPage = crossInk.charsPerPage;
      header.sourceSize = crossInk.sourceSize;
      header.sourceFingerprint = crossInk.sourceFingerprint;
      header.spineCount = static_cast<uint16_t>(crossInk.entries.size());
      header.totalPages = crossInk.totalPages;
      bool ok = writeAll(file, &header, sizeof(header));
      for (const Entry& entry : crossInk.entries) ok = ok && writeAll(file, &entry, sizeof(entry));
      file.flush();
      file.close();
      if (ok && Storage.rename(tempPath.c_str(), path.c_str())) {
        out = std::move(crossInk);
        LOG_INF("SPG", "Imported CrossInk page map: %s (%lu pages)", epub.getPath().c_str(),
                static_cast<unsigned long>(out.totalPages));
        return BuildResult::Built;
      }
      Storage.remove(tempPath.c_str());
    }
    LOG_DBG("SPG", "CrossInk map found but could not be cached; falling back to local scan");
  }

  Index result;
  result.charsPerPage = DEFAULT_CHARS_PER_PAGE;
  result.sourceSize = sourceSize(epub);
  result.sourceFingerprint = sourceFingerprint(epub, result.sourceSize);
  result.entries.reserve(static_cast<size_t>(spineCount));

  uint32_t cumulativePages = 0;
  for (int i = 0; i < spineCount; ++i) {
    const auto spine = epub.getSpineItem(i);
    unsigned long lastPulseMs = millis();
    VisibleTextCounter counter([&]() {
      // Check cancellation frequently, but redraw the e-ink popup at most once
      // every 1.5 seconds while a single unusually large chapter is inflating.
      if (progress && millis() - lastPulseMs >= 1500) {
        lastPulseMs = millis();
        return progress(static_cast<uint16_t>(i), static_cast<uint16_t>(spineCount), spine.href, cumulativePages);
      }
      return true;
    });
    if (!epub.readItemContentsToStream(spine.href, counter, STREAM_BUFFER_SIZE, true)) {
      LOG_ERR("SPG", "Could not read spine item %d: %s", i, spine.href.c_str());
      return BuildResult::Failed;
    }
    if (counter.cancelled()) return BuildResult::Cancelled;
    const uint32_t textUnits = counter.count();
    const uint32_t chapterPages = textUnits == 0 ? 0 : (textUnits + result.charsPerPage - 1) / result.charsPerPage;
    const uint32_t firstPage = chapterPages == 0 ? cumulativePages : cumulativePages + 1;
    cumulativePages = std::min<uint32_t>(UINT32_MAX - chapterPages, cumulativePages) + chapterPages;
    result.entries.push_back({textUnits, firstPage, cumulativePages});

    if (progress && !progress(static_cast<uint16_t>(i + 1), static_cast<uint16_t>(spineCount), spine.href,
                              cumulativePages)) {
      return BuildResult::Cancelled;
    }
  }

  result.totalPages = std::max<uint32_t>(1, cumulativePages);
  if (cumulativePages == 0 && !result.entries.empty()) {
    result.entries.back().firstPage = 1;
    result.entries.back().cumulativePages = 1;
  }
  const std::string path = epub.getCachePath() + "/" + getFileName();
  const std::string tempPath = path + ".tmp";
  Storage.remove(tempPath.c_str());
  HalFile file;
  if (!Storage.openFileForWrite("SPG", tempPath, file)) return BuildResult::Failed;

  CacheHeader header;
  header.charsPerPage = result.charsPerPage;
  header.sourceSize = result.sourceSize;
  header.sourceFingerprint = result.sourceFingerprint;
  header.spineCount = static_cast<uint16_t>(spineCount);
  header.totalPages = result.totalPages;
  bool ok = writeAll(file, &header, sizeof(header));
  for (const Entry& entry : result.entries) ok = ok && writeAll(file, &entry, sizeof(entry));
  file.flush();
  if (!ok) {
    file.close();
    Storage.remove(tempPath.c_str());
    return BuildResult::Failed;
  }
  file.close();
  if (!Storage.rename(tempPath.c_str(), path.c_str())) {
    Storage.remove(tempPath.c_str());
    return BuildResult::Failed;
  }

  out = std::move(result);
  LOG_INF("SPG", "Stable page cache ready: %s (%lu pages)", epub.getPath().c_str(),
          static_cast<unsigned long>(out.totalPages));
  return BuildResult::Built;
}

uint32_t StablePageCache::pageFor(const Index& index, const int spineIndex, const int currentPage,
                                  const int chapterPageCount) {
  if (!index.valid()) return 0;
  if (spineIndex < 0) return 1;
  if (spineIndex >= static_cast<int>(index.entries.size())) return index.totalPages;

  const Entry& entry = index.entries[spineIndex];
  const uint32_t before = spineIndex == 0 ? 0 : index.entries[spineIndex - 1].cumulativePages;
  const uint32_t firstPage = entry.firstPage == 0 ? before + 1 : entry.firstPage;
  const uint32_t chapterPages = entry.cumulativePages >= firstPage ? entry.cumulativePages - firstPage + 1 : 0;
  if (chapterPages == 0 || chapterPageCount <= 1) return std::min(index.totalPages, before + 1);

  const int clampedPage = std::clamp(currentPage, 0, chapterPageCount - 1);
  const uint32_t within = std::min<uint32_t>(chapterPages - 1,
                                             static_cast<uint32_t>(clampedPage) * chapterPages /
                                                 static_cast<uint32_t>(chapterPageCount));
  return std::min(index.totalPages, firstPage + within);
}
