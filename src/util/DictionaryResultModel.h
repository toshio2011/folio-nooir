#pragma once

#include <array>
#include <cstddef>
#include <string>
#include <utility>

// Lightweight metadata for one dictionary that matched the current word.
// Definition bodies are deliberately not part of this model.
struct DictionarySourceMatch {
  std::string name;
  std::string headword;
  bool preferred = false;
};

// Bounded source list used by the dictionary result screen. Keeping this as a
// fixed-capacity array makes the six-source limit structural rather than a
// convention at individual call sites.
class DictionarySourceMatches {
 public:
  static constexpr size_t MAX_MATCHES = 6;

  bool add(const std::string& name, const std::string& headword, const bool preferred) {
    if (name.empty()) return false;

    for (size_t i = 0; i < count_; i++) {
      if (matches_[i].name != name) continue;
      if (!headword.empty()) matches_[i].headword = headword;
      matches_[i].preferred = matches_[i].preferred || preferred;
      return true;
    }

    if (count_ >= MAX_MATCHES) return false;
    matches_[count_++] = DictionarySourceMatch{name, headword, preferred};
    return true;
  }

  bool contains(const std::string& name) const {
    for (size_t i = 0; i < count_; i++) {
      if (matches_[i].name == name) return true;
    }
    return false;
  }

  size_t size() const { return count_; }
  bool empty() const { return count_ == 0; }
  const DictionarySourceMatch& operator[](const size_t index) const { return matches_[index]; }

 private:
  std::array<DictionarySourceMatch, MAX_MATCHES> matches_;
  size_t count_ = 0;
};

// Failed-folder diagnostics are session-scoped and bounded. The first
// failure for a folder keeps the detailed Dictionary::open() diagnostic;
// later validation passes for that folder are silent.
class DictionaryOpenFailureLedger {
 public:
  static constexpr size_t MAX_TRACKED_FAILURES = 16;

  bool contains(const std::string& name) const {
    for (size_t i = 0; i < count_; i++) {
      if (names_[i] == name) return true;
    }
    return false;
  }

  bool canTrack() const { return count_ < MAX_TRACKED_FAILURES; }

  void remember(const std::string& name) {
    if (name.empty() || contains(name) || !canTrack()) return;
    names_[count_++] = name;
  }

 private:
  std::array<std::string, MAX_TRACKED_FAILURES> names_;
  size_t count_ = 0;
};
