#include "SpineShelfPlanner.h"

#include <algorithm>
#include <cstring>

namespace SpineShelfPlanner {
namespace {

constexpr int kShelfInset = 2;

bool isContinuation(const unsigned char byte) { return (byte & 0xC0u) == 0x80u; }

size_t sequenceLength(const unsigned char lead) {
  if (lead < 0x80u) return 1;
  if (lead >= 0xC2u && lead <= 0xDFu) return 2;
  if (lead >= 0xE0u && lead <= 0xEFu) return 3;
  if (lead >= 0xF0u && lead <= 0xF4u) return 4;
  return 0;
}

bool validSequence(const char* text, const size_t length) {
  if (text == nullptr || length == 0) return false;
  const auto* bytes = reinterpret_cast<const unsigned char*>(text);
  if (length == 1) return bytes[0] < 0x80u;
  for (size_t i = 1; i < length; ++i) {
    if (!isContinuation(bytes[i])) return false;
  }
  // Reject overlong encodings, UTF-16 surrogate values, and codepoints above
  // Unicode's scalar range while keeping this helper allocation-free.
  if (length == 2 && bytes[0] < 0xC2u) return false;
  if (length == 3 && bytes[0] == 0xE0u && bytes[1] < 0xA0u) return false;
  if (length == 3 && bytes[0] == 0xEDu && bytes[1] >= 0xA0u) return false;
  if (length == 4 && bytes[0] == 0xF0u && bytes[1] < 0x90u) return false;
  if (length == 4 && bytes[0] == 0xF4u && bytes[1] >= 0x90u) return false;
  return true;
}

bool validUtf8Text(const char* text) {
  if (text == nullptr || *text == '\0') return false;
  size_t offset = 0;
  while (text[offset] != '\0') {
    const size_t length = sequenceLength(reinterpret_cast<const unsigned char*>(text + offset)[0]);
    if (!validSequence(text + offset, length)) return false;
    offset += length;
  }
  return true;
}

bool copyFilenameStem(char* output, const size_t outputCapacity, const char* key) {
  if (output == nullptr || outputCapacity == 0 || key == nullptr || *key == '\0') return false;
  output[0] = '\0';

  const char* start = key;
  for (const char* cursor = key; *cursor != '\0'; ++cursor) {
    if (*cursor == '/' || *cursor == '\\') start = cursor + 1;
  }
  const char* end = start;
  const char* lastDot = nullptr;
  for (const char* cursor = start; *cursor != '\0'; ++cursor) {
    if (*cursor == '.') lastDot = cursor;
    end = cursor + 1;
  }
  if (lastDot != nullptr && lastDot != start) end = lastDot;
  if (end <= start) return false;

  const size_t length = static_cast<size_t>(end - start);
  if (length >= outputCapacity) return false;
  std::memcpy(output, start, length);
  output[length] = '\0';
  return validUtf8Text(output);
}

size_t pageCountFor(const Book* books, const size_t bookCount, const Rect& bounds) {
  if (books == nullptr || bookCount == 0) return 0;
  const size_t count = std::min(bookCount, MAX_BOOKS);
  size_t start = 0;
  size_t pages = 0;
  while (start < count && pages < MAX_BOOKS) {
    const Page page = [&] {
      Page result{};
      result.firstIndex = start;
      const int left = bounds.x + kShelfInset;
      const int right = std::max(left + 1, bounds.x + bounds.width - kShelfInset);
      const int bottom = bounds.y + std::max(1, bounds.height - SHELF_BOTTOM_GAP);
      int cursor = left;
      for (size_t index = start; index < count && result.itemCount < MAX_BOOKS; ++index) {
        const int remaining = right - cursor;
        int width = bookWidth(books[index], bounds);
        if (result.itemCount > 0 && width > remaining) break;
        if (result.itemCount == 0) width = std::max(1, std::min(width, remaining));
        if (width <= 0) break;
        const int height = bookHeight(books[index], bounds);
        result.slots[result.itemCount] = Slot{index, Rect{cursor, bottom - height, width, height}};
        ++result.itemCount;
        cursor += width + BOOK_GAP;
      }
      return result;
    }();
    if (page.itemCount == 0) break;
    start += page.itemCount;
    ++pages;
  }
  return pages;
}

Page pageAtStart(const Book* books, const size_t bookCount, const size_t start, const Rect& bounds) {
  Page result{};
  result.firstIndex = std::min(start, std::min(bookCount, MAX_BOOKS));
  if (books == nullptr || bookCount == 0 || start >= std::min(bookCount, MAX_BOOKS)) return result;

  const size_t count = std::min(bookCount, MAX_BOOKS);
  const int left = bounds.x + kShelfInset;
  const int right = std::max(left + 1, bounds.x + bounds.width - kShelfInset);
  const int bottom = bounds.y + std::max(1, bounds.height - SHELF_BOTTOM_GAP);
  int cursor = left;
  for (size_t index = start; index < count && result.itemCount < MAX_BOOKS; ++index) {
    const int remaining = right - cursor;
    int width = bookWidth(books[index], bounds);
    if (result.itemCount > 0 && width > remaining) break;
    if (result.itemCount == 0) width = std::max(1, std::min(width, remaining));
    if (width <= 0) break;
    const int height = bookHeight(books[index], bounds);
    result.slots[result.itemCount] = Slot{index, Rect{cursor, bottom - height, width, height}};
    ++result.itemCount;
    cursor += width + BOOK_GAP;
  }

  // The plant is decoration only: choose a deterministic page-dependent
  // subset, then place it inside the largest already-empty shelf gap.
  const uint32_t seed = stableHash(books[start], start);
  if ((seed & 0x03u) == 0u && result.itemCount > 0) {
    int largestGapLeft = left;
    int largestGapRight = result.slots[0].rect.x;
    for (size_t slot = 1; slot < result.itemCount; ++slot) {
      const int gapLeft = result.slots[slot - 1].rect.x + result.slots[slot - 1].rect.width;
      const int gapRight = result.slots[slot].rect.x;
      if (gapRight - gapLeft > largestGapRight - largestGapLeft) {
        largestGapLeft = gapLeft;
        largestGapRight = gapRight;
      }
    }
    const int trailingLeft = result.slots[result.itemCount - 1].rect.x + result.slots[result.itemCount - 1].rect.width;
    const int trailingRight = right;
    if (trailingRight - trailingLeft > largestGapRight - largestGapLeft) {
      largestGapLeft = trailingLeft;
      largestGapRight = trailingRight;
    }

    constexpr int plantWidth = 24;
    const int plantHeight = std::min(34, std::max(18, bounds.height / 4));
    if (largestGapRight - largestGapLeft >= plantWidth + 8 && plantHeight + SHELF_BOTTOM_GAP < bounds.height) {
      const int plantX = largestGapLeft + (largestGapRight - largestGapLeft - plantWidth) / 2;
      result.plant = Plant{true, Rect{plantX, bottom - plantHeight, plantWidth, plantHeight}};
    }
  }

  // Center the complete visible composition after optional decoration is
  // known.  The shifted rectangles are the planner's final hit/render data;
  // there is no render-only offset that can desynchronize interaction.
  int occupiedLeft = result.slots[0].rect.x;
  int occupiedRight = result.slots[result.itemCount - 1].rect.x + result.slots[result.itemCount - 1].rect.width;
  if (result.plant.visible) {
    occupiedLeft = std::min(occupiedLeft, result.plant.rect.x);
    occupiedRight = std::max(occupiedRight, result.plant.rect.x + result.plant.rect.width);
  }
  const int compositionCenter = (occupiedLeft + occupiedRight) / 2;
  const int shelfCenter = (left + right) / 2;
  const int offset = shelfCenter - compositionCenter;
  for (size_t slot = 0; slot < result.itemCount; ++slot) result.slots[slot].rect.x += offset;
  if (result.plant.visible) result.plant.rect.x += offset;
  return result;
}

struct TwoRowGeometry {
  int top = 0;
  int height = 0;
  int shelfY = 0;
  int bookTop = 0;
};

TwoRowGeometry twoRowGeometry(const Rect& bounds, const size_t row) {
  const int totalHeight = std::max(60, bounds.height);
  const int rowHeight = std::max(30, (totalHeight - TWO_ROW_ROW_GAP) / 2);
  const int top = bounds.y + static_cast<int>(row) * (rowHeight + TWO_ROW_ROW_GAP);
  const int shelfY = top + rowHeight - TWO_ROW_SHELF_HEIGHT;
  return TwoRowGeometry{top, rowHeight, shelfY, top + 4};
}

int twoRowBookHeight(const Book& book, const Rect& bounds) {
  const int rowHeight = twoRowGeometry(bounds, 0).height;
  const int available = std::max(30, rowHeight - TWO_ROW_SHELF_HEIGHT - 8);
  if (available <= 48) return available;
  const int lower = std::max(48, available * 78 / 100);
  const int upper = std::max(lower, available - 2);
  const int target = available * 87 / 100;
  const int variation = std::max(3, available * 5 / 100);
  const int offset = static_cast<int>((stableHash(book, 17) >> 8) % (variation * 2 + 1)) - variation;
  return std::clamp(target + offset, lower, upper);
}

int twoRowBookWidth(const Book& book, const Rect& bounds) {
  const int available = std::max(1, bounds.width - kShelfInset * 2);
  const int width = TWO_ROW_MIN_BOOK_WIDTH +
                    static_cast<int>(stableHash(book, 19) % static_cast<uint32_t>(TWO_ROW_MAX_BOOK_WIDTH -
                                                                                   TWO_ROW_MIN_BOOK_WIDTH + 1));
  if (available <= TWO_ROW_MIN_BOOK_WIDTH) return available;
  return std::min(width, available);
}

TwoRowPage twoRowPageAtStart(const Book* books, const size_t bookCount, const size_t start, const Rect& bounds) {
  TwoRowPage result{};
  const size_t count = bookCount;
  result.firstIndex = std::min(start, count);
  if (books == nullptr || count == 0 || start >= count) return result;

  const int left = bounds.x + kShelfInset;
  const int right = std::max(left + 1, bounds.x + bounds.width - kShelfInset);
  size_t index = start;
  for (size_t row = 0; row < 2 && index < count; ++row) {
    const auto geometry = twoRowGeometry(bounds, row);
    int cursor = left;
    const size_t rowSlotStart = result.itemCount;
    size_t rowCount = 0;
    while (index < count && result.itemCount < TWO_ROW_SLOT_CAPACITY) {
      const int remaining = right - cursor;
      int width = twoRowBookWidth(books[index], bounds);
      if (rowCount > 0 && width > remaining) break;
      if (rowCount == 0) width = std::max(1, std::min(width, remaining));
      if (width <= 0) break;
      const int height = twoRowBookHeight(books[index], bounds);
      result.slots[result.itemCount] =
          Slot{index, Rect{cursor, geometry.shelfY - height, width, height}};
      ++result.itemCount;
      ++rowCount;
      ++index;
      cursor += width + TWO_ROW_BOOK_GAP;
    }

    if (rowCount == 0) continue;

    // Match the original Spine's balanced composition on each shelf. The
    // final rectangles are shared by rendering and hit-testing, so this is a
    // geometry adjustment rather than a render-only offset.
    const int groupLeft = result.slots[rowSlotStart].rect.x;
    const int groupRight = result.slots[result.itemCount - 1].rect.x +
                           result.slots[result.itemCount - 1].rect.width;
    const int groupCenter = (groupLeft + groupRight) / 2;
    const int shelfCenter = (left + right) / 2;
    const int offset = shelfCenter - groupCenter;
    for (size_t slot = rowSlotStart; slot < result.itemCount; ++slot) {
      result.slots[slot].rect.x += offset;
    }

    // Reuse the original Spine's sparse, deterministic plant idea only when
    // a side gap is genuinely available. It never displaces a book and stays
    // inside the same fixed page record.
    const uint32_t seed = stableHash(books[start + rowSlotStart], row + 29);
    if ((seed & 0x03u) == 0u) {
      const int groupLeftAfter = result.slots[rowSlotStart].rect.x;
      const int groupRightAfter = result.slots[result.itemCount - 1].rect.x +
                                  result.slots[result.itemCount - 1].rect.width;
      const int leftGap = groupLeftAfter - left;
      const int rightGap = right - groupRightAfter;
      const bool useLeft = leftGap >= rightGap;
      const int gap = useLeft ? leftGap : rightGap;
      constexpr int plantWidth = 24;
      const int plantHeight = std::min(28, std::max(16, geometry.height / 4));
      const int availablePlantHeight = geometry.shelfY - geometry.bookTop;
      if (gap >= plantWidth + 8 && availablePlantHeight >= plantHeight) {
        const int plantX = useLeft ? left + (gap - plantWidth) / 2
                                    : groupRightAfter + (gap - plantWidth) / 2;
        result.plants[row] = Plant{true, Rect{plantX, geometry.shelfY - plantHeight, plantWidth, plantHeight}};
      }
    }
  }
  return result;
}

size_t twoRowPageCountFor(const Book* books, const size_t bookCount, const Rect& bounds) {
  const size_t count = bookCount;
  size_t start = 0;
  size_t pages = 0;
  while (start < count && pages < 255) {
    const TwoRowPage page = twoRowPageAtStart(books, count, start, bounds);
    if (page.itemCount == 0) break;
    start += page.itemCount;
    ++pages;
  }
  return pages;
}

}  // namespace

uint32_t stableHash(const Book& book, const size_t itemIndex) {
  uint32_t hash = 2166136261u;
  const char* key = book.key != nullptr ? book.key : "";
  for (const auto* p = reinterpret_cast<const unsigned char*>(key); *p != 0; ++p) {
    hash ^= *p;
    hash *= 16777619u;
  }
  hash ^= static_cast<uint32_t>(itemIndex + 1);
  hash *= 16777619u;
  return hash;
}

int bookWidth(const Book& book, const Rect& bounds) {
  const int available = std::max(1, bounds.width - kShelfInset * 2);
  const int width = MIN_BOOK_WIDTH +
                    static_cast<int>(stableHash(book, 1) % static_cast<uint32_t>(MAX_BOOK_WIDTH - MIN_BOOK_WIDTH + 1));
  if (available <= MIN_BOOK_WIDTH) return available;
  return std::min(width, available);
}

int bookHeight(const Book& book, const Rect& bounds) {
  const int available = std::max(12, bounds.height - SHELF_BOTTOM_GAP);
  if (available <= MIN_BOOK_HEIGHT) return available;

  // Use the actual book region rather than a fixed pixel height. Most books
  // sit around 82% of the available height, with a bounded spread and a hard
  // 74-90% envelope so the row stays book-like instead of becoming a chart.
  const int lower = std::max(MIN_BOOK_HEIGHT, available * 74 / 100);
  const int upper = std::min(MAX_BOOK_HEIGHT, available * 90 / 100);
  if (upper <= lower) return available;
  const int targetHeight = available * 82 / 100;
  const int variation = std::max(6, available * 7 / 100);
  const int offset = static_cast<int>((stableHash(book, 7) >> 8) % (variation * 2 + 1)) - variation;
  return std::clamp(targetHeight + offset, lower, upper);
}

SpineStyle styleFor(const Book& book) {
  return static_cast<SpineStyle>(stableHash(book, 3) % 6u);
}

SpineTone toneFor(const Book& book) {
  switch (stableHash(book, 11) % 8u) {
    case 4:
    case 5:
      return SpineTone::LightGray;
    case 6:
    case 7:
      return SpineTone::DarkGray;
    default:
      return SpineTone::White;
  }
}

Rect shelfRect(const Page& page, const Rect& bounds) {
  const int shelfY = bounds.y + std::max(1, bounds.height - SHELF_BOTTOM_GAP);
  if (page.itemCount == 0) {
    return Rect{bounds.x, shelfY, std::max(1, bounds.width), SHELF_PLANK_HEIGHT};
  }

  int occupiedLeft = page.slots[0].rect.x;
  int occupiedRight = page.slots[0].rect.x + page.slots[0].rect.width;
  for (size_t slot = 1; slot < page.itemCount; ++slot) {
    occupiedLeft = std::min(occupiedLeft, page.slots[slot].rect.x);
    occupiedRight = std::max(occupiedRight, page.slots[slot].rect.x + page.slots[slot].rect.width);
  }
  if (page.plant.visible) {
    occupiedLeft = std::min(occupiedLeft, page.plant.rect.x);
    occupiedRight = std::max(occupiedRight, page.plant.rect.x + page.plant.rect.width);
  }

  const int overhang = std::clamp(bounds.width / 40, SHELF_OVERHANG_MIN, SHELF_OVERHANG_MAX);
  const int left = std::max(bounds.x, occupiedLeft - overhang);
  const int right = std::min(bounds.x + bounds.width, occupiedRight + overhang);
  return Rect{left, shelfY, std::max(1, right - left), SHELF_PLANK_HEIGHT};
}

Rect twoRowShelfRect(const TwoRowPage& page, const Rect& bounds, const size_t row) {
  const auto geometry = twoRowGeometry(bounds, std::min<size_t>(row, 1));
  bool found = false;
  for (size_t index = 0; index < page.itemCount; ++index) {
    const auto& slot = page.slots[index];
    if (slot.rect.y < geometry.bookTop || slot.rect.y >= geometry.shelfY) continue;
    found = true;
  }
  if (!found) return Rect{bounds.x, geometry.shelfY, 0, 0};
  // Each populated row owns the full intended shelf width, like the original
  // Spine. An empty row has no shelf at all, preventing a detached plank on a
  // partial final page.
  return Rect{bounds.x, geometry.shelfY, std::max(1, bounds.width), TWO_ROW_SHELF_HEIGHT};
}

Page pageForSelection(const Book* books, const size_t bookCount, size_t selectedIndex, const Rect& bounds) {
  Page result{};
  if (books == nullptr || bookCount == 0) return result;
  const size_t count = std::min(bookCount, MAX_BOOKS);
  selectedIndex = std::min(selectedIndex, count - 1);

  size_t start = 0;
  uint8_t pageNumber = 1;
  while (start < count && pageNumber <= MAX_BOOKS) {
    const Page candidate = pageAtStart(books, count, start, bounds);
    if (candidate.itemCount == 0) break;
    if (selectedIndex >= candidate.firstIndex && selectedIndex < candidate.firstIndex + candidate.itemCount) {
      result = candidate;
      result.pageNumber = pageNumber;
      break;
    }
    start += candidate.itemCount;
    ++pageNumber;
  }
  result.pageCount = static_cast<uint8_t>(pageCountFor(books, count, bounds));
  return result;
}

size_t nextPageStart(const Book* books, const size_t bookCount, const size_t selectedIndex, const Rect& bounds) {
  const Page current = pageForSelection(books, bookCount, selectedIndex, bounds);
  if (current.itemCount == 0 || current.pageCount <= 1 || current.pageNumber >= current.pageCount) return 0;
  return current.firstIndex + current.itemCount;
}

size_t previousPageStart(const Book* books, const size_t bookCount, const size_t selectedIndex, const Rect& bounds) {
  const Page current = pageForSelection(books, bookCount, selectedIndex, bounds);
  if (current.itemCount == 0 || current.pageCount <= 1) return 0;
  if (current.firstIndex == 0) {
    size_t start = 0;
    size_t last = 0;
    while (start < std::min(bookCount, MAX_BOOKS)) {
      const Page candidate = pageAtStart(books, std::min(bookCount, MAX_BOOKS), start, bounds);
      if (candidate.itemCount == 0) break;
      last = start;
      start += candidate.itemCount;
    }
    return last;
  }

  size_t start = 0;
  size_t previous = 0;
  while (start < current.firstIndex) {
    const Page candidate = pageAtStart(books, std::min(bookCount, MAX_BOOKS), start, bounds);
    if (candidate.itemCount == 0) break;
    previous = start;
    start += candidate.itemCount;
  }
  return previous;
}

TwoRowPage twoRowPageForSelection(const Book* books, const size_t bookCount, size_t selectedIndex,
                                  const Rect& bounds) {
  TwoRowPage result{};
  const size_t count = bookCount;
  if (books == nullptr || count == 0) return result;
  selectedIndex = std::min(selectedIndex, count - 1);

  size_t start = 0;
  size_t pageNumber = 1;
  while (start < count && pageNumber <= 255) {
    const TwoRowPage candidate = twoRowPageAtStart(books, count, start, bounds);
    if (candidate.itemCount == 0) break;
    if (selectedIndex >= candidate.firstIndex && selectedIndex < candidate.firstIndex + candidate.itemCount) {
      result = candidate;
      result.pageNumber = static_cast<uint8_t>(std::min<size_t>(pageNumber, 255));
      break;
    }
    start += candidate.itemCount;
    ++pageNumber;
  }
  result.pageCount = static_cast<uint8_t>(std::min<size_t>(255, twoRowPageCountFor(books, count, bounds)));
  return result;
}

size_t twoRowNextPageStart(const Book* books, const size_t bookCount, const size_t selectedIndex,
                           const Rect& bounds) {
  const TwoRowPage current = twoRowPageForSelection(books, bookCount, selectedIndex, bounds);
  if (current.itemCount == 0 || current.pageCount <= 1 || current.pageNumber >= current.pageCount) return 0;
  return current.firstIndex + current.itemCount;
}

size_t twoRowPreviousPageStart(const Book* books, const size_t bookCount, const size_t selectedIndex,
                               const Rect& bounds) {
  const TwoRowPage current = twoRowPageForSelection(books, bookCount, selectedIndex, bounds);
  if (current.itemCount == 0 || current.pageCount <= 1) return 0;
  if (current.firstIndex == 0) {
    size_t start = 0;
    size_t last = 0;
    while (start < bookCount) {
      const TwoRowPage page = twoRowPageAtStart(books, bookCount, start, bounds);
      if (page.itemCount == 0) break;
      last = start;
      start += page.itemCount;
    }
    return last;
  }

  size_t start = 0;
  size_t previous = 0;
  while (start < current.firstIndex) {
    const TwoRowPage page = twoRowPageAtStart(books, bookCount, start, bounds);
    if (page.itemCount == 0) break;
    previous = start;
    start += page.itemCount;
  }
  return previous;
}

bool makeSpineText(char* output, const size_t outputCapacity, const char* text, const Rect& spine,
                   const int minimumWidth, const int minimumHeight, const int codepointDivisor,
                   const size_t minimumCodepoints, const size_t maximumCodepoints) {
  if (output == nullptr || outputCapacity == 0) return false;
  output[0] = '\0';
  if (text == nullptr || *text == '\0' || spine.width < minimumWidth || spine.height < minimumHeight) return false;

  const size_t maxCodepoints = static_cast<size_t>(
      std::clamp((spine.height - 12) / codepointDivisor, static_cast<int>(minimumCodepoints),
                 static_cast<int>(maximumCodepoints)));
  size_t inputOffset = 0;
  size_t outputOffset = 0;
  size_t codepoints = 0;
  bool truncated = false;
  while (text[inputOffset] != '\0') {
    const auto* bytes = reinterpret_cast<const unsigned char*>(text + inputOffset);
    const size_t length = sequenceLength(*bytes);
    if (!validSequence(text + inputOffset, length)) return false;
    // Never intentionally draw the Unicode replacement glyph on a binding.
    // The renderer's font coverage check separately rejects codepoints that
    // would fall back to a replacement glyph.
    if (length == 3 && bytes[0] == 0xEFu && bytes[1] == 0xBFu && bytes[2] == 0xBDu) return false;
    if (codepoints >= maxCodepoints || outputOffset + length + 1 >= outputCapacity) {
      truncated = true;
      break;
    }
    std::memcpy(output + outputOffset, text + inputOffset, length);
    outputOffset += length;
    inputOffset += length;
    ++codepoints;
  }
  if (codepoints == 0) return false;

  if (truncated) {
    if (outputOffset + 3 >= outputCapacity) return false;
    output[outputOffset++] = '.';
    output[outputOffset++] = '.';
    output[outputOffset++] = '.';
  }
  output[outputOffset] = '\0';
  return true;
}

bool makeTitle(char* output, const size_t outputCapacity, const char* title, const Rect& spine) {
  return makeSpineText(output, outputCapacity, title, spine, 32, 52, 10, 4, 11);
}

bool makeBookTitle(char* output, const size_t outputCapacity, const Book& book, const Rect& spine) {
  // A valid metadata title always wins.  A filename stem is only a fallback
  // for absent or malformed metadata; a valid title that does not fit or is
  // not covered by the selected display font is intentionally omitted rather
  // than silently replaced.
  if (book.title != nullptr && *book.title != '\0') {
    if (validUtf8Text(book.title)) return makeTitle(output, outputCapacity, book.title, spine);
  }

  char fallback[64];
  if (!copyFilenameStem(fallback, sizeof(fallback), book.key)) return false;
  return makeTitle(output, outputCapacity, fallback, spine);
}

bool makeAuthor(char* output, const size_t outputCapacity, const char* author, const Rect& spine) {
  return makeSpineText(output, outputCapacity, author, spine, 36, 60, 12, 3, 5);
}

}  // namespace SpineShelfPlanner
