#include "LongOperationIndicator.h"

#include <Arduino.h>

#include <algorithm>
#include <cstring>

#include "Logging.h"

void LongOperationIndicator::begin(const char* operationName) {
  // A stale scope should never survive a reader transition, but keeping begin
  // idempotent makes the helper safe if a caller retries an operation in place.
  if (active) cancel("replaced");
  active = true;
  // Preserve a loading line that was shown by the preceding event-loop
  // phase. The normal render will replace it with the completed page.
  presented = loadingUiShown;
  operation = safeName(operationName);
  currentStage = "preparing";
  startedAt = millis();
  LOG_DBG("LONGOP", "begin op=%s stage=%s", operation, currentStage);
}

void LongOperationIndicator::stage(const char* stageName) {
  if (!active) return;
  const char* next = safeName(stageName);
  if (currentStage && std::strcmp(currentStage, next) == 0) return;
  currentStage = next;
  LOG_DBG("LONGOP", "stage op=%s stage=%s elapsed=%lums", operation, currentStage,
          static_cast<unsigned long>(millis() - startedAt));
}

bool LongOperationIndicator::showBeforeWork(const char* operationName, const bool allowRefresh) {
  if (!allowRefresh || !renderer.hasFrameBuffer()) {
    LOG_DBG("LONGOP", "loading_ui=skipped op=%s reason=%s", safeName(operationName),
            allowRefresh ? "no_framebuffer" : "profile");
    return false;
  }

  const uint32_t refreshStarted = millis();
  operation = safeName(operationName);
  // The reader has already presented the previous page. Paint one static line
  // into that existing framebuffer and use the same lightweight refresh mode
  // already used for ordinary reader page updates.
  renderer.setRenderMode(GfxRenderer::BW);
  const int screenWidth = renderer.getScreenWidth();
  const int screenHeight = renderer.getScreenHeight();
  const int indicatorX = std::min(INDICATOR_INSET, std::max(0, screenWidth - 1));
  const int indicatorY = std::min(INDICATOR_INSET, std::max(0, screenHeight - 1));
  const int indicatorWidth = std::max(1, screenWidth - indicatorX * 2);
  const int indicatorHeight = std::min(INDICATOR_HEIGHT, std::max(1, screenHeight - indicatorY));
  renderer.fillRect(indicatorX, indicatorY, indicatorWidth, indicatorHeight, true);
  renderer.displayBuffer(HalDisplay::FAST_REFRESH);
  const uint32_t refreshMs = millis() - refreshStarted;
  loadingUiShown = true;
  presented = true;
  LOG_DBG("LONGOP", "loading_ui=shown op=%s", operation);
  LOG_DBG("LONGOP", "loading_ui=shown rect=%d,%d,%d,%d", indicatorX, indicatorY, indicatorWidth,
          indicatorHeight);
  LOG_DBG("LONGOP", "loading_ui_refresh_ms=%lums", static_cast<unsigned long>(refreshMs));
  return true;
}

bool LongOperationIndicator::drawIfDue() {
  if (!active || presented) return false;
  const uint32_t elapsed = millis() - startedAt;
  if (elapsed < SHOW_DELAY_MS) return false;

  // This is intentionally a single thin line rather than a text popup or an
  // animated bar. The caller invokes us immediately before its normal refresh;
  // no extra refresh is scheduled and the next reader render removes the line.
  const int screenWidth = renderer.getScreenWidth();
  const int screenHeight = renderer.getScreenHeight();
  const int indicatorX = std::min(INDICATOR_INSET, std::max(0, screenWidth - 1));
  const int indicatorY = std::min(INDICATOR_INSET, std::max(0, screenHeight - 1));
  const int indicatorWidth = std::max(1, screenWidth - indicatorX * 2);
  const int indicatorHeight = std::min(INDICATOR_HEIGHT, std::max(1, screenHeight - indicatorY));
  renderer.fillRect(indicatorX, indicatorY, indicatorWidth, indicatorHeight, true);
  presented = true;
  LOG_DBG("LONGOP", "present op=%s stage=%s elapsed=%lums rect=%d,%d,%d,%d", operation,
          safeName(currentStage), static_cast<unsigned long>(elapsed), indicatorX, indicatorY,
          indicatorWidth, indicatorHeight);
  return true;
}

void LongOperationIndicator::complete() {
  if (!active) return;
  const uint32_t elapsed = millis() - startedAt;
  if (!presented && elapsed < SHOW_DELAY_MS) {
    LOG_DBG("LONGOP", "skip_fast op=%s duration=%lums", operation, static_cast<unsigned long>(elapsed));
  } else {
    LOG_DBG("LONGOP", "complete op=%s stage=%s duration=%lums presented=%d", operation, safeName(currentStage),
            static_cast<unsigned long>(elapsed), presented ? 1 : 0);
  }
  if (loadingUiShown) LOG_DBG("LONGOP", "loading_ui=hidden op=%s", safeName(operation));
  active = false;
  presented = false;
  loadingUiShown = false;
  operation = nullptr;
  currentStage = nullptr;
}

void LongOperationIndicator::cancel(const char* reason) {
  if (!active && !loadingUiShown) return;
  if (active) {
    LOG_DBG("LONGOP", "cancel op=%s stage=%s reason=%s elapsed=%lums", operation, safeName(currentStage),
            safeName(reason), static_cast<unsigned long>(millis() - startedAt));
  }
  if (loadingUiShown) LOG_DBG("LONGOP", "loading_ui=hidden op=%s", safeName(operation));
  active = false;
  presented = false;
  loadingUiShown = false;
  operation = nullptr;
  currentStage = nullptr;
}
