#include "ClipFile.h"

#include <ArduinoJson.h>
#include <HalClock.h>
#include <HalStorage.h>
#include <Logging.h>
#include <PersistableStore.h>

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <cstdint>
#include <memory>
#include <new>
#include <utility>

#include "BookmarkUtil.h"
#include "ClippingsJsonStream.h"

namespace {

constexpr char CLIPPINGS_DIR[] = "/.crosspoint/clippings/";
constexpr char MASTER_CLIPPINGS_FILE[] = "/My Clippings.txt";

bool styleMaskHasBold(const uint8_t styleMask) {
  // styleMask stores a variant bit (1 << (style & 0x03)); variants 1 and 3
  // are BOLD and BOLD_ITALIC respectively.
  return (styleMask & 0x0Au) != 0;
}

std::string boundedNote(const char* note) {
  if (!note) return {};
  constexpr size_t INPUT_LIMIT = ClipFile::MAX_NOTE_BYTES + 3;
  size_t inputLength = 0;
  while (inputLength < INPUT_LIMIT && note[inputLength] != '\0') ++inputLength;
  std::string safe = ClipFile::normalizeText(std::string(note, inputLength));
  if (safe.size() <= ClipFile::MAX_NOTE_BYTES) return safe;

  size_t end = ClipFile::MAX_NOTE_BYTES;
  // normalizeText() guarantees valid UTF-8. If the cap lands inside a
  // multi-byte character, back up to its leading byte before truncating.
  while (end > 0 && (static_cast<uint8_t>(safe[end]) & 0xC0u) == 0x80u) --end;
  safe.resize(end);
  return safe;
}

std::string boundedNote(const std::string& note) { return boundedNote(note.c_str()); }

std::string boundedClippingText(const char* text) {
  if (!text) return {};
  constexpr size_t INPUT_LIMIT = ClipFile::MAX_CLIPPING_TEXT_BYTES + 3;
  size_t inputLength = 0;
  while (inputLength < INPUT_LIMIT && text[inputLength] != '\0') ++inputLength;
  return ClipFile::normalizeText(std::string(text, inputLength)).substr(0, ClipFile::MAX_CLIPPING_TEXT_BYTES);
}

std::string clippingPath(const std::string& bookPath) {
  const std::string bookmarkPath = BookmarkUtil::getBookmarkPath(bookPath);
  const std::string bookmarkDir = BookmarkUtil::getBookmarksDir();
  if (bookmarkPath.rfind(bookmarkDir, 0) == 0) {
    return std::string(CLIPPINGS_DIR) + bookmarkPath.substr(bookmarkDir.size());
  }
  // Defensive fallback; BookmarkUtil currently always returns the path above.
  return std::string(CLIPPINGS_DIR) + "book.json";
}

void decodeClipping(JsonObjectConst item, ClippingEntry& clipping);

// The source JSON is scanned in fixed-size chunks. Only one clipping record
// is copied into the bounded record buffer at a time.
class HalFileJsonCursor {
 public:
  explicit HalFileJsonCursor(HalFile& file) : file(file) {}

  int peek() {
    if (position >= length) {
      const int result = file.read(buffer, sizeof(buffer));
      if (result <= 0) return -1;
      position = 0;
      length = static_cast<size_t>(result);
    }
    return buffer[position];
  }

  int read() {
    const int value = peek();
    if (value >= 0) ++position;
    return value;
  }

 private:
  HalFile& file;
  uint8_t buffer[256]{};
  size_t position = 0;
  size_t length = 0;
};

// JSON may spell each stored string byte with a six-byte Unicode escape.
// The fixed buffer covers the documented quote/note caps plus field metadata.
constexpr size_t MAX_CLIPPING_RECORD_JSON_BYTES =
    6 * (ClipFile::MAX_CLIPPING_TEXT_BYTES + ClipFile::MAX_NOTE_BYTES) + 1024;

bool loadClippingsPageFromFile(const std::string& path, const size_t offset, const size_t outputLimit,
                              std::vector<ClippingEntry>& clippings, std::vector<std::string>* notes,
                              size_t& totalCount, bool* foundFile = nullptr) {
  if (foundFile) *foundFile = false;
  const String backupPath = String(path.c_str()) + ".bak";
  const String pendingPath = String(path.c_str()) + ".tmp";
  const String candidates[] = {String(path.c_str()), backupPath, pendingPath};
  for (size_t index = 0; index < sizeof(candidates) / sizeof(candidates[0]); ++index) {
    const String& candidate = candidates[index];
    if (!Storage.exists(candidate.c_str())) continue;
    if (foundFile) *foundFile = true;

    HalFile file = Storage.open(candidate.c_str());
    if (!file) {
      LOG_ERR("CLP", "Could not open clipping JSON %s", candidate.c_str());
      continue;
    }
    std::unique_ptr<char[]> recordBuffer(new (std::nothrow) char[MAX_CLIPPING_RECORD_JSON_BYTES]);
    if (!recordBuffer) {
      LOG_ERR("CLP", "Could not allocate bounded clipping record buffer");
      return false;
    }

    HalFileJsonCursor cursor(file);
    JsonDocument record;
    size_t parsedCount = 0;
    const auto result = ClippingsJsonStream::parse(
        cursor, recordBuffer.get(), MAX_CLIPPING_RECORD_JSON_BYTES,
        [&](char* json, size_t) {
          record.clear();
          const auto error = deserializeJson(record, json);
          if (error) {
            LOG_ERR("CLP", "Invalid clipping record in %s: %s", candidate.c_str(), error.c_str());
            return ClippingsJsonStream::RecordStatus::invalid;
          }

          JsonObjectConst item = record.as<JsonObjectConst>();
          const char* text = item["text"] | "";
          if (!text[0] || parsedCount >= ClipFile::MAX_CLIPPINGS_PER_BOOK) {
            return ClippingsJsonStream::RecordStatus::ignored;
          }

          const size_t currentIndex = parsedCount++;
          if (currentIndex >= offset && clippings.size() < outputLimit) {
            ClippingEntry clipping;
            decodeClipping(item, clipping);
            clippings.push_back(std::move(clipping));
            if (notes) notes->push_back(boundedNote(item["note"] | ""));
          }
          return ClippingsJsonStream::RecordStatus::accepted;
        });

    if (result.valid) {
      totalCount = parsedCount;
      if (index != 0) LOG_ERR("CLP", "Recovered clipping JSON from %s", candidate.c_str());
      return true;
    }

    LOG_ERR("CLP", "Invalid clipping JSON structure in %s", candidate.c_str());
    clippings.clear();
    if (notes) notes->clear();
    totalCount = 0;
  }
  return false;
}

std::string singleLine(std::string text) {
  for (char& c : text) {
    if (c == '\r' || c == '\n' || c == '\t') c = ' ';
  }
  return text;
}

std::string dateLabel(const uint32_t dateKey) {
  if (dateKey == 0) return "unknown date";
  char buf[16];
  snprintf(buf, sizeof(buf), "%04lu-%02lu-%02lu", static_cast<unsigned long>(dateKey / 10000UL),
           static_cast<unsigned long>((dateKey / 100UL) % 100UL), static_cast<unsigned long>(dateKey % 100UL));
  return buf;
}

bool save(const std::string& bookPath, const std::vector<ClippingEntry>& clippings,
          const std::vector<std::string>* notes = nullptr) {
  JsonDocument doc;
  JsonArray array = doc["clippings"].to<JsonArray>();
  for (size_t index = 0; index < clippings.size(); ++index) {
    const auto& clipping = clippings[index];
    JsonObject item = array.add<JsonObject>();
    item["text"] = ClipFile::normalizeText(clipping.text).substr(0, ClipFile::MAX_CLIPPING_TEXT_BYTES);
    item["percentage"] = clipping.percentage;
    item["spine"] = clipping.spineIndex;
    item["page"] = clipping.page;
    item["date"] = clipping.dateKey;
    if (clipping.hasWordRange) {
      item["firstWord"] = clipping.firstWord;
      item["lastWord"] = clipping.lastWord;
    }
    if (clipping.styleMask != 0) item["styleMask"] = clipping.styleMask;
    // Always write the explicit key for newly saved entries.  Missing keys
    // in older files remain backward-compatible through load()'s fallback.
    item["bold"] = clipping.bold;
    if (notes && index < notes->size()) {
      const std::string note = boundedNote((*notes)[index]);
      if (!note.empty()) item["note"] = note;
    }
    LOG_DBG("CLP", "save_json style=%u bold=%u highlighted=1", static_cast<unsigned>(clipping.styleMask),
            clipping.bold ? 1u : 0u);
  }

  Storage.mkdir(CLIPPINGS_DIR);
  return PersistableStoreBase::writeDocToFile(clippingPath(bookPath).c_str(), doc);
}

void decodeClipping(JsonObjectConst item, ClippingEntry& clipping) {
  const char* text = item["text"] | "";
  clipping.text = boundedClippingText(text);
  clipping.percentage = std::clamp(item["percentage"] | 0.0f, 0.0f, 1.0f);
  clipping.spineIndex = item["spine"] | static_cast<uint16_t>(0);
  clipping.page = item["page"] | static_cast<uint16_t>(0);
  clipping.dateKey = item["date"] | static_cast<uint32_t>(0);
  if (!item["firstWord"].isNull() && !item["lastWord"].isNull()) {
    clipping.firstWord = item["firstWord"] | static_cast<uint16_t>(0);
    clipping.lastWord = item["lastWord"] | static_cast<uint16_t>(0);
    clipping.hasWordRange = clipping.firstWord <= clipping.lastWord;
  }
  clipping.styleMask = item["styleMask"] | static_cast<uint8_t>(0);
  // Old JSON files have no explicit bold key. Derive it from the existing
  // aggregate style mask, while preserving an explicit false for new
  // regular-only clippings.
  clipping.bold = item["bold"].isNull() ? styleMaskHasBold(clipping.styleMask) : (item["bold"] | false);
  LOG_DBG("CLP", "load style=%u bold=%u highlighted=1", static_cast<unsigned>(clipping.styleMask),
          clipping.bold ? 1u : 0u);
}

bool loadRange(const std::string& bookPath, const size_t offset, const size_t limit,
               std::vector<ClippingEntry>& clippings, std::vector<std::string>* notes, size_t& totalCount,
               bool* foundFile = nullptr) {
  clippings.clear();
  if (notes) notes->clear();
  totalCount = 0;
  const size_t outputLimit = std::min(limit, ClipFile::MAX_CLIPPINGS_PER_BOOK);
  clippings.reserve(outputLimit);
  if (notes) notes->reserve(outputLimit);
  return loadClippingsPageFromFile(clippingPath(bookPath), offset, outputLimit, clippings, notes, totalCount,
                                   foundFile);
}

bool loadImpl(const std::string& bookPath, std::vector<ClippingEntry>& clippings,
              std::vector<std::string>* notes) {
  clippings.clear();
  if (notes) notes->clear();
  JsonDocument doc;
  if (!PersistableStoreBase::readDocFromFile(clippingPath(bookPath).c_str(), doc)) return false;

  JsonArrayConst array = doc["clippings"].as<JsonArrayConst>();
  clippings.reserve(ClipFile::MAX_CLIPPINGS_PER_BOOK);
  if (notes) notes->reserve(ClipFile::MAX_CLIPPINGS_PER_BOOK);
  for (JsonObjectConst item : array) {
    const char* text = item["text"] | "";
    if (!text[0]) continue;
    if (clippings.size() >= ClipFile::MAX_CLIPPINGS_PER_BOOK) break;
    ClippingEntry clipping;
    decodeClipping(item, clipping);
    clippings.push_back(std::move(clipping));
    if (notes) notes->push_back(boundedNote(item["note"] | ""));
  }
  return true;
}

bool appendMaster(const std::string& title, const ClippingEntry& clipping) {
  HalFile file = Storage.open(MASTER_CLIPPINGS_FILE, O_WRITE | O_CREAT | O_APPEND);
  if (!file) {
    LOG_ERR("CLP", "Could not open %s", MASTER_CLIPPINGS_FILE);
    return false;
  }

  const std::string safeTitle = singleLine(title.empty() ? "Unknown book" : title);
  const std::string location = "- Your Highlight on page " + std::to_string(static_cast<unsigned>(clipping.page) + 1) +
                               " | Added on " + dateLabel(clipping.dateKey) + "\n";
  const std::string block = safeTitle + "\n" + location + clipping.text + "\n==========\n";
  const size_t written = file.write(block.data(), block.size());
  file.flush();
  return written == block.size();
}

}  // namespace

namespace ClipFile {

std::string normalizeText(const std::string& text) {
  std::string normalized;
  normalized.reserve(text.size());
  for (size_t i = 0; i < text.size();) {
    const uint8_t first = static_cast<uint8_t>(text[i]);
    if (first < 0x80) {
      normalized.push_back(first < 0x20 && first != '\n' && first != '\t' ? ' ' : static_cast<char>(first));
      ++i;
      continue;
    }

    int length = 0;
    uint32_t codepoint = 0;
    if ((first & 0xE0) == 0xC0) {
      length = 2;
      codepoint = first & 0x1F;
    } else if ((first & 0xF0) == 0xE0) {
      length = 3;
      codepoint = first & 0x0F;
    } else if ((first & 0xF8) == 0xF0) {
      length = 4;
      codepoint = first & 0x07;
    }
    if (length == 0 || i + static_cast<size_t>(length) > text.size()) {
      normalized.push_back(' ');
      ++i;
      continue;
    }

    bool valid = true;
    for (int byte = 1; byte < length; ++byte) {
      const uint8_t continuation = static_cast<uint8_t>(text[i + byte]);
      if ((continuation & 0xC0) != 0x80) {
        valid = false;
        break;
      }
      codepoint = (codepoint << 6) | (continuation & 0x3F);
    }
    // Reject malformed, overlong, surrogate, and out-of-range sequences.
    // These otherwise reach the font renderer as an unknown glyph.
    const uint32_t minimumCodepoint = length == 2 ? 0x80 : (length == 3 ? 0x800 : 0x10000);
    const bool invalidCodepoint = codepoint < minimumCodepoint || codepoint > 0x10FFFF ||
                                  (codepoint >= 0xD800 && codepoint <= 0xDFFF);
    if (!valid || invalidCodepoint) {
      normalized.push_back(' ');
      ++i;
      continue;
    }

    // Keep normal Latin letters (including accents), while avoiding the
    // replacement diamond and symbol/punctuation ranges that are not
    // available in the device font set.
    const bool unsupportedGlyph =
        codepoint == 0x00A0 || codepoint == 0x00AD || codepoint == 0x1680 || codepoint == 0x180E ||
        codepoint == 0xFFFD ||
        codepoint == 0x3000 || (codepoint >= 0x2000 && codepoint <= 0x206F) ||
        (codepoint >= 0x2500 && codepoint <= 0x25FF) || (codepoint >= 0xFFF0 && codepoint <= 0xFFFF);
    if (unsupportedGlyph) {
      normalized.push_back(' ');
    } else {
      normalized.append(text, i, static_cast<size_t>(length));
    }
    i += static_cast<size_t>(length);
  }
  return normalized;
}

bool load(const std::string& bookPath, std::vector<ClippingEntry>& clippings) {
  return loadImpl(bookPath, clippings, nullptr);
}

bool load(const std::string& bookPath, std::vector<ClippingEntry>& clippings, std::vector<std::string>& notes) {
  return loadImpl(bookPath, clippings, &notes);
}

bool loadPage(const std::string& bookPath, const size_t offset, const size_t limit,
              std::vector<ClippingEntry>& clippings, std::vector<std::string>& notes, size_t& totalCount) {
  return loadRange(bookPath, offset, limit, clippings, &notes, totalCount);
}

bool count(const std::string& bookPath, size_t& totalCount) {
  std::vector<ClippingEntry> unusedClippings;
  bool foundFile = false;
  const bool loaded = loadRange(bookPath, ClipFile::MAX_CLIPPINGS_PER_BOOK, 0, unusedClippings, nullptr, totalCount,
                                &foundFile);
  // Books without annotations are a normal case while browsing the library.
  // Distinguish them from unreadable or malformed existing clipping files so
  // the all-books page does not report a false storage error for every such book.
  if (!loaded && !foundFile) {
    totalCount = 0;
    return true;
  }
  return loaded;
}

bool replace(const std::string& bookPath, const std::vector<ClippingEntry>& clippings) {
  // Keep notes attached to unchanged passages for legacy callers that still
  // replace only the clipping vector. The manager uses the aligned overload
  // below so edited text and deletion remain unambiguous.
  std::vector<ClippingEntry> previous;
  std::vector<std::string> previousNotes;
  loadImpl(bookPath, previous, &previousNotes);
  std::vector<std::string> notes(clippings.size());
  std::vector<bool> matched(previous.size(), false);
  for (size_t i = 0; i < clippings.size(); ++i) {
    const auto& target = clippings[i];
    size_t match = previous.size();
    for (size_t j = 0; j < previous.size(); ++j) {
      if (matched[j] || previous[j].spineIndex != target.spineIndex || previous[j].page != target.page) continue;
      if (previous[j].text == target.text) {
        match = j;
        break;
      }
      // If an older caller edited the quote text, keep its note only when the
      // page contains one unambiguous saved passage.
      if (match == previous.size()) match = j;
      else match = previous.size() + 1;
    }
    if (match < previous.size()) {
      matched[match] = true;
      if (match < previousNotes.size()) notes[i] = previousNotes[match];
    }
  }
  return replace(bookPath, clippings, notes);
}

bool replace(const std::string& bookPath, const std::vector<ClippingEntry>& clippings,
             const std::vector<std::string>& notes) {
  std::vector<ClippingEntry> bounded;
  std::vector<std::string> boundedNotes;
  bounded.reserve(std::min(clippings.size(), MAX_CLIPPINGS_PER_BOOK));
  boundedNotes.reserve(std::min(clippings.size(), MAX_CLIPPINGS_PER_BOOK));
  for (size_t index = 0; index < clippings.size(); ++index) {
    const auto& clipping = clippings[index];
    if (clipping.text.empty()) continue;
    ClippingEntry copy = clipping;
    copy.text = normalizeText(copy.text);
    copy.text.resize(std::min(copy.text.size(), MAX_CLIPPING_TEXT_BYTES));
    bounded.push_back(std::move(copy));
    boundedNotes.push_back(index < notes.size() ? boundedNote(notes[index]) : std::string{});
    if (bounded.size() >= MAX_CLIPPINGS_PER_BOOK) break;
  }
  return save(bookPath, bounded, &boundedNotes);
}

bool append(const std::string& bookPath, const std::string& bookTitle, ClippingEntry clipping) {
  if (clipping.text.empty()) return false;
  clipping.text = normalizeText(clipping.text);
  clipping.text.resize(std::min(clipping.text.size(), MAX_CLIPPING_TEXT_BYTES));
  if (clipping.dateKey == 0) clipping.dateKey = halClock.getDateKey();

  std::vector<ClippingEntry> clippings;
  std::vector<std::string> notes;
  load(bookPath, clippings, notes);
  const auto duplicate = std::find_if(clippings.begin(), clippings.end(), [&](const ClippingEntry& item) {
    return item.page == clipping.page && item.spineIndex == clipping.spineIndex && item.text == clipping.text;
  });
  if (duplicate != clippings.end()) {
    // Re-clipping the same passage must refresh style metadata.  Older
    // entries (and entries created before bold persistence was added) would
    // otherwise return early forever with styleMask=0/bold=0.
    const bool metadataChanged = duplicate->styleMask != clipping.styleMask || duplicate->bold != clipping.bold ||
                                  (clipping.hasWordRange &&
                                   (!duplicate->hasWordRange || duplicate->firstWord != clipping.firstWord ||
                                    duplicate->lastWord != clipping.lastWord));
    if (metadataChanged) {
      duplicate->styleMask = clipping.styleMask;
      duplicate->bold = clipping.bold;
      if (clipping.hasWordRange) {
        duplicate->firstWord = clipping.firstWord;
        duplicate->lastWord = clipping.lastWord;
        duplicate->hasWordRange = true;
      }
      if (!save(bookPath, clippings, &notes)) {
        LOG_ERR("CLP", "Failed to update clipping metadata for %s", bookPath.c_str());
        return false;
      }
      LOG_DBG("CLP", "updated duplicate style=%u bold=%u highlighted=1",
              static_cast<unsigned>(duplicate->styleMask), duplicate->bold ? 1u : 0u);
    }
    return true;
  }

  if (clippings.size() >= MAX_CLIPPINGS_PER_BOOK) {
    clippings.erase(clippings.begin());
    if (!notes.empty()) notes.erase(notes.begin());
  }
  clippings.push_back(clipping);
  notes.emplace_back();
  if (!save(bookPath, clippings, &notes)) {
    LOG_ERR("CLP", "Failed to save clipping for %s", bookPath.c_str());
    return false;
  }
  if (!appendMaster(bookTitle, clipping)) {
    LOG_ERR("CLP", "Saved per-book clipping but failed to append master export");
  }
  return true;
}

}  // namespace ClipFile
