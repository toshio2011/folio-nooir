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

struct Book {
  const char* key = nullptr;
  const char* title = nullptr;
  uint8_t progressPercent = 0;
  const char* author = nullptr;
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

struct Slot {
  size_t itemIndex = 0;
  Rect rect{};
};

struct Plant {
  bool visible = false;
  Rect rect{};
};

enum class SpineStyle : uint8_t {
  Plain = 0,
  TopBand = 1,
  BottomBand = 2,
  InsetDivider = 3,
  DoubleRules = 4,
  Ornament = 5,
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
