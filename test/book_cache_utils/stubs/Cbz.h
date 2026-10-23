#pragma once
#include <string>
#include "BookCacheStub.h"
class Cbz {
 public:
  Cbz(std::string path, const std::string&) : cachePath(testCachePath("cbz", path)) {}
  const std::string& getCachePath() const { return cachePath; }
  bool clearCache() const { return clearTestCache(cachePath); }
  void setupCacheDir() const { setupTestCache(cachePath); }
 private:
  std::string cachePath;
};
