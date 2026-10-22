#pragma once

#include <string>

class Cbz {
 public:
  Cbz(std::string, const std::string&) {}
  bool loadCachedMetadataOnly() { return false; }
  const std::string& getTitle() const { return empty_; }
  const std::string& getAuthor() const { return empty_; }
  const std::string& getThumbBmpPath() const { return empty_; }
  const std::string& getSynopsis() const { return empty_; }
 private:
  std::string empty_;
};
