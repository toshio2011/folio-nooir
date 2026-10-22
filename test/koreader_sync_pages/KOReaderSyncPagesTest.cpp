#include "activities/reader/KOReaderSyncActivity.h"

#include <cstdint>
#include <limits>

#define CHECK(condition) \
  do {                    \
    if (!(condition)) return __LINE__; \
  } while (false)

int main() {
  const auto firstPage = KOReaderSyncActivity::pageProgress(0, 100);
  CHECK(firstPage.xpath == "1");
  CHECK(firstPage.percentage == 0.01f);

  const auto lastPage = KOReaderSyncActivity::pageProgress(99, 100);
  CHECK(lastPage.xpath == "100");
  CHECK(lastPage.percentage == 1.0f);

  const auto clampedPage = KOReaderSyncActivity::pageProgress(100, 100);
  CHECK(clampedPage.xpath == "100");
  CHECK(clampedPage.percentage == 1.0f);

  const auto emptyBook = KOReaderSyncActivity::pageProgress(0, 0);
  CHECK(emptyBook.xpath.empty());
  CHECK(emptyBook.percentage == 0.0f);

  CHECK(KOReaderSyncActivity::pageFromProgress("1", 100) == 0);
  CHECK(KOReaderSyncActivity::pageFromProgress("100", 100) == 99);
  CHECK(!KOReaderSyncActivity::pageFromProgress("0", 100).has_value());
  CHECK(!KOReaderSyncActivity::pageFromProgress("101", 100).has_value());
  CHECK(!KOReaderSyncActivity::pageFromProgress("/body/p[1]", 100).has_value());
  CHECK(!KOReaderSyncActivity::pageFromProgress("", 100).has_value());
  CHECK(!KOReaderSyncActivity::pageFromProgress("4294967296", std::numeric_limits<uint32_t>::max()).has_value());
  CHECK(!KOReaderSyncActivity::pageFromProgress("1", 0).has_value());
}
