#pragma once

#include <algorithm>
#include <cstddef>
#include <cstdint>

namespace reader_memory {

// The physical trace loses 6,676 bytes between a quiescent page and the next
// loaded page. Round that observed page/deserialization cost up to 8 KiB.
inline constexpr std::size_t kPageLoadReserve = 8 * 1024;
inline constexpr std::size_t kAllocationReserve = 2 * 1024;
inline constexpr std::size_t kLargestBlockMargin = 64;
inline constexpr std::size_t kHeavyFontGroupRequestBytes = 30 * 1024;

enum class PressureTier : uint8_t { Normal = 0, Constrained = 1, Heavy = 2 };

struct CacheBudget {
  uint16_t pageGlyphLimit;
  uint16_t scanTextBytesPerFont;
};

constexpr CacheBudget cacheBudgetFor(PressureTier tier) {
  switch (tier) {
    case PressureTier::Heavy: return {256, 2048};
    case PressureTier::Constrained: return {384, 3072};
    case PressureTier::Normal: default: return {512, 4096};
  }
}

struct PressureObservation {
  std::size_t freeBytes = 0;
  std::size_t largestBlock = 0;
  std::size_t recentMinFreeBytes = 0;
  std::size_t largestRenderRequestBytes = 0;
  bool readerWindowPressure = false;
  bool renderAllocationFailed = false;
  bool imageOrGrayscaleWork = false;
};

constexpr PressureTier classifyPressure(const PressureObservation& observation) {
  if (observation.readerWindowPressure || observation.renderAllocationFailed ||
      observation.freeBytes < 32 * 1024 || observation.largestBlock < 24 * 1024 ||
      (observation.recentMinFreeBytes > 0 && observation.recentMinFreeBytes < 16 * 1024) ||
      (observation.largestRenderRequestBytes >= kHeavyFontGroupRequestBytes &&
       observation.largestBlock < 40 * 1024)) {
    return PressureTier::Heavy;
  }
  if (observation.freeBytes < 72 * 1024 || observation.largestBlock < 48 * 1024 ||
      (observation.recentMinFreeBytes > 0 && observation.recentMinFreeBytes < 28 * 1024) ||
      observation.largestRenderRequestBytes >= 16 * 1024 || observation.imageOrGrayscaleWork) {
    return PressureTier::Constrained;
  }
  return PressureTier::Normal;
}

// Pressure rises immediately, while recovery needs two successful light
// renders per tier. That avoids oscillating the rebuildable caches on a noisy
// heap sample and lets later lighter sections recover toward normal budgets.
constexpr PressureTier advancePressureTier(const PressureTier current, const PressureTier observed,
                                           const uint8_t healthyRenderCount) {
  if (static_cast<uint8_t>(observed) >= static_cast<uint8_t>(current)) return observed;
  if (healthyRenderCount < 2) return current;
  return static_cast<PressureTier>(static_cast<uint8_t>(current) - 1);
}

struct RenderBudget {
  std::size_t freeBytes;
  std::size_t largestBlock;
};

// Call only at render entry/completion. The page-load and font scan arenas are
// rebuildable; the measured largest request carries forward the real font
// group sizes seen on this device. A per-prewarm admission gate checks the
// exact groups on a first-seen page before allocating them.
constexpr RenderBudget cachedRenderBudget(std::size_t scanBytes, std::size_t codepointBytes,
                                         std::size_t stripBytes, std::size_t glyphArenaBytes,
                                         std::size_t highlightBytes, std::size_t observedPeakBytes = 0,
                                         std::size_t observedLargestRequestBytes = 0,
                                         bool pageAlreadyLoaded = false) {
  const auto pageBytes = pageAlreadyLoaded ? 0 : kPageLoadReserve;
  const auto largestRequest = observedLargestRequestBytes;
  // Scan strings and the measured group request overlap page prewarm; gray
  // scratch is later. Do not invent a 32 KiB request for books that have never
  // needed one: the FDC gate predicts the exact group set for the first page.
  const auto prewarmBytes = scanBytes + codepointBytes + glyphArenaBytes + largestRequest;
  const auto base = pageBytes + std::max(prewarmBytes, stripBytes) + highlightBytes + kAllocationReserve;
  const auto measured = pageAlreadyLoaded ? 0 : observedPeakBytes + kPageLoadReserve;
  const auto largest = std::max({scanBytes, codepointBytes, stripBytes, glyphArenaBytes, highlightBytes,
                                 largestRequest});
  const auto largestMargin = largestRequest > 0 && largest == largestRequest ? kLargestBlockMargin
                                                                              : kAllocationReserve;
  return {std::max(base, measured), largest + largestMargin};
}

// After lazy font preparation, protect the remaining BW/gray pass and the
// bounded 1 KiB highlight-normalization strings (including growth overlap).
constexpr RenderBudget finalPassBudget(std::size_t stripBytes, bool hasHighlights) {
  const auto textBytes = hasHighlights ? 4 * 1024 : 0;
  return {stripBytes + textBytes + kAllocationReserve,
          std::max(stripBytes, std::size_t(hasHighlights ? 2 * 1024 : 0)) + kAllocationReserve};
}

constexpr bool renderAdmitted(std::size_t freeBytes, std::size_t largestBlock, RenderBudget required) {
  return freeBytes >= required.freeBytes && largestBlock >= required.largestBlock;
}

constexpr bool survivalStopRequired(bool enabled, bool running, std::size_t freeBytes,
                                    std::size_t largestBlock, RenderBudget required) {
  return enabled && running && !renderAdmitted(freeBytes, largestBlock, required);
}

}  // namespace reader_memory
