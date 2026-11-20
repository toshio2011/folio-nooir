#include "BleMappingProfilesActivity.h"

#include <GfxRenderer.h>
#include <I18n.h>

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <memory>
#include <vector>

#include "BleButtonMapActivity.h"
#include "MappedInputManager.h"
#include "activities/util/ConfirmationActivity.h"
#include "activities/util/KeyboardEntryActivity.h"
#include "components/UITheme.h"
#include "fontIds.h"

namespace {
constexpr size_t kNameMaxLength = bleinput::profiles::kProfileNameBytes - 1;
constexpr int kFooterHeight = 62;
constexpr int kQuickSwitchDefaultAction = 1;  // First non-cancel action is the reversible profile switch.
constexpr uint8_t kFactoryIds[] = {bleinput::profiles::kFactoryYiserId,
                                   bleinput::profiles::kFactoryFree2Id,
                                   bleinput::profiles::kFactoryFree3Id};
}

int BleMappingProfilesActivity::displayCount() const { return static_cast<int>(profileCount) + 3; }

uint8_t BleMappingProfilesActivity::selectedProfileId() const {
  if (selectedIndex >= 0 && selectedIndex < 3) return kFactoryIds[selectedIndex];
  const int stored = selectedIndex - 3;
  return stored >= 0 && stored < profileCount ? profiles[stored].id : 0xFF;
}

void BleMappingProfilesActivity::reload() {
  catalogReady = BleMappingProfileStore::list(profiles, profileCount);
  if (!catalogReady) {
    profileCount = 0;
    selectedIndex = 0;
    statusMessage = "Profile catalog unavailable; existing mappings were left unchanged.";
    return;
  }
  selectedIndex = std::clamp(selectedIndex, 0, std::max(0, displayCount() - 1));
}

void BleMappingProfilesActivity::onEnter() {
  Activity::onEnter();
  reload();
  requestUpdate();
}

void BleMappingProfilesActivity::showActions() {
  if (!catalogReady || selectedIndex < 0 || selectedIndex >= displayCount()) return;
  const uint8_t id = selectedProfileId();
  const bool factory = BleMappingProfileStore::isFactory(id);
  bleinput::profiles::ProfileRecord selected{};
  selected.id = id;
  if (factory) std::strncpy(selected.name, BleMappingProfileStore::factoryName(id), sizeof(selected.name) - 1);
  else selected = profiles[selectedIndex - 3];
  std::vector<std::string> options = factory
      ? std::vector<std::string>{"Cancel", "Use preset", "Duplicate preset", "View assignments"}
      : std::vector<std::string>{"Cancel", "Switch to this profile", "Duplicate profile",
                                 "Rename profile", "Edit mappings", "Delete profile", "Create empty profile"};
  actionsPopup.show(StrId::STR_SETTINGS_PROFILES, options, kQuickSwitchDefaultAction,
                    [this, selected](const int action) {
    const uint8_t id = selected.id;
    const bool factory = BleMappingProfileStore::isFactory(id);
    if (factory) {
      if (action == 1) statusMessage = BleMappingProfileStore::switchTo(id)
          ? "Built-in preset selected; its assignments are read-only." : "Could not select built-in preset.";
      else if (action == 2) promptForName(true, false);
      else if (action == 3) {
        if (BleMappingProfileStore::switchTo(id)) editMappings(true, selected.name);
        else statusMessage = "Could not load built-in assignments.";
        return;
      }
      reload(); requestUpdate(true); return;
    }
    if (action == 1) {
      if (BleMappingProfileStore::switchTo(id)) statusMessage = "Active mapping profile changed.";
      else statusMessage = "Could not switch profile; the current mapping was preserved.";
      reload();
    } else if (action == 2) {
      promptForName(true, false);
    } else if (action == 3) {
      promptForName(false, true);
    } else if (action == 4) {
      if (BleMappingProfileStore::switchTo(id)) editMappings(false, selected.name);
      else statusMessage = "Could not activate this profile for editing.";
    } else if (action == 5) {
      if (id == bleinput::profiles::kDefaultProfileId || id == BleMappingProfileStore::activeId()) {
        statusMessage = "Default and the active profile cannot be deleted.";
        requestUpdate(true);
        return;
      }
      startActivityForResult(
          std::make_unique<ConfirmationActivity>(renderer, mappedInput, "Delete mapping profile?", selected.name),
          [this, id](const ActivityResult& result) {
            if (!result.isCancelled) {
              statusMessage = BleMappingProfileStore::remove(id) ? "Profile deleted." : "Profile delete failed.";
              reload();
            }
            requestUpdate(true);
          });
      return;
    } else if (action == 6) {
      promptForName(false, false);
      return;
    }
    requestUpdate(true);
  });
}

void BleMappingProfilesActivity::promptForName(const bool duplicate, const bool renameSelected) {
  if (selectedIndex < 0 || selectedIndex >= displayCount()) return;
  const uint8_t sourceId = selectedProfileId();
  const char* sourceName = BleMappingProfileStore::isFactory(sourceId)
      ? BleMappingProfileStore::factoryName(sourceId) : profiles[selectedIndex - 3].name;
  const std::string initial = renameSelected ? sourceName : std::string{};
  startActivityForResult(
      std::make_unique<KeyboardEntryActivity>(renderer, mappedInput,
                                               duplicate ? "Duplicate mapping profile" :
                                               renameSelected ? "Rename mapping profile" : "New mapping profile",
                                               initial, kNameMaxLength),
      [this, duplicate, renameSelected, sourceId](const ActivityResult& result) {
        if (!result.isCancelled && std::holds_alternative<KeyboardResult>(result.data)) {
          const std::string name = std::get<KeyboardResult>(result.data).text;
          bool ok = false;
          if (duplicate) ok = BleMappingProfileStore::duplicate(sourceId, name.c_str());
          else if (renameSelected) ok = BleMappingProfileStore::rename(sourceId, name.c_str());
          else ok = BleMappingProfileStore::createEmpty(name.c_str());
          statusMessage = ok ? (duplicate ? "Profile duplicated." : renameSelected ? "Profile renamed." : "Profile created.")
                             : "Profile operation failed. Check the name and SD card.";
          reload();
        }
        requestUpdate(true);
      });
}

void BleMappingProfilesActivity::editMappings(const bool viewOnly, const char* profileName) {
  startActivityForResult(std::make_unique<BleButtonMapActivity>(renderer, mappedInput, viewOnly, profileName),
                         [this](const ActivityResult&) {
    // The map screen reloads the active profile on entry and saves each accepted
    // assignment atomically. Never resave its in-memory map here: that copy may
    // be stale when the screen deliberately presented a load error.
    statusMessage = "Returned to profiles. Successful assignments save as they are made.";
    reload();
    requestUpdate(true);
  });
}

void BleMappingProfilesActivity::loop() {
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
  const int top = metrics.topPadding + metrics.headerHeight + metrics.verticalSpacing;
  const int height = renderer.getScreenHeight() - top - kFooterHeight - metrics.verticalSpacing;
  if (handleListTouch(selectedIndex, displayCount(), top, height, false) == ListTouchResult::Activated) {
    showActions();
    requestUpdate();
    return;
  }
  const int total = std::max(0, displayCount());
  buttonNavigator.onNextRelease([this, total] {
    if (total) selectedIndex = ButtonNavigator::nextIndex(selectedIndex, total);
    requestUpdate();
  });
  buttonNavigator.onPreviousRelease([this, total] {
    if (total) selectedIndex = ButtonNavigator::previousIndex(selectedIndex, total);
    requestUpdate();
  });
}

void BleMappingProfilesActivity::render(RenderLock&&) {
  renderer.setUiScaleTextEnabled(true);
  renderer.clearScreen();
  const auto& metrics = UITheme::getInstance().getMetrics();
  const int width = renderer.getScreenWidth();
  const int height = renderer.getScreenHeight();
  GUI.drawHeader(renderer, Rect{0, metrics.topPadding, width, metrics.headerHeight}, tr(STR_SETTINGS_PROFILES));
  const int top = metrics.topPadding + metrics.headerHeight + metrics.verticalSpacing;
  const int noticeHeight = statusMessage.empty() ? 0 : 32;
  const int listHeight = height - top - kFooterHeight - metrics.verticalSpacing - noticeHeight;
  if (!catalogReady) {
    renderer.drawCenteredText(SMALL_FONT_ID, top + listHeight / 2, statusMessage.c_str());
  } else if (profileCount == 0) {
    renderer.drawCenteredText(SMALL_FONT_ID, top + listHeight / 2, "No mapping profiles available");
  } else {
    GUI.drawList(renderer, Rect{0, top, width, listHeight}, displayCount(), selectedIndex,
                 [this](const int index) {
                   if (index < 3) {
                     std::string label = BleMappingProfileStore::factoryName(kFactoryIds[index]);
                     if (kFactoryIds[index] == BleMappingProfileStore::activeId()) label += " (Active)";
                     return label;
                   }
                   const auto& record = profiles[index - 3];
                   std::string label = record.name;
                   if (record.id == bleinput::profiles::kDefaultProfileId) label += " (legacy mapping)";
                   if (record.id == BleMappingProfileStore::activeId()) label += "  (Active)";
                   return label;
                 }, nullptr, nullptr, nullptr, true);
  }
  if (!statusMessage.empty())
    renderer.drawCenteredText(SMALL_FONT_ID, height - kFooterHeight, statusMessage.c_str());
  if (actionsPopup.processRender(renderer, mappedInput)) return;
  const auto labels = mappedInput.mapLabels(tr(STR_BACK), tr(STR_SELECT), tr(STR_DIR_UP), tr(STR_DIR_DOWN));
  GUI.drawButtonHints(renderer, labels.btn1, labels.btn2, labels.btn3, labels.btn4);
  renderer.displayBuffer();
}
