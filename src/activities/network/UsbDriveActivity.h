#pragma once

#include <HalStorage.h>

#include "activities/Activity.h"

class UsbDriveActivity final : public Activity {
 public:
  UsbDriveActivity(GfxRenderer& renderer, MappedInputManager& mappedInput)
      : Activity("UsbDrive", renderer, mappedInput) {}

  void onEnter() override;
  void onExit() override;
  void loop() override;
  void render(RenderLock&&) override;
  bool preventAutoSleep() override;
  bool requiresExclusiveStorageLoop() const override { return true; }

 private:
  static constexpr unsigned long HOST_WAIT_TIMEOUT_MS = 5UL * 60UL * 1000UL;
  static constexpr unsigned long START_FAILURE_TIMEOUT_MS = 30UL * 1000UL;
  static constexpr unsigned long FORCED_DISCONNECT_TIMEOUT_MS = 1000UL;
  static constexpr unsigned long HOST_SUSPEND_TIMEOUT_MS = 2000UL;

  void restartAfterStorageHandoff();

  UsbDriveState state = UsbDriveState::Unsupported;
  bool preparing = true;
  bool startFailed = false;
  bool restartRequested = false;
  bool forcedDisconnectRequested = false;
  bool hostSuspendPending = false;
  unsigned long hostWaitStartedAt = 0;
  unsigned long startFailureStartedAt = 0;
  unsigned long forcedDisconnectRequestedAt = 0;
  unsigned long hostSuspendStartedAt = 0;
};
