#include "CarouselTheme.h"

#include <GfxRenderer.h>

#include <algorithm>

CarouselLayout CarouselTheme::carouselLayout(const GfxRenderer& renderer, const int synopsisLineCount,
                                             const int synopsisLineHeight) const {
  const int pageWidth = renderer.getScreenWidth();
  const int lineCount = std::clamp(synopsisLineCount, 1, 5);
  const int lineHeight = std::max(1, synopsisLineHeight);
  CarouselLayout layout{};
  // Keep the normal hero tall and book-like. A one/two-line information block
  // gets only a modest height boost, avoiding a dramatic jump between books.
  layout.centerHeight = 400 + (lineCount <= 1 ? 8 : (lineCount == 2 ? 4 : 0));
  layout.titleRegion = Rect{20, 72, std::max(0, pageWidth - 40), 42};
  layout.carouselRegion = Rect{0, 122, pageWidth, layout.centerHeight};
  // The synopsis follows the actual hero boundary with a 12 px breathing gap.
  // Its height is content-aware, while the status row follows the lines drawn.
  layout.synopsisRegion = Rect{24, layout.carouselRegion.y + layout.carouselRegion.height + 12,
                               std::max(0, pageWidth - 48), lineCount * lineHeight};
  layout.statusRegion = Rect{24, layout.synopsisRegion.y + layout.synopsisRegion.height,
                             std::max(0, pageWidth - 48), lineHeight};
  return layout;
}
