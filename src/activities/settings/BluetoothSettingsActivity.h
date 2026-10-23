#pragma once

#include <I18n.h>

#include <string>
#include <vector>
#include <cstdint>
#include <atomic>

#include "activities/Activity.h"
#include "components/OptionPopup.h"
#include "util/ButtonNavigator.h"

// Bluetooth page-turner settings. One screen with three views:
//   Menu   — enable/disable BT, scan & pair, disconnect, map buttons, presets,
//            and the bounded input diagnostic ring.
//   Scan   — live list of discovered BLE HID devices; Confirm connects.
//   Paired — bonded devices; Confirm forgets the selected one.
// All BLE access goes through the FreeInk BleHid singleton; everything no-ops
// gracefully when BLE is compiled out (BleHid.begin() returns false).
class BluetoothSettingsActivity final : public Activity {
 public:
  enum class Mode : uint8_t { Settings, ReaderControl };

  explicit BluetoothSettingsActivity(GfxRenderer& renderer, MappedInputManager& mappedInput,
                                     const Mode mode = Mode::Settings)
      : Activity("BluetoothSettings", renderer, mappedInput), mode(mode) {}

  void onEnter() override;
  void onExit() override;
  void loop() override;
  void render(RenderLock&&) override;
  bool keepsBluetoothAlive() const override { return true; }
  bool bluetoothResourceSensitive() const override;
  void onBluetoothActivationResult(bool started);

 private:
  enum class View { Menu, Scan, Paired, Diagnostics };
  enum class ConnectOrigin : uint8_t { None, Scan, PairedScan, PairedDirect };

  // Menu row actions.
  enum class Action {
    ToggleBt,
    NoDeviceTimeout,
    Scan,
    Disconnect,
    MapButtons,
    PairedDevices,
    Diagnostics,
    PresetFree2,
    PresetFree3,
    PresetYiser,
    ResetDefaults,
    ClearMap
  };
  struct MenuRow {
    Action action;
    StrId label;
  };

  View view = View::Menu;
  enum class ActivationState : uint8_t { Ready, Preparing, Failed };
  const Mode mode;
  std::atomic<ActivationState> activationState{ActivationState::Ready};
  std::atomic<int8_t> activationResult{-1};
  std::atomic<bool> controlledRetryUsed{false};
  std::atomic<bool> preparationReclaimDone{false};
  std::atomic<int> failureIndex{0};
  std::vector<MenuRow> menuRows;
  int menuIndex = 0;
  int scanIndex = 0;
  int pairedIndex = 0;

  ButtonNavigator buttonNavigator;
  OptionPopup noDeviceTimeoutPopup;

  // Transient status banner (connect result, forget confirmation, etc.).
  std::string banner;
  unsigned long bannerUntil = 0;

  // Set when a connect() has been issued and we're waiting for the async result.
  bool awaitingConnect = false;
  bool pairedScanActive = false;
  bool pairedFallbackAttempted = false;
  ConnectOrigin connectOrigin = ConnectOrigin::None;
  char pairedTargetAddr[18] = {};
  char pairedTargetName[32] = {};
  // Guards the Paired view's hold-to-forget so it fires once per hold and suppresses
  // the tap-to-connect on the same press.
  bool pairedActionTaken = false;

  void rebuildMenuRows();
  void finishCancelled();
  void handleMenuConfirm();
  void beginPreparation(bool controlledRetry);
  void startScanView();
  void beginPairedConnect();
  void applyPreset(bool free3);
  void applyYiserPreset();
  void resetMappingsToDefaults();
  void setBanner(const char* text);

  std::string deviceLabel(int index) const;   // scan list row text
  std::string pairedLabel(int index) const;    // paired list row text
};
