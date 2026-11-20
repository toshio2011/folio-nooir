#pragma once

#include <cstdint>

namespace ble_session {

enum class NoDeviceTimeoutWindowState : uint8_t { Inactive, Started, Waiting, Expired };

// The configured timeout measures the whole enabled, running, disconnected
// session. Scanning and reconnect retries are implementation details; an idle
// host with no bonded device must still eventually turn itself off.
constexpr bool noDeviceTimeoutWindowShouldRun(const bool bluetoothEnabled, const uint16_t timeoutSeconds,
                                              const bool hostRunning, const bool connected) {
  return bluetoothEnabled && timeoutSeconds != 0 && hostRunning && !connected;
}

constexpr bool noDeviceTimeoutExpired(const uint32_t nowMs, const uint32_t startMs,
                                      const uint16_t timeoutSeconds) {
  return timeoutSeconds != 0 && static_cast<uint32_t>(nowMs - startMs) >=
                                     static_cast<uint32_t>(timeoutSeconds) * 1000U;
}

// Keep the timeout window's lifecycle in a small, testable policy object.
// In particular, stopping BLE for Reader memory pressure resets elapsed time;
// a later safe readmission starts a fresh no-device interval.
class NoDeviceTimeoutWindow {
 public:
  NoDeviceTimeoutWindowState update(const bool bluetoothEnabled, const uint16_t timeoutSeconds,
                                    const bool hostRunning, const bool connected, const uint32_t nowMs) {
    if (!noDeviceTimeoutWindowShouldRun(bluetoothEnabled, timeoutSeconds, hostRunning, connected)) {
      reset();
      return NoDeviceTimeoutWindowState::Inactive;
    }

    if (!active_) {
      active_ = true;
      startMs_ = nowMs;
      return NoDeviceTimeoutWindowState::Started;
    }

    return noDeviceTimeoutExpired(nowMs, startMs_, timeoutSeconds) ? NoDeviceTimeoutWindowState::Expired
                                                                   : NoDeviceTimeoutWindowState::Waiting;
  }

  void reset() {
    active_ = false;
    startMs_ = 0;
  }

  bool active() const { return active_; }
  uint32_t startMs() const { return startMs_; }

 private:
  bool active_ = false;
  uint32_t startMs_ = 0;
};

}  // namespace ble_session
