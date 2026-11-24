#include "HalFrontlight.h"

#if defined(NOOIR_S3_SDK_COMPAT) && NOOIR_S3_SDK_COMPAT && defined(FREEINK_CAP_FRONTLIGHT) && FREEINK_CAP_FRONTLIGHT

#include <Logging.h>

HalFrontlight HalFrontlight::instance;

void HalFrontlight::begin(const uint8_t brightness, const uint8_t warmth, const bool on) {
#ifdef FREEINK_FRONTLIGHT_LS
  manager.releaseOnWake();
#endif
  manager.begin();
  if (!manager.present()) return;

  lastBrightness = brightness > 100 ? 100 : brightness;
  lit = on;
  manager.setBrightness(lit ? lastBrightness : 0);
  manager.setColorTemperature(warmth > 100 ? 100 : warmth);
  LOG_INF("LIGHT", "X4 Pro frontlight: %u%% warm=%u%% %s", lastBrightness, manager.colorTemperature(),
          lit ? "on" : "off");
}

void HalFrontlight::prepareForDeepSleep() {
  if (!manager.present()) return;
  manager.prepareForDeepSleep();
  lit = false;
}

void HalFrontlight::setBrightness(const uint8_t percent) {
  lastBrightness = percent > 100 ? 100 : percent;
  if (lit) manager.setBrightness(lastBrightness);
}

void HalFrontlight::setWarmth(const uint8_t warmPercent) {
  manager.setColorTemperature(warmPercent > 100 ? 100 : warmPercent);
}

void HalFrontlight::setOn(const bool on) {
  if (on == lit) return;
  lit = on;
  manager.setBrightness(lit ? lastBrightness : 0);
}

#endif
