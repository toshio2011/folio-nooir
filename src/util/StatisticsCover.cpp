#include "StatisticsCover.h"

#include <Bitmap.h>
#include <GfxRenderer.h>
#include <HalStorage.h>

#include <algorithm>

#include "StatisticsSnapshot.h"
#include "components/UITheme.h"
#include "components/themes/BaseTheme.h"
#include "util/BookCacheUtils.h"

namespace {
constexpr int STATISTICS_HQ_COVER_HEIGHT = 360;
}  // namespace

bool ensureStatisticsCover(StatisticsBookSnapshot& book) {
  if (book.coverChecked) return book.coverAvailable;
  book.coverChecked = true;
  book.coverAvailable = false;
  book.coverPath220.clear();
  if (book.coverTemplatePath.empty()) return false;
  book.coverPath220 = UITheme::getCoverThumbPath(book.coverTemplatePath, STATISTICS_COVER_HEIGHT);
  book.coverAvailable = isValidBookThumbnail(book.coverPath220);
  if (!book.coverAvailable) book.coverPath220.clear();
  return book.coverAvailable;
}

std::string selectStatisticsCoverPath(const StatisticsBookSnapshot& book) {
  if (!book.coverAvailable || book.coverPath220.empty()) return {};
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

  const int drawWidth = std::min(bounds.width, bounds.height * bitmap.getWidth() / bitmap.getHeight());
  const int drawHeight = std::min(bounds.height, bounds.width * bitmap.getHeight() / bitmap.getWidth());
  const int x = bounds.x + (bounds.width - drawWidth) / 2;
  const int y = bounds.y + (bounds.height - drawHeight) / 2;
  renderer.drawBitmap(bitmap, x, y, drawWidth, drawHeight);
  renderer.drawRect(x, y, drawWidth, drawHeight);
  return true;
}
