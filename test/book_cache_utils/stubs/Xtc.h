#pragma once
#include <string>
#include "BookCacheStub.h"
class Xtc {
 public:
  Xtc(std::string path, const std::string&) : cachePath(testCachePath("xtc", path)) {}
  const std::string& getCachePath() const { return cachePath; }
  bool clearCache() const { return clearTestCache(cachePath); }
  void setupCacheDir() const { setupTestCache(cachePath); }
 private:
  std::string cachePath;
};
