#pragma once

#if defined(NOOIR_S3_SDK_COMPAT) && NOOIR_S3_SDK_COMPAT && defined(FREEINK_CAP_FRONTLIGHT) && FREEINK_CAP_FRONTLIGHT

#include <FrontlightManager.h>

// Small Nooir HAL over the upstream FreeInk Pro frontlight driver. Persistent
// values live in CrossPointSettings; this class only owns live PWM state.
class HalFrontlight {
 public:
  static HalFrontlight& getInstance() { return instance; }

  void begin(uint8_t brightness, uint8_t warmth, bool on);
  void prepareForDeepSleep();

  bool present() const { return manager.present(); }
  bool hasColorTemperature() const { return manager.hasColorTemperature(); }
  void setBrightness(uint8_t percent);
  void setWarmth(uint8_t warmPercent);
  void setOn(bool on);

  uint8_t brightness() const { return lastBrightness; }
  uint8_t warmth() const { return manager.colorTemperature(); }
  bool isOn() const { return lit; }

 private:
  HalFrontlight() = default;

  FrontlightManager manager;
  uint8_t lastBrightness = 60;
  bool lit = false;

  static HalFrontlight instance;
};

#define Frontlight HalFrontlight::getInstance()

#endif
