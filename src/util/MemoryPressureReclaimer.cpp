#include "MemoryPressureReclaimer.h"

#include <Arduino.h>
#include <Logging.h>

#include "../SdCardFontSystem.h"
#include "../activities/RenderLock.h"

namespace memorypressure {
namespace {

bool targetsMet(const size_t freeHeap, const size_t largestBlock, const size_t targetFree,
                const size_t targetLargest) {
  return freeHeap >= targetFree && largestBlock >= targetLargest;
}

}  // namespace

ReclaimReport reclaimForHeadroom(const size_t targetFree, const size_t targetLargest, const char* reason,
                                 const ReclaimLevel allowedLevel) {
  ReclaimReport report;
  report.freeBefore = ESP.getFreeHeap();
  report.largestBefore = ESP.getMaxAllocHeap();
  report.freeAfter = report.freeBefore;
  report.largestAfter = report.largestBefore;

  LOG_INF("MEMP", "request reason=%s target=%u/%u free=%u largest=%u",
          reason ? reason : "unspecified", static_cast<unsigned>(targetFree),
          static_cast<unsigned>(targetLargest), static_cast<unsigned>(report.freeBefore),
          static_cast<unsigned>(report.largestBefore));

  if (targetsMet(report.freeBefore, report.largestBefore, targetFree, targetLargest)) {
    report.result = ReclaimResult::AlreadyEnough;
    LOG_INF("MEMP", "stop result=target_met free=%u largest=%u", static_cast<unsigned>(report.freeAfter),
            static_cast<unsigned>(report.largestAfter));
    return report;
  }

  if (allowedLevel < ReclaimLevel::Reconstructible) {
    report.result = ReclaimResult::NoCandidate;
    LOG_INF("MEMP", "stop result=no_candidate free=%u largest=%u", static_cast<unsigned>(report.freeAfter),
            static_cast<unsigned>(report.largestAfter));
    return report;
  }

  // Font/cache ownership is shared with rendering. Never mutate it while the
  // renderer owns its lock. peek() avoids waiting when the caller already
  // knows cleanup is unsafe; acquiring afterward closes the observation race.
  if (RenderLock::peek()) {
    report.result = ReclaimResult::UnsafeNow;
    LOG_INF("MEMP", "stop result=unsafe free=%u largest=%u", static_cast<unsigned>(report.freeAfter),
            static_cast<unsigned>(report.largestAfter));
    return report;
  }
  RenderLock renderLock;

  // The Reader's own FontCacheManager release is the cheaper, subsystem-owned
  // Level 1 and runs before this shared request. This Level 2 hook drops only
  // disposable mini glyph caches of size-matched UI fallback fonts; registered
  // font objects, coverage/layout tables, settings, and fallback bindings stay.
  sdFontSystem.releaseUiFallbackCaches();
  report.freeAfter = ESP.getFreeHeap();
  report.largestAfter = ESP.getMaxAllocHeap();
  const size_t freed = report.freeAfter > report.freeBefore ? report.freeAfter - report.freeBefore : 0;
  const size_t largestGain = report.largestAfter > report.largestBefore
                                 ? report.largestAfter - report.largestBefore
                                 : 0;
  LOG_INF("MEMP", "reclaim level=2 item=sd_ui_fallback_glyph_caches freed=%u largest_gain=%u free=%u largest=%u",
          static_cast<unsigned>(freed), static_cast<unsigned>(largestGain),
          static_cast<unsigned>(report.freeAfter), static_cast<unsigned>(report.largestAfter));

  if (targetsMet(report.freeAfter, report.largestAfter, targetFree, targetLargest)) {
    report.result = ReclaimResult::Reclaimed;
  } else if (report.memoryImproved()) {
    report.result = ReclaimResult::StillInsufficient;
  } else {
    report.result = ReclaimResult::NoGain;
  }
  const char* resultName = report.result == ReclaimResult::Reclaimed
                               ? "target_met"
                               : (report.result == ReclaimResult::StillInsufficient ? "still_low" : "no_gain");
  LOG_INF("MEMP", "stop result=%s free=%u largest=%u", resultName,
          static_cast<unsigned>(report.freeAfter), static_cast<unsigned>(report.largestAfter));
  return report;
}

}  // namespace memorypressure
