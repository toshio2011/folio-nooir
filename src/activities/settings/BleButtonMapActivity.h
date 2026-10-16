#pragma once

#include <I18n.h>

#include <cstdint>

#include "MappedInputManager.h"
#include "activities/Activity.h"
#include "util/ButtonNavigator.h"

// Capture-then-assign mapping for BLE page-turner buttons. The user presses a
// button on the remote; we capture its decoded key identity (via the
// MappedInputManager BLE capture mode) and let them bind it to a logical button.
// Repeat to map each remote button; Back exits. Mirrors ButtonRemapActivity's
// flow, but the input source is the BLE host instead of the front buttons.
class BleButtonMapActivity final : public Activity {
 public:
  explicit BleButtonMapActivity(GfxRenderer& renderer, MappedInputManager& mappedInput)
      : Activity("BleButtonMap", renderer, mappedInput) {}

  void onEnter() override;
  void onExit() override;
  void loop() override;
  void render(RenderLock&&) override;

 private:
  // Logical functions a remote button can be bound to.
  struct Fn {
    uint8_t actionType;
    uint8_t target;
    StrId label;
    bool remove = false;
  };
  static const Fn kFunctions[];
  static const uint8_t kFunctionCount;

  enum class Step { WaitForKey, SelectFunction };
  Step step = Step::WaitForKey;
  unsigned long waitStartedAt = 0;
  bool unsupportedNoticeShown = false;

  uint8_t capturedCount = 0;
  uint8_t capturedKinds[MappedInputManager::kBleSignatureMaxEvents] = {};
  uint8_t capturedValues[MappedInputManager::kBleSignatureMaxEvents] = {};
  int functionIndex = 0;

  // Transient assignment feedback, including explicit remove/not-found results.
  unsigned long noticeUntil = 0;
  StrId noticeString = StrId::STR_BT_MAP_FULL;

  ButtonNavigator buttonNavigator;

  // Assign, replace, or remove the captured bounded identity and persist it.
  bleinput::mapping::AssignResult applyCapturedChoice(const Fn& choice);
};
