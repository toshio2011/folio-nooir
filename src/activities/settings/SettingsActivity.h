#pragma once
#include <I18n.h>

#include <functional>
#include <atomic>
#include <cstdint>
#include <string>
#include <vector>

#include "CrossPointSettings.h"
#include "activities/Activity.h"
#include "components/OptionPopup.h"
#include "components/themes/BaseTheme.h"
#include "util/ButtonNavigator.h"

enum class SettingType { TOGGLE, ENUM, ACTION, VALUE, STRING };

enum class SettingAction {
  None,
  RemapFrontButtons,
  RemapReaderFrontButtons,
  CustomiseStatusBar,
  KOReaderSync,
  OPDSBrowser,
  Network,
  ClearCache,
  CheckForUpdates,
  SdFirmwareUpdate,
  Language,
  DownloadFonts,
  TextSettings,
  DictionarySettings,
  DictionaryIndexes,
  Bluetooth,
  ClockWeatherSync,
  ToDoList,
  SettingsProfiles,
  VisualPreview,
  SleepScreenPreview,
  RandomSleepPool,
  ClearFavoriteSleepImage,
  QuickActions,
  DictionaryPicker,
  DeviceInfo,
};

struct SettingInfo {
  StrId nameId;
  SettingType type;
  uint8_t CrossPointSettings::* valuePtr = nullptr;
  std::vector<StrId> enumValues;
  std::vector<std::string> enumStringValues;  // runtime alternative to StrId enumValues (for SD card fonts etc.)
  SettingAction action = SettingAction::None;

  struct ValueRange {
    uint8_t min;
    uint8_t max;
    uint8_t step;
  };
  ValueRange valueRange = {};

  const char* key = nullptr;             // JSON API key (nullptr for ACTION types)
  StrId category = StrId::STR_NONE_OPT;  // Category for web UI grouping
  bool obfuscated = false;               // Save/load via base64 obfuscation (passwords)
  bool inTextSettings = false;           // Surfaced in the Text Settings screen; hidden from the flat Reader list

  // Direct char[] string fields (for settings stored in CrossPointSettings)
  size_t stringOffset = 0;
  size_t stringMaxLen = 0;

  // Dynamic accessors (for settings stored outside CrossPointSettings, e.g. KOReaderCredentialStore)
  std::function<uint8_t()> valueGetter;
  std::function<void(uint8_t)> valueSetter;
  std::function<std::string()> stringGetter;
  std::function<void(const std::string&)> stringSetter;

  SettingInfo& withObfuscated() {
    obfuscated = true;
    return *this;
  }

  SettingInfo& withTextSettings() {
    inTextSettings = true;
    return *this;
  }

  static SettingInfo Toggle(StrId nameId, uint8_t CrossPointSettings::* ptr, const char* key = nullptr,
                            StrId category = StrId::STR_NONE_OPT) {
    SettingInfo s;
    s.nameId = nameId;
    s.type = SettingType::TOGGLE;
    s.valuePtr = ptr;
    s.key = key;
    s.category = category;
    return s;
  }

  static SettingInfo Enum(StrId nameId, uint8_t CrossPointSettings::* ptr, std::vector<StrId> values,
                          const char* key = nullptr, StrId category = StrId::STR_NONE_OPT) {
    SettingInfo s;
    s.nameId = nameId;
    s.type = SettingType::ENUM;
    s.valuePtr = ptr;
    s.enumValues = std::move(values);
    s.key = key;
    s.category = category;
    return s;
  }

  static SettingInfo Action(StrId nameId, SettingAction action, StrId category = StrId::STR_NONE_OPT) {
    SettingInfo s;
    s.nameId = nameId;
    s.type = SettingType::ACTION;
    s.action = action;
    s.category = category;
    return s;
  }

  static SettingInfo Value(StrId nameId, uint8_t CrossPointSettings::* ptr, const ValueRange valueRange,
                           const char* key = nullptr, StrId category = StrId::STR_NONE_OPT) {
    SettingInfo s;
    s.nameId = nameId;
    s.type = SettingType::VALUE;
    s.valuePtr = ptr;
    s.valueRange = valueRange;
    s.key = key;
    s.category = category;
    return s;
  }

  static SettingInfo String(StrId nameId, char* ptr, size_t maxLen, const char* key = nullptr,
                            StrId category = StrId::STR_NONE_OPT) {
    SettingInfo s;
    s.nameId = nameId;
    s.type = SettingType::STRING;
    s.stringOffset = (size_t)ptr - (size_t)&SETTINGS;
    s.stringMaxLen = maxLen;
    s.key = key;
    s.category = category;
    return s;
  }

  static SettingInfo DynamicEnum(StrId nameId, std::vector<StrId> values, std::function<uint8_t()> getter,
                                 std::function<void(uint8_t)> setter, const char* key = nullptr,
                                 StrId category = StrId::STR_NONE_OPT) {
    SettingInfo s;
    s.nameId = nameId;
    s.type = SettingType::ENUM;
    s.enumValues = std::move(values);
    s.valueGetter = std::move(getter);
    s.valueSetter = std::move(setter);
    s.key = key;
    s.category = category;
    return s;
  }

  static SettingInfo DynamicString(StrId nameId, std::function<std::string()> getter,
                                   std::function<void(const std::string&)> setter, const char* key = nullptr,
                                   StrId category = StrId::STR_NONE_OPT) {
    SettingInfo s;
    s.nameId = nameId;
    s.type = SettingType::STRING;
    s.stringGetter = std::move(getter);
    s.stringSetter = std::move(setter);
    s.key = key;
    s.category = category;
    return s;
  }
};

class SettingsActivity final : public Activity {
  // Settings is only resource-sensitive for an explicit lifecycle boundary.
  // A queued redraw is not itself a new boundary: BLE notifications and
  // ordinary menu updates also queue renders while the stable screen remains
  // safe to keep connected.
  enum class BluetoothLifecycleState : uint8_t {
    TransitionSensitive,
    ReturnPending,
    StableIdle,
  };

  ButtonNavigator buttonNavigator;

  int selectedCategoryIndex = 0;  // Historic top-level tab: Display, Reader, Controls, System
  int selectedSectionIndex = 0;   // Accordion section within the selected tab
  int selectedSettingIndex = 0;
  int settingsCount = 0;
  int loadedSectionIndex = -1;
  int loadedTabIndex = -1;
  bool sectionExpanded = false;
  bool hasFavoriteSleepImage = false;
  uint8_t settingsUiThemeAtEntry = 0;

  // Only the selected group's children are materialized; switching groups
  // clears and reuses this single descriptor vector.
  std::vector<SettingInfo> currentGroupSettings;
  const std::vector<SettingInfo>* currentSettings = nullptr;
  std::vector<TabInfo> tabItems;

  bool preserveQuickResumeTimeoutOn = false;
  bool quickResumeTimeoutAutoEnabled = false;
  // The shared input manager can still carry a source screen's edge/held
  // button when Settings is entered. Ignore it until the controls return idle.
  bool ignoreEntryInputUntilNeutral = false;

  OptionPopup optionPopup;

  // Settings may need BLE stopped for its initial/returned full-screen render,
  // but a stable parent screen is safe for the normal event-driven admission
  // path. The render task publishes the transition boundary exactly once.
  std::atomic<BluetoothLifecycleState> bluetoothLifecycleState{
      BluetoothLifecycleState::TransitionSensitive};
  std::atomic<uint32_t> bluetoothLifecycleEpoch{0};
#if FREEINK_CAP_BLE_HID_HOST && NOOIR_BLE_READER_COEXISTENCE
  // One stable-idle reclaim for the return from Bluetooth Settings. This is
  // deliberately narrower than the Reader cache release: only the built-in
  // compressed-font scratch buffers are dropped and rebuilt on the next draw.
  bool bluetoothReturnArmed = false;
  bool bluetoothTransientFontCachesReleased = false;
#endif

  static constexpr int categoryCount = 4;
  static const StrId categoryNames[categoryCount];

  int visibleRowCount() const;
  int selectedVisibleRow() const;
  void selectVisibleRow(int rowIndex);
  void switchCategory(int categoryIndex);
  void toggleAccordion();
  void openDictionaryPicker();
  void toggleCurrentSetting();
  void openSleepTimeoutPicker();
  void openSleepScreenPreview();
  void openRandomSleepPoolPicker(int selectedIndex = 0);
  void openQuickActionsPicker();
  void rebuildSettingsLists();
  void syncQuickResumeTimeoutForSleepScreen(bool sleepScreenChanged, bool quickResumeTimeoutChanged);
  void armBluetoothReturn(const char* reason);
  void markBluetoothQuiescent();
#if FREEINK_CAP_BLE_HID_HOST && NOOIR_BLE_READER_COEXISTENCE
  void reclaimStableBluetoothTransientCaches();
#endif

 public:
  explicit SettingsActivity(GfxRenderer& renderer, MappedInputManager& mappedInput);
  ~SettingsActivity() override;
  void onEnter() override;
  void onExit() override;
  void loop() override;
  void render(RenderLock&&) override;
  // Consumes the one-shot Settings return transition after NimBLE has been
  // admitted successfully. This prevents a notification/redraw from being
  // mistaken for another resource-sensitive transition.
  void markBluetoothAdmitted();
  // Settings rebuilds several transient lists and can open heavier child
  // screens; release the optional BLE controller until the full-screen render
  // has completed. A stable parent then uses normal event-driven admission.
  bool bluetoothResourceSensitive() const override {
    // Do not fold ActivityManager's generic render queue into this predicate.
    // A BLE notification itself queues a Settings redraw; treating that queue
    // as permanent pressure creates the connect -> notify -> stop loop.
    return bluetoothLifecycleState.load(std::memory_order_acquire) ==
           BluetoothLifecycleState::TransitionSensitive;
  }
};
