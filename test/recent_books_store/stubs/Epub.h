#pragma once

#include <string>

class Epub {
 public:
  Epub(std::string, const std::string&) {}
  bool load(bool = true, bool = false) { return false; }
  const std::string& getTitle() const { return empty_; }
  const std::string& getAuthor() const { return empty_; }
  const std::string& getThumbBmpPath() const { return empty_; }
  const std::string& getDescription() const { return empty_; }
 private:
  std::string empty_;
};
