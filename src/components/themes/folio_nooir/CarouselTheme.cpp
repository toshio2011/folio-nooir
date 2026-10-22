#include "CarouselTheme.h"

#include <GfxRenderer.h>

#include <algorithm>

#include "util/FolioLiteLayoutPolicy.h"

CarouselLayout CarouselTheme::carouselLayout(const GfxRenderer& renderer, const int synopsisLineCount,
                                             const int synopsisLineHeight, const int lowerContentBottom) const {
  const int pageWidth = renderer.getScreenWidth();
  const int lineCount = std::clamp(synopsisLineCount, 1, 5);
  const int lineHeight = std::max(1, synopsisLineHeight);
  CarouselLayout layout{};
  const int synopsisHeight = lineCount * lineHeight;
  const int carouselTop = FolioLiteLayoutPolicy::LITE_CAROUSEL_TOP;
  const int carouselSynopsisGap = FolioLiteLayoutPolicy::LITE_CAROUSEL_SYNOPSIS_GAP;
  layout.centerHeight = lowerContentBottom > carouselTop
                             ? FolioLiteLayoutPolicy::carouselHeroHeight(lowerContentBottom, lineHeight)
                             : FolioLiteLayoutPolicy::LITE_CAROUSEL_PREFERRED_HERO_HEIGHT;
  layout.titleRegion = Rect{20, 72, std::max(0, pageWidth - 40), 24};
  layout.authorRegion = Rect{20, layout.titleRegion.y + layout.titleRegion.height,
                             std::max(0, pageWidth - 40), 18};
  layout.carouselRegion = Rect{0, carouselTop, pageWidth, layout.centerHeight};
  // Both the hero and synopsis bounds depend on the reserved line count, not
  // on whether the selected book has a description.
  layout.synopsisRegion = Rect{24, layout.carouselRegion.y + layout.carouselRegion.height + carouselSynopsisGap,
                               std::max(0, pageWidth - 48), synopsisHeight};
  layout.statusRegion = Rect{24, layout.synopsisRegion.y + layout.synopsisRegion.height,
                             std::max(0, pageWidth - 48),
                             lineHeight + FolioLiteLayoutPolicy::LITE_CAROUSEL_STATUS_TOP_INSET};
  layout.paginationRegion =
      Rect{0, FolioLiteLayoutPolicy::paginationTop(lowerContentBottom), pageWidth,
           FolioLiteLayoutPolicy::LITE_CAROUSEL_PAGINATION_HEIGHT};
  return layout;
}
