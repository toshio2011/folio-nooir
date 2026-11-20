#pragma once

#include <string>

#include "BleMappingProfileStore.h"
#include "activities/Activity.h"
#include "components/OptionPopup.h"
#include "util/ButtonNavigator.h"

class BleMappingProfilesActivity final : public Activity {
 public:
  BleMappingProfilesActivity(GfxRenderer& renderer, MappedInputManager& mappedInput)
      : Activity("BleMappingProfiles", renderer, mappedInput) {}

  void onEnter() override;
  void loop() override;
  void render(RenderLock&&) override;

 private:
  void reload();
  void showActions();
  void promptForName(bool duplicate, bool rename);
  void editMappings(bool viewOnly = false, const char* profileName = "");
  uint8_t selectedProfileId() const;
  int displayCount() const;

  bleinput::profiles::ProfileRecord profiles[bleinput::profiles::kMaxProfiles]{};
  uint8_t profileCount = 0;
  int selectedIndex = 0;
  bool catalogReady = false;
  std::string statusMessage;
  OptionPopup actionsPopup;
  ButtonNavigator buttonNavigator;
};
