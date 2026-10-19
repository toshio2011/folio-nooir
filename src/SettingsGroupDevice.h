#pragma once

#include "SettingsGroups.h"
#include "activities/settings/SettingsActivity.h"

// Device-only classification for unkeyed actions and descriptors. Keeping it
// separate lets the Web API share the stable group/key contract without
// pulling the full device Settings and settings-persistence headers into its
// small host policy tests.
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
      return SettingsGroupId::System;
    case SettingAction::ClockWeatherSync:
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
      return SettingsGroupId::Library;
    case StrId::STR_TODO_LIST:
      return SettingsGroupId::System;
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
