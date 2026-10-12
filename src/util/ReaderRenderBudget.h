#pragma once

#include <algorithm>
#include <cstddef>

namespace reader_memory {

// The physical trace loses 6,676 bytes between a quiescent page and the next
// loaded page. Round that observed page/deserialization cost up to 8 KiB.
inline constexpr std::size_t kPageLoadReserve = 8 * 1024;
inline constexpr std::size_t kAllocationReserve = 2 * 1024;

struct RenderBudget {
  std::size_t freeBytes;
  std::size_t largestBlock;
};

// Call only at render entry/completion. Inputs are the actual renderer/SD-font
// limits: two 4 KiB scan strings, 512 uint32_t codepoints, a 40-row strip,
// and the maximum mini glyph array. These are runtime survival requirements;
// the independent cold BLE admission floors are unchanged.
constexpr RenderBudget cachedRenderBudget(std::size_t scanBytes, std::size_t codepointBytes,
                                         std::size_t stripBytes, std::size_t glyphArenaBytes,
                                         std::size_t highlightBytes, std::size_t observedPeakBytes = 0,
                                         bool pageAlreadyLoaded = false) {
  const auto pageBytes = pageAlreadyLoaded ? 0 : kPageLoadReserve;
  // Scan strings survive prewarm. Codepoints and the new mini glyph arena
  // overlap while loading; grayscale scratch belongs to the later pass.
  const auto base = pageBytes + scanBytes + std::max(codepointBytes + glyphArenaBytes, stripBytes) +
                    highlightBytes + kAllocationReserve;
  const auto measured = pageAlreadyLoaded ? 0 : observedPeakBytes + kPageLoadReserve;
  return {std::max(base, measured),
          std::max({scanBytes, codepointBytes, stripBytes, glyphArenaBytes, highlightBytes}) +
              kAllocationReserve};
}

// After lazy font preparation, protect the remaining BW/gray pass and the
// bounded 1 KiB highlight-normalization strings (including growth overlap).
// This check performs no release and is not a BLE return/admission event.
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
