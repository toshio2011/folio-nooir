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
#include <utility>

#include "BookmarkUtil.h"

namespace {

constexpr size_t MAX_CLIPPINGS = 32;
constexpr size_t MAX_CLIP_BYTES = 1024;
constexpr char CLIPPINGS_DIR[] = "/.crosspoint/clippings/";
constexpr char MASTER_CLIPPINGS_FILE[] = "/My Clippings.txt";

bool styleMaskHasBold(const uint8_t styleMask) {
  // styleMask stores a variant bit (1 << (style & 0x03)); variants 1 and 3
  // are BOLD and BOLD_ITALIC respectively.
  return (styleMask & 0x0Au) != 0;
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

bool save(const std::string& bookPath, const std::vector<ClippingEntry>& clippings) {
  JsonDocument doc;
  JsonArray array = doc["clippings"].to<JsonArray>();
  for (const auto& clipping : clippings) {
    JsonObject item = array.add<JsonObject>();
    item["text"] = ClipFile::normalizeText(clipping.text).substr(0, MAX_CLIP_BYTES);
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
    LOG_DBG("CLP", "save_json style=%u bold=%u highlighted=1", static_cast<unsigned>(clipping.styleMask),
            clipping.bold ? 1u : 0u);
  }

  Storage.mkdir(CLIPPINGS_DIR);
  return PersistableStoreBase::writeDocToFile(clippingPath(bookPath).c_str(), doc);
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
  clippings.clear();
  JsonDocument doc;
  if (!PersistableStoreBase::readDocFromFile(clippingPath(bookPath).c_str(), doc)) return false;

  JsonArrayConst array = doc["clippings"].as<JsonArrayConst>();
  clippings.reserve(std::min(array.size(), MAX_CLIPPINGS));
  for (JsonObjectConst item : array) {
    const char* text = item["text"] | "";
    if (!text[0]) continue;
    ClippingEntry clipping;
    clipping.text = normalizeText(std::string(text)).substr(0, MAX_CLIP_BYTES);
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
    // Old JSON files have no explicit bold key.  Derive it from the existing
    // aggregate style mask, while preserving an explicit false for new
    // regular-only clippings.
    clipping.bold = item["bold"].isNull() ? styleMaskHasBold(clipping.styleMask) : (item["bold"] | false);
    LOG_DBG("CLP", "load style=%u bold=%u highlighted=1", static_cast<unsigned>(clipping.styleMask),
            clipping.bold ? 1u : 0u);
    clippings.push_back(std::move(clipping));
    if (clippings.size() >= MAX_CLIPPINGS) break;
  }
  return true;
}

bool replace(const std::string& bookPath, const std::vector<ClippingEntry>& clippings) {
  std::vector<ClippingEntry> bounded;
  bounded.reserve(std::min(clippings.size(), MAX_CLIPPINGS));
  for (const auto& clipping : clippings) {
    if (clipping.text.empty()) continue;
    ClippingEntry copy = clipping;
    copy.text = normalizeText(copy.text);
    copy.text.resize(std::min(copy.text.size(), MAX_CLIP_BYTES));
    bounded.push_back(std::move(copy));
    if (bounded.size() >= MAX_CLIPPINGS) break;
  }
  return save(bookPath, bounded);
}

bool append(const std::string& bookPath, const std::string& bookTitle, ClippingEntry clipping) {
  if (clipping.text.empty()) return false;
  clipping.text = normalizeText(clipping.text);
  clipping.text.resize(std::min(clipping.text.size(), MAX_CLIP_BYTES));
  if (clipping.dateKey == 0) clipping.dateKey = halClock.getDateKey();

  std::vector<ClippingEntry> clippings;
  load(bookPath, clippings);
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
      if (!save(bookPath, clippings)) {
        LOG_ERR("CLP", "Failed to update clipping metadata for %s", bookPath.c_str());
        return false;
      }
      LOG_DBG("CLP", "updated duplicate style=%u bold=%u highlighted=1",
              static_cast<unsigned>(duplicate->styleMask), duplicate->bold ? 1u : 0u);
    }
    return true;
  }

  if (clippings.size() >= MAX_CLIPPINGS) clippings.erase(clippings.begin());
  clippings.push_back(clipping);
  if (!save(bookPath, clippings)) {
    LOG_ERR("CLP", "Failed to save clipping for %s", bookPath.c_str());
    return false;
  }
  if (!appendMaster(bookTitle, clipping)) {
    LOG_ERR("CLP", "Saved per-book clipping but failed to append master export");
  }
  return true;
}

}  // namespace ClipFile
