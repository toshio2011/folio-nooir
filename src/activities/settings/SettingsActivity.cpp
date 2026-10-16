#include "SettingsActivity.h"

#include <BoardConfig.h>
#include <FontCacheManager.h>
#include <GfxRenderer.h>
#include <HalStorage.h>
#include <Logging.h>
#include <esp_system.h>

#include <algorithm>
#include <array>
#include <cstdio>
#include <cstring>
#include <initializer_list>

#include "ButtonRemapActivity.h"
#include "BleInput.h"
#include "BluetoothSettingsActivity.h"
#include "ClearCacheActivity.h"
#include "../home/ToDoListActivity.h"
#include "ClockSyncActivity.h"
#include "CrossPointSettings.h"
#include "CrossPointState.h"
#include "DictionaryIndexActivity.h"
#include "FontDownloadActivity.h"
#include "KOReaderSettingsActivity.h"
#include "LanguageSelectActivity.h"
#include "MappedInputManager.h"
#include "OpdsServerListActivity.h"
#include "OtaUpdateActivity.h"
#include "SdCardFontSystem.h"
#include "SdFirmwareUpdateActivity.h"
#include "SettingsProfilesActivity.h"
#include "SettingsList.h"
#include "SettingsGroupDevice.h"
#include "StatusBarSettingsActivity.h"
#include "TextSettingsActivity.h"
#include "VisualPreviewActivity.h"
#include "activities/boot_sleep/SleepActivity.h"
#include "activities/network/WifiSelectionActivity.h"
#include "activities/util/IntervalSelectionActivity.h"
#include "components/UITheme.h"
#include "util/BleMemoryPolicy.h"
#include "fontIds.h"
#include "util/QuickActions.h"

namespace {
constexpr int kMaxValuePickerOptions = 32;

void logSettingsDiagnostic(const char* stage, const int detail = -1) {
  LOG_INF("SETDIAG", "stage=%s detail=%d free=%u min=%u largest=%u serial=%d reset=%d", stage, detail,
          static_cast<unsigned>(ESP.getFreeHeap()), static_cast<unsigned>(ESP.getMinFreeHeap()),
          static_cast<unsigned>(ESP.getMaxAllocHeap()), logSerial ? 1 : 0, static_cast<int>(esp_reset_reason()));
}

bool hasActiveButtonInput(const MappedInputManager& input) {
  if (input.wasAnyPressed() || input.wasAnyReleased()) return true;
  for (uint8_t index = 0; index < MappedInputManager::kButtonCount; ++index) {
    if (input.isPressed(static_cast<MappedInputManager::Button>(index))) return true;
  }
  return false;
}

std::string formatValuePickerOption(const StrId nameId, const int value) {
  std::string label = std::to_string(value);
  if (nameId == StrId::STR_UI_SCALE) label += "%";
  return label;
}
}  // namespace

SettingsActivity::SettingsActivity(GfxRenderer& renderer, MappedInputManager& mappedInput)
    : Activity("Settings", renderer, mappedInput) {
  logSettingsDiagnostic("entry");
}

SettingsActivity::~SettingsActivity() { logSettingsDiagnostic("destructor"); }

void SettingsActivity::armBluetoothReturn(const char* reason) {
  const uint32_t epoch = bluetoothLifecycleEpoch.fetch_add(1, std::memory_order_acq_rel) + 1;
  bluetoothLifecycleState.store(BluetoothLifecycleState::TransitionSensitive, std::memory_order_release);
#if FREEINK_CAP_BLE_HID_HOST && NOOIR_BLE_READER_COEXISTENCE
  bluetoothReturnArmed = true;
  bluetoothTransientFontCachesReleased = false;
#endif
  LOG_INF("BLEMEM", "stage=settings_ble_return_armed reason=%.24s epoch=%u free=%u largest=%u",
          reason ? reason : "event", static_cast<unsigned>(epoch),
          static_cast<unsigned>(ESP.getFreeHeap()), static_cast<unsigned>(ESP.getMaxAllocHeap()));
  bleinput::recordDiagnosticEvent("settings_ble_return_armed reason=%.24s epoch=%u", reason ? reason : "event",
                                  static_cast<unsigned>(epoch));
  bleinput::requestLifecycleReevaluation();
}

void SettingsActivity::markBluetoothQuiescent() {
  auto expected = BluetoothLifecycleState::TransitionSensitive;
  if (!bluetoothLifecycleState.compare_exchange_strong(expected, BluetoothLifecycleState::ReturnPending,
                                                       std::memory_order_acq_rel, std::memory_order_acquire))
    return;
  const uint32_t epoch = bluetoothLifecycleEpoch.load(std::memory_order_acquire);
  LOG_INF("BLEMEM", "stage=settings_ble_return_deferred epoch=%u free=%u largest=%u",
          static_cast<unsigned>(epoch), static_cast<unsigned>(ESP.getFreeHeap()),
          static_cast<unsigned>(ESP.getMaxAllocHeap()));
#if FREEINK_CAP_BLE_HID_HOST && NOOIR_BLE_READER_COEXISTENCE
  reclaimStableBluetoothTransientCaches();
#endif
  bleinput::recordDiagnosticEvent("settings_ble_return_deferred epoch=%u", static_cast<unsigned>(epoch));
  bleinput::requestLifecycleReevaluation();
}

void SettingsActivity::markBluetoothAdmitted() {
  const BluetoothLifecycleState previous =
      bluetoothLifecycleState.exchange(BluetoothLifecycleState::StableIdle, std::memory_order_acq_rel);
#if FREEINK_CAP_BLE_HID_HOST && NOOIR_BLE_READER_COEXISTENCE
  bluetoothReturnArmed = false;
  bluetoothTransientFontCachesReleased = false;
#endif
  if (previous != BluetoothLifecycleState::StableIdle) {
    const uint32_t epoch = bluetoothLifecycleEpoch.load(std::memory_order_acquire);
    LOG_INF("BLEMEM", "stage=settings_ble_stable epoch=%u free=%u largest=%u",
            static_cast<unsigned>(epoch), static_cast<unsigned>(ESP.getFreeHeap()),
            static_cast<unsigned>(ESP.getMaxAllocHeap()));
    bleinput::recordDiagnosticEvent("settings_ble_stable epoch=%u", static_cast<unsigned>(epoch));
  }
}

#if FREEINK_CAP_BLE_HID_HOST && NOOIR_BLE_READER_COEXISTENCE
void SettingsActivity::reclaimStableBluetoothTransientCaches() {
  if (!bluetoothReturnArmed || bluetoothTransientFontCachesReleased || BleHid.isRunning()) return;

  const size_t freeBefore = ESP.getFreeHeap();
  const size_t largestBefore = ESP.getMaxAllocHeap();
  if (freeBefore >= bleinput::kReaderBleStartMinFreeHeap &&
      largestBefore >= bleinput::kReaderBleStartMinLargestBlock)
    return;

  bluetoothTransientFontCachesReleased = true;
  // markBluetoothQuiescent() is called by render(), which already owns the
  // render lock. Do not acquire a nested RenderLock here.
  if (auto* fontCache = renderer.getFontCacheManager()) fontCache->releaseTransientFontCaches();
  // This is the stable, pre-admission boundary: BLE is stopped and the
  // existing heap floors say a cold start needs room. Drop the separately
  // selected UI font's interval tables and registrations as well as its
  // rebuildable glyph caches; ActivityManager keeps them unloaded while BLE
  // admission is pending or active.
  sdFontSystem.releaseInterfaceFontCaches();
  const size_t freeAfter = ESP.getFreeHeap();
  const size_t largestAfter = ESP.getMaxAllocHeap();
  LOG_INF("BLEMEM",
          "stage=settings_ble_return_reclaim item=font_decompressor_and_interface_assets free=%u->%u freed=%u largest=%u->%u gain=%u",
          static_cast<unsigned>(freeBefore), static_cast<unsigned>(freeAfter),
          static_cast<unsigned>(freeAfter > freeBefore ? freeAfter - freeBefore : 0),
          static_cast<unsigned>(largestBefore), static_cast<unsigned>(largestAfter),
          static_cast<unsigned>(largestAfter > largestBefore ? largestAfter - largestBefore : 0));
  // Settings has finished its frame. A selected Reader font is not live here
  // unless Reader is still on the stack; actual UI references are retained by
  // the manager. In particular, an unreferenced 8 pt font is not a UI owner.
  if (!activityManager.isReaderActivity()) sdFontSystem.releaseReaderFontOwner(renderer);
}
#endif

void SettingsActivity::rebuildSettingsLists() {
  currentGroupSettings.clear();
  currentSettings = &currentGroupSettings;

  const auto selectedGroup = static_cast<SettingsGroupId>(selectedCategoryIndex);
  std::vector<DictionaryEntry> dictionaries;
  if (!groupListActive &&
      (selectedGroup == SettingsGroupId::HomeAppearance || selectedGroup == SettingsGroupId::Reading)) {
    // Discover compatible Mini Pack families only when a font-owning group is
    // entered. This keeps font I/O out of Settings root and navigation redraws.
    sdFontSystem.refreshIfDirty();
  }
  if (selectedGroup == SettingsGroupId::Reading) {
    // Dictionary discovery remains scoped to Reading group entry.
    DictionaryRegistry::discover(dictionaries);
  }

  const bool folioLayoutTheme = SETTINGS.uiTheme == CrossPointSettings::UI_THEME::FOLIO_NOOIR ||
                                SETTINGS.uiTheme == CrossPointSettings::UI_THEME::FOLIO_NOOIR_LITE;
  const auto isFolioLayoutSetting = [](const StrId id) {
    return id == StrId::STR_RECENT_BOOK_LAYOUT || id == StrId::STR_FINISHED_BOOK_LAYOUT;
  };
  const auto isCarouselLayoutSetting = [](const StrId id) { return id == StrId::STR_CAROUSEL_LAYOUT; };
  const auto isVisualPreviewSetting = [](const StrId id) {
    switch (id) {
      case StrId::STR_COVER_PROGRESS_STYLE:
      case StrId::STR_FEATURED_SHOW_COVER:
      case StrId::STR_FEATURED_SHOW_FILE_TYPE:
      case StrId::STR_FEATURED_SHOW_AUTHOR:
      case StrId::STR_FEATURED_SHOW_SYNOPSIS:
      case StrId::STR_FEATURED_SHOW_STATUS:
      case StrId::STR_FEATURED_SHOW_PROGRESS:
      case StrId::STR_FEATURED_SHOW_STARTED:
      case StrId::STR_FEATURED_SHOW_FINISHED:
      case StrId::STR_FEATURED_SHOW_READING_TIME:
      case StrId::STR_FEATURED_SHOW_SESSIONS:
      case StrId::STR_SPINE_DECORATION:
      case StrId::STR_SPINE_APPEARANCE:
        return true;
      default:
        return false;
    }
  };
  const auto visibleInSelectedGroup = [&](const SettingInfo& setting) {
    if (setting.category == StrId::STR_NONE_OPT || isSleepComposerSetting(setting)) return false;
    if ((setting.nameId == StrId::STR_SLEEP_RANDOM_ORDER || setting.action == SettingAction::RandomSleepPool) &&
        SETTINGS.sleepScreen != CrossPointSettings::SLEEP_SCREEN_MODE::RANDOM) {
      return false;
    }
    if (isVisualPreviewSetting(setting.nameId)) return false;
    if (isFolioLayoutSetting(setting.nameId) && !folioLayoutTheme) return false;
    // Keep the legacy Carousel-specific control out of Settings for 1.6.6.
    // Recent/Finished layouts remain available in both Folio themes above.
    if (isCarouselLayoutSetting(setting.nameId)) return false;
    if (setting.category == StrId::STR_CAT_READER && setting.inTextSettings) return false;
    if (setting.category == StrId::STR_CAT_CONTROLS &&
        setting.valuePtr == &CrossPointSettings::pwrBtnFootnoteBack &&
        SETTINGS.shortPwrBtn != CrossPointSettings::SHORT_PWRBTN::FOOTNOTES) {
      return false;
    }
    if (setting.category != StrId::STR_CAT_DISPLAY && setting.category != StrId::STR_CAT_READER &&
        setting.category != StrId::STR_CAT_CONTROLS && setting.category != StrId::STR_CAT_SYSTEM) {
      return false;
    }
    return settingsGroupIdFor(setting) == selectedGroup;
  };
  const auto supportedOnThisBoard = [](const SettingInfo& setting) {
    if (!BoardConfig::hasTouch() && setting.nameId == StrId::STR_TOUCH_READER_CONTROLS) return false;
    if (BoardConfig::hasTouch() &&
        (setting.nameId == StrId::STR_FRONT_BTN_FOLLOW_ORIENTATION ||
         setting.nameId == StrId::STR_SUNLIGHT_FADING_FIX)) {
      return false;
    }
    return true;
  };

  const auto& baseSettings = getBaseSettingsList();
  size_t rowCount = 0;
  for (const auto& setting : baseSettings) {
    if (supportedOnThisBoard(setting) && visibleInSelectedGroup(setting)) ++rowCount;
  }
  if (selectedGroup == SettingsGroupId::Reading && !dictionaries.empty()) ++rowCount;

  const bool hasFavoriteSleep =
      selectedGroup == SettingsGroupId::Sleep &&
      (!APP_STATE.favoriteSleepImagePath.empty() ||
       (!APP_STATE.legacySleepImageDisabled && (Storage.exists("/sleep.bmp") || Storage.exists("/sleep.png"))));
  size_t actionCount = 0;
  switch (selectedGroup) {
    case SettingsGroupId::HomeAppearance:
      actionCount = 1;  // Visual Preview
      break;
    case SettingsGroupId::Sleep:
      actionCount = (hasFavoriteSleep ? 1U : 0U) +
                    (SETTINGS.sleepScreen != CrossPointSettings::SLEEP_SCREEN_MODE::FLEXIBLE_COMPOSITE ? 1U : 0U);
      break;
    case SettingsGroupId::Reading:
      actionCount = 5;
      break;
    case SettingsGroupId::Controls:
      actionCount = 1U + (BoardConfig::hasTouch() ? 0U : 2U);
      break;
    case SettingsGroupId::Connectivity:
      actionCount = 3;
#if FREEINK_CAP_BLE_HID_HOST
      ++actionCount;
#endif
      break;
    case SettingsGroupId::Library:
      actionCount = 1;
      break;
    case SettingsGroupId::System:
      actionCount = 5 + (BoardConfig::hasTouch() ? 0U : 1U);
      break;
    case SettingsGroupId::Unknown:
      break;
  }
  currentGroupSettings.reserve(rowCount + actionCount);

  for (const auto& base : baseSettings) {
    if (!supportedOnThisBoard(base) || !visibleInSelectedGroup(base)) continue;

    if (base.nameId == StrId::STR_SLEEP_SCREEN) {
      SettingInfo effective = base;
      configureSleepSetting(effective);
      currentGroupSettings.push_back(std::move(effective));
    } else if (base.nameId == StrId::STR_FONT_FAMILY) {
      currentGroupSettings.push_back(buildFontFamilySetting(&sdFontSystem.registry()));
    } else if (base.nameId == StrId::STR_INTERFACE_FONT) {
      currentGroupSettings.push_back(buildInterfaceFontSetting(&sdFontSystem.registry()));
    } else if (base.nameId == StrId::STR_DICTIONARY_FONT) {
      currentGroupSettings.push_back(buildDictionaryFontSetting());
    } else {
      currentGroupSettings.push_back(base);
    }
  }
  if (selectedGroup == SettingsGroupId::Reading && !dictionaries.empty()) {
    currentGroupSettings.push_back(buildDictionarySetting(dictionaries));
  }

  // Device-only actions are added only to the selected group's one child list.
  // They remain absent from the shared Web descriptor catalog.
  switch (selectedGroup) {
    case SettingsGroupId::HomeAppearance:
      currentGroupSettings.push_back(SettingInfo::Action(StrId::STR_VISUAL_PREVIEW, SettingAction::VisualPreview));
      break;
    case SettingsGroupId::Sleep:
      if (hasFavoriteSleep) {
        currentGroupSettings.push_back(
            SettingInfo::Action(StrId::STR_CLEAR_FAVORITE_SLEEP, SettingAction::ClearFavoriteSleepImage));
      }
      if (SETTINGS.sleepScreen != CrossPointSettings::SLEEP_SCREEN_MODE::FLEXIBLE_COMPOSITE) {
        currentGroupSettings.push_back(
            SettingInfo::Action(StrId::STR_PREVIEW_SLEEP_SCREEN, SettingAction::SleepScreenPreview));
      }
      break;
    case SettingsGroupId::Reading:
      currentGroupSettings.insert(
          currentGroupSettings.begin(), SettingInfo::Action(StrId::STR_TEXT_SETTINGS, SettingAction::TextSettings));
      currentGroupSettings.insert(currentGroupSettings.begin() + 1,
                                   SettingInfo::Action(StrId::STR_DICTIONARY_SETTINGS,
                                                       SettingAction::DictionarySettings));
      currentGroupSettings.insert(currentGroupSettings.begin() + 2,
                                   SettingInfo::Action(StrId::STR_DICTIONARY_INDEXES,
                                                       SettingAction::DictionaryIndexes));
      currentGroupSettings.insert(
          currentGroupSettings.begin() + 3, SettingInfo::Action(StrId::STR_MANAGE_FONTS, SettingAction::DownloadFonts));
      currentGroupSettings.push_back(
          SettingInfo::Action(StrId::STR_CUSTOMISE_STATUS_BAR, SettingAction::CustomiseStatusBar));
      break;
    case SettingsGroupId::Controls:
      currentGroupSettings.push_back(SettingInfo::Action(STR_QUICK_ACTIONS, SettingAction::QuickActions));
      if (!BoardConfig::hasTouch()) {
        currentGroupSettings.insert(
            currentGroupSettings.begin(), SettingInfo::Action(StrId::STR_REMAP_FRONT_BUTTONS,
                                                              SettingAction::RemapFrontButtons));
        currentGroupSettings.insert(
            currentGroupSettings.begin() + 1,
            SettingInfo::Action(StrId::STR_REMAP_READER_FRONT_BUTTONS, SettingAction::RemapReaderFrontButtons));
      }
      break;
    case SettingsGroupId::Connectivity:
      currentGroupSettings.push_back(SettingInfo::Action(StrId::STR_WIFI_NETWORKS, SettingAction::Network));
#if FREEINK_CAP_BLE_HID_HOST
      currentGroupSettings.push_back(SettingInfo::Action(StrId::STR_BLUETOOTH, SettingAction::Bluetooth));
#endif
      currentGroupSettings.push_back(SettingInfo::Action(StrId::STR_KOREADER_SYNC, SettingAction::KOReaderSync));
      currentGroupSettings.push_back(SettingInfo::Action(StrId::STR_OPDS_SERVERS, SettingAction::OPDSBrowser));
      break;
    case SettingsGroupId::Library:
      currentGroupSettings.push_back(SettingInfo::Action(StrId::STR_TODO_LIST, SettingAction::ToDoList));
      break;
    case SettingsGroupId::System:
      currentGroupSettings.push_back(
          SettingInfo::Action(StrId::STR_CLOCK_WEATHER_SYNC, SettingAction::ClockWeatherSync));
      currentGroupSettings.push_back(
          SettingInfo::Action(StrId::STR_SETTINGS_PROFILES, SettingAction::SettingsProfiles));
      currentGroupSettings.push_back(SettingInfo::Action(StrId::STR_CLEAR_READING_CACHE, SettingAction::ClearCache));
      if (!BoardConfig::hasTouch()) {
        currentGroupSettings.push_back(SettingInfo::Action(StrId::STR_CHECK_UPDATES, SettingAction::CheckForUpdates));
      }
      currentGroupSettings.push_back(
          SettingInfo::Action(StrId::STR_SD_FIRMWARE_UPDATE, SettingAction::SdFirmwareUpdate));
      currentGroupSettings.push_back(SettingInfo::Action(StrId::STR_LANGUAGE, SettingAction::Language));
      break;
    case SettingsGroupId::Unknown:
      break;
  }

  const auto reorder = [](std::vector<SettingInfo>& settings, const std::initializer_list<StrId> preferred) {
    // Move preferred rows into place in the already allocated child vector.
    // This keeps the same ordering without allocating a temporary rank table
    // or stable_sort buffer on group entry.
    auto next = settings.begin();
    for (const auto id : preferred) {
      const auto found = std::find_if(next, settings.end(), [id](const SettingInfo& setting) {
        return setting.nameId == id;
      });
      if (found == settings.end()) continue;
      std::rotate(next, found, found + 1);
      ++next;
    }
  };

  switch (selectedGroup) {
    case SettingsGroupId::HomeAppearance:
      reorder(currentGroupSettings, {StrId::STR_UI_THEME, StrId::STR_CAROUSEL_LAYOUT, StrId::STR_RECENT_BOOK_LAYOUT,
                                     StrId::STR_FINISHED_BOOK_LAYOUT, StrId::STR_INTERFACE_FONT,
                                     StrId::STR_VISUAL_PREVIEW, StrId::STR_UI_SCALE,
                                     StrId::STR_HIDE_BATTERY, StrId::STR_REFRESH_FREQ,
                                     StrId::STR_SUNLIGHT_FADING_FIX});
      break;
    case SettingsGroupId::Sleep:
      reorder(currentGroupSettings, {StrId::STR_SLEEP_SCREEN, StrId::STR_SLEEP_RANDOM_POOL,
                                     StrId::STR_SLEEP_RANDOM_ORDER, StrId::STR_PREVIEW_SLEEP_SCREEN,
                                     StrId::STR_SLEEP_COVER_MODE, StrId::STR_SLEEP_COVER_FILTER,
                                     StrId::STR_CLEAR_FAVORITE_SLEEP, StrId::STR_TODO_SLEEP_MODE,
                                     StrId::STR_QUICK_RESUME_TIMEOUT, StrId::STR_TIME_TO_SLEEP});
      break;
    case SettingsGroupId::Reading:
      reorder(currentGroupSettings, {StrId::STR_TEXT_SETTINGS, StrId::STR_PAGE_NUMBER_MODE, StrId::STR_DICTIONARY,
                                     StrId::STR_DICTIONARY_SETTINGS, StrId::STR_DICTIONARY_INDEXES,
                                     StrId::STR_MANAGE_FONTS, StrId::STR_IMAGES, StrId::STR_ORIENTATION,
                                     StrId::STR_CUSTOMISE_STATUS_BAR});
      break;
    case SettingsGroupId::Controls:
      reorder(currentGroupSettings, {StrId::STR_REMAP_FRONT_BUTTONS, StrId::STR_REMAP_READER_FRONT_BUTTONS,
                                     StrId::STR_FRONT_BTN_FOLLOW_ORIENTATION, StrId::STR_SIDE_BTN_LAYOUT,
                                     StrId::STR_SIDE_LONG_PRESS_ACTION, StrId::STR_LONG_PRESS_BEHAVIOR,
                                     StrId::STR_SHORT_PWR_BTN, StrId::STR_LONG_PWR_BTN, StrId::STR_LONG_PRESS_MENU,
                                     StrId::STR_TOUCH_READER_CONTROLS, StrId::STR_TILT_PAGE_TURN,
                                     StrId::STR_PWR_BTN_FOOTNOTE_BACK, StrId::STR_BACK_SHORT_TO_FILE_BROWSER,
                                     StrId::STR_QUICK_ACTIONS});
      break;
    case SettingsGroupId::Connectivity:
      reorder(currentGroupSettings, {StrId::STR_WIFI_NETWORKS, StrId::STR_BLUETOOTH, StrId::STR_OPDS_SERVERS,
                                     StrId::STR_KOREADER_SYNC});
      break;
    case SettingsGroupId::Library:
      reorder(currentGroupSettings, {StrId::STR_TODO_LIST, StrId::STR_SHOW_HIDDEN_FILES,
                                     StrId::STR_REMOVE_READ_FROM_RECENTS, StrId::STR_MOVE_FINISHED_TO_READ});
      break;
    case SettingsGroupId::System:
      reorder(currentGroupSettings, {StrId::STR_TIME_TO_SLEEP, StrId::STR_RESUME_READER_ON_WAKE,
                                     StrId::STR_CLOCK_WEATHER_SYNC, StrId::STR_WEATHER_SYNC_ENABLED,
                                     StrId::STR_SETTINGS_PROFILES, StrId::STR_CLEAR_READING_CACHE,
                                     StrId::STR_SHOW_HIDDEN_FILES, StrId::STR_REMOVE_READ_FROM_RECENTS,
                                     StrId::STR_MOVE_FINISHED_TO_READ, StrId::STR_CHECK_UPDATES,
                                     StrId::STR_SD_FIRMWARE_UPDATE, StrId::STR_LANGUAGE});
      break;
    case SettingsGroupId::Unknown:
      break;
  }

  settingsCount = static_cast<int>(currentGroupSettings.size());
}
void SettingsActivity::onEnter() {
  // Settings always uses the global front-button assignments. A Reader can
  // leave its reader-specific mapping active while an activity transition is
  // being prepared, so establish the screen's input context before accepting
  // input or drawing button hints.
  mappedInput.setReaderMappingMode(false);
  ignoreEntryInputUntilNeutral = hasActiveButtonInput(mappedInput);
  if (ignoreEntryInputUntilNeutral) logSettingsDiagnostic("entry_input_gate_armed");
  bluetoothLifecycleState.store(BluetoothLifecycleState::TransitionSensitive, std::memory_order_release);
#if FREEINK_CAP_BLE_HID_HOST && NOOIR_BLE_READER_COEXISTENCE
  bluetoothReturnArmed = false;
  bluetoothTransientFontCachesReleased = false;
#endif
  if (SETTINGS.bluetoothEnabled) armBluetoothReturn("activity_enter");
  logSettingsDiagnostic("onEnter_begin");
  Activity::onEnter();

  // Start at the small static group list. Child descriptors are not built
  // until the user opens a group.
  selectedCategoryIndex = 0;
  selectedGroupListIndex = 0;
  selectedSettingIndex = 0;
  groupListActive = true;
  currentGroupSettings.clear();
  currentSettings = &currentGroupSettings;
  settingsCount = 0;
  preserveQuickResumeTimeoutOn =
      SETTINGS.quickResumeSleepScreen == CrossPointSettings::QUICK_RESUME_SLEEP_SCREEN::QUICK_RESUME_AFTER_TIMEOUT;
  quickResumeTimeoutAutoEnabled = false;
  syncQuickResumeTimeoutForSleepScreen(/*sleepScreenChanged=*/true, /*quickResumeTimeoutChanged=*/false);

  logSettingsDiagnostic("group_list_ready", categoryCount);
  LOG_INF("SETDIAG", "owner=lazy_group_list groups=%u selected_children=0 capacity=%u bytes=%u",
          static_cast<unsigned>(categoryCount),
          static_cast<unsigned>(currentGroupSettings.capacity()),
          static_cast<unsigned>(currentGroupSettings.capacity() * sizeof(SettingInfo)));

  // Trigger first update
  requestUpdate();
}

void SettingsActivity::onExit() {
  logSettingsDiagnostic("onExit_begin");
  mappedInput.setReaderMappingMode(false);
  Activity::onExit();

  UITheme::getInstance().reload();  // Re-apply theme in case it was changed
  logSettingsDiagnostic("onExit_end");
}

void SettingsActivity::loop() {
  if (ignoreEntryInputUntilNeutral) {
    if (hasActiveButtonInput(mappedInput)) return;
    ignoreEntryInputUntilNeutral = false;
    logSettingsDiagnostic("entry_input_gate_cleared");
  }

  if (optionPopup.handleInput(mappedInput, [this] { requestUpdate(); })) return;
  const auto& metrics = UITheme::getInstance().getMetrics();
  const int listTop = metrics.topPadding + metrics.headerHeight +
                      (groupListActive ? 0 : metrics.tabBarHeight) + metrics.verticalSpacing;
  const int listHeight = renderer.getScreenHeight() -
                         (metrics.topPadding + metrics.headerHeight + (groupListActive ? 0 : metrics.tabBarHeight) +
                          metrics.buttonHintsHeight + metrics.verticalSpacing * 2);
  int tx = 0;
  int ty = 0;

  const auto enterSelectedGroup = [this] {
    selectedCategoryIndex = selectedGroupListIndex;
    groupListActive = false;
    selectedSettingIndex = 1;
    rebuildSettingsLists();
    if (settingsCount == 0) selectedSettingIndex = 0;
    logSettingsDiagnostic("group_enter", selectedCategoryIndex);
    LOG_INF("SETDIAG", "stage=group_enter group=%s children=%u capacity=%u bytes=%u",
            settingsGroupKey(static_cast<SettingsGroupId>(selectedCategoryIndex)),
            static_cast<unsigned>(currentGroupSettings.size()),
            static_cast<unsigned>(currentGroupSettings.capacity()),
            static_cast<unsigned>(currentGroupSettings.capacity() * sizeof(SettingInfo)));
    requestUpdate();
  };
  const auto returnToGroupList = [this] {
    groupListActive = true;
    selectedGroupListIndex = selectedCategoryIndex;
    selectedSettingIndex = 0;
    currentGroupSettings.clear();
    currentSettings = &currentGroupSettings;
    settingsCount = 0;
    logSettingsDiagnostic("group_return", selectedCategoryIndex);
    LOG_INF("SETDIAG", "stage=group_return group=%s children=0 retained_capacity=%u bytes=%u",
            settingsGroupKey(static_cast<SettingsGroupId>(selectedCategoryIndex)),
            static_cast<unsigned>(currentGroupSettings.capacity()),
            static_cast<unsigned>(currentGroupSettings.capacity() * sizeof(SettingInfo)));
    requestUpdate();
  };

  const int groupPageItems = GUI.getListPageItems(listHeight, false);
  const auto groupIndexFromPoint = [&](const int y, int& groupIndex) {
    if (y < listTop || y >= listTop + listHeight) return false;
    const int rowStep = GUI.getListRowStep(false);
    if (rowStep <= 0) return false;
    const int pageStart = selectedGroupListIndex / groupPageItems * groupPageItems;
    const int row = (y - listTop) / rowStep;
    const int touched = pageStart + row;
    if (row < 0 || row >= groupPageItems || touched < 0 || touched >= categoryCount) return false;
    groupIndex = touched;
    return true;
  };
  auto settingIndexFromPoint = [&](const int x, const int y, int& settingIndex) {
    (void)x;
    if (settingsCount <= 0 || y < listTop || y >= listTop + listHeight) return false;
    const int rowStep = GUI.getListRowStep(false);
    if (rowStep <= 0) return false;
    const int pageItems = GUI.getListPageItems(listHeight, false);
    const int selectedRow = std::max(0, selectedSettingIndex - 1);
    const int pageStart = selectedRow / pageItems * pageItems;
    const int row = (y - listTop) / rowStep;
    const int touched = pageStart + row;
    if (row < 0 || row >= pageItems || touched < 0 || touched >= settingsCount) return false;
    settingIndex = touched + 1;
    return true;
  };

  if (mappedInput.wasPressed(MappedInputManager::Button::Back)) {
    if (groupListActive) {
      logSettingsDiagnostic("return_home_requested");
      SETTINGS.saveToFile();
      onGoHome();
    } else {
      returnToGroupList();
    }
    return;
  }

  if (mappedInput.wasPressed(MappedInputManager::Button::Confirm)) {
    if (groupListActive) {
      enterSelectedGroup();
    } else if (selectedSettingIndex > 0) {
      toggleCurrentSetting();
      requestUpdate();
    }
    return;
  }

  if (mappedInput.wasScreenTouchDown(tx, ty)) {
    if (groupListActive) {
      int touchedGroup = -1;
      if (groupIndexFromPoint(ty, touchedGroup)) {
        if (selectedGroupListIndex != touchedGroup) {
          selectedGroupListIndex = touchedGroup;
          requestUpdate();
        }
        return;
      }
    } else {
      int touchedSetting = -1;
      if (settingIndexFromPoint(tx, ty, touchedSetting)) {
        if (selectedSettingIndex != touchedSetting) {
          selectedSettingIndex = touchedSetting;
          requestUpdate();
        }
        return;
      }
    }
  }

  if (mappedInput.wasScreenTapped(tx, ty)) {
    if (groupListActive) {
      int tappedGroup = -1;
      if (groupIndexFromPoint(ty, tappedGroup)) {
        selectedGroupListIndex = tappedGroup;
        enterSelectedGroup();
        return;
      }
    } else {
      int tappedSetting = -1;
      if (settingIndexFromPoint(tx, ty, tappedSetting)) {
        selectedSettingIndex = tappedSetting;
        toggleCurrentSetting();
        requestUpdate();
        return;
      }
    }
  }

  // Handle navigation
  const int settingsPageItems = groupPageItems;
  const auto swipe = mappedInput.wasSwipe();
  if (swipe == MappedInputManager::SwipeDir::Up) {
    if (groupListActive) {
      selectedGroupListIndex = ButtonNavigator::nextIndex(selectedGroupListIndex, categoryCount);
    } else {
      selectedSettingIndex = ButtonNavigator::nextPageIndex(selectedSettingIndex, settingsCount + 1,
                                                            settingsPageItems);
    }
    requestUpdate();
    return;
  }
  if (swipe == MappedInputManager::SwipeDir::Down) {
    if (groupListActive) {
      selectedGroupListIndex = ButtonNavigator::previousIndex(selectedGroupListIndex, categoryCount);
    } else {
      selectedSettingIndex = ButtonNavigator::previousPageIndex(selectedSettingIndex, settingsCount + 1,
                                                                settingsPageItems);
    }
    requestUpdate();
    return;
  }

  const auto moveToPreviousRow = [this] {
    selectedSettingIndex = ButtonNavigator::previousIndex(selectedSettingIndex, settingsCount + 1);
    requestUpdate();
  };
  const auto moveToNextRow = [this] {
    selectedSettingIndex = ButtonNavigator::nextIndex(selectedSettingIndex, settingsCount + 1);
    requestUpdate();
  };
  const auto moveToPreviousGroup = [this] {
    selectedGroupListIndex = ButtonNavigator::previousIndex(selectedGroupListIndex, categoryCount);
    requestUpdate();
  };
  const auto moveToNextGroup = [this] {
    selectedGroupListIndex = ButtonNavigator::nextIndex(selectedGroupListIndex, categoryCount);
    requestUpdate();
  };

  if (groupListActive) {
    buttonNavigator.onRelease({MappedInputManager::Button::Left}, moveToPreviousGroup);
    buttonNavigator.onRelease({MappedInputManager::Button::Right}, moveToNextGroup);
    buttonNavigator.onRelease({MappedInputManager::Button::Up}, moveToPreviousGroup);
    buttonNavigator.onRelease({MappedInputManager::Button::Down}, moveToNextGroup);
    buttonNavigator.onContinuous({MappedInputManager::Button::Left}, moveToPreviousGroup);
    buttonNavigator.onContinuous({MappedInputManager::Button::Right}, moveToNextGroup);
    buttonNavigator.onContinuous({MappedInputManager::Button::Up}, moveToPreviousGroup);
    buttonNavigator.onContinuous({MappedInputManager::Button::Down}, moveToNextGroup);
  } else {
    buttonNavigator.onRelease({MappedInputManager::Button::Left}, moveToPreviousRow);
    buttonNavigator.onRelease({MappedInputManager::Button::Right}, moveToNextRow);
    buttonNavigator.onRelease({MappedInputManager::Button::Up}, [this, settingsPageItems] {
      selectedSettingIndex = ButtonNavigator::previousPageIndex(selectedSettingIndex, settingsCount + 1,
                                                                 settingsPageItems);
      requestUpdate();
    });
    buttonNavigator.onRelease({MappedInputManager::Button::Down}, [this, settingsPageItems] {
      selectedSettingIndex = ButtonNavigator::nextPageIndex(selectedSettingIndex, settingsCount + 1,
                                                            settingsPageItems);
      requestUpdate();
    });
    buttonNavigator.onContinuous({MappedInputManager::Button::Left}, moveToPreviousRow);
    buttonNavigator.onContinuous({MappedInputManager::Button::Right}, moveToNextRow);
    buttonNavigator.onContinuous({MappedInputManager::Button::Up}, [this, settingsPageItems] {
      selectedSettingIndex = ButtonNavigator::previousPageIndex(selectedSettingIndex, settingsCount + 1,
                                                                 settingsPageItems);
      requestUpdate();
    });
    buttonNavigator.onContinuous({MappedInputManager::Button::Down}, [this, settingsPageItems] {
      selectedSettingIndex = ButtonNavigator::nextPageIndex(selectedSettingIndex, settingsCount + 1,
                                                            settingsPageItems);
      requestUpdate();
    });
  }
}

void SettingsActivity::toggleCurrentSetting() {
  int selectedSetting = selectedSettingIndex - 1;
  if (selectedSetting < 0 || selectedSetting >= settingsCount) {
    return;
  }

  const auto& setting = (*currentSettings)[selectedSetting];
  const bool sleepScreenChanged = setting.nameId == StrId::STR_SLEEP_SCREEN ||
                                  setting.valuePtr == &CrossPointSettings::sleepScreen;
  const bool quickResumeTimeoutChanged = setting.valuePtr == &CrossPointSettings::quickResumeSleepScreen;
  const bool listShapeChanged = sleepScreenChanged || setting.valuePtr == &CrossPointSettings::uiTheme ||
                                setting.valuePtr == &CrossPointSettings::shortPwrBtn;

  if (setting.nameId == StrId::STR_TIME_TO_SLEEP) {
    openSleepTimeoutPicker();
    return;
  }

  if (setting.type == SettingType::TOGGLE && setting.valuePtr != nullptr) {
    // Toggle the boolean value using the member pointer
    const bool currentValue = SETTINGS.*(setting.valuePtr);
    SETTINGS.*(setting.valuePtr) = !currentValue;
  } else if (setting.type == SettingType::ENUM && setting.valuePtr != nullptr &&
             !(setting.valueGetter && setting.valueSetter)) {
    const uint8_t currentValue = SETTINGS.*(setting.valuePtr);
    const uint8_t totalValues = setting.enumStringValues.empty()
                                    ? static_cast<uint8_t>(setting.enumValues.size())
                                    : static_cast<uint8_t>(setting.enumStringValues.size());
    if (totalValues > 2) {
      const auto valuePtr = setting.valuePtr;
      auto onSelect = [this, valuePtr, sleepScreenChanged, quickResumeTimeoutChanged, listShapeChanged](int idx) {
        SETTINGS.*valuePtr = idx;
        syncQuickResumeTimeoutForSleepScreen(sleepScreenChanged, quickResumeTimeoutChanged);
        SETTINGS.saveToFile();
        if (listShapeChanged) {
          rebuildSettingsLists();
          selectedSettingIndex = std::min(selectedSettingIndex, settingsCount);
        }
      };
      if (!setting.enumStringValues.empty()) {
        optionPopup.show(setting.nameId, setting.enumStringValues, currentValue, std::move(onSelect));
      } else {
        optionPopup.show(setting.nameId, setting.enumValues.data(), static_cast<int>(setting.enumValues.size()),
                         currentValue, std::move(onSelect));
      }
      requestUpdate();
      return;
    }
    SETTINGS.*(setting.valuePtr) = (currentValue + 1) % totalValues;
  } else if (setting.type == SettingType::ENUM && setting.valueGetter && setting.valueSetter) {
    const uint8_t totalValues = setting.enumStringValues.empty()
                                    ? static_cast<uint8_t>(setting.enumValues.size())
                                    : static_cast<uint8_t>(setting.enumStringValues.size());
    const uint8_t cur = setting.valueGetter();
    if (totalValues > 2) {
      const auto valueSetter = setting.valueSetter;
      auto onSelect = [this, valueSetter, sleepScreenChanged, quickResumeTimeoutChanged, listShapeChanged](int idx) {
        valueSetter(idx);
        syncQuickResumeTimeoutForSleepScreen(sleepScreenChanged, quickResumeTimeoutChanged);
        SETTINGS.saveToFile();
        if (listShapeChanged) {
          rebuildSettingsLists();
          selectedSettingIndex = std::min(selectedSettingIndex, settingsCount);
        }
      };
      if (!setting.enumStringValues.empty()) {
        optionPopup.show(setting.nameId, setting.enumStringValues, cur, std::move(onSelect));
      } else {
        optionPopup.show(setting.nameId, setting.enumValues.data(), static_cast<int>(setting.enumValues.size()), cur,
                         std::move(onSelect));
      }
      requestUpdate();
      return;
    }
    setting.valueSetter((cur + 1) % totalValues);
  } else if (setting.type == SettingType::VALUE && setting.valuePtr != nullptr) {
    const auto& range = setting.valueRange;
    const int minimum = range.min;
    const int maximum = range.max;
    const int step = range.step;
    const int optionCount = step > 0 && maximum >= minimum ? ((maximum - minimum) / step) + 1 : 0;

    // Bounded numeric settings get the same lightweight choice-list treatment
    // as enum settings. Binary/tiny ranges retain their existing cycle
    // behavior, while larger ranges are capped to keep the transient model
    // and dialog predictable on X3-class hardware.
    if (setting.nameId == StrId::STR_UI_SCALE && optionCount > 2 && optionCount <= kMaxValuePickerOptions) {
      std::vector<std::string> options;
      options.reserve(optionCount);
      for (int value = minimum; value <= maximum; value += step) {
        options.push_back(formatValuePickerOption(setting.nameId, value));
      }

      const int currentValue = SETTINGS.*(setting.valuePtr);
      const int currentIndex = std::clamp((currentValue - minimum) / step, 0, optionCount - 1);
      const auto valuePtr = setting.valuePtr;
      optionPopup.show(setting.nameId, options, currentIndex, [this, valuePtr, minimum, step](const int index) {
        SETTINGS.*valuePtr = static_cast<uint8_t>(minimum + index * step);
        SETTINGS.saveToFile();
      });
      requestUpdate();
      return;
    }

    const int currentValue = SETTINGS.*(setting.valuePtr);
    if (currentValue + setting.valueRange.step > setting.valueRange.max) {
      SETTINGS.*(setting.valuePtr) = setting.valueRange.min;
    } else {
      SETTINGS.*(setting.valuePtr) = currentValue + setting.valueRange.step;
    }
  } else if (setting.type == SettingType::ACTION) {
    logSettingsDiagnostic("navigation_requested", static_cast<int>(setting.action));
    auto resultHandler = [this](const ActivityResult&) {
      logSettingsDiagnostic("return_from_child");
      SETTINGS.saveToFile();
    };

    switch (setting.action) {
      case SettingAction::RemapFrontButtons:
        startActivityForResult(std::make_unique<ButtonRemapActivity>(renderer, mappedInput), resultHandler);
        break;
      case SettingAction::RemapReaderFrontButtons:
        startActivityForResult(std::make_unique<ButtonRemapActivity>(renderer, mappedInput, true), resultHandler);
        break;
      case SettingAction::CustomiseStatusBar:
        startActivityForResult(std::make_unique<StatusBarSettingsActivity>(renderer, mappedInput), resultHandler);
        break;
      case SettingAction::KOReaderSync:
        startActivityForResult(std::make_unique<KOReaderSettingsActivity>(renderer, mappedInput), resultHandler);
        break;
      case SettingAction::OPDSBrowser:
        startActivityForResult(std::make_unique<OpdsServerListActivity>(renderer, mappedInput), resultHandler);
        break;
      case SettingAction::Network:
        startActivityForResult(std::make_unique<WifiSelectionActivity>(renderer, mappedInput, false, true, false),
                               resultHandler);
        break;
#if FREEINK_CAP_BLE_HID_HOST
      case SettingAction::Bluetooth:
        startActivityForResult(
            std::make_unique<BluetoothSettingsActivity>(renderer, mappedInput), [this](const ActivityResult&) {
              logSettingsDiagnostic("return_from_child");
              SETTINGS.saveToFile();
              armBluetoothReturn("BluetoothSettings");
            });
        break;
#endif
      case SettingAction::ClockWeatherSync:
        startActivityForResult(std::make_unique<ClockSyncActivity>(renderer, mappedInput, true), resultHandler);
        break;
      case SettingAction::ToDoList:
        startActivityForResult(std::make_unique<ToDoListActivity>(renderer, mappedInput), resultHandler);
        break;
      case SettingAction::SettingsProfiles:
        startActivityForResult(std::make_unique<SettingsProfilesActivity>(renderer, mappedInput), resultHandler);
        break;
      case SettingAction::VisualPreview:
        startActivityForResult(std::make_unique<VisualPreviewActivity>(renderer, mappedInput),
                               [this](const ActivityResult&) {
                                 logSettingsDiagnostic("return_from_child");
                                 requestUpdate();
                               });
        break;
      case SettingAction::SleepScreenPreview:
        openSleepScreenPreview();
        break;
      case SettingAction::RandomSleepPool:
        openRandomSleepPoolPicker();
        break;
      case SettingAction::ClearFavoriteSleepImage:
        APP_STATE.favoriteSleepImagePath.clear();
        if (!APP_STATE.favoriteSleepImageBmpPath.empty()) {
          Storage.remove(APP_STATE.favoriteSleepImageBmpPath.c_str());
          APP_STATE.favoriteSleepImageBmpPath.clear();
        }
        APP_STATE.legacySleepImageDisabled = true;
        APP_STATE.saveToFile();
        rebuildSettingsLists();
        requestUpdate();
        break;
      case SettingAction::ClearCache:
        startActivityForResult(std::make_unique<ClearCacheActivity>(renderer, mappedInput), resultHandler);
        break;
      case SettingAction::CheckForUpdates:
        startActivityForResult(std::make_unique<OtaUpdateActivity>(renderer, mappedInput), resultHandler);
        break;
      case SettingAction::SdFirmwareUpdate:
        startActivityForResult(std::make_unique<SdFirmwareUpdateActivity>(renderer, mappedInput), resultHandler);
        break;
      case SettingAction::DownloadFonts:
        startActivityForResult(std::make_unique<FontDownloadActivity>(renderer, mappedInput),
                               [this](const ActivityResult&) {
                                  logSettingsDiagnostic("return_from_child");
                                 SETTINGS.saveToFile();
                                 rebuildSettingsLists();
                               });
        break;
      case SettingAction::TextSettings:
        startActivityForResult(std::make_unique<TextSettingsActivity>(renderer, mappedInput, &sdFontSystem.registry(),
                                                                      TextSettingsActivity::Tab::Family),
                               [this](const ActivityResult&) {
                                  logSettingsDiagnostic("return_from_child");
                                 SETTINGS.saveToFile();
                                 rebuildSettingsLists();
                               });
        break;
      case SettingAction::DictionarySettings:
        startActivityForResult(std::make_unique<TextSettingsActivity>(renderer, mappedInput, &sdFontSystem.registry(),
                                                                      TextSettingsActivity::Tab::Dictionary),
                               [this](const ActivityResult&) {
                                  logSettingsDiagnostic("return_from_child");
                                 SETTINGS.saveToFile();
                                 rebuildSettingsLists();
                               });
        break;
      case SettingAction::DictionaryIndexes:
        startActivityForResult(std::make_unique<DictionaryIndexActivity>(renderer, mappedInput), resultHandler);
        break;
      case SettingAction::Language:
        startActivityForResult(std::make_unique<LanguageSelectActivity>(renderer, mappedInput), resultHandler);
        break;
      case SettingAction::QuickActions:
        openQuickActionsPicker();
        break;
      case SettingAction::None:
        // Do nothing
        break;
    }
    return;  // Results will be handled in the result handler, so we can return early here
  } else {
    return;
  }

  syncQuickResumeTimeoutForSleepScreen(sleepScreenChanged, quickResumeTimeoutChanged);
  SETTINGS.saveToFile();
  if (listShapeChanged) {
    rebuildSettingsLists();
    selectedSettingIndex = std::min(selectedSettingIndex, settingsCount);
  }
}

void SettingsActivity::syncQuickResumeTimeoutForSleepScreen(bool sleepScreenChanged, bool quickResumeTimeoutChanged) {
  if (quickResumeTimeoutChanged) {
    preserveQuickResumeTimeoutOn =
        SETTINGS.quickResumeSleepScreen == CrossPointSettings::QUICK_RESUME_SLEEP_SCREEN::QUICK_RESUME_AFTER_TIMEOUT;
    quickResumeTimeoutAutoEnabled = false;
  }

  if (SETTINGS.sleepScreen == CrossPointSettings::SLEEP_SCREEN_MODE::QUICK_RESUME) {
    if (SETTINGS.quickResumeSleepScreen != CrossPointSettings::QUICK_RESUME_SLEEP_SCREEN::QUICK_RESUME_AFTER_TIMEOUT) {
      SETTINGS.quickResumeSleepScreen = CrossPointSettings::QUICK_RESUME_SLEEP_SCREEN::QUICK_RESUME_AFTER_TIMEOUT;
      quickResumeTimeoutAutoEnabled = !preserveQuickResumeTimeoutOn;
    } else if (sleepScreenChanged && !preserveQuickResumeTimeoutOn) {
      quickResumeTimeoutAutoEnabled = true;
    }
    return;
  }

  if (sleepScreenChanged && quickResumeTimeoutAutoEnabled && !preserveQuickResumeTimeoutOn) {
    SETTINGS.quickResumeSleepScreen = CrossPointSettings::QUICK_RESUME_SLEEP_SCREEN::QUICK_RESUME_NEVER;
    quickResumeTimeoutAutoEnabled = false;
  }
}

void SettingsActivity::openSleepScreenPreview() {
  const uint8_t selectedMode = SETTINGS.sleepScreen;
  logSettingsDiagnostic("sleep_preview_open", selectedMode);
  startActivityForResult(
      std::make_unique<SleepActivity>(renderer, mappedInput, SleepActivity::PreviewMode{selectedMode}),
      [this](const ActivityResult&) {
        logSettingsDiagnostic("sleep_preview_return");
        requestUpdate();
      });
}

void SettingsActivity::openRandomSleepPoolPicker(const int selectedIndex) {
  struct PoolOption {
    uint8_t mode;
    StrId label;
  };
  static constexpr std::array<PoolOption, RandomSleepPool::kStableModes.size()> optionsByMode = {{
      {SleepScreenModeValue::DARK, StrId::STR_DARK},
      {SleepScreenModeValue::LIGHT, StrId::STR_LIGHT},
      {SleepScreenModeValue::CUSTOM, StrId::STR_CUSTOM},
      {SleepScreenModeValue::COVER, StrId::STR_COVER},
      {SleepScreenModeValue::BLANK, StrId::STR_SLEEP_MODE_BLANK},
      {SleepScreenModeValue::QUICK_RESUME, StrId::STR_QUICK_RESUME},
      {SleepScreenModeValue::OVERLAY, StrId::STR_PAGE_OVERLAY},
      {SleepScreenModeValue::COVER_OVERLAY, StrId::STR_SLEEP_MODE_COVER_OVERLAY},
      {SleepScreenModeValue::READING_STATS_SLEEP, StrId::STR_SLEEP_MODE_READING_STATS},
      {SleepScreenModeValue::MINIMAL_STATS, StrId::STR_SLEEP_MODE_MINIMAL_STATS},
      {SleepScreenModeValue::CLIPPING_COVER, StrId::STR_SLEEP_MODE_CLIPPING_COVER},
      {SleepScreenModeValue::TODO_LIST, StrId::STR_TODO_LIST},
      {SleepScreenModeValue::READING_CALENDAR_SLEEP, StrId::STR_READING_CALENDAR_SLEEP},
      {SleepScreenModeValue::READING_SUMMARY_SLEEP, StrId::STR_READING_SUMMARY_SLEEP},
      {SleepScreenModeValue::COVER_PROGRESS, StrId::STR_SLEEP_COVER_PROGRESS},
  }};

  std::vector<std::string> labels;
  labels.reserve(optionsByMode.size() + 1);
  size_t currentIndex = 0;
  for (size_t i = 0; i < optionsByMode.size(); ++i) {
    const auto& option = optionsByMode[i];
    const bool enabled = (SETTINGS.randomSleepModeMask & RandomSleepPool::bitForMode(option.mode)) != 0;
    labels.emplace_back(enabled ? "[x] " : "[ ] ");
    labels.back() += I18N.get(option.label);
    if (static_cast<int>(i) == selectedIndex) currentIndex = i;
  }
  labels.emplace_back(tr(STR_DONE));
  const int doneIndex = static_cast<int>(optionsByMode.size());
  if (selectedIndex >= doneIndex) currentIndex = doneIndex;

  optionPopup.show(StrId::STR_SLEEP_RANDOM_POOL, labels, static_cast<int>(currentIndex), [this](int idx) {
    if (idx < 0 || idx >= static_cast<int>(optionsByMode.size())) return;
    const uint16_t selectedBit = RandomSleepPool::bitForMode(optionsByMode[static_cast<size_t>(idx)].mode);
    SETTINGS.randomSleepModeMask ^= selectedBit;
    APP_STATE.randomSleepShuffleRemainingMask = 0;
    APP_STATE.randomSleepShufflePoolMask = 0;
    SETTINGS.saveToFile();
    openRandomSleepPoolPicker(idx);
  });
  requestUpdate();
}

void SettingsActivity::openSleepTimeoutPicker() {
  logSettingsDiagnostic("navigation_requested_sleep_picker");
  startActivityForResult(
      std::make_unique<IntervalSelectionActivity>(
          renderer, mappedInput, "SleepTimeoutInterval", StrId::STR_TIME_TO_SLEEP, SETTINGS.sleepTimeoutMinutes,
          CrossPointSettings::MIN_SLEEP_TIMEOUT_MINUTES, CrossPointSettings::MAX_SLEEP_TIMEOUT_MINUTES, 1, 5,
          StrId::STR_SLEEP_TIMER_VALUE_FORMAT, false, true, StrId::STR_SLEEP_NEVER),
      [this](const ActivityResult& result) {
        logSettingsDiagnostic("return_from_child");
        if (!result.isCancelled) {
          SETTINGS.sleepTimeoutMinutes = static_cast<uint8_t>(std::get<IntervalResult>(result.data).value);
          SETTINGS.saveToFile();
        }
        requestUpdate();
      });
}

void SettingsActivity::openQuickActionsPicker() {
  std::vector<std::string> slotOptions;
  slotOptions.reserve(QuickActions::SLOT_COUNT);
  for (size_t i = 0; i < QuickActions::SLOT_COUNT; ++i) {
    std::string option = std::to_string(i + 1) + ": ";
    option += QuickActions::label(QuickActions::getSlot(i));
    slotOptions.push_back(std::move(option));
  }

  optionPopup.show(StrId::STR_QUICK_ACTIONS, slotOptions, 0, [this](const int slot) {
    if (slot < 0 || slot >= static_cast<int>(QuickActions::SLOT_COUNT)) return;

    std::vector<std::string> actionOptions;
    actionOptions.reserve(QuickActions::ACTION_COUNT + 1);
    for (const auto action : QuickActions::CONFIGURABLE_ACTIONS) {
      actionOptions.emplace_back(QuickActions::label(action));
    }
    actionOptions.emplace_back(I18N.get(StrId::STR_NONE_OPT));

    const auto current = QuickActions::getSlot(static_cast<size_t>(slot));
    int currentIndex = static_cast<int>(QuickActions::ACTION_COUNT);
    for (size_t i = 0; i < QuickActions::CONFIGURABLE_ACTIONS.size(); ++i) {
      if (QuickActions::CONFIGURABLE_ACTIONS[i] == current) {
        currentIndex = static_cast<int>(i);
        break;
      }
    }

    optionPopup.show(StrId::STR_QUICK_ACTIONS, actionOptions, currentIndex,
                     [this, slot](const int actionIndex) {
                       const auto action = actionIndex < static_cast<int>(QuickActions::ACTION_COUNT)
                                               ? QuickActions::CONFIGURABLE_ACTIONS[static_cast<size_t>(actionIndex)]
                                               : QuickActions::ActionId::None;
                       if (!QuickActions::setSlot(static_cast<size_t>(slot), action)) return;
                       switch (slot) {
                         case 0:
                           SETTINGS.quickActionSlot1 = static_cast<uint8_t>(action);
                           break;
                         case 1:
                           SETTINGS.quickActionSlot2 = static_cast<uint8_t>(action);
                           break;
                         case 2:
                           SETTINGS.quickActionSlot3 = static_cast<uint8_t>(action);
                           break;
                         default:
                           SETTINGS.quickActionSlot4 = static_cast<uint8_t>(action);
                           break;
                       }
                       SETTINGS.saveToFile();
                     });
    requestUpdate();
  });
  requestUpdate();
}

void SettingsActivity::render(RenderLock&&) {
  if (optionPopup.processRender(renderer, mappedInput)) {
    markBluetoothQuiescent();
    return;
  }

  renderer.clearScreen();

  const auto pageWidth = renderer.getScreenWidth();
  const auto pageHeight = renderer.getScreenHeight();

  const auto& metrics = UITheme::getInstance().getMetrics();

  GUI.drawHeader(renderer, Rect{0, metrics.topPadding, pageWidth, metrics.headerHeight}, tr(STR_SETTINGS_TITLE),
                 CROSSPOINT_VERSION);

  const int groupListTop = metrics.topPadding + metrics.headerHeight + metrics.verticalSpacing;
  const int groupListHeight = pageHeight - (metrics.topPadding + metrics.headerHeight +
                                             metrics.buttonHintsHeight + metrics.verticalSpacing * 2);
  if (groupListActive) {
    // This root contains only seven static labels. Children are built on
    // demand after confirmation, keeping the initial screen allocation-free.
    GUI.drawList(renderer, Rect{0, groupListTop, pageWidth, groupListHeight}, categoryCount,
                 selectedGroupListIndex,
                 [](const int index) {
                   return std::string(I18N.get(
                       settingsGroupLabelId(static_cast<SettingsGroupId>(index))));
                 }, nullptr, nullptr, nullptr, false, nullptr, nullptr,
                 [](const int) { return true; });
  } else {
    GUI.drawSubHeader(renderer,
                      Rect{0, metrics.topPadding + metrics.headerHeight, pageWidth, metrics.tabBarHeight},
                      I18N.get(settingsGroupLabelId(static_cast<SettingsGroupId>(selectedCategoryIndex))));
    const auto& settings = *currentSettings;
    GUI.drawList(
        renderer,
        Rect{0, metrics.topPadding + metrics.headerHeight + metrics.tabBarHeight + metrics.verticalSpacing, pageWidth,
             pageHeight - (metrics.topPadding + metrics.headerHeight + metrics.tabBarHeight +
                           metrics.buttonHintsHeight + metrics.verticalSpacing * 2)},
        settingsCount, selectedSettingIndex - 1,
        [&settings](int index) { return std::string(I18N.get(settings[index].nameId)); }, nullptr, nullptr,
        [&settings](int i) {
        const auto& setting = settings[i];
        std::string valueText = "";
        if (setting.type == SettingType::TOGGLE && setting.valuePtr != nullptr) {
          const bool value = SETTINGS.*(setting.valuePtr);
          valueText = value ? tr(STR_STATE_ON) : tr(STR_STATE_OFF);
        } else if (setting.type == SettingType::ENUM && setting.valuePtr != nullptr &&
                   !(setting.valueGetter && setting.valueSetter)) {
          const uint8_t value = SETTINGS.*(setting.valuePtr);
          if (!setting.enumStringValues.empty() && value < setting.enumStringValues.size()) {
            valueText = setting.enumStringValues[value];
          } else if (value < setting.enumValues.size()) {
            valueText = I18N.get(setting.enumValues[value]);
          }
        } else if (setting.type == SettingType::ENUM && setting.valueGetter) {
          const uint8_t value = setting.valueGetter();
          if (!setting.enumStringValues.empty() && value < setting.enumStringValues.size()) {
            valueText = setting.enumStringValues[value];
          } else if (value < setting.enumValues.size()) {
            valueText = I18N.get(setting.enumValues[value]);
          }
        } else if (setting.type == SettingType::VALUE && setting.valuePtr != nullptr) {
          if (setting.nameId == StrId::STR_TIME_TO_SLEEP) {
            char valueBuffer[32];
            if (SETTINGS.sleepTimeoutMinutes >= CrossPointSettings::SLEEP_TIMEOUT_NEVER_MINUTES) {
              valueText = tr(STR_SLEEP_NEVER);
            } else {
              snprintf(valueBuffer, sizeof(valueBuffer), tr(STR_SLEEP_TIMER_VALUE_FORMAT),
                       static_cast<unsigned int>(SETTINGS.*(setting.valuePtr)));
              valueText = valueBuffer;
            }
          } else {
            valueText = std::to_string(SETTINGS.*(setting.valuePtr));
            if (setting.nameId == StrId::STR_UI_SCALE) valueText += "%";
          }
        }
          return valueText;
        },
        true, nullptr,
        [&settings](const int index) -> int {
          const auto& setting = settings[index];
          if (setting.type != SettingType::TOGGLE || setting.valuePtr == nullptr) return -1;
          return SETTINGS.*(setting.valuePtr) ? 1 : 0;
        },
        [&settings](const int index) {
          const auto& setting = settings[index];
          if (setting.type == SettingType::ACTION) {
            return setting.action != SettingAction::ClearFavoriteSleepImage;
          }
          if (setting.type == SettingType::VALUE) return setting.nameId == StrId::STR_TIME_TO_SLEEP;
          if (setting.type != SettingType::ENUM) return false;
          const size_t optionCount = setting.enumStringValues.empty() ? setting.enumValues.size()
                                                                        : setting.enumStringValues.size();
          return optionCount > 2;
        });
  }

  // Draw help text
  const StrId selectedSettingId = !groupListActive && selectedSettingIndex > 0 && selectedSettingIndex <= settingsCount
                                      ? (*currentSettings)[selectedSettingIndex - 1].nameId
                                      : StrId::STR_NONE_OPT;
  const bool opensSleepPicker = selectedSettingId == StrId::STR_SLEEP_SCREEN ||
                                selectedSettingId == StrId::STR_SLEEP_RANDOM_POOL ||
                                selectedSettingId == StrId::STR_PREVIEW_SLEEP_SCREEN;
  const auto confirmLabel = groupListActive
                                ? tr(STR_SELECT)
                                : (opensSleepPicker || (selectedSettingIndex > 0 &&
                                                        (*currentSettings)[selectedSettingIndex - 1].nameId ==
                                                            StrId::STR_TIME_TO_SLEEP)
                                       ? tr(STR_SELECT)
                                       : (selectedSettingIndex > 0 &&
                                                  (*currentSettings)[selectedSettingIndex - 1].type == SettingType::VALUE
                                              ? tr(STR_SELECT)
                                              : tr(STR_TOGGLE)));

  const auto labels = mappedInput.mapLabels(tr(STR_BACK), confirmLabel, tr(STR_DIR_UP), tr(STR_DIR_DOWN));
  GUI.drawButtonHints(renderer, labels.btn1, labels.btn2, labels.btn3, labels.btn4);

  // Always use standard refresh for settings screen
  renderer.displayBuffer();
  markBluetoothQuiescent();
}
