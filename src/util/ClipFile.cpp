#include "ClipFile.h"

#include <ArduinoJson.h>
#include <HalClock.h>
#include <HalStorage.h>
#include <Logging.h>
#include <PersistableStore.h>

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <utility>

#include "BookmarkUtil.h"

namespace {

constexpr size_t MAX_CLIPPINGS = 32;
constexpr size_t MAX_CLIP_BYTES = 1024;
constexpr char CLIPPINGS_DIR[] = "/.crosspoint/clippings/";
constexpr char MASTER_CLIPPINGS_FILE[] = "/My Clippings.txt";

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
    item["text"] = clipping.text;
    item["percentage"] = clipping.percentage;
    item["spine"] = clipping.spineIndex;
    item["page"] = clipping.page;
    item["date"] = clipping.dateKey;
    if (clipping.hasWordRange) {
      item["firstWord"] = clipping.firstWord;
      item["lastWord"] = clipping.lastWord;
    }
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
    clipping.text = std::string(text).substr(0, MAX_CLIP_BYTES);
    clipping.percentage = std::clamp(item["percentage"] | 0.0f, 0.0f, 1.0f);
    clipping.spineIndex = item["spine"] | static_cast<uint16_t>(0);
    clipping.page = item["page"] | static_cast<uint16_t>(0);
    clipping.dateKey = item["date"] | static_cast<uint32_t>(0);
    if (!item["firstWord"].isNull() && !item["lastWord"].isNull()) {
      clipping.firstWord = item["firstWord"] | static_cast<uint16_t>(0);
      clipping.lastWord = item["lastWord"] | static_cast<uint16_t>(0);
      clipping.hasWordRange = clipping.firstWord <= clipping.lastWord;
    }
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
    copy.text.resize(std::min(copy.text.size(), MAX_CLIP_BYTES));
    bounded.push_back(std::move(copy));
    if (bounded.size() >= MAX_CLIPPINGS) break;
  }
  return save(bookPath, bounded);
}

bool append(const std::string& bookPath, const std::string& bookTitle, ClippingEntry clipping) {
  if (clipping.text.empty()) return false;
  clipping.text.resize(std::min(clipping.text.size(), MAX_CLIP_BYTES));
  if (clipping.dateKey == 0) clipping.dateKey = halClock.getDateKey();

  std::vector<ClippingEntry> clippings;
  load(bookPath, clippings);
  const bool duplicate = std::any_of(clippings.begin(), clippings.end(), [&](const ClippingEntry& item) {
    return item.page == clipping.page && item.spineIndex == clipping.spineIndex && item.text == clipping.text;
  });
  if (duplicate) return true;

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
