#include "BleButtonMapActivity.h"

#include <GfxRenderer.h>
#include <Logging.h>

#include <cstdio>
#include <cstring>

#include "BleInput.h"
#include "CrossPointSettings.h"
#include "components/UITheme.h"
#include "fontIds.h"

namespace {
constexpr unsigned long kLearnTimeoutMs = 30000;
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
    {0xFF, 0xFF, StrId::STR_DELETE, true},
};
const uint8_t BleButtonMapActivity::kFunctionCount =
    static_cast<uint8_t>(sizeof(kFunctions) / sizeof(kFunctions[0]));

void BleButtonMapActivity::onEnter() {
  Activity::onEnter();
  step = Step::WaitForKey;
  capturedCount = 0;
  functionIndex = 0;
  waitStartedAt = millis();
  unsupportedNoticeShown = false;
  lastSaveFailed = false;
  noticeUntil = 0;
  mappedInput.setBleCaptureMode(true);
  requestUpdate();
}

void BleButtonMapActivity::onExit() {
  mappedInput.setBleCaptureMode(false);
  Activity::onExit();
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
  if (result == bleinput::mapping::AssignResult::Added || result == bleinput::mapping::AssignResult::Replaced ||
      result == bleinput::mapping::AssignResult::Removed) {
    if (!SETTINGS.saveToFile()) {
      memcpy(SETTINGS.bleKeyMap, previousMap, sizeof(previousMap));
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

  if (step == Step::WaitForKey) {
    GUI.drawSubHeader(renderer, Rect{0, metrics.topPadding + metrics.headerHeight, pageWidth, metrics.tabBarHeight},
                      tr(STR_BT_PRESS_REMOTE));
    // Show the current mappings so the user sees progress.
    int row = 0;
    for (const auto& e : SETTINGS.bleKeyMap) {
      if (!bleinput::mapping::entryIsValid(e)) continue;
      char keyName[24];
      const uint16_t first = e.signatureCount > 0 ? e.signature[0]
                                                   : (static_cast<uint16_t>(e.keyKind) << 8) | e.keyValue;
      bleinput::describeKey(static_cast<uint8_t>(first >> 8), static_cast<uint8_t>(first & 0xFF), keyName,
                            sizeof(keyName));
      const char* fnName = "";
      for (uint8_t i = 0; i < kFunctionCount; i++) {
        if (!kFunctions[i].remove && kFunctions[i].actionType == e.actionType &&
            ((e.actionType == bleinput::mapping::kButtonAction && kFunctions[i].target == e.button) ||
             (e.actionType == bleinput::mapping::kShortcutAction && kFunctions[i].target == e.shortcut))) {
          fnName = I18N.get(kFunctions[i].label);
          break;
        }
      }
      if (fnName[0] == '\0' && e.actionType == bleinput::mapping::kButtonAction) {
        if (e.button == static_cast<uint8_t>(MappedInputManager::Button::PageForward))
          fnName = I18N.get(StrId::STR_BT_PAGE_FORWARD);
        else if (e.button == static_cast<uint8_t>(MappedInputManager::Button::PageBack))
          fnName = I18N.get(StrId::STR_BT_PAGE_BACK);
      }
      char line[64];
      if (e.signatureCount > 1) {
        snprintf(line, sizeof(line), "%s +%u  ->  %s", keyName,
                 static_cast<unsigned>(e.signatureCount - 1), fnName);
      } else {
        snprintf(line, sizeof(line), "%s  ->  %s", keyName, fnName);
      }
      GUI.drawHelpText(renderer, Rect{0, topOffset + row * 22, pageWidth, 20}, line);
      row++;
    }
  } else {
    char captured[48] = {};
    bleinput::describeKey(capturedKinds[0], capturedValues[0], captured, sizeof(captured));
    if (capturedCount > 1) {
      char suffix[16];
      snprintf(suffix, sizeof(suffix), " +%u", static_cast<unsigned>(capturedCount - 1));
      strncat(captured, suffix, sizeof(captured) - strlen(captured) - 1);
    }
    GUI.drawSubHeader(renderer, Rect{0, metrics.topPadding + metrics.headerHeight, pageWidth, metrics.tabBarHeight},
                      captured);
    GUI.drawList(
        renderer, Rect{0, topOffset, pageWidth, contentHeight}, kFunctionCount, functionIndex,
        [this](int i) { return std::string(I18N.get(kFunctions[i].label)); }, nullptr, nullptr, nullptr, false);
  }

  if (static_cast<int32_t>(noticeUntil - millis()) > 0) {
    GUI.drawHelpText(renderer, Rect{0, pageHeight - metrics.buttonHintsHeight - 22, pageWidth, 20},
                     I18N.get(noticeString));
  } else if (step == Step::WaitForKey && mappedInput.bleCaptureSawUnsupported()) {
    GUI.drawHelpText(renderer, Rect{0, pageHeight - metrics.buttonHintsHeight - 22, pageWidth, 20},
                     tr(STR_BT_MAP_UNSUPPORTED));
  }

  const char* confirm = step == Step::WaitForKey ? "" : tr(STR_SELECT);
  const auto labels = mappedInput.mapLabels(tr(STR_BACK), confirm, tr(STR_DIR_UP), tr(STR_DIR_DOWN));
  GUI.drawButtonHints(renderer, labels.btn1, labels.btn2, labels.btn3, labels.btn4);

  renderer.displayBuffer();
}
