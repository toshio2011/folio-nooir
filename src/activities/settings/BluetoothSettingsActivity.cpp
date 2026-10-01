#include "BluetoothSettingsActivity.h"

#include <BleKeyboardHost.h>
#include <BoardConfig.h>
#include <GfxRenderer.h>

#include <cstdio>
#include <cstring>

#include "BleButtonMapActivity.h"
#include "BleInput.h"
#include "CrossPointSettings.h"
#include "MappedInputManager.h"
#include "activities/ActivityManager.h"
#include "components/UITheme.h"
#include "fontIds.h"

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
  view = View::Menu;
  menuIndex = 0;
  rebuildMenuRows();
  requestUpdate();
}

void BluetoothSettingsActivity::onExit() {
  if (BleHid.isScanning()) BleHid.stopScan();
  Activity::onExit();
}

void BluetoothSettingsActivity::setBanner(const char* text) {
  banner = text ? text : "";
  bannerUntil = millis() + kBannerMs;
}

void BluetoothSettingsActivity::rebuildMenuRows() {
  menuRows.clear();
  menuRows.reserve(11);
  menuRows.push_back({Action::ToggleBt, StrId::STR_BLUETOOTH});
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

void BluetoothSettingsActivity::startScanView() {
  if (!bleinput::connectionAdmissionAllowed()) {
    setBanner(tr(STR_BT_START_FAILED));
    requestUpdate();
    return;
  }
  if (!BleHid.isRunning() && !bleinput::ensureStarted()) {
    SETTINGS.bluetoothEnabled = 0;
    SETTINGS.saveToFile();
    rebuildMenuRows();
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
  bleinput::recordDiagnosticEvent("reconnect_start direct addr=%.17s", pairedTargetAddr);
  // The host already knows the bonded address type. Try that stable identity
  // first instead of making every manual reconnect depend on a scan response;
  // if the peripheral is using a rotated/private address, the bounded scan
  // fallback below can still recover it by address or name.
  if (!BleHid.connect(pairedTargetAddr) && !BleHid.isConnecting()) {
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
      bleinput::toggleBluetooth();
      rebuildMenuRows();
      requestUpdate();
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
          bleinput::recordDiagnosticEvent("reconnect_start scan addr=%.17s", address);
          setBanner(tr(STR_CONNECTING));
          BleHid.connect(address);
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
        bleinput::recordDiagnosticEvent("pair_start addr=%.17s name=%.24s type=%u", d.addr, d.name,
                                        static_cast<unsigned>(d.addrType));
        setBanner(tr(STR_CONNECTING));
        BleHid.connect(d.addr);
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

  const char* title = tr(STR_BLUETOOTH);
  GUI.drawHeader(renderer, Rect{0, metrics.topPadding, pageWidth, metrics.headerHeight}, title);

  // Sub-header: connection status.
  const char* status = BleHid.isConnected() ? BleHid.connectedName() : tr(STR_BT_NOT_CONNECTED);
  GUI.drawSubHeader(renderer, Rect{0, metrics.topPadding + metrics.headerHeight, pageWidth, metrics.tabBarHeight},
                    status);

  const int topOffset = metrics.topPadding + metrics.headerHeight + metrics.tabBarHeight + metrics.verticalSpacing;
  const int contentHeight = pageHeight - topOffset - metrics.buttonHintsHeight - metrics.verticalSpacing;
  const Rect listRect{0, topOffset, pageWidth, contentHeight};

  if (view == View::Menu) {
    GUI.drawList(
        renderer, listRect, static_cast<int>(menuRows.size()), menuIndex,
        [this](int i) {
#if FREEINK_CAP_BLE_HID_HOST
          if (menuRows[i].action == Action::Diagnostics) return std::string("Input Diagnostics");
#endif
          return std::string(I18N.get(menuRows[i].label));
        }, nullptr, nullptr,
        [this](int i) -> std::string {
          if (menuRows[i].action == Action::ToggleBt) return SETTINGS.bluetoothEnabled ? tr(STR_STATE_ON) : tr(STR_STATE_OFF);
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
                       "No BLE diagnostic events");
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
                       "Confirm: clear diagnostics");
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
