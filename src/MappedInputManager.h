#pragma once

#include <cstdint>

#include "util/BleKeyMapping.h"

#ifndef NOOIR_BLE_DIAGNOSTICS
#define NOOIR_BLE_DIAGNOSTICS 0
#endif

class HalGPIO;
class GfxRenderer;

namespace mappedinput {

// Fixed-size per-button BLE hold timing. A release duration remains available
// until the next input frame so consumers can classify release-frame actions.
class BleHeldDuration {
 public:
  void beginFrame() { releaseValid_ = false; }

  void press(const uint32_t now) {
    pressedAt_ = now;
    held_ = true;
    releasedMs_ = 0;
    releaseValid_ = false;
  }

  void release(const uint32_t now) {
    if (!held_) return;
    releasedMs_ = static_cast<uint32_t>(now - pressedAt_);
    held_ = false;
    releaseValid_ = true;
  }

  void clear() {
    pressedAt_ = 0;
    releasedMs_ = 0;
    held_ = false;
    releaseValid_ = false;
  }

  uint32_t duration(const uint32_t now) const {
    if (held_) return static_cast<uint32_t>(now - pressedAt_);
    return releaseValid_ ? releasedMs_ : 0;
  }

 private:
  uint32_t pressedAt_ = 0;
  uint32_t releasedMs_ = 0;
  bool held_ = false;
  bool releaseValid_ = false;
};

}  // namespace mappedinput

class MappedInputManager {
 public:
  enum class Button { Back, Confirm, Left, Right, Up, Down, Power, PageBack, PageForward, NavNext, NavPrevious };
  static constexpr uint8_t kButtonCount = 11;
  enum class SwipeDir { None, Left, Right, Up, Down };

  struct Labels {
    const char* btn1;
    const char* btn2;
    const char* btn3;
    const char* btn4;
  };

  MappedInputManager(HalGPIO& gpio, const GfxRenderer& renderer) : gpio(gpio), renderer(renderer) {}

  void update() const;
  bool wasPressed(Button button) const;
  bool wasReleased(Button button) const;
  bool isPressed(Button button) const;
#if NOOIR_BLE_DIAGNOSTICS
  // Diagnostics can identify when an activity consumed a BLE-mapped edge
  // without treating a coincident GPIO event as remote input.
  bool bleWasPressed(Button button) const;
  bool bleWasReleased(Button button) const;
  bool bleIsPressed(Button button) const;
#endif
  // ButtonNavigator-only semantic queries. Directional BLE bindings remain
  // available to menus while reader page-turn queries keep their own edges.
  bool wasNavigationPressed(Button button) const;
  bool wasNavigationReleased(Button button) const;
  bool isNavigationPressed(Button button) const;
#if NOOIR_BLE_DIAGNOSTICS
  bool bleWasNavigationPressed(Button button) const;
  bool bleWasNavigationReleased(Button button) const;
  bool bleIsNavigationPressed(Button button) const;
#endif
  bool hasTouch() const;
  bool wasScreenTapped(int& x, int& y) const;
  bool wasScreenTouchDown(int& x, int& y) const;
  bool isScreenTouchHeld(int& x, int& y) const;
  bool wasTapInRect(int x, int y, int width, int height) const;
  bool wasListItemTapped(int& index, int itemCount, int selectedIndex, int listTop, int listHeight,
                         bool hasSubtitle) const;
  bool wasListItemTouchedDown(int& index, int itemCount, int selectedIndex, int listTop, int listHeight,
                              bool hasSubtitle) const;

  // Combined touch interaction for a band of equal rows with caller-supplied
  // geometry — the shared hit-test for lists the theme helpers above do not
  // cover (custom row heights, option prompts, menus). Down = a held
  // tap-candidate is on a row (update the selection highlight); Tap = a tap
  // released on one (activate). rowHeight limits the hit to the top rowHeight
  // px of each step (0 = the full step, no gap band).
  enum class RowTouch : uint8_t { None, Down, Tap };
  RowTouch rowTouch(int& row, int top, int rowStep, int rowCount, int xStart = 0, int xEnd = INT32_MAX,
                    int rowHeight = 0) const;
  // Horizontal variant for side-by-side button pairs (confirmation prompts).
  RowTouch colTouch(int& col, int left, int colStep, int colCount, int yStart, int yEnd, int colWidth = 0) const;

  SwipeDir wasSwipe() const;
  bool wasHomeGesture() const;
  bool wasMenuGesture() const;
  bool wasAnyPressed() const;
  bool wasAnyReleased() const;
  unsigned long getHeldTime() const;
  const GfxRenderer& getRenderer() const { return renderer; }
  Labels mapLabels(const char* back, const char* confirm, const char* previous, const char* next) const;
  // Returns the raw front button index that was pressed this frame (or -1 if none).
  int getPressedFrontButton() const;

  // Drain BLE HID events into the same logical-button interface used by
  // activities. Call once per frame after BleHid.poll().
  void pollBle();
  bool bleHadActivityThisFrame() const { return bleActivityThisFrame; }
  bool takeBleShortcut(uint8_t& action);

  // Mapping screens temporarily capture an unmapped remote key instead of
  // dispatching it to the active activity.
  static constexpr uint8_t kBleSignatureMaxEvents = 8;
  void setBleCaptureMode(bool enabled);
  bool bleCaptureSawUnsupported() const { return bleCaptureSawUnsupportedInput; }
  bool takeCapturedBleKey(uint8_t& kind, uint8_t& value);
  bool takeCapturedBleSignature(uint8_t* kinds, uint8_t* values, uint8_t capacity, uint8_t& count);

  // True when the control axis is flipped relative to the physical buttons: the user opted into
  // orientation-following front buttons AND the screen is *currently rendered* rotated (INVERTED /
  // LANDSCAPE_CCW). Keyed on the live renderer orientation rather than the persisted reader setting,
  // so portrait UI (home, settings) never swaps while the reader and its menus do.
  [[nodiscard]] bool isNavDirectionSwapped() const;

  // Reader activities can opt into their own front-button mapping while the
  // home/settings UI continues using the global mapping.
  void setReaderMappingMode(bool enabled) { readerMappingMode = enabled; }
  bool isReaderMappingMode() const { return readerMappingMode; }

 private:
  HalGPIO& gpio;
  // Logical-to-physical button mapping depends on what the user is actually looking at: when the
  // screen is rendered rotated, the directional buttons must flip to match. The renderer is the only
  // authority on the *live* orientation (the reader rotates it and restores portrait on exit), so we
  // read it here instead of CrossPointSettings.orientation, which is just the persisted reader
  // preference and stays "rotated" even while portrait UI like home/settings is on screen.
  const GfxRenderer& renderer;
  bool readerMappingMode = false;

  bool mapButton(Button button, bool (HalGPIO::*fn)(uint8_t) const) const;
  bool bleEdge(const bool* edges, Button button) const;
  bool bleNavigationAlias(const bool* edges, Button button) const;
  bool wasBackGesture() const;
  // Fetch the pending swipe (if any) and map both endpoints to logical screen coords
  bool decodeSwipe(int& sx, int& sy, int& ex, int& ey) const;
  bool listItemFromPoint(int x, int y, int& index, int itemCount, int selectedIndex, int listTop, int listHeight,
                         bool hasSubtitle) const;
  void rememberTouchHeldTime() const;

  mutable bool touchHeldOverrideValid = false;
  mutable unsigned long touchHeldOverrideMs = 0;
  mutable unsigned long touchHeldOverrideAt = 0;
  bool blePressEdge[kButtonCount] = {};
  bool bleReleaseEdge[kButtonCount] = {};
  bool bleHeld[kButtonCount] = {};
  mappedinput::BleHeldDuration bleHeldDuration[kButtonCount] = {};
  bool bleActiveBinding[kButtonCount] = {};
  uint8_t bleActiveKinds[kButtonCount] = {};
  uint8_t bleActiveValues[kButtonCount] = {};
  bool bleActivityThisFrame = false;
  int8_t blePendingShortcut = -1;
  bool bleShortcutActive[bleinput::mapping::kMapCapacity] = {};
  bool bleCaptureMode = false;
  bool bleCaptureSawUnsupportedInput = false;
  bool bleHasCaptured = false;
  unsigned long bleCaptureQuietUntil = 0;
  unsigned long bleCaptureStartedAt = 0;
  uint8_t bleCapturedCount = 0;
  uint8_t bleCapturedKinds[kBleSignatureMaxEvents] = {};
  uint8_t bleCapturedValues[kBleSignatureMaxEvents] = {};
  unsigned long bleLastDispatchAt[kButtonCount] = {};
  unsigned long bleLastShortcutDispatchAt[bleinput::mapping::kShortcutCount] = {};
  int8_t blePendingMap = -1;
  uint8_t blePendingIndex = 0;
  unsigned long blePendingLastEvent = 0;
};
