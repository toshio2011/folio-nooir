#pragma once

#include <HalGPIO.h>

#include <cstdint>

namespace nooir::device_identity {

enum class Model : uint8_t { X3, X4, X4Pro, X4Classic };

inline Model currentModel(const HalGPIO& gpio) {
#if defined(SIMULATOR_DEVICE_X4_PRO) || (defined(FREEINK_DEVICE_X4PRO) && FREEINK_DEVICE_X4PRO)
  (void)gpio;
  return Model::X4Pro;
#elif defined(SIMULATOR_DEVICE_X4_CLASSIC) || \
    (defined(FREEINK_DEVICE_X4CLASSIC) && FREEINK_DEVICE_X4CLASSIC)
  (void)gpio;
  return Model::X4Classic;
#else
  return gpio.deviceIsX3() ? Model::X3 : Model::X4;
#endif
}

inline const char* statusName(const Model model) {
  switch (model) {
    case Model::X3: return "X3";
    case Model::X4: return "X4";
    case Model::X4Pro: return "X4 Pro";
    case Model::X4Classic: return "X4 Classic";
  }
  return "Unknown";
}

}  // namespace nooir::device_identity
