#pragma once
#include <string>
#include "BookCacheStub.h"
class Txt {
 public:
  Txt(std::string path, const std::string&) : cachePath(testCachePath("txt", path)) {}
  const std::string& getCachePath() const { return cachePath; }
  bool clearCache() const { return clearTestCache(cachePath); }
  void setupCacheDir() const { setupTestCache(cachePath); }
 private:
  std::string cachePath;
};
