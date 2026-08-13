#pragma once

#include <string>
#include <vector>

#include "SettingsProfileStore.h"
#include "activities/Activity.h"
#include "components/OptionPopup.h"
#include "util/ButtonNavigator.h"

class SettingsProfilesActivity final : public Activity {
 public:
  SettingsProfilesActivity(GfxRenderer& renderer, MappedInputManager& mappedInput)
      : Activity("SettingsProfiles", renderer, mappedInput) {}

  void onEnter() override;
  void loop() override;
  void render(RenderLock&&) override;

 private:
  void reload();
  void showActions();
  void saveCurrentProfile();
  void applySelectedProfile();
  void deleteSelectedProfile();

  std::vector<std::string> profiles;
  int selectedIndex = 0;
  ButtonNavigator buttonNavigator;
  OptionPopup actionsPopup;
};
