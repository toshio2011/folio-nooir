#pragma once

#include <cstdint>
#include <string>
#include <vector>

// Lightweight, settings-only profiles. Book progress, reading statistics,
// bookmarks, clippings, Wi-Fi credentials, and hardware calibration remain in
// their own stores and are deliberately not copied into a profile.
class SettingsProfileStore final {
 public:
  static constexpr uint8_t SCHEMA_VERSION = 1;

  static std::vector<std::string> list();
  static bool saveCurrent(const std::string& name);
  static bool apply(const std::string& name);
  static bool remove(const std::string& name);
  static std::string sanitizeName(const std::string& name);

 private:
  static constexpr const char* PROFILE_DIR = "/.crosspoint/profiles";

  static std::string pathForName(const std::string& name);
  static std::string nameFromEntry(const char* entryName);
};
