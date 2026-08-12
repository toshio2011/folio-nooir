#pragma once

#include <ArduinoJson.h>
#include <PersistableStore.h>

#include <string>
#include <vector>

// Explicit metadata edits made from the Web UI.  EPUB/XTCH files remain
// untouched; this small sidecar lets the device shelf show the user's title,
// author, and synopsis for any book, including books that are not in Recent.
struct BookMetadataOverride {
  std::string path;
  std::string title;
  std::string author;
  std::string synopsis;
};

class BookMetadataOverridesStore final : public PersistableStore<BookMetadataOverridesStore> {
 private:
  std::vector<BookMetadataOverride> entries;

  BookMetadataOverridesStore() = default;
  ~BookMetadataOverridesStore() = default;
  friend class PersistableStore<BookMetadataOverridesStore>;

 public:
  static const char* getFilePath() { return "/.crosspoint/book-metadata.json"; }

  void toJson(JsonDocument& doc) const;
  bool fromJson(JsonVariantConst doc);

  const BookMetadataOverride* find(const std::string& path) const;
  // Saves a bounded, explicit override. Empty values are valid and allow a
  // user to clear a field; the shelf can still fall back to the filename.
  bool update(const std::string& path, const std::string& title, const std::string& author,
              const std::string& synopsis);
  bool removeByPath(const std::string& path);
  void updatePath(const std::string& oldPath, const std::string& newPath);
};

#define BOOK_METADATA_OVERRIDES BookMetadataOverridesStore::getInstance()
