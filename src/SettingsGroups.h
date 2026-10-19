#pragma once

#include <I18nKeys.h>

#include <array>
#include <cstdint>
#include <cstring>

// Stable presentation/API groups. These IDs do not replace setting keys,
// persisted values, or enum indexes; they only select a bounded child view.
enum class SettingsGroupId : uint8_t {
  HomeAppearance,
  Sleep,
  Reading,
  Controls,
  Connectivity,
  Library,
  System,
  Unknown,
};

struct SettingsGroupInfo {
  SettingsGroupId id;
  const char* key;
  StrId labelId;
};

inline constexpr std::array<SettingsGroupInfo, 7> kSettingsGroups = {{
    {SettingsGroupId::HomeAppearance, "home_appearance", StrId::STR_CAT_DISPLAY},
    {SettingsGroupId::Sleep, "sleep", StrId::STR_SLEEP_SCREEN},
    {SettingsGroupId::Reading, "reading", StrId::STR_CAT_READER},
    {SettingsGroupId::Controls, "controls", StrId::STR_CAT_CONTROLS},
    {SettingsGroupId::Connectivity, "connectivity", StrId::STR_WIFI_NETWORKS},
    {SettingsGroupId::Library, "library", StrId::STR_LIBRARY},
    {SettingsGroupId::System, "system", StrId::STR_CAT_SYSTEM},
}};

inline const std::array<SettingsGroupInfo, 7>& settingsGroupDescriptors() { return kSettingsGroups; }

inline const SettingsGroupInfo* settingsGroupInfo(SettingsGroupId id) {
  for (const auto& group : kSettingsGroups) {
    if (group.id == id) return &group;
  }
  return nullptr;
}

inline const char* settingsGroupKey(SettingsGroupId id) {
  const auto* info = settingsGroupInfo(id);
  return info ? info->key : nullptr;
}

inline StrId settingsGroupLabelId(SettingsGroupId id) {
  const auto* info = settingsGroupInfo(id);
  return info ? info->labelId : StrId::STR_NONE_OPT;
}

inline SettingsGroupId parseSettingsGroupId(const char* key) {
  if (!key) return SettingsGroupId::Unknown;
  for (const auto& group : kSettingsGroups) {
    if (std::strcmp(group.key, key) == 0) return group.id;
  }
  return SettingsGroupId::Unknown;
}
template <size_t N>
inline bool settingsKeyIn(const char* key, const char* const (&keys)[N]) {
  if (!key) return false;
  for (const char* candidate : keys) {
    if (std::strcmp(key, candidate) == 0) return true;
  }
  return false;
}

inline SettingsGroupId settingsGroupIdForSettingKey(const char* key) {
  static constexpr const char* sleepKeys[] = {
      "sleepScreen",          "randomSleepOrder",     "sleepBackgroundElement", "sleepOverlayAElement",
      "sleepOverlayBElement", "sleepScreenCoverMode", "sleepScreenCoverFilter", "todoSleepMode",
      "quickResumeSleepScreen", "sleepTimeoutMinutes",
  };
  if (settingsKeyIn(key, sleepKeys)) return SettingsGroupId::Sleep;

  static constexpr const char* homeKeys[] = {
      "hideBatteryPercentage", "refreshFrequency",       "uiTheme",                 "carouselLayout",
      "recentBookLayout",       "finishedBookLayout",    "coverProgressStyle",      "featuredShowCover",
      "liteRecentBookLayout",   "liteFinishedBookLayout",
      "featuredShowFileType",   "featuredShowAuthor",    "featuredShowSynopsis",    "featuredShowStatus",
      "featuredShowProgress",   "featuredShowStartedDate", "featuredShowFinishedDate", "featuredShowReadingTime",
      "featuredShowSessions",   "spineDecorationStyle",  "spineAppearanceMode",     "uiScalePercent",
      "fadingFix",
  };
  if (settingsKeyIn(key, homeKeys)) return SettingsGroupId::HomeAppearance;

  static constexpr const char* controlsKeys[] = {
      "sideButtonLayout",          "touchReaderControls", "frontButtonFollowOrientation", "longPressButtonBehavior",
      "sideLongPressAction",       "longPressMenuFunction", "shortPwrBtn",                  "longPwrBtn",
      "pwrBtnFootnoteBack",        "backShortToFileBrowser", "tiltPageTurn", "frontButtonBack",
      "frontButtonConfirm",        "frontButtonLeft",       "frontButtonRight", "readerFrontButtonBack",
      "readerFrontButtonConfirm",  "readerFrontButtonLeft", "readerFrontButtonRight", "quickActionSlot1",
      "quickActionSlot2",          "quickActionSlot3",      "quickActionSlot4",
  };
  if (settingsKeyIn(key, controlsKeys)) return SettingsGroupId::Controls;

  static constexpr const char* connectivityKeys[] = {
      "koUsername", "koPassword", "koServerUrl", "koDeviceName", "koMatchMethod", "koSendMetadata",
      "koSyncBehavior", "bleControllerPreset", "bleKeyMap", "bluetoothEnabled", "bluetoothNoDeviceTimeoutSeconds",
  };
  if (settingsKeyIn(key, connectivityKeys)) return SettingsGroupId::Connectivity;

  static constexpr const char* libraryKeys[] = {
      "showHiddenFiles", "removeReadBooksFromRecents", "moveFinishedToReadFolder", "opdsDownloadFolder",
      "opdsFilenameFormat",
  };
  if (settingsKeyIn(key, libraryKeys)) return SettingsGroupId::Library;

  // Reader and status-bar keys that are exposed by the shared descriptor list.
  static constexpr const char* readingKeys[] = {
      "fontFamily", "fontSize", "dictionaryFontFamily", "dictionaryFontSize", "lineSpacing", "screenMargin",
      "paragraphAlignment", "embeddedStyle", "readerDarkMode", "highlightColor", "focusReadingEnabled",
      "forceParagraphIndents", "guideDots", "hyphenationEnabled", "orientation", "extraParagraphSpacing",
      "textAntiAliasing", "imageRendering", "pageNumberMode", "statusBarChapterPageCount",
      "statusBarBookProgressPercentage", "statusBarPercentageFormat", "statusBarProgressBar",
      "statusBarProgressBarThickness", "statusBarTitle", "statusBarBattery", "xtcStatusBarMode", "statusBarClock",
      "clockUtcOffsetQ", "clockFormat", "clockHasBeenSynced",
  };
  if (settingsKeyIn(key, readingKeys)) return SettingsGroupId::Reading;

  // `sleepTimeoutMinutes` is intentionally presented under Sleep although its
  // persisted descriptor retains the historical System category.
  // System settings are otherwise the final keyed category. Unknown keys are
  // rejected by callers instead of silently becoming a global catalogue.
  static constexpr const char* systemKeys[] = {"weatherSyncEnabled", "resumeReaderOnWake", "language"};
  if (settingsKeyIn(key, systemKeys)) return SettingsGroupId::System;
  return SettingsGroupId::Unknown;
}
