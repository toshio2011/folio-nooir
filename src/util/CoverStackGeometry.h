#pragma once

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>

struct CoverStackRect {
  int x = 0;
  int y = 0;
  int width = 0;
  int height = 0;
};

struct CoverStackSlot {
  bool valid = false;
  size_t itemIndex = 0;
  uint8_t depth = 0;
  CoverStackRect rect{};
  int leftHeight = 0;
  int rightHeight = 0;
};

namespace CoverStackGeometry {

inline size_t wrappedIndex(const size_t selectedIndex, const int offset, const size_t itemCount) {
  if (itemCount == 0) return 0;
  const int count = static_cast<int>(itemCount);
  int index = static_cast<int>(selectedIndex % itemCount) + offset;
  index %= count;
  if (index < 0) index += count;
  return static_cast<size_t>(index);
}

// Stack 5 geometry is authored for the X4's 480 px portrait width. The
// center remains mathematically centered on every supported portrait width;
// side positions are derived from symmetric visible exposures rather than
// independently scaled absolute coordinates. Vertical geometry is shared by
// X4 and X3 so both simulators exercise the same fan.
inline std::array<CoverStackSlot, 5> layout(const int screenWidth, const size_t itemCount,
                                            const size_t selectedIndex, const int centerHeight) {
  std::array<CoverStackSlot, 5> slots{};
  if (screenWidth <= 0 || itemCount == 0) return slots;

  const size_t selected = std::min(selectedIndex, itemCount - 1);
  const int heroHeight = std::clamp(centerHeight, 390, 410);
  const int centerWidth = 276;
  const int nearWidth = 64;
  const int farWidth = 52;
  const int centerY = 122;
  const int centerX = (screenWidth - centerWidth) / 2;
  const int extraWidth = std::max(0, screenWidth - 480);
  const int nearExposure = 44 + extraWidth / 12;
  const int farExposure = 24 + extraWidth / 16;
  const int nearLeftX = centerX - nearExposure;
  const int nearRightX = screenWidth - nearLeftX - nearWidth;
  const int farLeftX = nearLeftX - farExposure;
  const int farRightX = screenWidth - farLeftX - farWidth;
  const int sideY = centerY + 30;
  const int nearOuterHeight = heroHeight * 304 / 400;
  const int nearInnerHeight = heroHeight * 334 / 400;
  const int farOuterHeight = heroHeight * 276 / 400;
  const int farInnerHeight = heroHeight * 312 / 400;

  const auto makeSlot = [&](const int x, const int y, const int width, const int height, const int leftHeight,
                            const int rightHeight, const uint8_t depth, const size_t itemIndex, const bool valid) {
    return CoverStackSlot{valid, itemIndex, depth, CoverStackRect{x, y, width, height}, leftHeight, rightHeight};
  };

  // The inner edge of every side cover is taller. The common y baseline
  // matches CrossInk's recessed fan treatment; smaller far cards therefore
  // sit visually behind the near cards without a second renderer.
  if (itemCount == 2) {
    // With two books, show the other book on the direction it naturally
    // occupies without duplicating it on both sides of the center.
    slots[2] = makeSlot(nearLeftX, sideY, nearWidth, nearInnerHeight, nearOuterHeight, nearInnerHeight, 1,
                        0, selected == 1);
    slots[3] = makeSlot(nearRightX, sideY, nearWidth, nearInnerHeight, nearInnerHeight, nearOuterHeight, 1,
                        1, selected == 0);
  } else if (itemCount == 3) {
    // Three unique books fit in the two near positions and center. Keep the
    // far positions empty so the first/last frame reads as a three-book fan.
    slots[2] = makeSlot(nearLeftX, sideY, nearWidth, nearInnerHeight, nearOuterHeight, nearInnerHeight, 1,
                        wrappedIndex(selected, -1, itemCount), true);
    slots[3] = makeSlot(nearRightX, sideY, nearWidth, nearInnerHeight, nearInnerHeight, nearOuterHeight, 1,
                        wrappedIndex(selected, 1, itemCount), true);
  } else {
    const size_t farLeftIndex = wrappedIndex(selected, -2, itemCount);
    const size_t nearLeftIndex = wrappedIndex(selected, -1, itemCount);
    const size_t nearRightIndex = wrappedIndex(selected, 1, itemCount);
    const size_t farRightIndex = wrappedIndex(selected, 2, itemCount);
    slots[0] = makeSlot(farLeftX, sideY, farWidth, farInnerHeight, farOuterHeight, farInnerHeight, 2,
                        farLeftIndex, true);
    slots[1] = makeSlot(farRightX, sideY, farWidth, farInnerHeight, farInnerHeight, farOuterHeight, 2,
                        farRightIndex, farRightIndex != farLeftIndex);
    slots[2] = makeSlot(nearLeftX, sideY, nearWidth, nearInnerHeight, nearOuterHeight, nearInnerHeight, 1,
                        nearLeftIndex, true);
    slots[3] = makeSlot(nearRightX, sideY, nearWidth, nearInnerHeight, nearInnerHeight, nearOuterHeight, 1,
                        nearRightIndex, true);
  }
  slots[4] = makeSlot(centerX, centerY, centerWidth, heroHeight, heroHeight, heroHeight, 0, selected, true);
  return slots;
}

// Three-cover Carousel variant: the same opaque perspective renderer and
// mirrored fan treatment, with the two side books given more exposure and a
// slightly wider fixed hero because there are no far cards to share the row.
// The returned array keeps the center in the final slot so callers can use the
// same far/near-to-center draw order and center outline logic.
inline std::array<CoverStackSlot, 5> layoutThree(const int screenWidth, const size_t itemCount,
                                                 const size_t selectedIndex, const int centerHeight) {
  std::array<CoverStackSlot, 5> slots{};
  if (screenWidth <= 0 || itemCount == 0) return slots;

  const size_t selected = std::min(selectedIndex, itemCount - 1);
  const int heroHeight = std::clamp(centerHeight, 390, 410);
  const int centerWidth = 300;
  const int sideWidth = 78;
  const int centerY = 122;
  const int centerX = (screenWidth - centerWidth) / 2;
  const int extraWidth = std::max(0, screenWidth - 480);
  const int sideExposure = 54 + extraWidth / 10;
  const int leftX = centerX - sideExposure;
  const int rightX = screenWidth - leftX - sideWidth;
  const int sideY = centerY + 30;
  const int sideOuterHeight = heroHeight * 304 / 400;
  const int sideInnerHeight = heroHeight * 334 / 400;

  const auto makeSlot = [&](const int x, const int y, const int width, const int height, const int leftHeight,
                            const int rightHeight, const uint8_t depth, const size_t itemIndex, const bool valid) {
    return CoverStackSlot{valid, itemIndex, depth, CoverStackRect{x, y, width, height}, leftHeight, rightHeight};
  };

  if (itemCount == 2) {
    // Do not render the same second book on both sides of the center.
    slots[0] = makeSlot(leftX, sideY, sideWidth, sideInnerHeight, sideOuterHeight, sideInnerHeight, 1, 0, selected == 1);
    slots[1] = makeSlot(rightX, sideY, sideWidth, sideInnerHeight, sideInnerHeight, sideOuterHeight, 1, 1, selected == 0);
  } else {
    slots[0] = makeSlot(leftX, sideY, sideWidth, sideInnerHeight, sideOuterHeight, sideInnerHeight, 1,
                        wrappedIndex(selected, -1, itemCount), true);
    slots[1] = makeSlot(rightX, sideY, sideWidth, sideInnerHeight, sideInnerHeight, sideOuterHeight, 1,
                        wrappedIndex(selected, 1, itemCount), true);
  }
  slots[4] = makeSlot(centerX, centerY, centerWidth, heroHeight, heroHeight, heroHeight, 0, selected, true);
  return slots;
}

// Folio Nooir keeps its Featured Book and summary strip on screen, so its
// Carousel occupies only the existing book-presentation region. The same
// perspective and wrapped-slot rules are reused with dimensions derived from
// that region instead of the independent full-screen Carousel geometry.
inline std::array<CoverStackSlot, 5> layoutFolioShelf(const int screenWidth, const int shelfTop,
                                                      const int shelfHeight, const size_t itemCount,
                                                      const size_t selectedIndex, const bool threeCover) {
  std::array<CoverStackSlot, 5> slots{};
  if (screenWidth <= 0 || shelfHeight <= 0 || itemCount == 0) return slots;

  const size_t selected = std::min(selectedIndex, itemCount - 1);
  const int verticalPadding = std::min(8, std::max(3, shelfHeight / 12));
  const int heroHeight = std::max(40, shelfHeight - verticalPadding * 2);
  const int centerWidth = std::min(heroHeight * 2 / 3, std::max(40, screenWidth - 20));
  const int centerX = (screenWidth - centerWidth) / 2;
  const int centerY = shelfTop + (shelfHeight - heroHeight) / 2;
  const int nearWidth = threeCover ? std::max(28, centerWidth * 2 / 5) : std::max(24, centerWidth / 3);
  const int farWidth = std::max(22, nearWidth * 3 / 4);
  const int nearExposure = std::max(18, nearWidth * 2 / 3);
  const int farExposure = std::max(14, farWidth / 2);
  const int nearLeftX = centerX - nearExposure;
  const int nearRightX = screenWidth - nearLeftX - nearWidth;
  const int farLeftX = nearLeftX - farExposure;
  const int farRightX = screenWidth - farLeftX - farWidth;
  const int nearOuterHeight = heroHeight * (threeCover ? 70 : 64) / 100;
  const int nearInnerHeight = heroHeight * (threeCover ? 80 : 72) / 100;
  const int farOuterHeight = heroHeight * 55 / 100;
  const int farInnerHeight = heroHeight * 64 / 100;
  const int sideY = shelfTop + (shelfHeight - nearInnerHeight) / 2;

  const auto makeSlot = [&](const int x, const int width, const int leftHeight, const int rightHeight,
                            const uint8_t depth, const size_t itemIndex, const bool valid) {
    return CoverStackSlot{valid, itemIndex, depth, CoverStackRect{x, sideY, width, std::max(leftHeight, rightHeight)},
                          leftHeight, rightHeight};
  };

  // A single book must remain a single center cover. In particular, do not
  // use wrapped neighbors here because that would render the same book three
  // times in a small Folio shelf.
  if (itemCount == 1) {
    slots[4] = CoverStackSlot{true, selected, 0, CoverStackRect{centerX, centerY, centerWidth, heroHeight},
                              heroHeight, heroHeight};
    return slots;
  }

  if (threeCover) {
    if (itemCount == 2) {
      slots[0] = makeSlot(nearLeftX, nearWidth, nearOuterHeight, nearInnerHeight, 1, 0, selected == 1);
      slots[1] = makeSlot(nearRightX, nearWidth, nearInnerHeight, nearOuterHeight, 1, 1, selected == 0);
    } else {
      slots[0] = makeSlot(nearLeftX, nearWidth, nearOuterHeight, nearInnerHeight, 1,
                          wrappedIndex(selected, -1, itemCount), true);
      slots[1] = makeSlot(nearRightX, nearWidth, nearInnerHeight, nearOuterHeight, 1,
                          wrappedIndex(selected, 1, itemCount), true);
    }
  } else if (itemCount == 2) {
    slots[2] = makeSlot(nearLeftX, nearWidth, nearOuterHeight, nearInnerHeight, 1, 0, selected == 1);
    slots[3] = makeSlot(nearRightX, nearWidth, nearInnerHeight, nearOuterHeight, 1, 1, selected == 0);
  } else if (itemCount == 3) {
    slots[2] = makeSlot(nearLeftX, nearWidth, nearOuterHeight, nearInnerHeight, 1,
                        wrappedIndex(selected, -1, itemCount), true);
    slots[3] = makeSlot(nearRightX, nearWidth, nearInnerHeight, nearOuterHeight, 1,
                        wrappedIndex(selected, 1, itemCount), true);
  } else {
    const size_t farLeftIndex = wrappedIndex(selected, -2, itemCount);
    const size_t nearLeftIndex = wrappedIndex(selected, -1, itemCount);
    const size_t nearRightIndex = wrappedIndex(selected, 1, itemCount);
    const size_t farRightIndex = wrappedIndex(selected, 2, itemCount);
    slots[0] = makeSlot(farLeftX, farWidth, farOuterHeight, farInnerHeight, 2, farLeftIndex, true);
    slots[1] = makeSlot(farRightX, farWidth, farInnerHeight, farOuterHeight, 2, farRightIndex,
                        farRightIndex != farLeftIndex);
    slots[2] = makeSlot(nearLeftX, nearWidth, nearOuterHeight, nearInnerHeight, 1, nearLeftIndex, true);
    slots[3] = makeSlot(nearRightX, nearWidth, nearInnerHeight, nearOuterHeight, 1, nearRightIndex, true);
  }

  slots[4] = CoverStackSlot{true, selected, 0, CoverStackRect{centerX, centerY, centerWidth, heroHeight},
                            heroHeight, heroHeight};
  return slots;
}

}  // namespace CoverStackGeometry
