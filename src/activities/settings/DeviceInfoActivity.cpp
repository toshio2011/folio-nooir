#include "DeviceInfoActivity.h"

#include <Arduino.h>
#include <HalDisplay.h>
#include <HalGPIO.h>
#include <HalPowerManager.h>
#include <HalStorage.h>
#include <I18n.h>
#include <WiFi.h>
#include <esp_system.h>

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <string>

#include "BleInput.h"
#include "CrossPointState.h"
#include "MappedInputManager.h"
#include "components/UITheme.h"
#include "fontIds.h"

namespace {
const char* releaseBuildSha() {
#if defined(NOOIR_BUILD_SHA)
  return NOOIR_BUILD_SHA[0] == '\0' ? nullptr : NOOIR_BUILD_SHA;
#else
  return nullptr;
#endif
}

int buildShaLineCount(const GfxRenderer& renderer, const int screenWidth, const int sidePadding) {
  const char* sha = releaseBuildSha();
  if (sha == nullptr) return 0;
  const int availableWidth = screenWidth - sidePadding * 2;
  return renderer.getTextWidth(SMALL_FONT_ID, sha) <= availableWidth ? 1 : 2;
}

int deviceInfoListTop(const GfxRenderer& renderer, const ThemeMetrics& metrics) {
  int top = metrics.topPadding + metrics.headerHeight + metrics.verticalSpacing;
  const int hashLines = buildShaLineCount(renderer, renderer.getScreenWidth(), metrics.contentSidePadding);
  if (hashLines > 0) {
    top += renderer.getLineHeight(SMALL_FONT_ID) * (hashLines + 1) + 2 + metrics.verticalSpacing;
  }
  return top;
}

const char* resetReasonText(const int reason) {
  switch (reason) {
    case 1: return tr(STR_DEVICE_RESET_POWER_ON);
    case 2: return tr(STR_DEVICE_RESET_EXTERNAL);
    case 3: return tr(STR_DEVICE_RESET_SOFTWARE);
    case 4: return tr(STR_DEVICE_RESET_PANIC);
    case 5: return tr(STR_DEVICE_RESET_INT_WATCHDOG);
    case 6: return tr(STR_DEVICE_RESET_TASK_WATCHDOG);
    case 7: return tr(STR_DEVICE_RESET_WATCHDOG);
    case 8: return tr(STR_DEVICE_RESET_DEEP_SLEEP);
    case 9: return tr(STR_DEVICE_RESET_BROWNOUT);
    case 10: return tr(STR_DEVICE_RESET_SDIO);
    default: return tr(STR_DEVICE_RESET_UNKNOWN);
  }
}

}  // namespace

DeviceInfoActivity::DeviceInfoActivity(GfxRenderer& renderer, MappedInputManager& mappedInput)
    : Activity("DeviceInfo", renderer, mappedInput) {}

void DeviceInfoActivity::buildRows() {
  rowCount_ = 0;
  const auto add = [this](const Row row) {
    if (rowCount_ < rows_.size()) rows_[rowCount_++] = row;
  };
  add(Row::Model);
  if (snapshot_.panelWidth > 0 && snapshot_.panelHeight > 0) add(Row::ActivePanel);
  add(Row::FirmwareVersion);
  if (snapshot_.batteryKnown) add(Row::Battery);
  add(Row::SdCard);
  if (snapshot_.sdReady && snapshot_.sdCapacityMiB > 0) add(Row::SdCapacity);
  add(Row::Wifi);
#if FREEINK_CAP_BLE_HID_HOST
  add(Row::Bluetooth);
#endif
  add(Row::Uptime);
  add(Row::SystemDetails);
  if (detailsExpanded_) {
    add(Row::ResetReason);
    add(Row::FreeHeap);
    add(Row::LargestFreeBlock);
  }
  selectedIndex_ = std::min(selectedIndex_, std::max(0, static_cast<int>(rowCount_) - 1));
}

void DeviceInfoActivity::onEnter() {
  Activity::onEnter();
  // Capture only cheap cached HAL/runtime values once. Storage.ready() is a
  // readiness flag; this page deliberately does not ask FAT for free space.
  snapshot_.modelIsX3 = gpio.deviceIsX3();
  snapshot_.panelWidth = display.getDisplayWidth();
  snapshot_.panelHeight = display.getDisplayHeight();
  snapshot_.sdReady = Storage.ready();
  if (snapshot_.sdReady) {
    const uint64_t capacityBytes = Storage.cardCapacityBytes();
    snapshot_.sdCapacityMiB = static_cast<uint32_t>(capacityBytes / (1024U * 1024U));
  }
  const uint16_t batteryPercent = powerManager.getBatteryPercentage();
  snapshot_.batteryKnown = batteryPercent <= 100;
  if (snapshot_.batteryKnown) snapshot_.batteryPercent = batteryPercent;
  const bool wifiActive = WiFi.getMode() != WIFI_MODE_NULL;
  snapshot_.wifi = !wifiActive ? LinkState::Off
                               : (WiFi.status() == WL_CONNECTED ? LinkState::Connected : LinkState::On);
#if FREEINK_CAP_BLE_HID_HOST
  snapshot_.bluetooth = BleHid.isConnected() ? LinkState::Connected
                                             : (BleHid.isRunning() ? LinkState::On : LinkState::Off);
#endif
  snapshot_.uptimeSeconds = millis() / 1000U;
  snapshot_.resetReason = static_cast<int>(esp_reset_reason());
  snapshot_.freeHeap = ESP.getFreeHeap();
  snapshot_.largestFreeBlock = ESP.getMaxAllocHeap();
  selectedIndex_ = 0;
  detailsExpanded_ = false;
  buildRows();
  requestUpdate();
}

const char* DeviceInfoActivity::rowTitle(const Row row) const {
  switch (row) {
    case Row::Model: return tr(STR_DEVICE_MODEL);
    case Row::ActivePanel: return tr(STR_ACTIVE_PANEL);
    case Row::FirmwareVersion: return tr(STR_FIRMWARE_VERSION);
    case Row::Battery: return tr(STR_BATTERY);
    case Row::SdCard: return tr(STR_DEVICE_SD_CARD);
    case Row::SdCapacity: return tr(STR_DEVICE_SD_CAPACITY);
    case Row::Wifi: return tr(STR_DEVICE_WIFI);
    case Row::Bluetooth: return tr(STR_DEVICE_BLUETOOTH);
    case Row::Uptime: return tr(STR_DEVICE_UPTIME);
    case Row::SystemDetails: return tr(STR_SYSTEM_DETAILS);
    case Row::ResetReason: return tr(STR_DEVICE_RESET_REASON);
    case Row::FreeHeap: return tr(STR_DEVICE_FREE_HEAP);
    case Row::LargestFreeBlock: return tr(STR_DEVICE_LARGEST_BLOCK);
  }
  return tr(STR_DEVICE_INFO);
}

std::string DeviceInfoActivity::rowValue(const Row row) const {
  // UITheme's list callback accepts std::string; values are formatted only
  // for visible rows, while the per-entry snapshot and row table stay fixed.
  char value[64]{};
  switch (row) {
    case Row::Model: return snapshot_.modelIsX3 ? tr(STR_DEVICE_MODEL_X3) : tr(STR_DEVICE_MODEL_X4);
    case Row::ActivePanel:
      snprintf(value, sizeof(value), tr(STR_DEVICE_PANEL_FORMAT), static_cast<unsigned>(snapshot_.panelWidth),
               static_cast<unsigned>(snapshot_.panelHeight));
      return value;
    case Row::FirmwareVersion: return CROSSPOINT_VERSION;
    case Row::Battery:
      snprintf(value, sizeof(value), tr(STR_DEVICE_PERCENT_FORMAT), static_cast<unsigned>(snapshot_.batteryPercent));
      return value;
    case Row::SdCard: return snapshot_.sdReady ? tr(STR_DEVICE_SD_READY) : tr(STR_DEVICE_SD_NOT_READY);
    case Row::SdCapacity:
      snprintf(value, sizeof(value), tr(STR_DEVICE_CAPACITY_FORMAT), static_cast<unsigned>(snapshot_.sdCapacityMiB));
      return value;
    case Row::Wifi:
    case Row::Bluetooth: {
      const LinkState state = row == Row::Wifi ? snapshot_.wifi : snapshot_.bluetooth;
      if (state == LinkState::Connected) return tr(STR_CONNECTED);
      return state == LinkState::On ? tr(STR_STATE_ON) : tr(STR_STATE_OFF);
    }
    case Row::Uptime: {
      const uint32_t days = snapshot_.uptimeSeconds / 86400U;
      const uint32_t hours = (snapshot_.uptimeSeconds / 3600U) % 24U;
      const uint32_t minutes = (snapshot_.uptimeSeconds / 60U) % 60U;
      const uint32_t seconds = snapshot_.uptimeSeconds % 60U;
      snprintf(value, sizeof(value), tr(STR_DEVICE_UPTIME_FORMAT), static_cast<unsigned long>(days),
               static_cast<unsigned>(hours), static_cast<unsigned>(minutes), static_cast<unsigned>(seconds));
      return value;
    }
    case Row::SystemDetails: return detailsExpanded_ ? "-" : "+";
    case Row::ResetReason: return resetReasonText(snapshot_.resetReason);
    case Row::FreeHeap:
      snprintf(value, sizeof(value), tr(STR_DEVICE_MEMORY_FORMAT),
               static_cast<unsigned>(snapshot_.freeHeap / 1024U));
      return value;
    case Row::LargestFreeBlock:
      snprintf(value, sizeof(value), tr(STR_DEVICE_MEMORY_FORMAT),
               static_cast<unsigned>(snapshot_.largestFreeBlock / 1024U));
      return value;
  }
  return {};
}

void DeviceInfoActivity::activateSelectedRow() {
  if (selectedIndex_ >= rowCount_) return;
  if (rows_[selectedIndex_] == Row::SystemDetails) {
    detailsExpanded_ = !detailsExpanded_;
    buildRows();
    requestUpdate();
  }
}

void DeviceInfoActivity::loop() {
  if (mappedInput.wasPressed(MappedInputManager::Button::Back)) {
    if (detailsExpanded_) {
      detailsExpanded_ = false;
      buildRows();
      requestUpdate();
    } else {
      finish();
    }
    return;
  }
  if (mappedInput.wasPressed(MappedInputManager::Button::Confirm)) {
    activateSelectedRow();
    return;
  }

  const auto& metrics = UITheme::getInstance().getMetrics();
  const int listTop = deviceInfoListTop(renderer, metrics);
  const int listHeight = renderer.getScreenHeight() -
                         listTop - metrics.buttonHintsHeight - metrics.verticalSpacing;
  switch (handleListTouch(selectedIndex_, rowCount(), listTop, listHeight, false)) {
    case ListTouchResult::Activated:
      activateSelectedRow();
      requestUpdate();
      return;
    case ListTouchResult::Consumed:
      return;
    case ListTouchResult::None:
      break;
  }

  const int total = rowCount();
  buttonNavigator_.onNextRelease([this, total] {
    selectedIndex_ = ButtonNavigator::nextIndex(selectedIndex_, total);
    requestUpdate();
  });
  buttonNavigator_.onPreviousRelease([this, total] {
    selectedIndex_ = ButtonNavigator::previousIndex(selectedIndex_, total);
    requestUpdate();
  });
}

void DeviceInfoActivity::render(RenderLock&&) {
  renderer.clearScreen();
  const auto& metrics = UITheme::getInstance().getMetrics();
  const int pageWidth = renderer.getScreenWidth();
  const int pageHeight = renderer.getScreenHeight();
  GUI.drawHeader(renderer, Rect{0, metrics.topPadding, pageWidth, metrics.headerHeight}, tr(STR_DEVICE_INFO));

  const char* sha = releaseBuildSha();
  const int shaLines = buildShaLineCount(renderer, pageWidth, metrics.contentSidePadding);
  int shaTop = metrics.topPadding + metrics.headerHeight + metrics.verticalSpacing;
  if (sha != nullptr) {
    const int lineHeight = renderer.getLineHeight(SMALL_FONT_ID);
    renderer.drawText(SMALL_FONT_ID, metrics.contentSidePadding, shaTop, tr(STR_BUILD_SHA), true);
    shaTop += lineHeight + 2;
    if (shaLines == 1) {
      renderer.drawText(SMALL_FONT_ID, metrics.contentSidePadding, shaTop, sha, true);
    } else {
      const size_t shaLength = strlen(sha);
      const size_t split = shaLength / 2;
      char firstHalf[41]{};
      char secondHalf[41]{};
      const size_t firstLength = std::min(split, sizeof(firstHalf) - 1);
      const size_t secondLength = std::min(shaLength - firstLength, sizeof(secondHalf) - 1);
      memcpy(firstHalf, sha, firstLength);
      memcpy(secondHalf, sha + firstLength, secondLength);
      renderer.drawText(SMALL_FONT_ID, metrics.contentSidePadding, shaTop, firstHalf, true);
      renderer.drawText(SMALL_FONT_ID, metrics.contentSidePadding, shaTop + lineHeight, secondHalf, true);
    }
  }

  const int listTop = deviceInfoListTop(renderer, metrics);
  const int listHeight = pageHeight - listTop - metrics.buttonHintsHeight - metrics.verticalSpacing;
  GUI.drawList(renderer, Rect{0, listTop, pageWidth, listHeight}, rowCount(), selectedIndex_,
               [this](const int index) { return std::string(rowTitle(rows_[index])); }, nullptr, nullptr,
               [this](const int index) { return rowValue(rows_[index]); }, true);

  const auto labels = mappedInput.mapLabels(tr(STR_BACK), tr(STR_SELECT), tr(STR_DIR_UP), tr(STR_DIR_DOWN));
  GUI.drawButtonHints(renderer, labels.btn1, labels.btn2, labels.btn3, labels.btn4);
  renderer.displayBuffer();
}
