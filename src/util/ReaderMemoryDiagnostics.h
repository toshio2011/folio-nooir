#pragma once

#ifndef NOOIR_READER_TRANSITION_DIAGNOSTICS
#define NOOIR_READER_TRANSITION_DIAGNOSTICS 0
#endif

#if NOOIR_READER_TRANSITION_DIAGNOSTICS
#include <Arduino.h>
#include <Logging.h>

namespace reader_memory_diagnostics {

inline void checkpoint(const char* stage, const char* owner) {
  LOG_INF("RDMEM", "stage=%.24s owner=%.28s free=%u largest=%u min=%u", stage ? stage : "?",
          owner ? owner : "?", static_cast<unsigned>(ESP.getFreeHeap()),
          static_cast<unsigned>(ESP.getMaxAllocHeap()), static_cast<unsigned>(ESP.getMinFreeHeap()));
}

}  // namespace reader_memory_diagnostics

#define NOOIR_READER_MEMORY_CHECKPOINT(stage, owner) \
  ::reader_memory_diagnostics::checkpoint((stage), (owner))
#else
#define NOOIR_READER_MEMORY_CHECKPOINT(stage, owner) \
  do {                                                \
  } while (0)
#endif
