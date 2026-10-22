#include "StatisticsCover.h"

#include <Bitmap.h>
#include <GfxRenderer.h>
#include <HalStorage.h>

#include <algorithm>
#include <cctype>

#include "StatisticsSnapshot.h"
#include "components/UITheme.h"
#include "components/themes/BaseTheme.h"
#include "FsHelpers.h"
#include "util/BookCacheUtils.h"
#include "Xtc.h"

namespace {
constexpr int STATISTICS_HQ_COVER_HEIGHT = 360;
constexpr int MAX_CACHED_COVER_WIDTH = 2048;
constexpr int MAX_CACHED_COVER_HEIGHT = 3072;

bool isXtchPath(const std::string& path) {
  if (path.size() < 5) return false;
  const size_t extension = path.size() - 5;
  return path[extension] == '.' && std::tolower(static_cast<unsigned char>(path[extension + 1])) == 'x' &&
         std::tolower(static_cast<unsigned char>(path[extension + 2])) == 't' &&
         std::tolower(static_cast<unsigned char>(path[extension + 3])) == 'c' &&
         std::tolower(static_cast<unsigned char>(path[extension + 4])) == 'h';
}

bool isCompleteCachedXtcCover(const std::string& path, const uint16_t expectedBpp) {
  if (path.empty()) return false;
  HalFile file;
  if (!Storage.openFileForRead("STAT", path, file)) return false;

  Bitmap bitmap(file);
  const bool parsed = bitmap.parseHeaders() == BmpReaderError::Ok;
  const bool valid = parsed && bitmap.getWidth() > 0 && bitmap.getWidth() <= MAX_CACHED_COVER_WIDTH &&
                     bitmap.getHeight() > 0 && bitmap.getHeight() <= MAX_CACHED_COVER_HEIGHT &&
                     bitmap.getBpp() == expectedBpp &&
                     file.fileSize64() >= static_cast<uint64_t>(file.position()) +
                                              static_cast<uint64_t>(bitmap.getRowBytes()) *
                                                  static_cast<uint64_t>(bitmap.getHeight());
  file.close();
  return valid;
}

std::string existingFullXtcCover(const StatisticsBookSnapshot& book) {
  if (!FsHelpers::hasXtcExtension(book.path)) return {};

  const bool xtch = isXtchPath(book.path);
  Xtc xtc(book.path, "/.crosspoint");
  const std::string path = xtc.getCachePath() + (xtch ? "/cover-2bit.bmp" : "/cover.bmp");
  return isCompleteCachedXtcCover(path, xtch ? 2 : 1) ? path : std::string{};
}
}  // namespace

bool ensureStatisticsCover(StatisticsBookSnapshot& book) {
  if (book.coverChecked) return book.coverAvailable;
  book.coverChecked = true;
  book.coverAvailable = false;
  book.coverPath220.clear();
  // A full-resolution XTC cover is already in the per-book cache when present.
  // Reuse it without loading the source archive or generating anything during
  // sleep; Bitmap's parser bounds dimensions and the data extent is validated
  // before it is selected.
  const std::string fullXtcCover = existingFullXtcCover(book);
  if (!fullXtcCover.empty()) {
    book.coverPath220 = fullXtcCover;
    book.coverAvailable = true;
    return true;
  }

  if (book.coverTemplatePath.empty()) return false;
  book.coverPath220 = UITheme::getCoverThumbPath(book.coverTemplatePath, STATISTICS_COVER_HEIGHT);
  book.coverAvailable = isValidBookThumbnail(book.coverPath220);
  if (!book.coverAvailable) book.coverPath220.clear();
  return book.coverAvailable;
}

std::string selectStatisticsCoverPath(const StatisticsBookSnapshot& book) {
  if (!book.coverAvailable || book.coverPath220.empty()) return {};
  // ensureStatisticsCover stores a validated full XTC cover in this field when
  // one exists. Prefer its native detail over either thumbnail size.
  if (book.coverPath220.size() >= 10 &&
      (book.coverPath220.compare(book.coverPath220.size() - 10, 10, "/cover.bmp") == 0 ||
       (book.coverPath220.size() >= 15 &&
        book.coverPath220.compare(book.coverPath220.size() - 15, 15, "/cover-2bit.bmp") == 0))) {
    return book.coverPath220;
  }
  if (!book.coverTemplatePath.empty()) {
    const std::string hqPath = UITheme::getCoverThumbPath(book.coverTemplatePath, STATISTICS_HQ_COVER_HEIGHT);
    if (isValidBookThumbnail(hqPath)) return hqPath;
  }
  return book.coverPath220;
}

bool drawStatisticsCover(GfxRenderer& renderer, const std::string& path, const Rect& bounds) {
  if (path.empty() || bounds.width <= 0 || bounds.height <= 0) return false;
  HalFile file;
  if (!Storage.openFileForRead("STAT", path, file)) return false;
  Bitmap bitmap(file);
  if (bitmap.parseHeaders() != BmpReaderError::Ok || bitmap.getWidth() <= 0 || bitmap.getHeight() <= 0) return false;

  const int drawWidth = std::max(1, std::min(bounds.width, bounds.height * bitmap.getWidth() / bitmap.getHeight()));
  const int drawHeight = std::max(1, std::min(bounds.height, bounds.width * bitmap.getHeight() / bitmap.getWidth()));
  const int x = bounds.x + (bounds.width - drawWidth) / 2;
  const int y = bounds.y + (bounds.height - drawHeight) / 2;
  renderer.drawBitmap(bitmap, x, y, drawWidth, drawHeight, 0, 0, true);
  renderer.drawRect(x, y, drawWidth, drawHeight);
  return true;
}
