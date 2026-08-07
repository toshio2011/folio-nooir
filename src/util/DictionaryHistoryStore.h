#pragma once

#include <ArduinoJson.h>
#include <PersistableStore.h>

#include <string>
#include <vector>

// Small persistent preference/history for dictionary lookups.  We keep only
// the normalized query and the dictionary folder that answered it; definition
// bodies can be large and remain on the SD card.  The history is used as a
// fast hint, never as the source of truth, so deleting or replacing a
// dictionary remains safe.
struct DictionaryHistoryEntry {
  std::string word;
  std::string dictionary;
};

class DictionaryHistoryStore : public PersistableStore<DictionaryHistoryStore> {
 private:
  static constexpr size_t MAX_ENTRIES = 16;
  static constexpr size_t MAX_WORD_BYTES = 96;
  static constexpr size_t MAX_DICTIONARY_BYTES = 31;

  std::vector<DictionaryHistoryEntry> entries;
  bool loaded = false;

  DictionaryHistoryStore() = default;
  ~DictionaryHistoryStore() = default;
  friend class PersistableStore<DictionaryHistoryStore>;

  void ensureLoaded();

 public:
  static const char* getFilePath() { return "/.crosspoint/dictionary-history.json"; }

  void toJson(JsonDocument& doc) const;
  bool fromJson(JsonVariantConst doc);

  // Returns the most recently successful dictionary for word, or an empty
  // string when this word has not been seen before.
  std::string preferredDictionary(const std::string& word);

  // Move a successful lookup to the front, deduplicate it, and persist the
  // bounded history.  Empty values are ignored.
  void remember(const std::string& word, const std::string& dictionary);
};

#define DICTIONARY_HISTORY DictionaryHistoryStore::getInstance()
