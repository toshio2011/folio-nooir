#pragma once

#include "components/themes/lyra/LyraTheme.h"

struct CarouselLayout {
  Rect titleRegion;
  Rect carouselRegion;
  Rect synopsisRegion;
  Rect statusRegion;
  int centerHeight = 432;
};

class CarouselTheme final : public LyraTheme {
 public:
  bool usesBookshelfHome() const override { return true; }
  bool usesGraphicalLibrary() const override { return true; }

  CarouselLayout carouselLayout(const GfxRenderer& renderer, int synopsisLineCount = 5,
                                int synopsisLineHeight = 18, int lowerContentBottom = -1) const;
};
