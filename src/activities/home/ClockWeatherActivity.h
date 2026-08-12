#pragma once

#include <string>
#include <vector>

#include "activities/Activity.h"

// Lightweight on-device view of the persisted clock/weather cache. It does
// not touch Wi-Fi until the user explicitly chooses Sync now.
class ClockWeatherActivity final : public Activity {
 public:
  ClockWeatherActivity(GfxRenderer& renderer, MappedInputManager& mappedInput)
      : Activity("ClockWeather", renderer, mappedInput) {}

  void onEnter() override;
  void loop() override;
  void render(RenderLock&&) override;

 private:
  std::vector<std::string> lines;
  // The menu's Confirm release can still be visible on the first tick after
  // this page is opened. Consume that release so opening the page never
  // accidentally starts a sync; Sync now must be an explicit second action.
  bool ignoreInitialConfirmRelease = false;
  bool ignoreInitialBackRelease = false;
  bool suppressBackUntilRelease = false;

  void rebuildLines();
  void syncNow();
};
