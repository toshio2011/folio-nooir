#include "MappedInputManager.h"

#include <GfxRenderer.h>
#include <Logging.h>

#include <algorithm>
#include <cstdlib>

#include "BleInput.h"
#include "CrossPointSettings.h"
#include "components/UITheme.h"

namespace {
// Cheap page-turner rings often emit several keyboard/consumer usages for one
// click. Treat codes arriving in this window as one physical-button burst.
constexpr unsigned long BLE_CAPTURE_QUIET_MS = 220;
constexpr unsigned long BLE_CAPTURE_MAX_MS = 900;
constexpr unsigned long BLE_SIGNATURE_TIMEOUT_MS = 700;
// Also suppress repeats of the same logical action across adjacent HID reports.
// 280 ms remains responsive for deliberate reading while preventing double turns.
constexpr unsigned long BLE_ACTION_DEBOUNCE_MS = 280;
static_assert(MappedInputManager::kButtonCount == bleinput::mapping::kLegacyButtonCount,
              "BLE map button count must track MappedInputManager::Button values");
static_assert(static_cast<uint8_t>(MappedInputManager::Button::Down) + 1 ==
                  bleinput::mapping::kPhysicalButtonCount,
              "BLE Learn physical targets must remain Back/Confirm/Left/Right/Up/Down");
}  // namespace

bool MappedInputManager::isNavDirectionSwapped() const {
  // Key the swap on the orientation the screen is *actually* rendered at, not the persisted reader
  // setting. The reader (and its modal menus) render rotated, so navigation/labels flip there; the
  // home and settings UI render in portrait, so they never flip even when a rotated reader is configured.
  const auto orientation = renderer.getOrientation();
  return SETTINGS.frontButtonFollowOrientation &&
         (orientation == GfxRenderer::PortraitInverted || orientation == GfxRenderer::LandscapeCounterClockwise);
}

bool MappedInputManager::mapButton(const Button button, bool (HalGPIO::*fn)(uint8_t) const) const {
  const auto sideLayout = SETTINGS.sideButtonLayout;
  const uint8_t frontBack = readerMappingMode ? SETTINGS.readerFrontButtonBack : SETTINGS.frontButtonBack;
  const uint8_t frontConfirm = readerMappingMode ? SETTINGS.readerFrontButtonConfirm : SETTINGS.frontButtonConfirm;
  const uint8_t frontLeft = readerMappingMode ? SETTINGS.readerFrontButtonLeft : SETTINGS.frontButtonLeft;
  const uint8_t frontRight = readerMappingMode ? SETTINGS.readerFrontButtonRight : SETTINGS.frontButtonRight;

  switch (button) {
    case Button::Back:
      // Logical Back maps to user-configured front button.
      return (gpio.*fn)(frontBack);
    case Button::Confirm:
      // Logical Confirm maps to user-configured front button.
      return (gpio.*fn)(frontConfirm);
    case Button::Left:
      // Logical Left maps to user-configured front button.
      return (gpio.*fn)(frontLeft);
    case Button::Right:
      // Logical Right maps to user-configured front button.
      return (gpio.*fn)(frontRight);
    case Button::Up:
      // Side buttons remain fixed for Up/Down.
      return (gpio.*fn)(HalGPIO::BTN_UP);
    case Button::Down:
      // Side buttons remain fixed for Up/Down.
      return (gpio.*fn)(HalGPIO::BTN_DOWN);
    case Button::Power:
      // Power button bypasses remapping.
      return (gpio.*fn)(HalGPIO::BTN_POWER);
    case Button::PageBack:
      // Reader page navigation uses side buttons and can be swapped via settings.
      switch (sideLayout) {
        case CrossPointSettings::PREV_NEXT:
          return (gpio.*fn)(HalGPIO::BTN_UP);
        case CrossPointSettings::NEXT_PREV:
          return (gpio.*fn)(HalGPIO::BTN_DOWN);
        case CrossPointSettings::SIDE_BUTTONS_DISABLED:
        default:
          return false;
      }
    case Button::PageForward:
      // Reader page navigation uses side buttons and can be swapped via settings.
      switch (sideLayout) {
        case CrossPointSettings::PREV_NEXT:
          return (gpio.*fn)(HalGPIO::BTN_DOWN);
        case CrossPointSettings::NEXT_PREV:
          return (gpio.*fn)(HalGPIO::BTN_UP);
        case CrossPointSettings::SIDE_BUTTONS_DISABLED:
        default:
          return false;
      }
    case Button::NavNext:
      // Logical "next item" navigation: side Down + front Right, with the control axis flipped in
      // INVERTED / LANDSCAPE_CCW (frontButtonFollowOrientation) so it matches the rotated hint labels.
      return isNavDirectionSwapped() ? (mapButton(Button::Up, fn) || mapButton(Button::Left, fn))
                                     : (mapButton(Button::Down, fn) || mapButton(Button::Right, fn));
    case Button::NavPrevious:
      // Logical "previous item" navigation: side Up + front Left, axis-flipped in the same orientations.
      return isNavDirectionSwapped() ? (mapButton(Button::Down, fn) || mapButton(Button::Right, fn))
                                     : (mapButton(Button::Up, fn) || mapButton(Button::Left, fn));
  }

  return false;
}

namespace {
constexpr float LEFT_EDGE_BACK_GESTURE_FRAC_X = 0.25f;
constexpr float BOTTOM_EDGE_BACK_GESTURE_FRAC_Y = 0.14f;
constexpr float TOP_EDGE_MENU_GESTURE_FRAC_Y = 0.14f;
constexpr unsigned long TOUCH_DOWN_SELECT_DELAY_MS = 90;
constexpr unsigned long TOUCH_HELD_OVERRIDE_WINDOW_MS = 250;
}  // namespace

bool MappedInputManager::hasTouch() const { return gpio.hasTouch(); }

void MappedInputManager::rememberTouchHeldTime() const {
  touchHeldOverrideValid = true;
  touchHeldOverrideMs = gpio.lastTouchHeldMs();
  touchHeldOverrideAt = millis();
}

bool MappedInputManager::wasScreenTapped(int& x, int& y) const {
  float nx = 0.0f;
  float ny = 0.0f;
  if (!gpio.wasTouchTap(nx, ny)) return false;
  renderer.tapToLogical(nx, ny, x, y);
  rememberTouchHeldTime();
  return true;
}

bool MappedInputManager::wasScreenTouchDown(int& x, int& y) const {
  float nx = 0.0f;
  float ny = 0.0f;
  unsigned long heldMs = 0;
  if (!gpio.isTouchTapCandidate(nx, ny, heldMs)) return false;
  if (heldMs < TOUCH_DOWN_SELECT_DELAY_MS) return false;
  renderer.tapToLogical(nx, ny, x, y);
  return true;
}

bool MappedInputManager::isScreenTouchHeld(int& x, int& y) const {
  // Live contact position while the finger is down (no tap-slop gate) — drag tracking.
  float nx = 0.0f;
  float ny = 0.0f;
  if (!gpio.isTouchHeldAt(nx, ny)) return false;
  renderer.tapToLogical(nx, ny, x, y);
  return true;
}

bool MappedInputManager::wasTapInRect(const int x, const int y, const int width, const int height) const {
  int tx = 0;
  int ty = 0;
  return wasScreenTapped(tx, ty) && tx >= x && tx < x + width && ty >= y && ty < y + height;
}

bool MappedInputManager::listItemFromPoint(const int x, const int y, int& index, const int itemCount,
                                           const int selectedIndex, const int listTop, const int listHeight,
                                           const bool hasSubtitle) const {
  (void)x;
  if (itemCount <= 0) return false;
  if (y < listTop || y >= listTop + listHeight) return false;

  const auto& theme = UITheme::getInstance().getTheme();
  const int rowStep = theme.getListRowStep(hasSubtitle);
  if (rowStep <= 0) return false;

  const int pageItems = theme.getListPageItems(listHeight, hasSubtitle);
  if (pageItems <= 0) return false;
  const int pageStart = std::max(0, selectedIndex / pageItems) * pageItems;
  const int row = (y - listTop) / rowStep;
  const int tapped = pageStart + row;
  if (row < 0 || row >= pageItems || tapped >= itemCount) return false;
  index = tapped;
  return true;
}

bool MappedInputManager::wasListItemTapped(int& index, const int itemCount, const int selectedIndex, const int listTop,
                                           const int listHeight, const bool hasSubtitle) const {
  int tx = 0;
  int ty = 0;
  return wasScreenTapped(tx, ty) &&
         listItemFromPoint(tx, ty, index, itemCount, selectedIndex, listTop, listHeight, hasSubtitle);
}

bool MappedInputManager::wasListItemTouchedDown(int& index, const int itemCount, const int selectedIndex,
                                                const int listTop, const int listHeight, const bool hasSubtitle) const {
  int tx = 0;
  int ty = 0;
  return wasScreenTouchDown(tx, ty) &&
         listItemFromPoint(tx, ty, index, itemCount, selectedIndex, listTop, listHeight, hasSubtitle);
}

MappedInputManager::RowTouch MappedInputManager::rowTouch(int& row, const int top, const int rowStep,
                                                          const int rowCount, const int xStart, const int xEnd,
                                                          const int rowHeight) const {
  if (rowStep <= 0 || rowCount <= 0) return RowTouch::None;
  const auto hit = [&](const int x, const int y) {
    if (x < xStart || x >= xEnd || y < top) return false;
    const int r = (y - top) / rowStep;
    if (r >= rowCount) return false;
    if (rowHeight > 0 && (y - top) % rowStep >= rowHeight) return false;
    row = r;
    return true;
  };
  int x = 0;
  int y = 0;
  if (wasScreenTouchDown(x, y) && hit(x, y)) return RowTouch::Down;
  if (wasScreenTapped(x, y) && hit(x, y)) return RowTouch::Tap;
  return RowTouch::None;
}

MappedInputManager::RowTouch MappedInputManager::colTouch(int& col, const int left, const int colStep,
                                                          const int colCount, const int yStart, const int yEnd,
                                                          const int colWidth) const {
  if (colStep <= 0 || colCount <= 0) return RowTouch::None;
  const auto hit = [&](const int x, const int y) {
    if (y < yStart || y >= yEnd || x < left) return false;
    const int c = (x - left) / colStep;
    if (c >= colCount) return false;
    if (colWidth > 0 && (x - left) % colStep >= colWidth) return false;
    col = c;
    return true;
  };
  int x = 0;
  int y = 0;
  if (wasScreenTouchDown(x, y) && hit(x, y)) return RowTouch::Down;
  if (wasScreenTapped(x, y) && hit(x, y)) return RowTouch::Tap;
  return RowTouch::None;
}

bool MappedInputManager::decodeSwipe(int& sx, int& sy, int& ex, int& ey) const {
  float nxs = 0.0f;
  float nys = 0.0f;
  float nxe = 0.0f;
  float nye = 0.0f;
  if (!gpio.wasSwipe(nxs, nys, nxe, nye)) return false;
  renderer.tapToLogical(nxs, nys, sx, sy);
  renderer.tapToLogical(nxe, nye, ex, ey);
  return true;
}

MappedInputManager::SwipeDir MappedInputManager::wasSwipe() const {
  int sx = 0;
  int sy = 0;
  int ex = 0;
  int ey = 0;
  if (!decodeSwipe(sx, sy, ex, ey)) return SwipeDir::None;
  const int dx = ex - sx;
  const int dy = ey - sy;
  if (std::abs(dx) >= std::abs(dy)) {
    return dx < 0 ? SwipeDir::Left : SwipeDir::Right;
  }
  return dy < 0 ? SwipeDir::Up : SwipeDir::Down;
}

bool MappedInputManager::wasBackGesture() const {
  // Back = left-to-right swipe starting near the left edge. Edge-anchored so that
  // mid-screen horizontal swipes stay available to activities that consume
  // SwipeDir::Left/Right (e.g. percent selection, image viewer).
  int sx = 0;
  int sy = 0;
  int ex = 0;
  int ey = 0;
  if (!decodeSwipe(sx, sy, ex, ey)) return false;
  const bool hit = sx <= renderer.getScreenWidth() * LEFT_EDGE_BACK_GESTURE_FRAC_X && ex > sx &&
                   std::abs(ex - sx) > std::abs(ey - sy);
  if (hit) rememberTouchHeldTime();
  return hit;
}

bool MappedInputManager::wasMenuGesture() const {
  // Downward swipe starting at the top edge (mirror of the bottom-edge home gesture).
  int sx = 0;
  int sy = 0;
  int ex = 0;
  int ey = 0;
  if (!decodeSwipe(sx, sy, ex, ey)) return false;
  const int topEdgeBottom = static_cast<int>(renderer.getScreenHeight() * TOP_EDGE_MENU_GESTURE_FRAC_Y);
  const bool hit = sy <= topEdgeBottom && ey > sy && std::abs(ey - sy) > std::abs(ex - sx);
  if (hit) rememberTouchHeldTime();
  return hit;
}

bool MappedInputManager::wasHomeGesture() const {
  int sx = 0;
  int sy = 0;
  int ex = 0;
  int ey = 0;
  if (decodeSwipe(sx, sy, ex, ey)) {
    const int bottomEdgeTop =
        renderer.getScreenHeight() - static_cast<int>(renderer.getScreenHeight() * BOTTOM_EDGE_BACK_GESTURE_FRAC_Y);
    if (sy >= bottomEdgeTop && ey < sy && std::abs(ey - sy) > std::abs(ex - sx)) {
      rememberTouchHeldTime();
      return true;
    }
  }
  return false;
}

bool MappedInputManager::bleEdge(const bool* edges, const Button button) const {
  if (!SETTINGS.bluetoothEnabled) return false;

  // A decoded BLE event has one canonical physical-style owner. Keep semantic
  // aliases narrow and context-bound: Settings consumes Left/Right as rows and
  // Up/Down as tabs, while Reader may reinterpret its side buttons as
  // PageBack/PageForward according to the configured side layout. Never alias
  // PageForward to NavNext (or PageBack to NavPrevious) globally; that was the
  // source of duplicate navigation actions.
  switch (button) {
    case Button::NavNext:
      return edges[(int)Button::NavNext] ||
             (!readerMappingMode &&
              (isNavDirectionSwapped() ? (edges[(int)Button::Up] || edges[(int)Button::Left])
                                       : (edges[(int)Button::Down] || edges[(int)Button::Right])));
    case Button::NavPrevious:
      return edges[(int)Button::NavPrevious] ||
             (!readerMappingMode &&
              (isNavDirectionSwapped() ? (edges[(int)Button::Down] || edges[(int)Button::Right])
                                       : (edges[(int)Button::Up] || edges[(int)Button::Left])));
    case Button::PageBack:
      return edges[(int)Button::PageBack] ||
             (readerMappingMode &&
              (SETTINGS.sideButtonLayout == CrossPointSettings::PREV_NEXT
                   ? edges[(int)Button::Up]
                   : SETTINGS.sideButtonLayout == CrossPointSettings::NEXT_PREV ? edges[(int)Button::Down] : false));
    case Button::PageForward:
      return edges[(int)Button::PageForward] ||
             (readerMappingMode &&
              (SETTINGS.sideButtonLayout == CrossPointSettings::PREV_NEXT
                   ? edges[(int)Button::Down]
                   : SETTINGS.sideButtonLayout == CrossPointSettings::NEXT_PREV ? edges[(int)Button::Up] : false));
    default:
      return edges[(int)button];
  }
}

bool MappedInputManager::wasPressed(const Button button) const {
  if (button == Button::Back && wasBackGesture()) return true;
  return mapButton(button, &HalGPIO::wasPressed) || bleEdge(blePressEdge, button);
}

bool MappedInputManager::wasReleased(const Button button) const {
  if (button == Button::Back && wasBackGesture()) return true;
  return mapButton(button, &HalGPIO::wasReleased) || bleEdge(bleReleaseEdge, button);
}

bool MappedInputManager::isPressed(const Button button) const {
  return mapButton(button, &HalGPIO::isPressed) || bleEdge(bleHeld, button);
}

void MappedInputManager::setBleCaptureMode(const bool enabled) {
  bleCaptureMode = enabled;
  if (enabled) {
    bleCaptureSawUnsupportedInput = false;
    blePendingShortcut = -1;
    for (uint8_t i = 0; i < bleinput::mapping::kMapCapacity; ++i) bleShortcutActive[i] = false;
  }
  bleHasCaptured = false;
  bleCaptureQuietUntil = 0;
  bleCaptureStartedAt = 0;
  bleCapturedCount = 0;
  blePendingMap = -1;
  blePendingIndex = 0;
  blePendingLastEvent = 0;
  for (uint8_t i = 0; i < kButtonCount; i++) {
    blePressEdge[i] = false;
    bleReleaseEdge[i] = false;
    bleHeld[i] = false;
    bleHeldSince[i] = 0;
    bleActiveBinding[i] = false;
    bleActiveKinds[i] = 0;
    bleActiveValues[i] = 0;
    bleLastDispatchAt[i] = 0;
  }
}

bool MappedInputManager::takeCapturedBleKey(uint8_t& kind, uint8_t& value) {
  uint8_t capturedKind = 0xFF;
  uint8_t capturedValue = 0;
  uint8_t count = 0;
  if (!takeCapturedBleSignature(&capturedKind, &capturedValue, 1, count)) return false;
  kind = capturedKind;
  value = capturedValue;
  return count > 0;
}

bool MappedInputManager::takeCapturedBleSignature(uint8_t* kinds, uint8_t* values, const uint8_t capacity,
                                                  uint8_t& count) {
  count = 0;
  if (!bleHasCaptured || !kinds || !values || capacity == 0) return false;
  const unsigned long now = millis();
  if (static_cast<int32_t>(now - bleCaptureQuietUntil) < 0 && now - bleCaptureStartedAt < BLE_CAPTURE_MAX_MS)
    return false;
  const uint8_t copied = std::min<uint8_t>(bleCapturedCount, capacity);
  for (uint8_t i = 0; i < copied; ++i) {
    kinds[i] = bleCapturedKinds[i];
    values[i] = bleCapturedValues[i];
  }
  count = copied;
  bleHasCaptured = false;
  bleCapturedCount = 0;
  bleCaptureSawUnsupportedInput = false;
  return count > 0;
}

void MappedInputManager::pollBle() {
  bleActivityThisFrame = false;
  auto clearBleInputState = [&] {
    for (uint8_t i = 0; i < kButtonCount; i++) {
      blePressEdge[i] = false;
      bleReleaseEdge[i] = false;
      bleHeld[i] = false;
      bleHeldSince[i] = 0;
      bleActiveBinding[i] = false;
      bleActiveKinds[i] = 0;
      bleActiveValues[i] = 0;
      bleLastDispatchAt[i] = 0;
    }
    for (uint8_t i = 0; i < bleinput::mapping::kShortcutCount; ++i) bleLastShortcutDispatchAt[i] = 0;
    blePendingShortcut = -1;
    for (uint8_t i = 0; i < bleinput::mapping::kMapCapacity; ++i) {
      bleShortcutActive[i] = false;
    }
    blePendingMap = -1;
    blePendingIndex = 0;
    blePendingLastEvent = 0;
    bleHasCaptured = false;
    bleCapturedCount = 0;
    bleCaptureQuietUntil = 0;
    bleCaptureStartedAt = 0;
  };

  // A stopped or disconnected host is an input lifecycle boundary. Clear the
  // application-side held state here as well as in the host so reconnect cannot
  // inherit a stale key after an intentional Reader/Web teardown.
  if (!SETTINGS.bluetoothEnabled || !BleHid.isRunning() || !BleHid.isConnected()) {
    clearBleInputState();
    return;
  }
  for (uint8_t i = 0; i < kButtonCount; i++) {
    blePressEdge[i] = false;
    bleReleaseEdge[i] = false;
  }

  freeink::KeyEvent event;
  while (BleHid.popKey(event)) {
    uint8_t legacyKind = 0xFF;
    uint8_t legacyValue = 0;
    if (!bleinput::encodeKey(event, legacyKind, legacyValue)) {
      if (bleCaptureMode) bleCaptureSawUnsupportedInput = true;
#if FREEINK_CAP_BLE_HID_HOST
      bleinput::recordDecodedKey(event, legacyKind, legacyValue, 0xFF, bleinput::InputDisposition::Unusable, false);
#endif
      continue;
    }
    uint8_t learnedKind = 0xFF;
    uint8_t learnedValue = 0;
    const bool hasSafeLearnIdentity = bleinput::encodeLearnedKey(event, learnedKind, learnedValue);
    uint8_t kind = hasSafeLearnIdentity ? learnedKind : legacyKind;
    uint8_t value = hasSafeLearnIdentity ? learnedValue : legacyValue;
    uint8_t mappedButton = 0xFF;
    if (bleCaptureMode) {
      // Capture a bounded decoded signature for one physical press. The quiet
      // window ends a burst; the hard cap prevents a held/repeating remote from
      // retaining state or delaying the mapping screen indefinitely.
      // A mapping signature represents the decoded press burst, not its
      // release or firmware repeat. Capturing those phases would turn one
      // physical click into a multi-event mapping and make the next release
      // look like a second action.
      if (!hasSafeLearnIdentity) {
        bleCaptureSawUnsupportedInput = true;
#if FREEINK_CAP_BLE_HID_HOST
        bleinput::recordDecodedKey(event, legacyKind, legacyValue, 0xFC,
                                   bleinput::InputDisposition::Unusable, false);
#endif
        continue;
      }
      if (event.phase != freeink::KeyPhase::Press) {
#if FREEINK_CAP_BLE_HID_HOST
        bleinput::recordDecodedKey(event, kind, value, 0xFE, bleinput::InputDisposition::Captured, false);
#endif
        continue;
      }
      if (!bleHasCaptured) {
        bleCaptureStartedAt = millis();
        bleCapturedCount = 0;
        bleHasCaptured = true;
      }
      const auto captureResult = bleinput::mapping::addCapturedIdentity(
          bleCapturedKinds, bleCapturedValues, kBleSignatureMaxEvents, bleCapturedCount, kind, value);
      if (captureResult == bleinput::mapping::CaptureResult::Added) {
        // Repeated copies of one key do not keep Learn open. Distinct usages
        // from a rolling-code remote may still form its bounded signature.
        bleCaptureQuietUntil = millis() + BLE_CAPTURE_QUIET_MS;
      }
      mappedButton = 0xFE;  // captured by the mapping screen, not dispatched
#if FREEINK_CAP_BLE_HID_HOST
      bleinput::recordDecodedKey(event, kind, value, mappedButton, bleinput::InputDisposition::Captured, false);
#endif
      continue;
    }
    bool mappingFound = false;
    bool debounceRejected = false;
    bool signaturePending = false;
    const unsigned long now = millis();
    const bool isRelease = event.phase == freeink::KeyPhase::Release;
    const bool isRepeat = event.phase == freeink::KeyPhase::Repeat;

    uint8_t matchedKind = legacyKind;
    uint8_t matchedValue = legacyValue;
    auto entryMatches = [&](const CrossPointSettings::BleKeyMapEntry& entry, const uint8_t index) {
      if (hasSafeLearnIdentity && bleinput::mapping::matchesAt(entry, learnedKind, learnedValue, index)) {
        matchedKind = learnedKind;
        matchedValue = learnedValue;
        return true;
      }
      if (bleinput::mapping::matchesAt(entry, legacyKind, legacyValue, index)) {
        matchedKind = legacyKind;
        matchedValue = legacyValue;
        return true;
      }
      return false;
    };
    auto applyButton = [&](const uint8_t button, const freeink::KeyPhase phase) {
      if (button >= kButtonCount) return;
      const auto inputPhase = phase == freeink::KeyPhase::Release
                                  ? bleinput::mapping::InputPhase::Release
                                  : (phase == freeink::KeyPhase::Repeat ? bleinput::mapping::InputPhase::Repeat
                                                                        : bleinput::mapping::InputPhase::Press);
      const auto transition = bleinput::mapping::transition(inputPhase, bleHeld[button], bleActiveBinding[button],
                                                            now, bleLastDispatchAt[button],
                                                            BLE_ACTION_DEBOUNCE_MS);
      if (transition == bleinput::mapping::ButtonTransition::Released) {
        bleHeld[button] = false;
        bleHeldSince[button] = 0;
        bleActiveBinding[button] = false;
        bleReleaseEdge[button] = true;
        bleActivityThisFrame = true;
        mappedButton = button;
        return;
      }
      if (transition == bleinput::mapping::ButtonTransition::Repeated) {
        bleHeld[button] = true;
        bleActivityThisFrame = true;
        mappedButton = button;
        return;
      }
      if (transition == bleinput::mapping::ButtonTransition::Debounced) {
        debounceRejected = true;
        return;
      }
      if (transition != bleinput::mapping::ButtonTransition::Pressed) return;
      bleLastDispatchAt[button] = now;
      bleHeld[button] = true;
      bleHeldSince[button] = now;
      bleActiveBinding[button] = true;
      bleActiveKinds[button] = matchedKind;
      bleActiveValues[button] = matchedValue;
      blePressEdge[button] = true;
      bleActivityThisFrame = true;
      mappedButton = button;
    };
    auto dispatch = [&](const CrossPointSettings::BleKeyMapEntry& entry, const uint8_t mapIndex) {
      const auto action = bleinput::mapping::resolve(entry);
      if (action.type == bleinput::mapping::ResolvedActionType::Invalid) return;
      if (action.type == bleinput::mapping::ResolvedActionType::Shortcut) {
        if (event.phase != freeink::KeyPhase::Press || mapIndex >= bleinput::mapping::kMapCapacity) return;
        if (bleShortcutActive[mapIndex]) {
          debounceRejected = true;
          return;
        }
        if (action.target >= bleinput::mapping::kShortcutCount ||
            (bleLastShortcutDispatchAt[action.target] != 0 &&
             !bleinput::mapping::debounceElapsed(now, bleLastShortcutDispatchAt[action.target],
                                                 BLE_ACTION_DEBOUNCE_MS))) {
          debounceRejected = true;
          return;
        }
        bleLastShortcutDispatchAt[action.target] = now;
        bleShortcutActive[mapIndex] = true;
        blePendingShortcut = static_cast<int8_t>(action.target);
        bleActivityThisFrame = true;
        mappedButton = 0xFD;  // shortcut queued for ActivityManager
        return;
      }
      applyButton(action.target, event.phase);
    };

    // Releases and repeats belong to the binding that consumed the original
    // press. Resolve them before signature recognition so they can never start
    // or advance a new multi-report mapping.
    if (isRelease || isRepeat) {
      for (uint8_t mapIndex = 0; mapIndex < CrossPointSettings::BLE_MAP_CAPACITY; ++mapIndex) {
        const auto& entry = SETTINGS.bleKeyMap[mapIndex];
        if (bleinput::mapping::resolve(entry).type != bleinput::mapping::ResolvedActionType::Shortcut ||
            !bleShortcutActive[mapIndex] ||
            !entryMatches(entry, 0))
          continue;
        mappingFound = true;
        if (isRelease) bleShortcutActive[mapIndex] = false;
      }
      for (uint8_t button = 0; button < kButtonCount; ++button) {
        const bool activeIdentityMatches =
            (bleActiveKinds[button] == legacyKind && bleActiveValues[button] == legacyValue) ||
            (hasSafeLearnIdentity && bleActiveKinds[button] == learnedKind &&
             bleActiveValues[button] == learnedValue);
        if (!bleActiveBinding[button] || !activeIdentityMatches) continue;
        mappingFound = true;
        applyButton(button, event.phase);
      }
      if (blePendingMap >= 0 && isRelease &&
          bleinput::mapping::releaseKeepsPendingSignature(bleinput::mapping::InputPhase::Release)) {
        signaturePending = true;
      }
    }

    // Continue a previously recognized multi-event signature first.
    if (!isRelease && !isRepeat && blePendingMap >= 0) {
      auto& pending = SETTINGS.bleKeyMap[blePendingMap];
      if (now - blePendingLastEvent <= BLE_SIGNATURE_TIMEOUT_MS && entryMatches(pending, blePendingIndex)) {
        ++blePendingIndex;
        blePendingLastEvent = now;
        mappingFound = true;
        if (blePendingIndex >= bleinput::mapping::entryCount(pending)) {
          dispatch(pending, static_cast<uint8_t>(blePendingMap));
          blePendingMap = -1;
          blePendingIndex = 0;
        } else {
          signaturePending = true;
        }
      } else {
        blePendingMap = -1;
        blePendingIndex = 0;
      }
    }

    // If no signature was pending, start one from this event or dispatch a
    // legacy single-event binding immediately.
    if (!isRelease && !isRepeat && blePendingMap < 0 && !mappingFound) {
      const int8_t mapIndex = bleinput::mapping::findFirstMatch(
          SETTINGS.bleKeyMap, hasSafeLearnIdentity ? learnedKind : legacyKind,
          hasSafeLearnIdentity ? learnedValue : legacyValue, legacyKind, legacyValue, 0);
      if (mapIndex >= 0) {
        const auto& entry = SETTINGS.bleKeyMap[mapIndex];
        if (!entryMatches(entry, 0)) continue;
        mappingFound = true;
        if (bleinput::mapping::entryCount(entry) > 1) {
          blePendingMap = mapIndex;
          blePendingIndex = 1;
          blePendingLastEvent = now;
          signaturePending = true;
        } else {
          dispatch(entry, static_cast<uint8_t>(mapIndex));
        }
      }
    }
#if FREEINK_CAP_BLE_HID_HOST
    const auto disposition = mappedButton != 0xFF
                                 ? bleinput::InputDisposition::Mapped
                                 : (signaturePending ? bleinput::InputDisposition::Pending
                                                     : (debounceRejected ? bleinput::InputDisposition::Debounced
                                                                         : (mappingFound ? bleinput::InputDisposition::Debounced
                                                                                           : bleinput::InputDisposition::Unmapped)));
    bleinput::recordDecodedKey(event, kind, value, mappedButton, disposition, mappedButton != 0xFF);
#endif
  }
}

bool MappedInputManager::takeBleShortcut(uint8_t& action) {
  if (blePendingShortcut < 0) return false;
  action = static_cast<uint8_t>(blePendingShortcut);
  blePendingShortcut = -1;
  return action < bleinput::mapping::kShortcutCount;
}

bool MappedInputManager::wasAnyPressed() const {
  if (gpio.wasAnyPressed()) return true;
  for (uint8_t i = 0; i < kButtonCount; ++i) {
    if (blePressEdge[i]) return true;
  }
  return false;
}

bool MappedInputManager::wasAnyReleased() const {
  if (gpio.wasAnyReleased()) return true;
  for (uint8_t i = 0; i < kButtonCount; ++i) {
    if (bleReleaseEdge[i]) return true;
  }
  return false;
}

unsigned long MappedInputManager::getHeldTime() const {
  if (!gpio.wasAnyPressed() && !gpio.wasAnyReleased() && touchHeldOverrideValid &&
      millis() - touchHeldOverrideAt <= TOUCH_HELD_OVERRIDE_WINDOW_MS) {
    return touchHeldOverrideMs;
  }
  touchHeldOverrideValid = false;
  unsigned long held = gpio.getHeldTime();
  const unsigned long now = millis();
  for (uint8_t i = 0; i < kButtonCount; ++i) {
    if (bleHeld[i] && bleHeldSince[i] != 0) held = std::max(held, now - bleHeldSince[i]);
  }
  return held;
}

MappedInputManager::Labels MappedInputManager::mapLabels(const char* back, const char* confirm, const char* previous,
                                                         const char* next) const {
  // Swap previous/next labels to match the page turn direction swap in INVERTED and LANDSCAPE_CCW.
  const bool swapLabels = isNavDirectionSwapped();
  const char* leftLabel = swapLabels ? next : previous;
  const char* rightLabel = swapLabels ? previous : next;

  // Build the label order based on the configured hardware mapping.
  const uint8_t frontBack = readerMappingMode ? SETTINGS.readerFrontButtonBack : SETTINGS.frontButtonBack;
  const uint8_t frontConfirm = readerMappingMode ? SETTINGS.readerFrontButtonConfirm : SETTINGS.frontButtonConfirm;
  const uint8_t frontLeft = readerMappingMode ? SETTINGS.readerFrontButtonLeft : SETTINGS.frontButtonLeft;
  const uint8_t frontRight = readerMappingMode ? SETTINGS.readerFrontButtonRight : SETTINGS.frontButtonRight;
  auto labelForHardware = [&](uint8_t hw) -> const char* {
    // Compare against configured logical roles and return the matching label.
    if (hw == frontBack) {
      return back;
    }
    if (hw == frontConfirm) {
      return confirm;
    }
    if (hw == frontLeft) {
      return leftLabel;
    }
    if (hw == frontRight) {
      return rightLabel;
    }
    return "";
  };

  return {labelForHardware(HalGPIO::BTN_BACK), labelForHardware(HalGPIO::BTN_CONFIRM),
          labelForHardware(HalGPIO::BTN_LEFT), labelForHardware(HalGPIO::BTN_RIGHT)};
}

int MappedInputManager::getPressedFrontButton() const {
  // Scan the raw front buttons in hardware order.
  // This bypasses remapping so the remap activity can capture physical presses.
  if (gpio.wasPressed(HalGPIO::BTN_BACK)) {
    return HalGPIO::BTN_BACK;
  }
  if (gpio.wasPressed(HalGPIO::BTN_CONFIRM)) {
    return HalGPIO::BTN_CONFIRM;
  }
  if (gpio.wasPressed(HalGPIO::BTN_LEFT)) {
    return HalGPIO::BTN_LEFT;
  }
  if (gpio.wasPressed(HalGPIO::BTN_RIGHT)) {
    return HalGPIO::BTN_RIGHT;
  }
  return -1;
}
