#pragma once

// FreeRTOS declares this on ESP32; the pinned native simulator omits it.
#if defined(SIMULATOR) && defined(__cplusplus)
using UBaseType_t = unsigned int;
#endif

// Native simulator stand-ins for ESP32 Arduino CPU-frequency helpers. The
// physical power-management HAL remains excluded from simulator builds.
static inline int getCpuFrequencyMhz() { return 240; }
#ifdef __cplusplus
inline bool setCpuFrequencyMhz(int) { return true; }
#else
static inline int setCpuFrequencyMhz(int) { return 1; }
#endif
