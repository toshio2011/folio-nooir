#pragma once

// Temporary, compile-gated EPUB memory/timing diagnostics.  The normal build
// leaves this completely inert: no counters, buffers, or log calls are kept.
// Records contain only bounded stage metadata and heap statistics; never pass
// book text, dictionary contents, or content paths to this interface.

#include <cstddef>
#include <cstdint>

#ifndef NOOIR_EPUB_DIAGNOSTICS
#define NOOIR_EPUB_DIAGNOSTICS 0
#endif

#if NOOIR_EPUB_DIAGNOSTICS
#include <Arduino.h>
#include <Logging.h>
#endif

namespace EpubDiagnostics {

#if NOOIR_EPUB_DIAGNOSTICS
inline unsigned long phaseStartMs = 0;

struct HeapWindow {
  unsigned long minFree = 0xFFFFFFFFUL;
  unsigned long minLargest = 0xFFFFFFFFUL;

  void sample(const unsigned long freeHeap, const unsigned long largestFreeBlock) {
    if (freeHeap < minFree) minFree = freeHeap;
    if (largestFreeBlock < minLargest) minLargest = largestFreeBlock;
  }
};

// Diagnostic-only nested window. Each scope restores its enclosing window.
inline HeapWindow* activeHeapWindow = nullptr;
#endif

inline void record(const char* stage, const int spine = -1, const int page = -1,
                   const unsigned long elapsedMs = 0, const size_t requestedBytes = 0,
                   const unsigned long itemCount = 0, const size_t itemBytes = 0, const int result = 0,
                   const unsigned long operationMinFree = 0, const unsigned long operationMinLargest = 0) {
#if NOOIR_EPUB_DIAGNOSTICS
  const unsigned long freeHeap = static_cast<unsigned long>(ESP.getFreeHeap());
  const unsigned long minFreeHeap = static_cast<unsigned long>(ESP.getMinFreeHeap());
  const unsigned long largestFreeBlock = static_cast<unsigned long>(ESP.getMaxAllocHeap());
  if (activeHeapWindow) activeHeapWindow->sample(freeHeap, largestFreeBlock);
  if (operationMinFree || operationMinLargest) {
    LOG_INF("EPDMEM",
            "stage=%s spine=%d page=%d elapsed_ms=%lu free=%lu min=%lu max=%lu req=%lu count=%lu size=%lu result=%d opmin=%lu opmax=%lu",
            stage ? stage : "?", spine, page, elapsedMs, freeHeap, minFreeHeap, largestFreeBlock,
            static_cast<unsigned long>(requestedBytes), itemCount, static_cast<unsigned long>(itemBytes), result,
            operationMinFree, operationMinLargest);
  } else {
    LOG_INF("EPDMEM",
            "stage=%s spine=%d page=%d elapsed_ms=%lu free=%lu min=%lu max=%lu req=%lu count=%lu size=%lu result=%d",
            stage ? stage : "?", spine, page, elapsedMs, freeHeap, minFreeHeap, largestFreeBlock,
            static_cast<unsigned long>(requestedBytes), itemCount, static_cast<unsigned long>(itemBytes), result);
  }
#else
  (void)stage;
  (void)spine;
  (void)page;
  (void)elapsedMs;
  (void)requestedBytes;
  (void)itemCount;
  (void)itemBytes;
  (void)result;
  (void)operationMinFree;
  (void)operationMinLargest;
#endif
}

// Reusable lifecycle timing window for boot, sleep, wake, and shutdown traces.
// It remains completely inert in normal builds.
inline void startPhase() {
#if NOOIR_EPUB_DIAGNOSTICS
  phaseStartMs = millis();
#endif
}

inline void phaseRecord(const char* stage, const int result = 0, const size_t requestedBytes = 0,
                        const unsigned long itemCount = 0, const size_t itemBytes = 0) {
#if NOOIR_EPUB_DIAGNOSTICS
  const unsigned long elapsed = phaseStartMs == 0 ? 0 : millis() - phaseStartMs;
  record(stage, -1, -1, elapsed, requestedBytes, itemCount, itemBytes, result);
#else
  (void)stage;
  (void)result;
  (void)requestedBytes;
  (void)itemCount;
  (void)itemBytes;
#endif
}


struct Scope {
  const char* beginStage;
  const char* endStage;
  int spine;
  int page;
  unsigned long startedMs = 0;
#if NOOIR_EPUB_DIAGNOSTICS
  HeapWindow heapWindow;
  HeapWindow* previousHeapWindow = nullptr;
#endif

  Scope(const char* beginStage, const char* endStage, const int spine = -1, const int page = -1)
      : beginStage(beginStage), endStage(endStage), spine(spine), page(page) {
#if NOOIR_EPUB_DIAGNOSTICS
    startedMs = millis();
    previousHeapWindow = activeHeapWindow;
    activeHeapWindow = &heapWindow;
    record(beginStage, spine, page);
#endif
  }

  ~Scope() {
#if NOOIR_EPUB_DIAGNOSTICS
    record(endStage, spine, page, millis() - startedMs, 0, 0, 0, 0, heapWindow.minFree,
           heapWindow.minLargest);
    if (previousHeapWindow) previousHeapWindow->sample(heapWindow.minFree, heapWindow.minLargest);
    activeHeapWindow = previousHeapWindow;
#endif
  }

  Scope(const Scope&) = delete;
  Scope& operator=(const Scope&) = delete;
};

}  // namespace EpubDiagnostics
