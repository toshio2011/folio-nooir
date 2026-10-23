#pragma once

#include <string>

#include <HalStorage.h>

inline std::string testCachePath(const std::string& type, const std::string& path) {
  return "/.crosspoint/" + type + "_" + path;
}

inline bool clearTestCache(const std::string& path) { return Storage.removeDir(path.c_str()); }
inline void setupTestCache(const std::string& path) { Storage.mkdir(path.c_str()); }
