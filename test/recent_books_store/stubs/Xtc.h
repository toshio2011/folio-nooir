#pragma once

#include <string>

class Xtc {
 public:
  Xtc(std::string, const std::string&) {}
  bool load() { return false; }
  const std::string& getDescription() const { return empty_; }
  const std::string& getSeries() const { return empty_; }
  const std::string& getTitle() const { return empty_; }
  const std::string& getAuthor() const { return empty_; }
  const std::string& getThumbBmpPath() const { return empty_; }
  bool hasLoadedExtendedMetadata() const { return false; }
 private:
  std::string empty_;
};
