#include "BleButtonMapActivity.h"

#include <GfxRenderer.h>
#include <Logging.h>

#include <cstdio>
#include <cstring>
#include <memory>
#include <string>
#include <variant>

#include "BleInput.h"
#include "BleMappingProfileStore.h"
#include "CrossPointSettings.h"
#include "activities/util/KeyboardEntryActivity.h"
#include "components/UITheme.h"
#include "fontIds.h"
#include "util/QuickActions.h"

namespace {
constexpr unsigned long kLearnTimeoutMs = 30000;
constexpr uint8_t kNameInputFunction = 0xFE;
}

// New bindings target virtual buttons or safe ActivityManager shortcuts.
// PageBack/PageForward remain explicit choices for reader remotes, matching the
// 1.6.5 mapping workflow and the shared EPUB/XTC/XTCH page-turn input path.
const BleButtonMapActivity::Fn BleButtonMapActivity::kFunctions[] = {
    {bleinput::mapping::kButtonAction, static_cast<uint8_t>(MappedInputManager::Button::Back), StrId::STR_BACK},
    {bleinput::mapping::kButtonAction, static_cast<uint8_t>(MappedInputManager::Button::Confirm), StrId::STR_CONFIRM},
    {bleinput::mapping::kButtonAction,
     static_cast<uint8_t>(MappedInputManager::Button::PageBack), StrId::STR_BT_PAGE_BACK},
    {bleinput::mapping::kButtonAction,
     static_cast<uint8_t>(MappedInputManager::Button::PageForward), StrId::STR_BT_PAGE_FORWARD},
    {bleinput::mapping::kButtonAction, static_cast<uint8_t>(MappedInputManager::Button::Left), StrId::STR_DIR_LEFT},
    {bleinput::mapping::kButtonAction, static_cast<uint8_t>(MappedInputManager::Button::Right), StrId::STR_DIR_RIGHT},
    {bleinput::mapping::kButtonAction, static_cast<uint8_t>(MappedInputManager::Button::Up), StrId::STR_DIR_UP},
    {bleinput::mapping::kButtonAction, static_cast<uint8_t>(MappedInputManager::Button::Down), StrId::STR_DIR_DOWN},
    {bleinput::mapping::kShortcutAction, bleinput::mapping::kShortcutHome, StrId::STR_HOME},
    {bleinput::mapping::kShortcutAction, bleinput::mapping::kShortcutLibrary, StrId::STR_LIBRARY},
    {bleinput::mapping::kShortcutAction, bleinput::mapping::kShortcutRecent, StrId::STR_RECENT},
    {bleinput::mapping::kShortcutAction, bleinput::mapping::kShortcutClippings, StrId::STR_CLIPPINGS},
    {bleinput::mapping::kShortcutAction, bleinput::mapping::readerShortcutTarget(0), StrId::STR_BT_MAP_BUTTONS, false, 0},
    {bleinput::mapping::kShortcutAction, bleinput::mapping::readerShortcutTarget(1), StrId::STR_BT_MAP_BUTTONS, false, 1},
    {bleinput::mapping::kShortcutAction, bleinput::mapping::readerShortcutTarget(2), StrId::STR_BT_MAP_BUTTONS, false, 2},
    {bleinput::mapping::kShortcutAction, bleinput::mapping::readerShortcutTarget(3), StrId::STR_BT_MAP_BUTTONS, false, 3},
    {bleinput::mapping::kShortcutAction, bleinput::mapping::readerShortcutTarget(4), StrId::STR_BT_MAP_BUTTONS, false, 4},
    {bleinput::mapping::kShortcutAction, bleinput::mapping::readerShortcutTarget(5), StrId::STR_BT_MAP_BUTTONS, false, 5},
    {bleinput::mapping::kShortcutAction, bleinput::mapping::readerShortcutTarget(6), StrId::STR_BT_MAP_BUTTONS, false, 6},
    {bleinput::mapping::kShortcutAction, bleinput::mapping::readerShortcutTarget(7), StrId::STR_BT_MAP_BUTTONS, false, 7},
    {bleinput::mapping::kShortcutAction, bleinput::mapping::readerShortcutTarget(8), StrId::STR_BT_MAP_BUTTONS, false, 8},
    {bleinput::mapping::kShortcutAction, bleinput::mapping::readerShortcutTarget(9), StrId::STR_BT_MAP_BUTTONS, false, 9},
    {bleinput::mapping::kShortcutAction, bleinput::mapping::readerShortcutTarget(10), StrId::STR_BT_MAP_BUTTONS, false, 10},
    {bleinput::mapping::kShortcutAction, bleinput::mapping::readerShortcutTarget(11), StrId::STR_BT_MAP_BUTTONS, false, 11},
    {bleinput::mapping::kShortcutAction, bleinput::mapping::readerShortcutTarget(12), StrId::STR_BT_MAP_NEXT_CHAPTER},
    {bleinput::mapping::kShortcutAction, bleinput::mapping::readerShortcutTarget(13), StrId::STR_BT_MAP_PREVIOUS_CHAPTER},
    {bleinput::mapping::kShortcutAction, bleinput::mapping::readerShortcutTarget(14), StrId::STR_FOOTNOTES},
    {kNameInputFunction, 0xFF, StrId::STR_SETTINGS_PROFILES, false, kNameInputFunction},
    {0xFF, 0xFF, StrId::STR_DELETE, true},
};
const uint8_t BleButtonMapActivity::kFunctionCount =
    static_cast<uint8_t>(sizeof(kFunctions) / sizeof(kFunctions[0]));

void BleButtonMapActivity::onEnter() {
  Activity::onEnter();
  const bool profileLoaded = BleMappingProfileStore::loadActive();
  resolveActiveProfileName();
  step = !profileLoaded ? Step::LoadError : (viewOnly ? Step::ViewOnly : Step::WaitForKey);
  capturedCount = 0;
  pendingInputLabel[0] = '\0';
  functionIndex = 0;
  waitStartedAt = millis();
  unsupportedNoticeShown = false;
  lastSaveFailed = false;
  noticeUntil = 0;
  mappedInput.setBleCaptureMode(profileLoaded && !viewOnly);
  requestUpdate();
}

void BleButtonMapActivity::resolveActiveProfileName() {
  const uint8_t activeId = BleMappingProfileStore::activeId();
  if (BleMappingProfileStore::isFactory(activeId)) {
    std::strncpy(activeProfileName, BleMappingProfileStore::factoryName(activeId), sizeof(activeProfileName) - 1);
    activeProfileName[sizeof(activeProfileName) - 1] = '\0';
    return;
  }
  bleinput::profiles::ProfileRecord records[bleinput::profiles::kMaxProfiles]{};
  uint8_t count = 0;
  if (BleMappingProfileStore::list(records, count)) {
    for (uint8_t i = 0; i < count; ++i) {
      if (records[i].id != activeId) continue;
      if (activeId == bleinput::profiles::kDefaultProfileId)
        std::snprintf(activeProfileName, sizeof(activeProfileName), "%s (legacy)", records[i].name);
      else {
        std::strncpy(activeProfileName, records[i].name, sizeof(activeProfileName) - 1);
        activeProfileName[sizeof(activeProfileName) - 1] = '\0';
      }
      return;
    }
  }
  if (!activeProfileName[0] || std::strcmp(activeProfileName, "Active profile") == 0)
    std::strncpy(activeProfileName, "Default (legacy)", sizeof(activeProfileName) - 1);
  activeProfileName[sizeof(activeProfileName) - 1] = '\0';
}

void BleButtonMapActivity::beginSaveAsProfile() {
  const uint8_t sourceId = BleMappingProfileStore::activeId();
  mappedInput.setBleCaptureMode(false);
  startActivityForResult(
      std::make_unique<KeyboardEntryActivity>(renderer, mappedInput, "Save mapping as profile", "", 23),
      [this, sourceId](const ActivityResult& result) {
        if (!result.isCancelled && std::holds_alternative<KeyboardResult>(result.data)) {
          const std::string name = std::get<KeyboardResult>(result.data).text;
          char safeName[bleinput::profiles::kProfileNameBytes]{};
          bool saved = bleinput::profiles::sanitizeName(name.c_str(), safeName) &&
                       BleMappingProfileStore::duplicate(sourceId, safeName);
          if (saved) {
            bleinput::profiles::ProfileRecord records[bleinput::profiles::kMaxProfiles]{};
            uint8_t count = 0;
            uint8_t newId = 0xFF;
            if (BleMappingProfileStore::list(records, count)) {
              for (uint8_t i = 0; i < count; ++i) {
                if (std::strcmp(records[i].name, safeName) == 0) {
                  newId = records[i].id;
                  break;
                }
              }
            }
            saved = newId != 0xFF && BleMappingProfileStore::switchTo(newId);
            if (saved) {
              std::strncpy(activeProfileName, safeName, sizeof(activeProfileName) - 1);
              activeProfileName[sizeof(activeProfileName) - 1] = '\0';
              viewOnly = false;
              step = Step::WaitForKey;
              waitStartedAt = millis();
              unsupportedNoticeShown = false;
            }
          }
          if (!saved) LOG_ERR("BLEMAP", "save-as-profile failed; active mappings were retained");
          else LOG_INF("BLEMAP", "active mappings duplicated and selected as custom profile");
        }
        mappedInput.setBleCaptureMode(!viewOnly);
        requestUpdate();
      });
}

void BleButtonMapActivity::describeMappingInput(const char* label, const uint8_t kind, const uint8_t value,
                                                const uint8_t slot, char* out, const size_t outLength) const {
  if (!out || outLength == 0) return;
  if (label && label[0]) {
    std::snprintf(out, outLength, "%s", label);
  } else if (kind >= 6 && kind <= 12) {
    std::snprintf(out, outLength, "Learned Button %u", static_cast<unsigned>(slot + 1));
  } else {
    bleinput::describeKey(kind, value, out, outLength);
  }
}

void BleButtonMapActivity::beginInputLabelEdit() {
  bleinput::mapping::CapturedIdentity captured[MappedInputManager::kBleSignatureMaxEvents] = {};
  if (capturedCount == 0 || capturedCount > MappedInputManager::kBleSignatureMaxEvents) return;
  for (uint8_t i = 0; i < capturedCount; ++i) captured[i] = {capturedKinds[i], capturedValues[i]};

  int8_t existing = -1;
  for (uint8_t i = 0; i < CrossPointSettings::BLE_MAP_CAPACITY; ++i) {
    if (bleinput::mapping::sameCaptured(SETTINGS.bleKeyMap[i], captured, capturedCount)) {
      existing = static_cast<int8_t>(i);
      break;
    }
  }
  const std::string initial = existing >= 0 ? SETTINGS.bleKeyMap[existing].inputLabel : "";
  mappedInput.setBleCaptureMode(false);
  startActivityForResult(
      std::make_unique<KeyboardEntryActivity>(renderer, mappedInput, "Name this remote input", initial,
                                               bleinput::mapping::kInputLabelBytes - 1),
      [this, existing](const ActivityResult& result) {
        bool labelReady = false;
        if (!result.isCancelled && std::holds_alternative<KeyboardResult>(result.data)) {
          const std::string name = std::get<KeyboardResult>(result.data).text;
          if (existing >= 0) {
            CrossPointSettings::BleKeyMapEntry previous[CrossPointSettings::BLE_MAP_CAPACITY] = {};
            std::memcpy(previous, SETTINGS.bleKeyMap, sizeof(previous));
            const bool changed = bleinput::mapping::setInputLabel(SETTINGS.bleKeyMap[existing], name.c_str());
            labelReady = changed && SETTINGS.saveToFile() && BleMappingProfileStore::saveActive();
            if (!labelReady) {
              std::memcpy(SETTINGS.bleKeyMap, previous, sizeof(previous));
              SETTINGS.saveToFile();
              BleMappingProfileStore::saveActive();
            }
          } else if (!name.empty() && name.size() < sizeof(pendingInputLabel) &&
                     bleinput::mapping::validInputLabel(name.c_str())) {
            std::strncpy(pendingInputLabel, name.c_str(), sizeof(pendingInputLabel) - 1);
            pendingInputLabel[sizeof(pendingInputLabel) - 1] = '\0';
            labelReady = true;  // saved with the input's action choice
          }
        }
        if (!labelReady && existing < 0) pendingInputLabel[0] = '\0';
        if (labelReady && existing >= 0) {
          step = Step::WaitForKey;
          capturedCount = 0;
          waitStartedAt = millis();
        } else {
          step = Step::SelectFunction;
          functionIndex = 0;
        }
        mappedInput.setBleCaptureMode(!viewOnly);
        noticeUntil = millis() + 1800;
        noticeString = labelReady ? StrId::STR_BT_MAP_SAVED : StrId::STR_BT_MAP_SAVE_FAILED;
        requestUpdate();
      });
}

void BleButtonMapActivity::onExit() {
  mappedInput.setBleCaptureMode(false);
  Activity::onExit();
}

const char* BleButtonMapActivity::functionLabel(const Fn& choice) const {
  if (choice.quickAction == kNameInputFunction) return "Name this input";
  if (choice.quickAction < QuickActions::ACTION_COUNT)
    return QuickActions::label(static_cast<QuickActions::ActionId>(choice.quickAction));
  return I18N.get(choice.label);
}

bleinput::mapping::AssignResult BleButtonMapActivity::applyCapturedChoice(const Fn& choice) {
  lastSaveFailed = false;
  bleinput::mapping::CapturedIdentity captured[MappedInputManager::kBleSignatureMaxEvents] = {};
  if (capturedCount == 0 || capturedCount > MappedInputManager::kBleSignatureMaxEvents)
    return bleinput::mapping::AssignResult::Invalid;
  for (uint8_t i = 0; i < capturedCount; ++i) captured[i] = {capturedKinds[i], capturedValues[i]};

  CrossPointSettings::BleKeyMapEntry previousMap[CrossPointSettings::BLE_MAP_CAPACITY] = {};
  memcpy(previousMap, SETTINGS.bleKeyMap, sizeof(previousMap));
  const auto result = choice.remove
                          ? bleinput::mapping::remove(SETTINGS.bleKeyMap, captured, capturedCount)
                          : bleinput::mapping::assign(SETTINGS.bleKeyMap, captured, capturedCount,
                                                      choice.actionType, choice.target);
  if (!choice.remove && pendingInputLabel[0] && result != bleinput::mapping::AssignResult::Full &&
      result != bleinput::mapping::AssignResult::Invalid) {
    bool labelApplied = false;
    for (auto& entry : SETTINGS.bleKeyMap) {
      if (!bleinput::mapping::sameCaptured(entry, captured, capturedCount)) continue;
      labelApplied = bleinput::mapping::setInputLabel(entry, pendingInputLabel);
      break;
    }
    if (!labelApplied) {
      memcpy(SETTINGS.bleKeyMap, previousMap, sizeof(previousMap));
      return bleinput::mapping::AssignResult::Invalid;
    }
  }
  if (result == bleinput::mapping::AssignResult::Added || result == bleinput::mapping::AssignResult::Replaced ||
      result == bleinput::mapping::AssignResult::Removed) {
    if (!SETTINGS.saveToFile() || !BleMappingProfileStore::saveActive(choice.remove)) {
      memcpy(SETTINGS.bleKeyMap, previousMap, sizeof(previousMap));
      SETTINGS.saveToFile();
      BleMappingProfileStore::saveActive();
      lastSaveFailed = true;
      LOG_ERR("BLEMAP", "mapping persistence failed; restored previous in-memory map");
    }
  }
  return result;
}

void BleButtonMapActivity::loop() {
  // Front Back button exits the mapping screen at any step.
  if (mappedInput.wasPressed(MappedInputManager::Button::Back)) {
    finish();
    return;
  }
  if (step == Step::LoadError) return;

  if ((step == Step::WaitForKey || step == Step::ViewOnly) &&
      mappedInput.wasPressed(MappedInputManager::Button::Confirm)) {
    beginSaveAsProfile();
    return;
  }

  if (step == Step::ViewOnly) return;

  if (bleinput::mapping::learnTimedOut(waitStartedAt, millis(), kLearnTimeoutMs)) {
    // Leave Learn mode automatically so it cannot capture input forever.
    finish();
    return;
  }

  if (step == Step::WaitForKey) {
    if (mappedInput.bleCaptureSawUnsupported() && !unsupportedNoticeShown) {
      unsupportedNoticeShown = true;
      requestUpdate();
    }
    uint8_t count = 0;
    if (mappedInput.takeCapturedBleSignature(capturedKinds, capturedValues,
                                             MappedInputManager::kBleSignatureMaxEvents, count)) {
      capturedCount = count;
      LOG_INF("BLEMAP", "captured normalized input signature count=%u", static_cast<unsigned>(count));
      functionIndex = 0;
      step = Step::SelectFunction;
      waitStartedAt = millis();
      unsupportedNoticeShown = false;
      requestUpdate();
    }
    return;
  }

  // Step::SelectFunction — pick a logical function for the captured key.
  buttonNavigator.onNext([this] {
    functionIndex = ButtonNavigator::nextIndex(functionIndex, kFunctionCount);
    requestUpdate();
  });
  buttonNavigator.onPrevious([this] {
    functionIndex = ButtonNavigator::previousIndex(functionIndex, kFunctionCount);
    requestUpdate();
  });

  if (mappedInput.wasPressed(MappedInputManager::Button::Confirm)) {
    if (kFunctions[functionIndex].actionType == kNameInputFunction) {
      beginInputLabelEdit();
      return;
    }
    const auto result = applyCapturedChoice(kFunctions[functionIndex]);
    if (lastSaveFailed) {
      noticeString = StrId::STR_BT_MAP_SAVE_FAILED;
    } else {
      switch (result) {
        case bleinput::mapping::AssignResult::Added:
        case bleinput::mapping::AssignResult::Replaced:
          noticeString = StrId::STR_BT_MAP_SAVED;
          break;
        case bleinput::mapping::AssignResult::Removed:
          noticeString = StrId::STR_BT_MAP_REMOVED;
          break;
        case bleinput::mapping::AssignResult::NotFound:
          noticeString = StrId::STR_BT_MAP_NOT_FOUND;
          break;
        case bleinput::mapping::AssignResult::Full:
        case bleinput::mapping::AssignResult::Invalid:
          noticeString = StrId::STR_BT_MAP_FULL;
          break;
      }
    }
    noticeUntil = millis() + 2500;
    // Back to capturing so the user can map the next remote button.
    step = Step::WaitForKey;
    capturedCount = 0;
    pendingInputLabel[0] = '\0';
    waitStartedAt = millis();
    unsupportedNoticeShown = false;
    requestUpdate();
  }
}

void BleButtonMapActivity::render(RenderLock&&) {
  renderer.clearScreen();

  const auto& metrics = UITheme::getInstance().getMetrics();
  const auto pageWidth = renderer.getScreenWidth();
  const auto pageHeight = renderer.getScreenHeight();

  GUI.drawHeader(renderer, Rect{0, metrics.topPadding, pageWidth, metrics.headerHeight}, tr(STR_BT_MAP_BUTTONS));

  const int topOffset = metrics.topPadding + metrics.headerHeight + metrics.tabBarHeight + metrics.verticalSpacing;
  const int contentHeight = pageHeight - topOffset - metrics.buttonHintsHeight - metrics.verticalSpacing;
  GUI.drawSubHeader(renderer, Rect{0, metrics.topPadding + metrics.headerHeight, pageWidth, metrics.tabBarHeight},
                    activeProfileName[0] ? activeProfileName : "Default (legacy)");
  const int maxRows = contentHeight > 0 ? contentHeight / 22 : 0;

  if (step == Step::WaitForKey) {
    GUI.drawHelpText(renderer, Rect{0, topOffset, pageWidth, 20}, tr(STR_BT_PRESS_REMOTE));
    // Show the current mappings so the user sees progress.
    int row = 0;
    for (uint8_t slot = 0; slot < CrossPointSettings::BLE_MAP_CAPACITY; ++slot) {
      const auto& e = SETTINGS.bleKeyMap[slot];
      if (!bleinput::mapping::entryIsValid(e) || row >= maxRows - 1) continue;
      char keyName[40];
      const uint16_t first = e.signatureCount > 0 ? e.signature[0]
                                                   : (static_cast<uint16_t>(e.keyKind) << 8) | e.keyValue;
      describeMappingInput(e.inputLabel, static_cast<uint8_t>(first >> 8),
                           static_cast<uint8_t>(first & 0xFF), slot, keyName, sizeof(keyName));
      const char* fnName = "";
      for (uint8_t i = 0; i < kFunctionCount; i++) {
        if (!kFunctions[i].remove && kFunctions[i].actionType == e.actionType &&
            ((e.actionType == bleinput::mapping::kButtonAction && kFunctions[i].target == e.button) ||
             (e.actionType == bleinput::mapping::kShortcutAction && kFunctions[i].target == e.shortcut))) {
          fnName = functionLabel(kFunctions[i]);
          break;
        }
      }
      if (fnName[0] == '\0' && e.actionType == bleinput::mapping::kButtonAction) {
        if (e.button == static_cast<uint8_t>(MappedInputManager::Button::PageForward))
          fnName = I18N.get(StrId::STR_BT_PAGE_FORWARD);
        else if (e.button == static_cast<uint8_t>(MappedInputManager::Button::PageBack))
          fnName = I18N.get(StrId::STR_BT_PAGE_BACK);
      }
      if (fnName[0] == '\0') fnName = "Unassigned action";
      char line[64];
      if (e.signatureCount > 1) {
        snprintf(line, sizeof(line), "%s +%u  ->  %s", keyName,
                 static_cast<unsigned>(e.signatureCount - 1), fnName);
      } else {
        snprintf(line, sizeof(line), "%s  ->  %s", keyName, fnName);
      }
      GUI.drawHelpText(renderer, Rect{0, topOffset + 22 + row * 22, pageWidth, 20}, line);
      row++;
    }
    if (row == 0) GUI.drawHelpText(renderer, Rect{0, topOffset + 22, pageWidth, 20}, "No mappings saved yet.");
  } else if (step == Step::ViewOnly) {
    int row = 0;
    for (uint8_t slot = 0; slot < CrossPointSettings::BLE_MAP_CAPACITY; ++slot) {
      const auto& e = SETTINGS.bleKeyMap[slot];
      if (!bleinput::mapping::entryIsValid(e) || row >= maxRows) continue;
      char keyName[40], actionName[40], line[88];
      const uint16_t first = e.signatureCount ? e.signature[0]
          : static_cast<uint16_t>((static_cast<uint16_t>(e.keyKind) << 8) | e.keyValue);
      describeMappingInput(e.inputLabel, static_cast<uint8_t>(first >> 8),
                           static_cast<uint8_t>(first & 0xFF), slot, keyName, sizeof(keyName));
      std::strncpy(actionName, e.actionType == bleinput::mapping::kButtonAction ? "Button action" : "Quick action",
                   sizeof(actionName) - 1);
      actionName[sizeof(actionName) - 1] = '\0';
      for (uint8_t i = 0; i < kFunctionCount; ++i) {
        if (!kFunctions[i].remove && kFunctions[i].actionType == e.actionType &&
            ((e.actionType == bleinput::mapping::kButtonAction && kFunctions[i].target == e.button) ||
             (e.actionType == bleinput::mapping::kShortcutAction && kFunctions[i].target == e.shortcut))) {
          std::snprintf(actionName, sizeof(actionName), "%s", functionLabel(kFunctions[i]));
          break;
        }
      }
      std::snprintf(line, sizeof(line), "%s -> %s", keyName, actionName);
      GUI.drawHelpText(renderer, Rect{0, topOffset + row++ * 22, pageWidth, 20}, line);
    }
    if (row == 0) GUI.drawHelpText(renderer, Rect{0, topOffset, pageWidth, 20}, "This profile has no assignments.");
  } else if (step == Step::LoadError) {
    GUI.drawHelpText(renderer, Rect{0, topOffset, pageWidth, 40},
                     "Could not load the saved profile. Existing profile files were left unchanged.");
  } else {
    char captured[48] = {};
    if (capturedKinds[0] >= 6 && capturedKinds[0] <= 12)
      std::snprintf(captured, sizeof(captured), "Learned HID input");
    else
      bleinput::describeKey(capturedKinds[0], capturedValues[0], captured, sizeof(captured));
    if (capturedCount > 1) {
      char suffix[16];
      snprintf(suffix, sizeof(suffix), " +%u", static_cast<unsigned>(capturedCount - 1));
      strncat(captured, suffix, sizeof(captured) - strlen(captured) - 1);
    }
    GUI.drawHelpText(renderer, Rect{0, topOffset, pageWidth, 20}, captured);
    GUI.drawList(
        renderer, Rect{0, topOffset + 22, pageWidth, contentHeight - 22}, kFunctionCount, functionIndex,
        [this](int i) { return std::string(functionLabel(kFunctions[i])); }, nullptr, nullptr, nullptr, false);
  }

  if (static_cast<int32_t>(noticeUntil - millis()) > 0) {
    GUI.drawHelpText(renderer, Rect{0, pageHeight - metrics.buttonHintsHeight - 22, pageWidth, 20},
                     I18N.get(noticeString));
  } else if (step == Step::WaitForKey && mappedInput.bleCaptureSawUnsupported()) {
    const char* diagnostic = tr(STR_BT_MAP_UNSUPPORTED);
    switch (mappedInput.bleCaptureRejectReason()) {
      case static_cast<uint8_t>(freeink::CaptureRejectReason::DescriptorLength):
        diagnostic = "HID report length mismatch";
        break;
      case static_cast<uint8_t>(freeink::CaptureRejectReason::InvalidTouchReport):
        diagnostic = "Touch report not recognized";
        break;
      case static_cast<uint8_t>(freeink::CaptureRejectReason::EventQueueFull):
        diagnostic = "Input queue full; try again";
        break;
      default:
        break;
    }
    GUI.drawHelpText(renderer, Rect{0, pageHeight - metrics.buttonHintsHeight - 22, pageWidth, 20},
                     diagnostic);
  }

  const char* confirm = step == Step::WaitForKey || step == Step::ViewOnly ? "Save profile" : tr(STR_SELECT);
  const auto labels = mappedInput.mapLabels(tr(STR_BACK), confirm, tr(STR_DIR_UP), tr(STR_DIR_DOWN));
  GUI.drawButtonHints(renderer, labels.btn1, labels.btn2, labels.btn3, labels.btn4);

  renderer.displayBuffer();
}
