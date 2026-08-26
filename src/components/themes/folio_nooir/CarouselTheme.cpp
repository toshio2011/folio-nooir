#include "CarouselTheme.h"

#include <GfxRenderer.h>

#include <algorithm>

CarouselLayout CarouselTheme::carouselLayout(const GfxRenderer& renderer, const int synopsisLineCount,
                                             const int synopsisLineHeight, const int lowerContentBottom) const {
  const int pageWidth = renderer.getScreenWidth();
  const int lineCount = std::clamp(synopsisLineCount, 1, 5);
  const int lineHeight = std::max(1, synopsisLineHeight);
  constexpr int carouselTop = 122;
  constexpr int carouselSynopsisGap = 10;
  constexpr int summaryBreathingGap = 10;
  constexpr int normalHeroHeight = 424;
  constexpr int minimumHeroHeight = 320;
  constexpr int maximumHeroHeight = 466;
  CarouselLayout layout{};
  const int synopsisHeight = lineCount * lineHeight;
  const int desiredHeroHeight = normalHeroHeight + (lineCount <= 1 ? 14 : (lineCount == 2 ? 7 : 0));
  const int availableHeroHeight = lowerContentBottom > carouselTop
                                      ? lowerContentBottom - carouselTop - carouselSynopsisGap - synopsisHeight -
                                            lineHeight - summaryBreathingGap
                                      : desiredHeroHeight;
  // Use the space above the fixed summary strip when it is available, while
  // bounding the short-synopsis variant so adjacent books do not jump wildly
  // in size. Long synopsis text naturally reduces the hero height first.
  layout.centerHeight = std::clamp(std::max(desiredHeroHeight, availableHeroHeight),
                                   minimumHeroHeight, maximumHeroHeight);
  layout.titleRegion = Rect{20, 72, std::max(0, pageWidth - 40), 42};
  layout.carouselRegion = Rect{0, carouselTop, pageWidth, layout.centerHeight};
  // The synopsis follows the actual hero boundary with a small breathing gap.
  // Its height is content-aware, while the status row follows the lines drawn.
  layout.synopsisRegion = Rect{24, layout.carouselRegion.y + layout.carouselRegion.height + carouselSynopsisGap,
                               std::max(0, pageWidth - 48), synopsisHeight};
  layout.statusRegion = Rect{24, layout.synopsisRegion.y + layout.synopsisRegion.height,
                             std::max(0, pageWidth - 48), lineHeight};
  return layout;
}
