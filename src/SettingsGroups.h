#pragma once

#include <I18n.h>

#include <array>
#include <cstdint>
#include <cstring>

#include "activities/settings/SettingsActivity.h"

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

inline SettingsGroupId settingsGroupIdFor(const SettingInfo& setting) {
  if (setting.key) {
    const auto byKey = settingsGroupIdForSettingKey(setting.key);
    if (byKey != SettingsGroupId::Unknown) return byKey;
  }

  switch (setting.action) {
    case SettingAction::VisualPreview:
      return SettingsGroupId::HomeAppearance;
    case SettingAction::SleepScreenPreview:
    case SettingAction::RandomSleepPool:
    case SettingAction::ClearFavoriteSleepImage:
      return SettingsGroupId::Sleep;
    case SettingAction::TextSettings:
    case SettingAction::DictionarySettings:
    case SettingAction::DictionaryIndexes:
    case SettingAction::DownloadFonts:
    case SettingAction::CustomiseStatusBar:
      return SettingsGroupId::Reading;
    case SettingAction::RemapFrontButtons:
    case SettingAction::RemapReaderFrontButtons:
    case SettingAction::QuickActions:
      return SettingsGroupId::Controls;
    case SettingAction::Network:
    case SettingAction::Bluetooth:
    case SettingAction::KOReaderSync:
    case SettingAction::OPDSBrowser:
      return SettingsGroupId::Connectivity;
    case SettingAction::ToDoList:
      return SettingsGroupId::Library;
    case SettingAction::ClockWeatherSync:
      return SettingsGroupId::System;
    case SettingAction::SettingsProfiles:
    case SettingAction::ClearCache:
    case SettingAction::CheckForUpdates:
    case SettingAction::SdFirmwareUpdate:
    case SettingAction::Language:
      return SettingsGroupId::System;
    case SettingAction::None:
      break;
  }

  switch (setting.nameId) {
    case StrId::STR_INTERFACE_FONT:
    case StrId::STR_UI_THEME:
    case StrId::STR_CAROUSEL_LAYOUT:
    case StrId::STR_RECENT_BOOK_LAYOUT:
    case StrId::STR_FINISHED_BOOK_LAYOUT:
    case StrId::STR_UI_SCALE:
    case StrId::STR_VISUAL_PREVIEW:
      return SettingsGroupId::HomeAppearance;
    case StrId::STR_SLEEP_SCREEN:
    case StrId::STR_SLEEP_RANDOM_ORDER:
    case StrId::STR_SLEEP_RANDOM_POOL:
    case StrId::STR_SLEEP_BACKGROUND:
    case StrId::STR_SLEEP_OVERLAY_A:
    case StrId::STR_SLEEP_OVERLAY_B:
    case StrId::STR_SLEEP_COVER_MODE:
    case StrId::STR_SLEEP_COVER_FILTER:
    case StrId::STR_TODO_SLEEP_MODE:
    case StrId::STR_QUICK_RESUME_TIMEOUT:
    case StrId::STR_PREVIEW_SLEEP_SCREEN:
      return SettingsGroupId::Sleep;
    case StrId::STR_FONT_FAMILY:
    case StrId::STR_FONT_SIZE:
    case StrId::STR_DICTIONARY:
    case StrId::STR_DICTIONARY_FONT:
    case StrId::STR_DICTIONARY_FONT_SIZE:
    case StrId::STR_LINE_SPACING:
    case StrId::STR_SCREEN_MARGIN:
    case StrId::STR_PARA_ALIGNMENT:
    case StrId::STR_PAGE_NUMBER_MODE:
      return SettingsGroupId::Reading;
    case StrId::STR_TOUCH_READER_CONTROLS:
    case StrId::STR_FRONT_BTN_FOLLOW_ORIENTATION:
    case StrId::STR_LONG_PRESS_BEHAVIOR:
    case StrId::STR_SIDE_LONG_PRESS_ACTION:
    case StrId::STR_LONG_PRESS_MENU:
    case StrId::STR_SHORT_PWR_BTN:
    case StrId::STR_LONG_PWR_BTN:
    case StrId::STR_PWR_BTN_FOOTNOTE_BACK:
    case StrId::STR_BACK_SHORT_TO_FILE_BROWSER:
      return SettingsGroupId::Controls;
    case StrId::STR_KOREADER_USERNAME:
    case StrId::STR_KOREADER_PASSWORD:
    case StrId::STR_SYNC_SERVER_URL:
    case StrId::STR_SYNC_DEVICE_NAME:
    case StrId::STR_DOCUMENT_MATCHING:
    case StrId::STR_SEND_METADATA:
    case StrId::STR_SYNC_BEHAVIOR:
    case StrId::STR_BLUETOOTH:
      return SettingsGroupId::Connectivity;
    case StrId::STR_OPDS_DOWNLOAD_FOLDER:
    case StrId::STR_OPDS_FILENAME_FORMAT:
    case StrId::STR_SHOW_HIDDEN_FILES:
    case StrId::STR_REMOVE_READ_FROM_RECENTS:
    case StrId::STR_MOVE_FINISHED_TO_READ:
    case StrId::STR_TODO_LIST:
      return SettingsGroupId::Library;
    default:
      break;
  }

  if (setting.category == StrId::STR_CAT_DISPLAY) return SettingsGroupId::HomeAppearance;
  if (setting.category == StrId::STR_CAT_READER || setting.category == StrId::STR_CUSTOMISE_STATUS_BAR)
    return SettingsGroupId::Reading;
  if (setting.category == StrId::STR_CAT_CONTROLS) return SettingsGroupId::Controls;
  if (setting.category == StrId::STR_KOREADER_SYNC) return SettingsGroupId::Connectivity;
  if (setting.category == StrId::STR_CAT_SYSTEM) return SettingsGroupId::System;
  return SettingsGroupId::Unknown;
}

inline bool settingBelongsToGroup(const SettingInfo& setting, SettingsGroupId group) {
  return settingsGroupIdFor(setting) == group;
}
