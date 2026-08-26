#include "BookMetadataOverridesStore.h"

#include <algorithm>
#include <utility>

#include "Logging.h"

namespace {
constexpr size_t MAX_TITLE = 192;
constexpr size_t MAX_AUTHOR = 128;
constexpr size_t MAX_SYNOPSIS = 4096;

std::string bounded(const std::string& value, const size_t limit) { return value.substr(0, limit); }
}  // namespace

void BookMetadataOverridesStore::toJson(JsonDocument& doc) const {
  JsonArray array = doc["books"].to<JsonArray>();
  for (const auto& entry : entries) {
    JsonObject item = array.add<JsonObject>();
    item["path"] = entry.path;
    item["title"] = entry.title;
    item["author"] = entry.author;
    item["synopsis"] = entry.synopsis;
  }
}

bool BookMetadataOverridesStore::fromJson(JsonVariantConst doc) {
  entries.clear();
  JsonArrayConst array = doc["books"].as<JsonArrayConst>();
  entries.reserve(array.size());
  for (JsonObjectConst item : array) {
    const char* path = item["path"] | "";
    if (!path[0]) continue;
    BookMetadataOverride entry;
    entry.path = path;
    entry.title = bounded(std::string(item["title"] | ""), MAX_TITLE);
    entry.author = bounded(std::string(item["author"] | ""), MAX_AUTHOR);
    entry.synopsis = bounded(std::string(item["synopsis"] | ""), MAX_SYNOPSIS);
    entries.push_back(std::move(entry));
  }
  return true;
}

const BookMetadataOverride* BookMetadataOverridesStore::find(const std::string& path) const {
  const auto it = std::find_if(entries.begin(), entries.end(), [&](const BookMetadataOverride& entry) {
    return entry.path == path;
  });
  return it == entries.end() ? nullptr : &*it;
}

bool BookMetadataOverridesStore::update(const std::string& path, const std::string& title,
                                        const std::string& author, const std::string& synopsis) {
  if (path.empty()) return false;
  auto it = std::find_if(entries.begin(), entries.end(), [&](const BookMetadataOverride& entry) {
    return entry.path == path;
  });
  if (it == entries.end()) {
    entries.push_back(BookMetadataOverride{path, bounded(title, MAX_TITLE), bounded(author, MAX_AUTHOR),
                                           bounded(synopsis, MAX_SYNOPSIS)});
  } else {
    it->title = bounded(title, MAX_TITLE);
    it->author = bounded(author, MAX_AUTHOR);
    it->synopsis = bounded(synopsis, MAX_SYNOPSIS);
  }
  if (!saveToFile()) {
    LOG_ERR("BMO", "Failed to save metadata override: %s", path.c_str());
    return false;
  }
  return true;
}

bool BookMetadataOverridesStore::removeByPath(const std::string& path) {
  const auto it = std::find_if(entries.begin(), entries.end(), [&](const BookMetadataOverride& entry) {
    return entry.path == path;
  });
  if (it == entries.end()) return true;
  entries.erase(it);
  return saveToFile();
}

void BookMetadataOverridesStore::updatePath(const std::string& oldPath, const std::string& newPath) {
  auto it = std::find_if(entries.begin(), entries.end(), [&](const BookMetadataOverride& entry) {
    return entry.path == oldPath;
  });
  if (it == entries.end()) return;
  it->path = newPath;
  if (!saveToFile()) LOG_ERR("BMO", "Failed to repoint metadata override: %s", oldPath.c_str());
}
