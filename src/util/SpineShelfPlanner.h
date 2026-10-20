#pragma once

#include <cstddef>
#include <cstdint>

// Fixed-size geometry for Folio Nooir's optional Spine shelf.  The planner is
// deliberately independent of the renderer so host tests can exercise the
// exact dimensions, packing, and hit rectangles used on the device.
namespace SpineShelfPlanner {

constexpr size_t MAX_BOOKS = 10;
constexpr int BOOK_GAP = 4;
constexpr int SHELF_PLANK_HEIGHT = 24;
constexpr int SHELF_SUPPORT_HEIGHT = 24;
constexpr int SHELF_BOTTOM_GAP = 44;
constexpr int SHELF_OVERHANG_MIN = 8;
constexpr int SHELF_OVERHANG_MAX = 12;
constexpr int MIN_BOOK_WIDTH = 42;
constexpr int MAX_BOOK_WIDTH = 62;
constexpr int MIN_BOOK_HEIGHT = 115;
constexpr int MAX_BOOK_HEIGHT = 246;
constexpr int TWO_ROW_BOOK_GAP = 3;
constexpr int TWO_ROW_MIN_BOOK_WIDTH = 32;
constexpr int TWO_ROW_MAX_BOOK_WIDTH = 48;
constexpr int TWO_ROW_ROW_GAP = 10;
constexpr int TWO_ROW_SHELF_HEIGHT = 16;
// The lower shelf must stay above FolioNooirTheme::drawPageIndicator().
constexpr int TWO_ROW_BOTTOM_GAP = 32;
// Bounded storage for the largest supported X3/X4 viewport. The planner still
// stops only when the final deterministic geometry no longer fits; this is a
// page-local safety ceiling, not a visible-book/page-size rule.
constexpr size_t TWO_ROW_SLOT_CAPACITY = 28;
constexpr int THREE_ROW_BOOK_GAP = 2;
constexpr int THREE_ROW_BOOK_MIN_WIDTH = 28;
constexpr int THREE_ROW_BOOK_MAX_WIDTH = 42;
constexpr int THREE_ROW_ROW_GAP = 5;
constexpr int THREE_ROW_SHELF_HEIGHT = 10;
constexpr int THREE_ROW_BOTTOM_GAP = 16;
constexpr size_t THREE_ROW_SLOT_CAPACITY = 42;

enum class Appearance : uint8_t { AllSpines = 0, SpinesAndCovers = 1, CoversOnly = 2 };

struct Book {
  const char* key = nullptr;
  const char* title = nullptr;
  uint8_t progressPercent = 0;
  const char* author = nullptr;
  // Stable caller seed and preference are supplied by the caller. They affect
  // appearance only; fixed default values preserve deterministic host use.
  uint32_t visualSeed = 0;
  uint8_t decorationStyle = 0;
  bool compact = false;
  Appearance appearance = Appearance::AllSpines;
  const char* coverBmpPath = nullptr;
};

struct Rect {
  int x = 0;
  int y = 0;
  int width = 0;
  int height = 0;

  bool contains(const int px, const int py) const {
    return px >= x && px < x + width && py >= y && py < y + height;
  }
};

// The Home focus frame is painted just outside a slot so erasing it cannot
// remove the book's own edge. Keep its complete inclusive paint extent with
// the final slot geometry so renderers and host tests agree on every border
// pixel, including the right stroke.
struct FocusFrame {
  int left = 0;
  int right = -1;
  int top = 0;
  int bottom = -1;
  Rect updateBounds{};

  bool valid() const { return right >= left && bottom >= top; }
};

inline FocusFrame focusFrameFor(const Rect& slot) {
  if (slot.width <= 2 || slot.height <= 2) return {};
  const int left = slot.x - 1;
  const int right = slot.x + slot.width;
  const int top = slot.y - 1;
  const int bottom = slot.y + slot.height - 1;
  return FocusFrame{left, right, top, bottom, Rect{left, top, right - left + 1, bottom - top + 1}};
}

// Retain the exact final geometry used for the last painted Spine focus frame.
// Selection can move before the next frame is composed, so recomputing the old
// item's rectangle from the new selection/page can erase the wrong edge.
struct RenderedFocusGeometry {
  size_t itemIndex = SIZE_MAX;
  Rect finalSlot{};

  bool valid() const { return itemIndex != SIZE_MAX && focusFrameFor(finalSlot).valid(); }

  void remember(const size_t index, const Rect& slotGeometry) {
    itemIndex = index;
    finalSlot = slotGeometry;
    if (!valid()) clear();
  }

  bool take(const size_t index, Rect& slotOutput) {
    if (!valid() || itemIndex != index) return false;
    slotOutput = finalSlot;
    clear();
    return true;
  }

  void clear() {
    itemIndex = SIZE_MAX;
    finalSlot = {};
  }
};

struct FocusDeselectionGeometry {
  Rect finalSlot{};
  FocusFrame frame{};
  bool restoredPreviousGeometry = false;
};

inline FocusDeselectionGeometry focusGeometryForDeselection(RenderedFocusGeometry& rendered,
                                                            const size_t index,
                                                            const Rect& currentSlot) {
  FocusDeselectionGeometry result{};
  result.restoredPreviousGeometry = rendered.take(index, result.finalSlot);
  if (result.restoredPreviousGeometry) {
    result.frame = focusFrameFor(result.finalSlot);
  }
  if (!result.restoredPreviousGeometry) {
    result.finalSlot = currentSlot;
    result.frame = focusFrameFor(currentSlot);
  }
  return result;
}

inline int slotRightEdgeX(const Rect& slot) { return slot.x + slot.width - 1; }

struct Slot {
  size_t itemIndex = 0;
  Rect rect{};
  bool cover = false;
};

struct Plant {
  bool visible = false;
  Rect rect{};
  uint32_t visualSeed = 0;
};

enum class SpineStyle : uint8_t {
  Plain = 0,
  TopBand = 1,
  BottomBand = 2,
  InsetDivider = 3,
  DoubleRules = 4,
  Ornament = 5,
  LeatherBands = 6,
  CenterLabel = 7,
  OldLibrary = 8,
};
enum class SpineTone : uint8_t { White = 0, LightGray = 1, DarkGray = 2 };

struct Page {
  size_t firstIndex = 0;
  uint8_t itemCount = 0;
  uint8_t pageNumber = 0;
  uint8_t pageCount = 0;
  Slot slots[MAX_BOOKS]{};
  Plant plant{};
};

// The additional two-row view uses a bounded page-local slot record; actual
// occupancy is determined by the final row geometry, not by a fixed per-row
// book count.
struct TwoRowPage {
  size_t firstIndex = 0;
  uint8_t itemCount = 0;
  uint8_t pageNumber = 0;
  uint8_t pageCount = 0;
  Slot slots[TWO_ROW_SLOT_CAPACITY]{};
  Plant plants[2]{};
};

struct ThreeRowPage {
  size_t firstIndex = 0;
  uint8_t itemCount = 0;
  uint8_t pageNumber = 0;
  uint8_t pageCount = 0;
  Slot slots[THREE_ROW_SLOT_CAPACITY]{};
  Plant plants[3]{};
};

uint32_t stableHash(const Book& book, size_t itemIndex = 0);
SpineStyle styleFor(const Book& book);
SpineTone toneFor(const Book& book);
int bookWidth(const Book& book, const Rect& bounds);
int bookHeight(const Book& book, const Rect& bounds);
Rect shelfRect(const Page& page, const Rect& bounds);

TwoRowPage twoRowPageForSelection(const Book* books, size_t bookCount, size_t selectedIndex,
                                  const Rect& bounds);
size_t twoRowNextPageStart(const Book* books, size_t bookCount, size_t selectedIndex, const Rect& bounds);
size_t twoRowPreviousPageStart(const Book* books, size_t bookCount, size_t selectedIndex, const Rect& bounds);
Rect twoRowShelfRect(const TwoRowPage& page, const Rect& bounds, size_t row);

ThreeRowPage threeRowPageForSelection(const Book* books, size_t bookCount, size_t selectedIndex,
                                      const Rect& bounds);
size_t threeRowNextPageStart(const Book* books, size_t bookCount, size_t selectedIndex, const Rect& bounds);
size_t threeRowPreviousPageStart(const Book* books, size_t bookCount, size_t selectedIndex, const Rect& bounds);
Rect threeRowShelfRect(const ThreeRowPage& page, const Rect& bounds, size_t row);

// Return the packed page containing selectedIndex.  Selection indexes are
// indexes in the visible (filtered) shelf list, not indexes in RecentBook
// storage.
Page pageForSelection(const Book* books, size_t bookCount, size_t selectedIndex, const Rect& bounds);

// Return the first visible-list index of the next/previous packed page.  Both
// helpers wrap, matching the existing Recent/Finished shelf navigation.
size_t nextPageStart(const Book* books, size_t bookCount, size_t selectedIndex, const Rect& bounds);
size_t previousPageStart(const Book* books, size_t bookCount, size_t selectedIndex, const Rect& bounds);

// Build a short title without splitting a UTF-8 sequence.  Returns false when
// the spine is too narrow/short or the source is malformed/unusable; callers
// should then leave the spine untitled and rely on the Featured Book panel.
bool makeTitle(char* output, size_t outputCapacity, const char* title, const Rect& spine);
bool makeBookTitle(char* output, size_t outputCapacity, const Book& book, const Rect& spine);
bool makeAuthor(char* output, size_t outputCapacity, const char* author, const Rect& spine);

}  // namespace SpineShelfPlanner
