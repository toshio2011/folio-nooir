#pragma once

#include <cstdint>
#include <string>

#include "activities/Activity.h"

// Small city-name editor and resolver used by Clock & Weather. Coordinates and
// timezone details are deliberately hidden from the normal on-device flow.
class ClockLocationActivity final : public Activity {
 public:
  ClockLocationActivity(GfxRenderer& renderer, MappedInputManager& mappedInput)
      : Activity("ClockLocation", renderer, mappedInput) {}

  void onEnter() override;
  void onExit() override;
  void loop() override;
  void render(RenderLock&&) override;

 private:
  enum class State : uint8_t { EDIT, RESOLVING, SYNCING, ERROR };
  State state = State::EDIT;
  std::string query;
  std::string status;
  bool ignoreInitialConfirmRelease = false;
  bool ignoreInitialBackRelease = false;
  bool wifiSessionStarted = false;

  void editLocation();
  void launchWifiSelection();
  void resolveLocation();
  void syncResolvedLocation();
};
