#include "BluetoothSettingsActivity.h"

#include <BleKeyboardHost.h>
#include <BoardConfig.h>
#include <FontCacheManager.h>
#include <GfxRenderer.h>
#include <Logging.h>

#include <algorithm>
#include <cstdio>
#include <cstring>

#include "BleButtonMapActivity.h"
#include "BleInput.h"
#include "CrossPointSettings.h"
#include "MappedInputManager.h"
#include "activities/ActivityManager.h"
#include "components/UITheme.h"
#include "fontIds.h"
#include "SdCardFontSystem.h"
#include "util/BleMemoryPolicy.h"

namespace {
constexpr unsigned long kBannerMs = 2000;
constexpr uint32_t kScanMs = 8000;
constexpr unsigned long kForgetHoldMs = 1200;  // hold Confirm this long in the Paired view to forget
constexpr StrId kNoDeviceTimeoutOptions[] = {StrId::STR_BT_TIMEOUT_NEVER, StrId::STR_BT_TIMEOUT_30_SEC,
                                              StrId::STR_BT_TIMEOUT_60_SEC, StrId::STR_BT_TIMEOUT_90_SEC};
constexpr uint16_t kNoDeviceTimeoutSeconds[] = {0, 30, 60, 90};

int noDeviceTimeoutOptionIndex(const uint16_t seconds) {
  for (int i = 0; i < 4; ++i) {
    if (kNoDeviceTimeoutSeconds[i] == seconds) return i;
  }
  return 2;
}

const char* noDeviceTimeoutLabel(const uint16_t seconds) {
  return I18N.get(kNoDeviceTimeoutOptions[noDeviceTimeoutOptionIndex(seconds)]);
}
}  // namespace

bool BluetoothSettingsActivity::bluetoothResourceSensitive() const {
  // This screen is lightweight. ActivityManager separately evaluates the
  // underlying Reader's live build/render state, and BLE admission still checks
  // RenderLock, pending render work, WiFi exclusion, and global heap floors.
  return false;
}

void BluetoothSettingsActivity::onEnter() {
  Activity::onEnter();
  activityManager.setBluetoothSettingsActivationFailed(false);
  view = View::Menu;
  activationState.store(ActivationState::Ready, std::memory_order_release);
  activationResult.store(-1, std::memory_order_release);
  controlledRetryUsed.store(false, std::memory_order_release);
  preparationReclaimDone.store(false, std::memory_order_release);
  failureIndex.store(0, std::memory_order_release);
  menuIndex = 0;
  rebuildMenuRows();
  const bool wanted = mode == Mode::ReaderSession ? bleinput::readerSessionBluetoothEnabled()
                                                  : SETTINGS.bluetoothEnabled != 0;
  if (wanted && !BleHid.isRunning()) beginPreparation(false);
  else requestUpdate();
}

void BluetoothSettingsActivity::onExit() {
  activityManager.setBluetoothSettingsActivationFailed(false);
  if (BleHid.isScanning()) BleHid.stopScan();
  Activity::onExit();
}

void BluetoothSettingsActivity::finishCancelled() {
  bleinput::clearReAdmissionPending();
  ActivityResult cancelled;
  cancelled.isCancelled = true;
  setResult(std::move(cancelled));
  finish();
}

void BluetoothSettingsActivity::setBanner(const char* text) {
  banner = text ? text : "";
  bannerUntil = millis() + kBannerMs;
}

void BluetoothSettingsActivity::beginPreparation(const bool controlledRetry) {
  // Initial activation and the explicit Retry action both permit exactly one
  // post-render lifecycle attempt. A previously failed screen stays latched
  // until this point.
  activityManager.setBluetoothSettingsActivationFailed(false);
  bleinput::clearNoDeviceTimeoutSuppression();
  controlledRetryUsed.store(controlledRetry, std::memory_order_release);
  activationResult.store(-1, std::memory_order_release);
  activationState.store(ActivationState::Preparing, std::memory_order_release);
  preparationReclaimDone.store(false, std::memory_order_release);
  failureIndex.store(0, std::memory_order_release);
  if (BleHid.isRunning()) {
    onBluetoothActivationResult(true);
    return;
  }
  bleinput::requestLifecycleReevaluation();
  requestUpdate();
}

void BluetoothSettingsActivity::onBluetoothActivationResult(const bool started) {
  if (activationState.load(std::memory_order_acquire) != ActivationState::Preparing) return;
  activityManager.setBluetoothSettingsActivationFailed(!started);
  activationResult.store(started ? 1 : 0, std::memory_order_release);
  requestUpdate();
}

void BluetoothSettingsActivity::rebuildMenuRows() {
  menuRows.clear();
  menuRows.reserve(mode == Mode::ReaderSession ? 1 : 12);
  menuRows.push_back({Action::ToggleBt, StrId::STR_BLUETOOTH});
  if (mode == Mode::ReaderSession) {
    menuIndex = 0;
    return;
  }
  menuRows.push_back({Action::NoDeviceTimeout, StrId::STR_BT_NO_DEVICE_TIMEOUT});
  if (SETTINGS.bluetoothEnabled) {
    menuRows.push_back({Action::Scan, StrId::STR_BT_SCAN_PAIR});
    if (BleHid.isConnected()) menuRows.push_back({Action::Disconnect, StrId::STR_BT_DISCONNECT});
    menuRows.push_back({Action::PairedDevices, StrId::STR_BT_PAIRED_DEVICES});
    menuRows.push_back({Action::MapButtons, StrId::STR_BT_MAP_BUTTONS});
#if FREEINK_CAP_BLE_HID_HOST
    // Keep the diagnostic entry English-only for now so this BLE experiment
    // does not require regenerating every language resource.
    menuRows.push_back({Action::Diagnostics, StrId::STR_BLUETOOTH});
#endif
    menuRows.push_back({Action::PresetFree2, StrId::STR_BT_PRESET_FREE2});
    menuRows.push_back({Action::PresetFree3, StrId::STR_BT_PRESET_FREE3});
    menuRows.push_back({Action::PresetYiser, StrId::STR_BT_PRESET_YISER});
    menuRows.push_back({Action::ResetDefaults, StrId::STR_BT_RESET_MAP_DEFAULTS});
    menuRows.push_back({Action::ClearMap, StrId::STR_BT_CLEAR_MAP});
  }
  if (menuIndex >= static_cast<int>(menuRows.size())) menuIndex = 0;
}

void BluetoothSettingsActivity::applyPreset(bool free3) {
  // Starter presets. Page-turner remotes commonly emit PageUp/PageDown (and a
  // center key on the 3-button Free3); the user can re-map via "Map Remote
  // Buttons" if their device sends different codes.
  using Btn = MappedInputManager::Button;
  SETTINGS.bleControllerPreset = CrossPointSettings::BLE_CONTROLLER_PRESET_GENERIC;
  BleHid.setInputPreset(freeink::InputPreset::GenericHid);
  for (auto& e : SETTINGS.bleKeyMap) e = CrossPointSettings::BleKeyMapEntry{};
  auto set = [&](int slot, freeink::SpecialKey key, Btn button) {
    SETTINGS.bleKeyMap[slot].keyKind = 0;  // SpecialKey
    SETTINGS.bleKeyMap[slot].keyValue = static_cast<uint8_t>(key);
    SETTINGS.bleKeyMap[slot].button = static_cast<uint8_t>(button);
    SETTINGS.bleKeyMap[slot].signatureCount = 1;
    SETTINGS.bleKeyMap[slot].signature[0] =
        static_cast<uint16_t>(static_cast<uint8_t>(key)) | (static_cast<uint16_t>(0) << 8);
  };
  set(0, freeink::SpecialKey::PageDown, Btn::PageForward);
  set(1, freeink::SpecialKey::PageUp, Btn::PageBack);
  if (free3) set(2, freeink::SpecialKey::Enter, Btn::Confirm);
  SETTINGS.saveToFile();
}

void BluetoothSettingsActivity::applyYiserPreset() {
  using Btn = MappedInputManager::Button;
  SETTINGS.bleControllerPreset = CrossPointSettings::BLE_CONTROLLER_PRESET_YISER_J6_RING;
  BleHid.setInputPreset(freeink::InputPreset::YiserJ6Ring);
  for (auto& e : SETTINGS.bleKeyMap) e = CrossPointSettings::BleKeyMapEntry{};
  auto set = [&](int slot, freeink::SpecialKey key, Btn button) {
    SETTINGS.bleKeyMap[slot].keyKind = 0;  // normalized SpecialKey
    SETTINGS.bleKeyMap[slot].keyValue = static_cast<uint8_t>(key);
    SETTINGS.bleKeyMap[slot].button = static_cast<uint8_t>(button);
    SETTINGS.bleKeyMap[slot].signatureCount = 1;
    SETTINGS.bleKeyMap[slot].signature[0] =
        static_cast<uint16_t>(static_cast<uint8_t>(key));
  };
  set(0, freeink::SpecialKey::Up, Btn::Up);
  set(1, freeink::SpecialKey::Down, Btn::Down);
  set(2, freeink::SpecialKey::Left, Btn::Left);
  set(3, freeink::SpecialKey::Right, Btn::Right);
  set(4, freeink::SpecialKey::Enter, Btn::Confirm);
  set(5, freeink::SpecialKey::Escape, Btn::Back);
  SETTINGS.saveToFile();
}

void BluetoothSettingsActivity::resetMappingsToDefaults() {
  SETTINGS.bleControllerPreset = CrossPointSettings::BLE_CONTROLLER_PRESET_GENERIC;
  BleHid.setInputPreset(freeink::InputPreset::GenericHid);
  const bleinput::mapping::DefaultBinding defaults[] = {
      {{0, static_cast<uint8_t>(freeink::SpecialKey::Right)}, bleinput::mapping::kButtonAction,
       static_cast<uint8_t>(MappedInputManager::Button::Right)},
      {{0, static_cast<uint8_t>(freeink::SpecialKey::Left)}, bleinput::mapping::kButtonAction,
       static_cast<uint8_t>(MappedInputManager::Button::Left)},
      {{0, static_cast<uint8_t>(freeink::SpecialKey::PageDown)}, bleinput::mapping::kButtonAction,
       static_cast<uint8_t>(MappedInputManager::Button::Right)},
      {{0, static_cast<uint8_t>(freeink::SpecialKey::PageUp)}, bleinput::mapping::kButtonAction,
       static_cast<uint8_t>(MappedInputManager::Button::Left)},
      {{0, static_cast<uint8_t>(freeink::SpecialKey::Enter)}, bleinput::mapping::kButtonAction,
       static_cast<uint8_t>(MappedInputManager::Button::Confirm)},
      {{0, static_cast<uint8_t>(freeink::SpecialKey::Escape)}, bleinput::mapping::kButtonAction,
       static_cast<uint8_t>(MappedInputManager::Button::Back)},
  };
  bleinput::mapping::resetToDefaults(SETTINGS.bleKeyMap, defaults,
                                     static_cast<uint8_t>(sizeof(defaults) / sizeof(defaults[0])));
  SETTINGS.saveToFile();
}

void BluetoothSettingsActivity::startScanView() {
  bleinput::clearNoDeviceTimeoutSuppression();
  if (!bleinput::connectionAdmissionAllowed()) {
    setBanner(tr(STR_BT_START_FAILED));
    requestUpdate();
    return;
  }
  if (!BleHid.isRunning() && !bleinput::ensureStarted()) {
    // A failed scan startup is a runtime failure, not a user preference
    // change. Keep saved ON intent so a later explicit retry can recover.
    setBanner(tr(STR_BT_START_FAILED));
    requestUpdate();
    return;
  }
  view = View::Scan;
  scanIndex = 0;
  awaitingConnect = false;
  pairedScanActive = false;
  connectOrigin = ConnectOrigin::None;
  BleHid.startScan(kScanMs);
  requestUpdate();
}

void BluetoothSettingsActivity::beginPairedConnect() {
  bleinput::clearNoDeviceTimeoutSuppression();
  if (pairedIndex >= BleHid.pairedCount()) return;
  if (!bleinput::connectionAdmissionAllowed()) {
    setBanner(tr(STR_BT_START_FAILED));
    requestUpdate();
    return;
  }
  if (!BleHid.isRunning() && !bleinput::ensureStarted()) {
    setBanner(tr(STR_BT_START_FAILED));
    requestUpdate();
    return;
  }
  const auto& paired = BleHid.paired(static_cast<uint8_t>(pairedIndex));
  strncpy(pairedTargetAddr, paired.addr, sizeof(pairedTargetAddr) - 1);
  pairedTargetAddr[sizeof(pairedTargetAddr) - 1] = '\0';
  strncpy(pairedTargetName, paired.name, sizeof(pairedTargetName) - 1);
  pairedTargetName[sizeof(pairedTargetName) - 1] = '\0';
  bleinput::recordDiagnosticEvent("bond_found addr=%.17s name=%.24s type=%u", pairedTargetAddr,
                                  pairedTargetName, static_cast<unsigned>(paired.addrType));
  awaitingConnect = true;
  pairedScanActive = false;
  pairedFallbackAttempted = false;
  connectOrigin = ConnectOrigin::PairedDirect;
  setBanner(tr(STR_CONNECTING));
  if (BleHid.isConnected()) {
    awaitingConnect = false;
    setBanner(tr(STR_CONNECTED));
    requestUpdate();
    return;
  }
  if (BleHid.isConnecting()) {
    bleinput::recordDiagnosticEvent("reconnect_join_inflight direct addr=%.17s", pairedTargetAddr);
    requestUpdate();
    return;
  }
  // The host already knows the bonded address type. Try that stable identity
  // first instead of making every manual reconnect depend on a scan response;
  // if the peripheral is using a rotated/private address, the bounded scan
  // fallback below can still recover it by address or name.
  const bool queued = BleHid.connect(pairedTargetAddr);
  if (queued) {
    bleinput::recordDiagnosticEvent("reconnect_start direct addr=%.17s", pairedTargetAddr);
  } else if (BleHid.isConnecting() || BleHid.isConnected()) {
    bleinput::recordDiagnosticEvent("reconnect_join_inflight direct addr=%.17s", pairedTargetAddr);
  }
  if (!queued && !BleHid.isConnecting() && !BleHid.isConnected()) {
    pairedFallbackAttempted = true;
    pairedScanActive = true;
    connectOrigin = ConnectOrigin::PairedScan;
    setBanner(tr(STR_SCANNING));
    BleHid.startScan(kScanMs);
  }
  requestUpdate();
}

void BluetoothSettingsActivity::handleMenuConfirm() {
  if (menuRows.empty()) return;
  const Action action = menuRows[menuIndex].action;
  switch (action) {
    case Action::ToggleBt:
      if (mode == Mode::ReaderSession) {
        const bool enabled = !bleinput::readerSessionBluetoothEnabled();
        bleinput::setReaderSessionBluetoothEnabled(enabled);
        if (enabled && !BleHid.isRunning()) beginPreparation(false);
        else {
          activityManager.setBluetoothSettingsActivationFailed(false);
          activationState.store(ActivationState::Ready, std::memory_order_release);
          rebuildMenuRows();
          requestUpdate();
        }
      } else {
        bleinput::setBluetoothEnabled(!SETTINGS.bluetoothEnabled);
        if (SETTINGS.bluetoothEnabled && !BleHid.isRunning()) beginPreparation(false);
        else {
          activityManager.setBluetoothSettingsActivationFailed(false);
          activationState.store(ActivationState::Ready, std::memory_order_release);
          rebuildMenuRows();
          requestUpdate();
        }
      }
      break;
    case Action::NoDeviceTimeout:
      noDeviceTimeoutPopup.show(StrId::STR_BT_NO_DEVICE_TIMEOUT, kNoDeviceTimeoutOptions, 4,
                                noDeviceTimeoutOptionIndex(SETTINGS.bluetoothNoDeviceTimeoutSeconds),
                                [this](const int index) {
                                  if (index < 0 || index >= 4) return;
                                  SETTINGS.bluetoothNoDeviceTimeoutSeconds = kNoDeviceTimeoutSeconds[index];
                                  SETTINGS.saveToFile();
                                });
      requestUpdate();
      break;
    case Action::Scan:
      startScanView();
      break;
    case Action::Disconnect:
      bleinput::suppressNextDisconnectNotice();
      BleHid.disconnect();
      setBanner(tr(STR_BT_NOT_CONNECTED));
      rebuildMenuRows();
      requestUpdate();
      break;
    case Action::PairedDevices:
      view = View::Paired;
      pairedIndex = 0;
      requestUpdate();
      break;
    case Action::Diagnostics:
      view = View::Diagnostics;
      requestUpdate();
      break;
    case Action::MapButtons:
      startActivityForResult(std::make_unique<BleButtonMapActivity>(renderer, mappedInput),
                             [this](const ActivityResult&) {
                               rebuildMenuRows();
                               requestUpdate();
                             });
      break;
    case Action::PresetFree2:
      applyPreset(false);
      setBanner(tr(STR_BT_PRESET_FREE2));
      requestUpdate();
      break;
    case Action::PresetFree3:
      applyPreset(true);
      setBanner(tr(STR_BT_PRESET_FREE3));
      requestUpdate();
      break;
    case Action::PresetYiser:
      applyYiserPreset();
      setBanner(tr(STR_BT_PRESET_YISER));
      requestUpdate();
      break;
    case Action::ResetDefaults:
      resetMappingsToDefaults();
      setBanner("Default remote mappings restored");
      requestUpdate();
      break;
    case Action::ClearMap:
      for (auto& e : SETTINGS.bleKeyMap) e = CrossPointSettings::BleKeyMapEntry{};
      SETTINGS.saveToFile();
      setBanner(tr(STR_BT_CLEAR_MAP));
      requestUpdate();
      break;
  }
}

void BluetoothSettingsActivity::loop() {
  if (noDeviceTimeoutPopup.handleInput(mappedInput, [this] { requestUpdate(); })) return;

  const int8_t activationEvent = activationResult.exchange(-1, std::memory_order_acq_rel);
  if (activationEvent >= 0 && activationState.load(std::memory_order_acquire) == ActivationState::Preparing) {
    if (activationEvent == 1 && BleHid.isRunning()) {
      activationState.store(ActivationState::Ready, std::memory_order_release);
      rebuildMenuRows();
      LOG_INF("BLEMEM", "stage=bluetooth_settings_ready mode=%s free=%u largest=%u",
              mode == Mode::ReaderSession ? "reader" : "settings", ESP.getFreeHeap(), ESP.getMaxAllocHeap());
    } else {
      activationState.store(ActivationState::Failed, std::memory_order_release);
      failureIndex.store(0, std::memory_order_release);
      LOG_ERR("BLEMEM", "stage=bluetooth_settings_failed mode=%s retry_used=%d free=%u largest=%u",
              mode == Mode::ReaderSession ? "reader" : "settings",
              controlledRetryUsed.load(std::memory_order_acquire) ? 1 : 0, ESP.getFreeHeap(), ESP.getMaxAllocHeap());
    }
    requestUpdate();
  }

  if (activationState.load(std::memory_order_acquire) == ActivationState::Preparing) {
    if (mappedInput.wasPressed(MappedInputManager::Button::Back)) {
      finishCancelled();
      return;
    }
    const auto metrics = UITheme::getInstance().getMetrics();
    const Rect screen = UITheme::getInstance().getScreenSafeArea(renderer, true, false);
    const int contentTop = screen.y + metrics.topPadding + metrics.headerHeight + metrics.tabBarHeight +
                           metrics.verticalSpacing;
    const int contentHeight = screen.height - contentTop - metrics.verticalSpacing;
    int selectedOption = std::clamp(failureIndex.load(std::memory_order_acquire), 0, 1);
    failureIndex.store(selectedOption, std::memory_order_release);
    const auto activateOption = [this] {
      if (failureIndex.load(std::memory_order_acquire) == 0) {
        // Keep an explicit escape hatch while admission or NimBLE startup is
        // pending. OFF cancels startup/reconnect through the same idempotent
        // path used by the normal Settings toggle.
        if (mode == Mode::ReaderSession) {
          bleinput::setReaderSessionBluetoothEnabled(false);
        } else {
          bleinput::setBluetoothEnabled(false);
        }
        activityManager.setBluetoothSettingsActivationFailed(false);
        activationResult.store(-1, std::memory_order_release);
        activationState.store(ActivationState::Ready, std::memory_order_release);
        failureIndex.store(0, std::memory_order_release);
        rebuildMenuRows();
        requestUpdate();
      } else {
        finishCancelled();
      }
    };
    const auto touchResult = handleListTouch(selectedOption, 2, contentTop, contentHeight, false);
    failureIndex.store(selectedOption, std::memory_order_release);
    if (touchResult == ListTouchResult::Activated) {
      activateOption();
      return;
    }
    if (touchResult == ListTouchResult::Consumed) return;
    buttonNavigator.onNext([this] {
      failureIndex.store(ButtonNavigator::nextIndex(failureIndex.load(std::memory_order_acquire), 2),
                         std::memory_order_release);
      requestUpdate();
    });
    buttonNavigator.onPrevious([this] {
      failureIndex.store(ButtonNavigator::previousIndex(failureIndex.load(std::memory_order_acquire), 2),
                         std::memory_order_release);
      requestUpdate();
    });
    if (mappedInput.wasPressed(MappedInputManager::Button::Confirm)) activateOption();
    return;
  }

  if (activationState.load(std::memory_order_acquire) == ActivationState::Failed) {
    if (mappedInput.wasPressed(MappedInputManager::Button::Back)) {
      finishCancelled();
      return;
    }
    const bool retryAvailable = !controlledRetryUsed.load(std::memory_order_acquire);
    const int offOptionIndex = retryAvailable ? 1 : 0;
    const int optionCount = retryAvailable ? 3 : 2;
    int selectedFailureOption = std::clamp(failureIndex.load(std::memory_order_acquire), 0, optionCount - 1);
    failureIndex.store(selectedFailureOption, std::memory_order_release);
    const auto metrics = UITheme::getInstance().getMetrics();
    const Rect screen = UITheme::getInstance().getScreenSafeArea(renderer, true, false);
    const int contentTop = screen.y + metrics.topPadding + metrics.headerHeight + metrics.tabBarHeight +
                           metrics.verticalSpacing;
    const int contentHeight = screen.height - contentTop - metrics.verticalSpacing;
    const auto activateFailureOption = [this, retryAvailable, offOptionIndex] {
      const int selected = failureIndex.load(std::memory_order_acquire);
      if (retryAvailable && selected == 0) {
        beginPreparation(true);
        return;
      }
      if (selected == offOptionIndex) {
        // Failure must not trap the user with Bluetooth left enabled. The
        // Reader page controls only its temporary override; Settings persists
        // the explicit OFF choice.
        if (mode == Mode::ReaderSession) {
          bleinput::setReaderSessionBluetoothEnabled(false);
        } else {
          bleinput::setBluetoothEnabled(false);
        }
        activityManager.setBluetoothSettingsActivationFailed(false);
        activationResult.store(-1, std::memory_order_release);
        activationState.store(ActivationState::Ready, std::memory_order_release);
        failureIndex.store(0, std::memory_order_release);
        rebuildMenuRows();
        requestUpdate();
        return;
      }
      finishCancelled();
    };
    const auto touchResult = handleListTouch(selectedFailureOption, optionCount, contentTop, contentHeight, false);
    failureIndex.store(selectedFailureOption, std::memory_order_release);
    switch (touchResult) {
      case ListTouchResult::Activated:
        activateFailureOption();
        return;
      case ListTouchResult::Consumed:
        return;
      case ListTouchResult::None:
        break;
    }
    buttonNavigator.onNext([this, optionCount] {
      failureIndex.store(ButtonNavigator::nextIndex(failureIndex.load(std::memory_order_acquire), optionCount),
                         std::memory_order_release);
      requestUpdate();
    });
    buttonNavigator.onPrevious([this, optionCount] {
      failureIndex.store(ButtonNavigator::previousIndex(failureIndex.load(std::memory_order_acquire), optionCount),
                         std::memory_order_release);
      requestUpdate();
    });
    if (mappedInput.wasPressed(MappedInputManager::Button::Confirm)) activateFailureOption();
    return;
  }

  // Clear an expired status banner.
  if (bannerUntil > 0 && static_cast<int32_t>(millis() - bannerUntil) >= 0) {
    banner.clear();
    bannerUntil = 0;
    requestUpdate();
  }

  // Watch for an async connect result (from either the scan list or the paired list).
  if (awaitingConnect) {
    if (!bleinput::connectionAdmissionAllowed()) {
      if (BleHid.isScanning()) BleHid.stopScan();
      awaitingConnect = false;
      pairedScanActive = false;
      connectOrigin = ConnectOrigin::None;
      setBanner(tr(STR_BT_START_FAILED));
      requestUpdate();
      return;
    }
    if (pairedScanActive && !BleHid.isConnected()) {
      if (BleHid.isScanning()) {
        int match = -1;
        for (int i = 0; i < BleHid.deviceCount(); ++i) {
          const auto& device = BleHid.device(static_cast<uint8_t>(i));
          const bool addressMatch = pairedTargetAddr[0] != '\0' && strcmp(device.addr, pairedTargetAddr) == 0;
          const bool nameMatch = pairedTargetName[0] != '\0' && strcmp(device.name, pairedTargetName) == 0;
          if (addressMatch || nameMatch) {
            match = i;
            break;
          }
        }
        if (match >= 0) {
          char address[18];
          strncpy(address, BleHid.device(static_cast<uint8_t>(match)).addr, sizeof(address) - 1);
          address[sizeof(address) - 1] = '\0';
          BleHid.stopScan();
          pairedScanActive = false;
          setBanner(tr(STR_CONNECTING));
          if (!BleHid.isConnecting() && !BleHid.isConnected()) {
            const bool queued = BleHid.connect(address);
            bleinput::recordDiagnosticEvent(queued ? "reconnect_start scan addr=%.17s"
                                                   : "reconnect_join_inflight scan addr=%.17s",
                                            address);
          } else {
            bleinput::recordDiagnosticEvent("reconnect_join_inflight scan addr=%.17s", address);
          }
        }
      } else if (!BleHid.isConnecting()) {
        // A direct bonded attempt was already made before this scan. Do not
        // spin between the same stale address and an empty scan; leave one
        // bounded manual attempt for the user to retry later.
        pairedScanActive = false;
        if (pairedFallbackAttempted) {
          awaitingConnect = false;
          connectOrigin = ConnectOrigin::None;
          setBanner(tr(STR_BT_NO_DEVICES));
          requestUpdate();
        }
      }
    }
    char reason[48];
    if (BleHid.isConnected()) {
      awaitingConnect = false;
      BleHid.releaseScanResults();
      view = View::Menu;
      rebuildMenuRows();
      char buf[64];
      snprintf(buf, sizeof(buf), tr(STR_BT_CONNECTED_TO), BleHid.connectedName());
      setBanner(buf);
      if (connectOrigin == ConnectOrigin::PairedScan || connectOrigin == ConnectOrigin::PairedDirect)
        bleinput::recordDiagnosticEvent("reconnect_success name=%.28s", BleHid.connectedName());
      connectOrigin = ConnectOrigin::None;
      requestUpdate();
    } else if (BleHid.takeConnectFailure(reason, sizeof(reason))) {
      if (connectOrigin == ConnectOrigin::PairedDirect && !pairedFallbackAttempted) {
        // Direct bonded addresses are the fastest and most reliable path for
        // normal reconnects. A single scan fallback handles devices that
        // rotate their resolvable address without re-pairing.
        pairedFallbackAttempted = true;
        pairedScanActive = true;
        connectOrigin = ConnectOrigin::PairedScan;
        bleinput::recordDiagnosticEvent("reconnect_fallback scan");
        setBanner(tr(STR_SCANNING));
        BleHid.startScan(kScanMs);
        requestUpdate();
      } else {
        awaitingConnect = false;
        if (connectOrigin == ConnectOrigin::PairedScan || connectOrigin == ConnectOrigin::PairedDirect)
          bleinput::recordDiagnosticEvent("reconnect_fail reason=%.36s", reason);
        connectOrigin = ConnectOrigin::None;
        setBanner(reason);
        requestUpdate();
      }
    }
  }

  // Back returns to the menu from a sub-view, or leaves the screen from the menu.
  if (mappedInput.wasPressed(MappedInputManager::Button::Back)) {
    if (view == View::Menu) {
      finish();
    } else {
      if (BleHid.isScanning()) BleHid.stopScan();
      view = View::Menu;
      rebuildMenuRows();
      requestUpdate();
    }
    return;
  }

  // Navigation within the active list.
#if FREEINK_CAP_BLE_HID_HOST
  const int count = view == View::Menu        ? static_cast<int>(menuRows.size())
                    : view == View::Scan      ? BleHid.deviceCount()
                    : view == View::Paired    ? BleHid.pairedCount()
                                              : bleinput::diagnosticCount();
#else
  const int count = view == View::Menu      ? static_cast<int>(menuRows.size())
                    : view == View::Scan    ? BleHid.deviceCount()
                                            : BleHid.pairedCount();
#endif
  int* idx = view == View::Menu ? &menuIndex : view == View::Scan ? &scanIndex : &pairedIndex;
  buttonNavigator.onNext([this, count, idx] {
    if (count > 0) *idx = ButtonNavigator::nextIndex(*idx, count);
    requestUpdate();
  });
  buttonNavigator.onPrevious([this, count, idx] {
    if (count > 0) *idx = ButtonNavigator::previousIndex(*idx, count);
    requestUpdate();
  });

  // Paired view: tap Confirm to connect, hold Confirm to forget. Uses release for
  // connect so a hold can fire forget without also connecting on the same press.
#if FREEINK_CAP_BLE_HID_HOST
  if (view == View::Diagnostics) {
    if (mappedInput.wasPressed(MappedInputManager::Button::Confirm)) {
      bleinput::clearDiagnostics();
      requestUpdate();
    }
    return;
  }
#endif

  if (view == View::Paired) {
    if (mappedInput.isPressed(MappedInputManager::Button::Confirm)) {
      if (!pairedActionTaken && mappedInput.getHeldTime() >= kForgetHoldMs && pairedIndex < BleHid.pairedCount()) {
        const auto& p = BleHid.paired(static_cast<uint8_t>(pairedIndex));
        bleinput::suppressNextDisconnectNotice();
        BleHid.forget(p.addr);
        if (pairedIndex > 0) pairedIndex--;
        setBanner(tr(STR_FORGET_BUTTON));
        pairedActionTaken = true;
        rebuildMenuRows();
        requestUpdate();
      }
    } else if (mappedInput.wasReleased(MappedInputManager::Button::Confirm)) {
      if (!pairedActionTaken && !awaitingConnect && pairedIndex < BleHid.pairedCount()) {
        beginPairedConnect();
      }
      pairedActionTaken = false;
    }
    return;
  }

  if (mappedInput.wasPressed(MappedInputManager::Button::Confirm)) {
    if (view == View::Menu) {
      handleMenuConfirm();
    } else if (view == View::Scan) {
      if (!awaitingConnect && scanIndex < BleHid.deviceCount()) {
        if (!bleinput::connectionAdmissionAllowed()) {
          setBanner(tr(STR_BT_START_FAILED));
          requestUpdate();
          return;
        }
        if (BleHid.isScanning()) BleHid.stopScan();
        const auto& d = BleHid.device(static_cast<uint8_t>(scanIndex));
        awaitingConnect = true;
        connectOrigin = ConnectOrigin::Scan;
        setBanner(tr(STR_CONNECTING));
        if (!BleHid.isConnecting() && !BleHid.isConnected()) {
          const bool queued = BleHid.connect(d.addr);
          bleinput::recordDiagnosticEvent(queued ? "pair_start addr=%.17s name=%.24s type=%u"
                                                 : "pair_join_inflight addr=%.17s",
                                          d.addr, d.name, static_cast<unsigned>(d.addrType));
        } else {
          bleinput::recordDiagnosticEvent("pair_join_inflight addr=%.17s", d.addr);
        }
        requestUpdate();
      }
    }
    return;
  }

  // The scan list changes as devices are discovered — keep repainting while active.
  if (view == View::Scan && BleHid.isScanning()) requestUpdate();
}

std::string BluetoothSettingsActivity::deviceLabel(int index) const {
  if (index >= BleHid.deviceCount()) return "";
  const auto& d = BleHid.device(static_cast<uint8_t>(index));
  return std::string(d.name);
}

std::string BluetoothSettingsActivity::pairedLabel(int index) const {
  if (index >= BleHid.pairedCount()) return "";
  const auto& p = BleHid.paired(static_cast<uint8_t>(index));
  return p.name[0] ? std::string(p.name) : std::string(p.addr);
}

void BluetoothSettingsActivity::render(RenderLock&&) {
  if (noDeviceTimeoutPopup.processRender(renderer, mappedInput)) return;

  renderer.clearScreen();

  const auto& metrics = UITheme::getInstance().getMetrics();
  const auto pageWidth = renderer.getScreenWidth();
  const auto pageHeight = renderer.getScreenHeight();

  const char* title = tr(STR_BT_BETA);
  GUI.drawHeader(renderer, Rect{0, metrics.topPadding, pageWidth, metrics.headerHeight}, title);

  const int topOffset = metrics.topPadding + metrics.headerHeight + metrics.tabBarHeight + metrics.verticalSpacing;
  const int contentHeight = pageHeight - topOffset - metrics.buttonHintsHeight - metrics.verticalSpacing;
  const Rect listRect{0, topOffset, pageWidth, contentHeight};
  const ActivationState state = activationState.load(std::memory_order_acquire);

  if (state == ActivationState::Preparing) {
    GUI.drawSubHeader(renderer, Rect{0, metrics.topPadding + metrics.headerHeight, pageWidth, metrics.tabBarHeight},
                      tr(STR_BT_PREPARING));
    const int selectedOption = std::clamp(failureIndex.load(std::memory_order_acquire), 0, 1);
    GUI.drawList(renderer, listRect, 2, selectedOption,
                 [](int index) {
                   return index == 0 ? std::string(tr(STR_BLUETOOTH)) + " " + tr(STR_STATE_OFF)
                                     : std::string(tr(STR_BACK));
                 },
                 nullptr, nullptr, nullptr, false);
    const auto labels = mappedInput.mapLabels(tr(STR_BACK), tr(STR_SELECT), "", "");
    GUI.drawButtonHints(renderer, labels.btn1, labels.btn2, labels.btn3, labels.btn4);
    renderer.displayBuffer();

    // Settings can start Bluetooth from its child page when its parent was
    // entered with Bluetooth Off. Yield only the established rebuildable font
    // owners, after this page has rendered and only when the normal cold-start
    // floors are short. Reader pages keep their existing reclaim lifecycle.
    if (!preparationReclaimDone.exchange(true, std::memory_order_acq_rel) && mode == Mode::Settings &&
        !BleHid.isRunning()) {
      const size_t freeBefore = ESP.getFreeHeap();
      const size_t largestBefore = ESP.getMaxAllocHeap();
      if (freeBefore < bleinput::kReaderBleStartMinFreeHeap ||
          largestBefore < bleinput::kReaderBleStartMinLargestBlock) {
        if (auto* fontCache = renderer.getFontCacheManager()) fontCache->releaseTransientFontCaches();
        sdFontSystem.releaseInterfaceFontCaches();
        if (!activityManager.isReaderActivity()) sdFontSystem.releaseReaderFontOwner(renderer);
      }
      LOG_INF("BLEMEM", "stage=bluetooth_settings_prepare_reclaim free=%u->%u largest=%u->%u floors=%u/%u",
              static_cast<unsigned>(freeBefore), static_cast<unsigned>(ESP.getFreeHeap()),
              static_cast<unsigned>(largestBefore), static_cast<unsigned>(ESP.getMaxAllocHeap()),
              static_cast<unsigned>(bleinput::kReaderBleStartMinFreeHeap),
              static_cast<unsigned>(bleinput::kReaderBleStartMinLargestBlock));
    }
    return;
  }

  if (state == ActivationState::Failed) {
    GUI.drawSubHeader(renderer, Rect{0, metrics.topPadding + metrics.headerHeight, pageWidth, metrics.tabBarHeight},
                      tr(STR_BT_START_FAILED));
    const bool retryAvailable = !controlledRetryUsed.load(std::memory_order_acquire);
    const int offOptionIndex = retryAvailable ? 1 : 0;
    const int count = retryAvailable ? 3 : 2;
    const int selectedFailureOption = std::clamp(failureIndex.load(std::memory_order_acquire), 0, count - 1);
    GUI.drawList(renderer, listRect, count, selectedFailureOption,
                 [retryAvailable, offOptionIndex](int index) {
                   if (retryAvailable && index == 0) return std::string(tr(STR_RETRY));
                   if (index == offOptionIndex) {
                     return std::string(tr(STR_BLUETOOTH)) + " " + tr(STR_STATE_OFF);
                   }
                   return std::string(tr(STR_BACK));
                 },
                 nullptr, nullptr, nullptr, false);
    const auto labels = mappedInput.mapLabels(tr(STR_BACK), tr(STR_SELECT), "", "");
    GUI.drawButtonHints(renderer, labels.btn1, labels.btn2, labels.btn3, labels.btn4);
    renderer.displayBuffer();
    return;
  }

  // Sub-header: connection status.
  const char* status = BleHid.isConnected() ? BleHid.connectedName() : tr(STR_BT_NOT_CONNECTED);
  GUI.drawSubHeader(renderer, Rect{0, metrics.topPadding + metrics.headerHeight, pageWidth, metrics.tabBarHeight},
                    status);

  if (view == View::Menu) {
    GUI.drawList(
        renderer, listRect, static_cast<int>(menuRows.size()), menuIndex,
        [this](int i) {
#if FREEINK_CAP_BLE_HID_HOST
          if (menuRows[i].action == Action::Diagnostics) return std::string(tr(STR_BT_DIAGNOSTICS));
          if (menuRows[i].action == Action::ResetDefaults) return std::string(tr(STR_BT_RESET_MAP_DEFAULTS));
#endif
          return std::string(I18N.get(menuRows[i].label));
        }, nullptr, nullptr,
        [this](int i) -> std::string {
          if (menuRows[i].action == Action::ToggleBt) {
            const bool enabled = mode == Mode::ReaderSession ? bleinput::readerSessionBluetoothEnabled()
                                                             : SETTINGS.bluetoothEnabled != 0;
            return enabled ? tr(STR_STATE_ON) : tr(STR_STATE_OFF);
          }
          if (menuRows[i].action == Action::NoDeviceTimeout)
            return noDeviceTimeoutLabel(SETTINGS.bluetoothNoDeviceTimeoutSeconds);
          return "";
        },
        true);
  } else if (view == View::Scan) {
    const int count = BleHid.deviceCount();
    if (count == 0) {
      GUI.drawHelpText(renderer, Rect{0, topOffset + metrics.verticalSpacing, pageWidth, 24},
                       BleHid.isScanning() ? tr(STR_SCANNING) : tr(STR_BT_NO_DEVICES));
    } else {
      GUI.drawList(
          renderer, listRect, count, scanIndex, [this](int i) { return deviceLabel(i); }, nullptr, nullptr, nullptr,
          false);
    }
  } else if (view == View::Paired) {
    const int count = BleHid.pairedCount();
    if (count == 0) {
      GUI.drawHelpText(renderer, Rect{0, topOffset + metrics.verticalSpacing, pageWidth, 24}, tr(STR_BT_NO_PAIRED));
    } else {
      GUI.drawList(
          renderer, listRect, count, pairedIndex, [this](int i) { return pairedLabel(i); }, nullptr, nullptr, nullptr,
          false);
    }
  }
#if FREEINK_CAP_BLE_HID_HOST
  else {  // Diagnostics
    const uint8_t count = bleinput::diagnosticCount();
    if (count == 0) {
      GUI.drawHelpText(renderer, Rect{0, topOffset + metrics.verticalSpacing, pageWidth, 24},
                       tr(STR_BT_NO_DIAGNOSTIC_EVENTS));
    } else {
      const int lineHeight = 22;
      const int maxRows = contentHeight / lineHeight > 0 ? contentHeight / lineHeight : 1;
      for (int row = 0; row < maxRows && row < count; ++row) {
        bleinput::DiagnosticRecord record;
        char line[72];
        if (!bleinput::diagnosticAtNewest(static_cast<uint8_t>(row), record)) break;
        bleinput::formatDiagnostic(record, line, sizeof(line));
        renderer.drawText(UI_10_FONT_ID, 8, topOffset + row * lineHeight, line);
      }
      GUI.drawHelpText(renderer, Rect{0, pageHeight - metrics.buttonHintsHeight - 22, pageWidth, 20},
                       tr(STR_BT_CLEAR_DIAGNOSTICS_HINT));
    }
  }
#endif

  // Transient banner above the hints.
  if (!banner.empty()) {
    GUI.drawHelpText(renderer, Rect{0, pageHeight - metrics.buttonHintsHeight - 22, pageWidth, 20}, banner.c_str());
  }

  // In the paired list, Confirm connects and a hold forgets — surface the hold hint.
  if (view == View::Paired && BleHid.pairedCount() > 0 && banner.empty()) {
    GUI.drawHelpText(renderer, Rect{0, pageHeight - metrics.buttonHintsHeight - 22, pageWidth, 20},
                     tr(STR_BT_FORGET_PROMPT));
  }

  // Button hints differ by view (Menu selects; Scan and Paired both connect).
#if FREEINK_CAP_BLE_HID_HOST
  const char* confirm = view == View::Menu        ? tr(STR_SELECT)
                       : view == View::Diagnostics ? tr(STR_CLEAR_BUTTON)
                                                   : tr(STR_CONNECT);
#else
  const char* confirm = view == View::Menu ? tr(STR_SELECT) : tr(STR_CONNECT);
#endif
  const auto labels = mappedInput.mapLabels(tr(STR_BACK), confirm, tr(STR_DIR_UP), tr(STR_DIR_DOWN));
  GUI.drawButtonHints(renderer, labels.btn1, labels.btn2, labels.btn3, labels.btn4);

  renderer.displayBuffer();
}
