#pragma once

#include <array>
#include <cstdint>

#include "activities/Activity.h"
#include "util/ButtonNavigator.h"

class DeviceInfoActivity final : public Activity {
  enum class Row : uint8_t {
    Model,
    ActivePanel,
    FirmwareVersion,
    Battery,
    SdCard,
    SdCapacity,
    Wifi,
    Bluetooth,
    Uptime,
    SystemDetails,
    ResetReason,
    FreeHeap,
    LargestFreeBlock,
  };

  enum class LinkState : uint8_t { Off, On, Connected };

  struct Snapshot {
    bool modelIsX3 = false;
    uint16_t panelWidth = 0;
    uint16_t panelHeight = 0;
    uint16_t batteryPercent = 0;
    bool batteryKnown = false;
    bool sdReady = false;
    uint32_t sdCapacityMiB = 0;
    LinkState wifi = LinkState::Off;
    LinkState bluetooth = LinkState::Off;
    uint32_t uptimeSeconds = 0;
    int resetReason = 0;
    uint32_t freeHeap = 0;
    uint32_t largestFreeBlock = 0;
  };

  // A fixed row table avoids heap allocation for this small, read-only page.
  // It is rebuilt only when the user expands/collapses System Details.
  std::array<Row, 13> rows_{};
  uint8_t rowCount_ = 0;
  int selectedIndex_ = 0;
  bool detailsExpanded_ = false;
  Snapshot snapshot_{};
  ButtonNavigator buttonNavigator_;

  void buildRows();
  int rowCount() const { return rowCount_; }
  void activateSelectedRow();
  const char* rowTitle(Row row) const;
  std::string rowValue(Row row) const;

 public:
  explicit DeviceInfoActivity(GfxRenderer& renderer, MappedInputManager& mappedInput);
  void onEnter() override;
  void loop() override;
  void render(RenderLock&&) override;
};
