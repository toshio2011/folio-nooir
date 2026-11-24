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
#include "BleMappingProfilesActivity.h"
#include "BleInput.h"
#include "CrossPointSettings.h"
#include "MappedInputManager.h"
#include "activities/ActivityManager.h"
#include "components/UITheme.h"
#include "fontIds.h"
#include "SdCardFontSystem.h"
#include "util/BluetoothSettingsNavigationPolicy.h"
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
  lastRuntimeRunning = BleHid.isRunning();
  lastRuntimeConnected = BleHid.isConnected();
  lastPreferenceEnabled = SETTINGS.bluetoothEnabled != 0;
  const bool wanted = SETTINGS.bluetoothEnabled != 0;
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

void BluetoothSettingsActivity::disableBluetoothFromSettings() {
  // The shared OFF path persists the user's choice and fully ends NimBLE,
  // cancelling an in-flight scan or connection attempt.
  if (BleHid.isScanning()) BleHid.stopScan();
  bleinput::setBluetoothEnabled(false);
  activityManager.setBluetoothSettingsActivationFailed(false);
  activationResult.store(-1, std::memory_order_release);
  activationState.store(ActivationState::Ready, std::memory_order_release);
  controlledRetryUsed.store(false, std::memory_order_release);
  failureIndex.store(0, std::memory_order_release);
  awaitingConnect = false;
  pairedScanActive = false;
  pairedFallbackAttempted = false;
  connectOrigin = ConnectOrigin::None;
  view = View::Menu;
  rebuildMenuRows();
  requestUpdate();
}

void BluetoothSettingsActivity::onBluetoothActivationResult(const bool started) {
  if (activationState.load(std::memory_order_acquire) != ActivationState::Preparing) return;
  activityManager.setBluetoothSettingsActivationFailed(!started);
  activationResult.store(started ? 1 : 0, std::memory_order_release);
  requestUpdate();
}

void BluetoothSettingsActivity::rebuildMenuRows() {
  const Action selectedAction = menuIndex >= 0 && menuIndex < static_cast<int>(menuRows.size())
                                    ? menuRows[static_cast<std::size_t>(menuIndex)].action
                                    : Action::ToggleBt;
  menuRows.clear();
  menuRows.reserve(mode == Mode::ReaderControl ? 1 : 10);
  menuRows.push_back({Action::ToggleBt, StrId::STR_BLUETOOTH});
  if (mode == Mode::ReaderControl) {
    menuIndex = 0;
    return;
  }
  menuRows.push_back({Action::NoDeviceTimeout, StrId::STR_BT_NO_DEVICE_TIMEOUT});
  menuRows.push_back({Action::MappingProfiles, StrId::STR_SETTINGS_PROFILES});
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
    menuRows.push_back({Action::ResetDefaults, StrId::STR_BT_RESET_MAP_DEFAULTS});
    menuRows.push_back({Action::ClearMap, StrId::STR_BT_CLEAR_MAP});
  }
  menuIndex = bluetooth_settings::selectionIndexForAction(menuRows, selectedAction, Action::ToggleBt);
}

void BluetoothSettingsActivity::resetMappingsToDefaults() {
  SETTINGS.bleControllerPreset = CrossPointSettings::BLE_CONTROLLER_PRESET_GENERIC;
#if FREEINK_CAP_BLE_HID_HOST
  BleHid.setInputPreset(freeink::InputPreset::GenericHid);
#endif
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
  lastScanDataCount = BleHid.deviceCount();
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
      if (SETTINGS.bluetoothEnabled) disableBluetoothFromSettings();
      else {
        bleinput::setBluetoothEnabled(true);
        if (!BleHid.isRunning()) beginPreparation(false);
        else requestUpdate();
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
      lastPairedDataCount = BleHid.pairedCount();
      requestUpdate();
      break;
    case Action::Diagnostics:
      view = View::Diagnostics;
      requestUpdate();
      break;
    case Action::MapButtons:
      startActivityForResult(std::make_unique<BleButtonMapActivity>(
                                 renderer, mappedInput, BleMappingProfileStore::isFactory(BleMappingProfileStore::activeId()),
                                 BleMappingProfileStore::isFactory(BleMappingProfileStore::activeId())
                                     ? BleMappingProfileStore::factoryName(BleMappingProfileStore::activeId())
                                     : "Active profile"),
                             [this](const ActivityResult&) {
                               rebuildMenuRows();
                               requestUpdate();
                             });
      break;
    case Action::MappingProfiles:
      startActivityForResult(std::make_unique<BleMappingProfilesActivity>(renderer, mappedInput),
                             [this](const ActivityResult&) {
                               rebuildMenuRows();
                               requestUpdate();
                             });
      break;
    case Action::ResetDefaults:
      if (BleMappingProfileStore::isFactory(BleMappingProfileStore::activeId()))
        BleMappingProfileStore::switchTo(bleinput::profiles::kDefaultProfileId);
      resetMappingsToDefaults();
      setBanner("Default remote mappings restored");
      requestUpdate();
      break;
    case Action::ClearMap:
      if (BleMappingProfileStore::isFactory(BleMappingProfileStore::activeId()))
        BleMappingProfileStore::switchTo(bleinput::profiles::kDefaultProfileId);
      for (auto& e : SETTINGS.bleKeyMap) e = CrossPointSettings::BleKeyMapEntry{};
      SETTINGS.saveToFile();
      setBanner(tr(STR_BT_CLEAR_MAP));
      requestUpdate();
      break;
  }
}

void BluetoothSettingsActivity::loop() {
  const bool userInputEdgePending = mappedInput.wasAnyPressed() || mappedInput.wasAnyReleased();
  const bool runtimeRunning = BleHid.isRunning();
  const bool runtimeConnected = BleHid.isConnected();
  const bool preferenceEnabled = SETTINGS.bluetoothEnabled != 0;
  const bool preferenceTurnedOff = bluetooth_settings::preferenceOffTransitionRequiresMenuRebuild(
      lastPreferenceEnabled, preferenceEnabled);
  if (runtimeRunning != lastRuntimeRunning || runtimeConnected != lastRuntimeConnected ||
      preferenceEnabled != lastPreferenceEnabled) {
    lastRuntimeRunning = runtimeRunning;
    lastRuntimeConnected = runtimeConnected;
    lastPreferenceEnabled = preferenceEnabled;
    if (preferenceTurnedOff) {
      // A no-device timeout can change the saved preference from outside this
      // screen. Rebuild the enabled-only rows immediately and discard async
      // connect state. Do not reinterpret the same button edge against the new
      // menu model; OFF is already applied by the timeout owner.
      if (BleHid.isScanning()) BleHid.stopScan();
      awaitingConnect = false;
      pairedScanActive = false;
      pairedFallbackAttempted = false;
      connectOrigin = ConnectOrigin::None;
      activationState.store(ActivationState::Ready, std::memory_order_release);
      activationResult.store(-1, std::memory_order_release);
      view = View::Menu;
      rebuildMenuRows();
    }
    requestUpdate();
    if (preferenceTurnedOff && userInputEdgePending) return;
  }

  if (noDeviceTimeoutPopup.handleInput(mappedInput, [this] { requestUpdate(); })) return;

  // Keep a button edge on the screen the user saw. Apply queued BLE status
  // changes on the next idle frame instead of reinterpreting that edge after
  // a row/view rebuild.
  int scanDataCount = lastScanDataCount;
  int pairedDataCount = lastPairedDataCount;
  if (view == View::Scan) {
    scanDataCount = BleHid.deviceCount();
    scanIndex = bluetooth_settings::selectionAfterDataCountChange(scanIndex, lastScanDataCount, scanDataCount);
    lastScanDataCount = scanDataCount;
  } else if (view == View::Paired) {
    pairedDataCount = BleHid.pairedCount();
    pairedIndex = bluetooth_settings::selectionAfterDataCountChange(pairedIndex, lastPairedDataCount, pairedDataCount);
    lastPairedDataCount = pairedDataCount;
  }

  const int8_t activationEvent = userInputEdgePending ? -1 : activationResult.exchange(-1, std::memory_order_acq_rel);
  if (activationEvent >= 0 && activationState.load(std::memory_order_acquire) == ActivationState::Preparing) {
    if (activationEvent == 1 && BleHid.isRunning()) {
      activationState.store(ActivationState::Ready, std::memory_order_release);
      rebuildMenuRows();
      LOG_INF("BLEMEM", "stage=bluetooth_settings_ready mode=%s free=%u largest=%u",
              mode == Mode::ReaderControl ? "reader" : "settings", ESP.getFreeHeap(), ESP.getMaxAllocHeap());
    } else {
      activationState.store(ActivationState::Failed, std::memory_order_release);
      const bool retryAvailable = !controlledRetryUsed.load(std::memory_order_acquire);
      failureIndex.store(bluetooth_settings::failureSelectionAfterPreparation(
                             failureIndex.load(std::memory_order_acquire), retryAvailable),
                         std::memory_order_release);
      LOG_ERR("BLEMEM", "stage=bluetooth_settings_failed mode=%s retry_used=%d free=%u largest=%u",
              mode == Mode::ReaderControl ? "reader" : "settings",
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
    int selectedOption = std::clamp(failureIndex.load(std::memory_order_acquire), 0, 2);
    failureIndex.store(selectedOption, std::memory_order_release);
    const auto activateOption = [this] {
      const int selected = failureIndex.load(std::memory_order_acquire);
      if (selected == 1) {
        // OFF cancels startup/reconnect through the same idempotent path used
        // by the normal Settings toggle.
        disableBluetoothFromSettings();
      } else if (selected == 2) {
        finishCancelled();
      }
    };
    const auto touchResult = handleListTouch(selectedOption, 3, contentTop, contentHeight, false);
    failureIndex.store(selectedOption, std::memory_order_release);
    if (touchResult == ListTouchResult::Activated) {
      activateOption();
      return;
    }
    if (touchResult == ListTouchResult::Consumed) return;
    buttonNavigator.onNext([this] {
      failureIndex.store(ButtonNavigator::nextIndex(failureIndex.load(std::memory_order_acquire), 3),
                         std::memory_order_release);
      requestUpdate();
    });
    buttonNavigator.onPrevious([this] {
      failureIndex.store(ButtonNavigator::previousIndex(failureIndex.load(std::memory_order_acquire), 3),
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
        // An explicit OFF choice updates the shared preference and stops the
        // host, even when activation failed or is still pending.
        disableBluetoothFromSettings();
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
  if (awaitingConnect && !userInputEdgePending) {
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
      if (view == View::Scan) {
        const int previousDataCount = lastScanDataCount;
        scanDataCount = BleHid.deviceCount();
        scanIndex = bluetooth_settings::selectionAfterDataCountChange(scanIndex, previousDataCount, scanDataCount);
        lastScanDataCount = scanDataCount;
      }
      // Connection state is asynchronous. Keep the list the user is
      // navigating, including its stable trailing OFF row, until they choose
      // to leave it. Refresh the menu model in place only when the menu is the
      // visible view; Back rebuilds it when leaving Scan or Paired.
      if (view == View::Menu) rebuildMenuRows();
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
                    : view == View::Scan      ? scanDataCount + 1
                    : view == View::Paired    ? pairedDataCount + 1
                                              : bleinput::diagnosticCount();
#else
  const int count = view == View::Menu      ? static_cast<int>(menuRows.size())
                    : view == View::Scan    ? scanDataCount + 1
                                            : pairedDataCount + 1;
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
      if (!pairedActionTaken && pairedIndex < pairedDataCount &&
          mappedInput.getHeldTime() >= kForgetHoldMs) {
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
      if (!pairedActionTaken && pairedIndex == pairedDataCount) {
        disableBluetoothFromSettings();
      } else if (!pairedActionTaken && !awaitingConnect && pairedIndex < pairedDataCount) {
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
      if (scanIndex == scanDataCount) {
        disableBluetoothFromSettings();
        return;
      }
      if (!awaitingConnect && scanIndex < scanDataCount) {
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
    const int selectedOption = std::clamp(failureIndex.load(std::memory_order_acquire), 0, 2);
    GUI.drawList(renderer, listRect, 3, selectedOption,
                 [](int index) {
                   if (index == 0) return std::string(tr(STR_CONNECTING));
                   if (index == 1) return std::string(tr(STR_BLUETOOTH)) + " " + tr(STR_STATE_OFF);
                   return std::string(tr(STR_BACK));
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
  const char* status = bleinput::userFacingStatusText();
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
            if (!SETTINGS.bluetoothEnabled) return tr(STR_STATE_OFF);
#if FREEINK_CAP_BLE_HID_HOST
            return BleHid.isRunning() ? tr(STR_STATE_ON) : tr(STR_BT_PAUSED);
#else
            return tr(STR_STATE_OFF);
#endif
          }
          if (menuRows[i].action == Action::NoDeviceTimeout)
            return noDeviceTimeoutLabel(SETTINGS.bluetoothNoDeviceTimeoutSeconds);
          return "";
        },
        true);
  } else if (view == View::Scan) {
    const int count = lastScanDataCount;
    Rect scanListRect = listRect;
    if (count == 0) {
      GUI.drawHelpText(renderer, Rect{0, topOffset + metrics.verticalSpacing, pageWidth, 24},
                       BleHid.isScanning() ? tr(STR_SCANNING) : tr(STR_BT_NO_DEVICES));
      scanListRect.y += 26;
      scanListRect.height = std::max(0, scanListRect.height - 26);
    }
    GUI.drawList(renderer, scanListRect, count + 1, scanIndex,
                 [this, count](int i) {
                   return i == count ? std::string(tr(STR_BLUETOOTH)) + " " + tr(STR_STATE_OFF) : deviceLabel(i);
                 },
                 nullptr, nullptr, nullptr, false);
  } else if (view == View::Paired) {
    const int count = lastPairedDataCount;
    Rect pairedListRect = listRect;
    if (count == 0) {
      GUI.drawHelpText(renderer, Rect{0, topOffset + metrics.verticalSpacing, pageWidth, 24}, tr(STR_BT_NO_PAIRED));
      pairedListRect.y += 26;
      pairedListRect.height = std::max(0, pairedListRect.height - 26);
    }
    GUI.drawList(renderer, pairedListRect, count + 1, pairedIndex,
                 [this, count](int i) {
                   return i == count ? std::string(tr(STR_BLUETOOTH)) + " " + tr(STR_STATE_OFF) : pairedLabel(i);
                 },
                 nullptr, nullptr, nullptr, false);
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
