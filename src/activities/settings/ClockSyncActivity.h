#pragma once

#include "activities/Activity.h"

// Manual clock/weather sync action. Runs a forced one-shot update, reports the
// result, then turns off the Wi-Fi radio before showing the result screen.
class ClockSyncActivity final : public Activity {
 public:
  explicit ClockSyncActivity(GfxRenderer& renderer, MappedInputManager& mappedInput, bool syncWeather = false)
      : Activity("ClockSync", renderer, mappedInput), syncWeather(syncWeather) {}

  void onEnter() override;
  void onExit() override;
  void loop() override;
  bool skipLoopDelay() override { return true; }
  void render(RenderLock&&) override;

 private:
  enum State { SYNCING, SUCCESS, PARTIAL, NO_LOCATION, NO_WIFI, FAILED };
  State state = SYNCING;
  char syncedTime[16] = {0};
  char weatherLine[64] = {0};
  bool syncWeather = false;
  bool shouldTearDownWifiOnExit = false;

  void runSync();
  void launchWifiSelection();
  void onWifiSelectionComplete(bool connected);
};
