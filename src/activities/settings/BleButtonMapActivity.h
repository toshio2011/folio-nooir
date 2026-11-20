#pragma once

#include <I18n.h>

#include <cstdint>
#include <cstring>

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
  explicit BleButtonMapActivity(GfxRenderer& renderer, MappedInputManager& mappedInput,
                                bool viewOnly = false, const char* profileName = "")
      : Activity("BleButtonMap", renderer, mappedInput), viewOnly(viewOnly) {
    if (profileName) std::strncpy(activeProfileName, profileName, sizeof(activeProfileName) - 1);
  }

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
    uint8_t quickAction = 0xFF;  // Existing QuickActions id, or 0xFF for a StrId label.
  };
  static const Fn kFunctions[];
  static const uint8_t kFunctionCount;

  enum class Step { WaitForKey, SelectFunction, ViewOnly, LoadError };
  Step step = Step::WaitForKey;
  bool viewOnly = false;
  char activeProfileName[40] = {};
  unsigned long waitStartedAt = 0;
  bool unsupportedNoticeShown = false;
  bool lastSaveFailed = false;

  uint8_t capturedCount = 0;
  uint8_t capturedKinds[MappedInputManager::kBleSignatureMaxEvents] = {};
  uint8_t capturedValues[MappedInputManager::kBleSignatureMaxEvents] = {};
  int functionIndex = 0;
  char pendingInputLabel[bleinput::mapping::kInputLabelBytes] = {};

  // Transient assignment feedback, including explicit remove/not-found results.
  unsigned long noticeUntil = 0;
  StrId noticeString = StrId::STR_BT_MAP_FULL;

  ButtonNavigator buttonNavigator;

  // Assign, replace, or remove the captured bounded identity and persist it.
  bleinput::mapping::AssignResult applyCapturedChoice(const Fn& choice);
  const char* functionLabel(const Fn& choice) const;
  void resolveActiveProfileName();
  void beginSaveAsProfile();
  void beginInputLabelEdit();
  void describeMappingInput(const char* label, uint8_t kind, uint8_t value, uint8_t slot,
                            char* out, size_t outLength) const;
};
