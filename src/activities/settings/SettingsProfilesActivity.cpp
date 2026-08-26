#include "SettingsProfilesActivity.h"

#include <GfxRenderer.h>
#include <I18n.h>

#include <algorithm>
#include <memory>

#include "CrossPointSettings.h"
#include "MappedInputManager.h"
#include "activities/util/ConfirmationActivity.h"
#include "activities/util/KeyboardEntryActivity.h"
#include "components/UITheme.h"
#include "fontIds.h"

namespace {
constexpr int PROFILE_NAME_LIMIT = 48;
constexpr int FOOTER_HEIGHT = 64;
}  // namespace

void SettingsProfilesActivity::reload() {
  profiles = SettingsProfileStore::list();
  if (profiles.empty()) {
    selectedIndex = 0;
  } else {
    selectedIndex = std::clamp(selectedIndex, 0, static_cast<int>(profiles.size()) - 1);
  }
}

void SettingsProfilesActivity::onEnter() {
  Activity::onEnter();
  reload();
  requestUpdate();
}

void SettingsProfilesActivity::saveCurrentProfile() {
  startActivityForResult(
      std::make_unique<KeyboardEntryActivity>(renderer, mappedInput, "Profile name", "", PROFILE_NAME_LIMIT),
      [this](const ActivityResult& result) {
        if (!result.isCancelled && std::holds_alternative<KeyboardResult>(result.data)) {
          const std::string name = std::get<KeyboardResult>(result.data).text;
          if (!name.empty() && SettingsProfileStore::saveCurrent(name)) {
            reload();
            const std::string safe = SettingsProfileStore::sanitizeName(name);
            const auto it = std::find(profiles.begin(), profiles.end(), safe);
            if (it != profiles.end()) selectedIndex = static_cast<int>(it - profiles.begin());
          }
        }
        requestUpdate(true);
      });
}

void SettingsProfilesActivity::applySelectedProfile() {
  if (selectedIndex < 0 || selectedIndex >= static_cast<int>(profiles.size())) return;
  if (SettingsProfileStore::apply(profiles[static_cast<size_t>(selectedIndex)])) {
    UITheme::getInstance().reload();
    finish();
  } else {
    requestUpdate(true);
  }
}

void SettingsProfilesActivity::deleteSelectedProfile() {
  if (selectedIndex < 0 || selectedIndex >= static_cast<int>(profiles.size())) return;
  const std::string name = profiles[static_cast<size_t>(selectedIndex)];
  startActivityForResult(std::make_unique<ConfirmationActivity>(renderer, mappedInput, "Delete profile?", name),
                         [this, name](const ActivityResult& result) {
                           if (!result.isCancelled) {
                             SettingsProfileStore::remove(name);
                             reload();
                           }
                           requestUpdate(true);
                         });
}

void SettingsProfilesActivity::showActions() {
  if (profiles.empty()) {
    const std::vector<std::string> options = {"Cancel", "Save current settings"};
    actionsPopup.show(StrId::STR_SETTINGS_PROFILES, options, 0, [this](const int action) {
      if (action == 1) saveCurrentProfile();
      requestUpdate(true);
    });
    return;
  }

  const std::vector<std::string> options = {"Cancel", "Apply profile", "Save current settings", "Delete profile"};
  actionsPopup.show(StrId::STR_SETTINGS_PROFILES, options, 0, [this](const int action) {
    if (action == 1) applySelectedProfile();
    else if (action == 2) saveCurrentProfile();
    else if (action == 3) deleteSelectedProfile();
    else requestUpdate(true);
  });
}

void SettingsProfilesActivity::loop() {
  renderer.setUiScaleTextEnabled(true);
  if (actionsPopup.handleInput(mappedInput, [this] { requestUpdate(); })) return;

  if (mappedInput.wasPressed(MappedInputManager::Button::Back)) {
    finish();
    return;
  }
  if (mappedInput.wasPressed(MappedInputManager::Button::Confirm)) {
    showActions();
    requestUpdate();
    return;
  }

  const auto& metrics = UITheme::getInstance().getMetrics();
  const int contentTop = metrics.topPadding + metrics.headerHeight + metrics.verticalSpacing;
  const int contentHeight = renderer.getScreenHeight() - contentTop - FOOTER_HEIGHT - metrics.verticalSpacing;
  switch (handleListTouch(selectedIndex, static_cast<int>(profiles.size()), contentTop, contentHeight, false)) {
    case ListTouchResult::Activated:
      showActions();
      requestUpdate();
      return;
    case ListTouchResult::Consumed:
      return;
    case ListTouchResult::None:
      break;
  }

  const int pageItems = UITheme::getNumberOfItemsPerPage(renderer, true, false, true, false);
  const auto swipe = mappedInput.wasSwipe();
  if (swipe == MappedInputManager::SwipeDir::Up) {
    if (!profiles.empty())
      selectedIndex = ButtonNavigator::nextPageIndex(selectedIndex, static_cast<int>(profiles.size()), pageItems);
    requestUpdate();
    return;
  }
  if (swipe == MappedInputManager::SwipeDir::Down) {
    if (!profiles.empty())
      selectedIndex =
          ButtonNavigator::previousPageIndex(selectedIndex, static_cast<int>(profiles.size()), pageItems);
    requestUpdate();
    return;
  }

  buttonNavigator.onNextRelease([this] {
    if (!profiles.empty()) selectedIndex = ButtonNavigator::nextIndex(selectedIndex, static_cast<int>(profiles.size()));
    requestUpdate();
  });
  buttonNavigator.onPreviousRelease([this] {
    if (!profiles.empty())
      selectedIndex = ButtonNavigator::previousIndex(selectedIndex, static_cast<int>(profiles.size()));
    requestUpdate();
  });
}

void SettingsProfilesActivity::render(RenderLock&&) {
  renderer.setUiScaleTextEnabled(true);
  renderer.clearScreen();
  const auto& metrics = UITheme::getInstance().getMetrics();
  const int pageWidth = renderer.getScreenWidth();
  const int pageHeight = renderer.getScreenHeight();

  GUI.drawHeader(renderer, Rect{0, metrics.topPadding, pageWidth, metrics.headerHeight}, tr(STR_SETTINGS_PROFILES));
  const int contentTop = metrics.topPadding + metrics.headerHeight + metrics.verticalSpacing;
  const int contentHeight = pageHeight - contentTop - FOOTER_HEIGHT - metrics.verticalSpacing;

  if (profiles.empty()) {
    renderer.drawCenteredText(UI_12_FONT_ID, pageHeight / 2 - 20, "No profiles saved", true, EpdFontFamily::BOLD);
    renderer.drawCenteredText(SMALL_FONT_ID, pageHeight / 2 + 16, "Press OK to save current settings");
  } else {
    GUI.drawList(renderer, Rect{0, contentTop, pageWidth, contentHeight}, static_cast<int>(profiles.size()), selectedIndex,
                 [this](const int index) { return profiles[static_cast<size_t>(index)]; }, nullptr, nullptr,
                 [](const int) { return std::string("Settings"); }, true);
  }

  if (actionsPopup.processRender(renderer, mappedInput)) return;

  const auto labels = mappedInput.mapLabels(tr(STR_BACK), tr(STR_SELECT), tr(STR_DIR_UP), tr(STR_DIR_DOWN));
  GUI.drawButtonHints(renderer, labels.btn1, labels.btn2, labels.btn3, labels.btn4);
  renderer.displayBuffer();
}
