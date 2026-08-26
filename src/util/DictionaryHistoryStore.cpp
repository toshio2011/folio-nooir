#include "DictionaryHistoryStore.h"

#include <Logging.h>

#include <algorithm>
#include <cctype>
#include <utility>

namespace {

std::string normalizeWord(const std::string& input) {
  std::string word = input;
  while (!word.empty() && std::isspace(static_cast<unsigned char>(word.front()))) word.erase(word.begin());
  while (!word.empty() && std::isspace(static_cast<unsigned char>(word.back()))) word.pop_back();
  for (char& c : word) {
    if (static_cast<unsigned char>(c) < 0x80) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
  }
  return word;
}

}  // namespace

void DictionaryHistoryStore::ensureLoaded() {
  if (loaded) return;
  // A missing file is the normal first-use case.  Keep the in-memory list
  // empty and avoid trying to read the SD card on every lookup.
  loadFromFile();
  loaded = true;
}

void DictionaryHistoryStore::toJson(JsonDocument& doc) const {
  JsonArray array = doc["entries"].to<JsonArray>();
  for (const auto& entry : entries) {
    JsonObject item = array.add<JsonObject>();
    item["word"] = entry.word;
    item["dictionary"] = entry.dictionary;
  }
}

bool DictionaryHistoryStore::fromJson(JsonVariantConst doc) {
  entries.clear();
  JsonArrayConst array = doc["entries"].as<JsonArrayConst>();
  entries.reserve(std::min(array.size(), MAX_ENTRIES));
  for (JsonObjectConst item : array) {
    const char* word = item["word"] | "";
    const char* dictionary = item["dictionary"] | "";
    if (!word[0] || !dictionary[0]) continue;

    DictionaryHistoryEntry entry;
    entry.word = normalizeWord(word).substr(0, MAX_WORD_BYTES);
    entry.dictionary = std::string(dictionary).substr(0, MAX_DICTIONARY_BYTES);
    if (entry.word.empty() || entry.dictionary.empty()) continue;
    entries.push_back(std::move(entry));
    if (entries.size() >= MAX_ENTRIES) break;
  }
  return true;
}

std::string DictionaryHistoryStore::preferredDictionary(const std::string& word) {
  ensureLoaded();
  const std::string normalized = normalizeWord(word);
  if (normalized.empty()) return {};
  const auto it = std::find_if(entries.begin(), entries.end(), [&](const DictionaryHistoryEntry& entry) {
    return entry.word == normalized;
  });
  return it == entries.end() ? std::string() : it->dictionary;
}

void DictionaryHistoryStore::remember(const std::string& word, const std::string& dictionary) {
  ensureLoaded();
  const std::string normalized = normalizeWord(word).substr(0, MAX_WORD_BYTES);
  const std::string boundedDictionary = dictionary.substr(0, MAX_DICTIONARY_BYTES);
  if (normalized.empty() || boundedDictionary.empty()) return;

  // Avoid an SD write when the same word/dictionary is already the newest
  // entry.  This is the common repeated-lookup path and should stay quick.
  if (!entries.empty() && entries.front().word == normalized && entries.front().dictionary == boundedDictionary) return;

  entries.erase(std::remove_if(entries.begin(), entries.end(), [&](const DictionaryHistoryEntry& entry) {
                  return entry.word == normalized;
                }),
                entries.end());
  entries.insert(entries.begin(), DictionaryHistoryEntry{normalized, boundedDictionary});
  if (entries.size() > MAX_ENTRIES) entries.resize(MAX_ENTRIES);
  if (!saveToFile()) LOG_ERR("DICT", "Failed to persist dictionary lookup history");
}
