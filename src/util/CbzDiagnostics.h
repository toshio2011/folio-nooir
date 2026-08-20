#pragma once

#include <Logging.h>

#include <string>

#ifndef NOOIR_CBZ_PATH_DIAGNOSTICS
#define NOOIR_CBZ_PATH_DIAGNOSTICS 0
#endif

// Temporary path tracing for the CBZ investigation. It deliberately logs
// only paths that look like CBZ paths (including the observed malformed
// ",cbz" spelling), so normal EPUB/TXT navigation is not noisy.
inline bool isCbzDiagnosticPath(const std::string& path) {
  if (path.size() >= 4) {
    const size_t start = path.size() - 4;
    if ((path[start] == '.' || path[start] == ',') && (path[start + 1] == 'c' || path[start + 1] == 'C') &&
        (path[start + 2] == 'b' || path[start + 2] == 'B') && (path[start + 3] == 'z' || path[start + 3] == 'Z')) {
      return true;
    }
  }
  return false;
}

inline void logCbzPath(const char* stage, const std::string& path) {
#if NOOIR_CBZ_PATH_DIAGNOSTICS
  if (isCbzDiagnosticPath(path)) LOG_DBG("CBZPATH", "stage=%s path=\"%s\"", stage, path.c_str());
#else
  (void)stage;
  (void)path;
#endif
}

// Temporary cache-lifetime diagnostics for the CBZ investigation. These are
// intentionally operation-level logs only: no image data or per-pixel output
// is emitted.
inline void logCbzCacheAction(const char* action, const char* reason, const std::string& path) {
  LOG_DBG("CBZCACHE", "action=%s reason=%s path=\"%s\"", action ? action : "", reason ? reason : "",
          path.c_str());
}

inline void logCbzCacheLookup(const std::string& path, const bool exists) {
  LOG_DBG("CBZCACHE", "action=lookup path=\"%s\" exists=%d", path.c_str(), exists ? 1 : 0);
}
