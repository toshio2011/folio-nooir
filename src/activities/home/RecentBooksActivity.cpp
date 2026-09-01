#include "RecentBooksActivity.h"

#include <GfxRenderer.h>
#include <HalClock.h>
#include <HalStorage.h>
#include <I18n.h>
#include <Bitmap.h>
#include <Cbz.h>
#include <Epub.h>
#include <FsHelpers.h>
#include <Xtc.h>

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <memory>

#include "MappedInputManager.h"
#include "RecentBooksStore.h"
#include "BookStateStore.h"
#include "activities/util/ConfirmationActivity.h"
#include "activities/home/SynopsisActivity.h"
#include "activities/home/ReadingStatsActivity.h"
#include "activities/home/ClockWeatherActivity.h"
#include "activities/home/ToDoListActivity.h"
#include "activities/reader/EpubReaderBookmarksActivity.h"
#include "activities/reader/EpubReaderClippingListActivity.h"
#include "util/BookCacheUtils.h"
#include "util/SynopsisPreview.h"
#include "components/UITheme.h"
#include "components/themes/folio_nooir/CarouselTheme.h"
#include "components/themes/folio_nooir/FolioNooirTheme.h"
#include "util/CoverStackGeometry.h"
#include "util/CbzDiagnostics.h"
#include "fontIds.h"

namespace {
enum class CarouselBitmapDrawResult : uint8_t { Unavailable, Drawn, Failed };
constexpr char FOLIO_HOME_SNAPSHOT[] = "/.crosspoint/folio_home.bin";
// This marker makes the cache probe a one-time migration step.  Recent is
// opened often, so checking every EPUB/thumbnail on every visit would bring
// back the post-boot hitch this activity was designed to avoid.
constexpr char RECENT_CACHE_BOOTSTRAP_MARKER[] = "/.crosspoint/recent_cache_bootstrap_v1";
constexpr uint32_t FOLIO_HOME_MAGIC = 0x464E484D;  // "FNHM"
constexpr uint16_t FOLIO_HOME_VERSION = 3;
// Manual cover refresh is the only path that may decode a source image. Keep
// it bounded for X3/X4, but allow normal high-quality covers (the old 512 KiB
// limit rejected many perfectly usable 2 MiB JPEGs before the converter could
// apply its own dimension and heap guards). Ordinary Recent bootstrap never
// enters this path and remains thumbnail-free.
constexpr size_t MAX_MANUAL_THUMBNAIL_SOURCE_BYTES = 3u * 1024u * 1024u;

bool isClippingsExport(const std::string& path) {
  std::string name = path;
  const size_t slash = name.find_last_of('/');
  if (slash != std::string::npos) name.erase(0, slash + 1);
  std::transform(name.begin(), name.end(), name.begin(),
                 [](const unsigned char c) { return static_cast<char>(std::tolower(c)); });
  return name == "my clippings.txt" || name == "clippings.txt";
}

void hashBytes(uint64_t& hash, const void* data, const size_t size) {
  const auto* bytes = static_cast<const uint8_t*>(data);
  for (size_t i = 0; i < size; ++i) {
    hash ^= bytes[i];
    hash *= 1099511628211ULL;
  }
}
}  // namespace

void RecentBooksActivity::loadRecentBooks() {
  recentBooks = RECENT_BOOKS.getBooks();
  invalidateCarouselHqProbes();
}

void RecentBooksActivity::invalidateCarouselHqProbes() {
  RenderLock lock;
  invalidateCarouselHqProbesLocked();
}

void RecentBooksActivity::invalidateCarouselHqProbesLocked() {
  for (auto& probe : carouselHqProbes) probe = {};
  invalidateCarouselSourceCacheLocked();
}

void RecentBooksActivity::invalidateCarouselSourceCache() {
  RenderLock lock;
  invalidateCarouselSourceCacheLocked();
}

void RecentBooksActivity::invalidateCarouselSourceCacheLocked() {
  for (auto& entry : carouselSourceCache) {
    entry.bitmap.reset();
    entry.file = HalFile{};
    entry.path.clear();
    entry.valid = false;
    entry.lastUsed = 0;
  }
  carouselSourceCacheClock = 0;
}

void RecentBooksActivity::invalidateCarouselSource(const std::string& path) {
  RenderLock lock;
  invalidateCarouselSourceLocked(path);
}

void RecentBooksActivity::invalidateCarouselSourceLocked(const std::string& path) {
  for (auto& entry : carouselSourceCache) {
    if (!entry.valid || entry.path != path) continue;
    entry.bitmap.reset();
    entry.file = HalFile{};
    entry.path.clear();
    entry.valid = false;
    entry.lastUsed = 0;
    return;
  }
}

RecentBooksActivity::CarouselSourceCacheEntry* RecentBooksActivity::getCarouselSource(
    const std::string& path, CarouselSourceTiming& timing, CarouselSourceFrame* frame) {
  timing = {};
  if (path.empty()) return nullptr;

  if (++carouselSourceCacheClock == 0) carouselSourceCacheClock = 1;
  for (auto& entry : carouselSourceCache) {
    if (!entry.valid || entry.path != path) continue;
    if (!entry.bitmap || !entry.file.isOpen()) {
      invalidateCarouselSourceLocked(path);
      break;
    }
    entry.lastUsed = carouselSourceCacheClock;
    timing.reused = true;
    return &entry;
  }

  CarouselSourceCacheEntry* replacement = nullptr;
  for (auto& entry : carouselSourceCache) {
    if (!entry.valid) {
      replacement = &entry;
      break;
    }
    if (frame != nullptr && frame->protects(entry.path)) continue;
    if (replacement == nullptr || entry.lastUsed < replacement->lastUsed) replacement = &entry;
  }
  if (replacement == nullptr) {
    return nullptr;
  }

  replacement->bitmap.reset();
  replacement->file = HalFile{};
  replacement->path.clear();
  replacement->valid = false;
  replacement->lastUsed = 0;

  const bool opened = Storage.openFileForRead("SHELF", path, replacement->file);
  if (!opened) {
    replacement->file = HalFile{};
    return nullptr;
  }

  std::unique_ptr<Bitmap> bitmap(new Bitmap(replacement->file));
  const BmpReaderError headerResult = bitmap->parseHeaders();
  const bool valid = headerResult == BmpReaderError::Ok && bitmap->getWidth() > 0 && bitmap->getHeight() > 0;
  if (valid) replacement->path = path;
  if (!valid) {
    bitmap.reset();
    replacement->file = HalFile{};
    return nullptr;
  }

  replacement->bitmap = std::move(bitmap);
  replacement->valid = true;
  replacement->lastUsed = carouselSourceCacheClock;
  return replacement;
}

void RecentBooksActivity::prepareCarouselSourceFrame(
    const std::array<CoverStackSlot, CAROUSEL_FRAME_SLOT_COUNT>& stackSlots, CarouselSourceFrame& frame) {
  frame = {};

  for (size_t slotIndex = 0; slotIndex < stackSlots.size(); ++slotIndex) {
    const auto& stackSlot = stackSlots[slotIndex];
    if (!stackSlot.valid || stackSlot.itemIndex >= visibleBookCount) continue;

    auto& planned = frame.slots[slotIndex];
    const RecentBook& book = recentBooks[visibleBookIndexes[stackSlot.itemIndex]];
    planned.valid = true;
    planned.fallbackPath = UITheme::getCoverThumbPath(book.coverBmpPath, FolioNooirTheme::COVER_HEIGHT);

    const bool isCenter = stackSlot.depth == 0 && !book.coverBmpPath.empty();
    if (isCenter) {
      auto& hqProbe = carouselHqProbe(book);
      if (!hqProbe.unavailable) {
        planned.hqAttempted = true;
        planned.primaryPath = UITheme::getCoverThumbPath(book.coverBmpPath, CAROUSEL_HQ_COVER_HEIGHT);
      }
    }
    if (planned.primaryPath.empty()) planned.primaryPath = planned.fallbackPath;

    // Keep both possible center variants protected when the HQ probe is still
    // eligible. Only one will become the resolved source, but the fallback
    // must not be evicted before an HQ open/read failure is known.
    frame.protectPath(planned.primaryPath);
    if (planned.hqAttempted) frame.protectPath(planned.fallbackPath);
  }

  for (size_t slotIndex = 0; slotIndex < stackSlots.size(); ++slotIndex) {
    auto& planned = frame.slots[slotIndex];
    if (!planned.valid || planned.primaryPath.empty()) continue;

    auto resolve = [&](const std::string& path, const bool hq, bool& fromCache) {
      fromCache = false;
      if (path.empty()) return false;
      CarouselSourceTiming timing;
      const bool available = getCarouselSource(path, timing, &frame) != nullptr;
      fromCache = available && timing.reused;
      if (!available && hq) {
        // The current render path marks an unavailable HQ file and then uses
        // the normal thumbnail without retrying it on later frames.
        const RecentBook& book = recentBooks[visibleBookIndexes[stackSlots[slotIndex].itemIndex]];
        carouselHqProbe(book).unavailable = true;
      }
      return available;
    };

    bool resolvedFromCache = false;
    if (planned.hqAttempted && resolve(planned.primaryPath, true, resolvedFromCache)) {
      planned.resolvedPath = planned.primaryPath;
      planned.resolvedHq = true;
      planned.resolvedFromCache = resolvedFromCache;
    } else if (resolve(planned.fallbackPath, false, resolvedFromCache)) {
      planned.resolvedPath = planned.fallbackPath;
      planned.resolvedHq = false;
      planned.resolvedFromCache = resolvedFromCache;
    }
  }

  // Candidate protection was needed while resolving HQ-versus-normal
  // fallback. During the actual draw, retain only the final source selected
  // for each slot; a render-failure fallback can use the spare cache slot.
  frame.retainResolvedPaths();
}

uint64_t RecentBooksActivity::carouselCoverIdentity(const RecentBook& book) const {
  uint64_t identity = 1469598103934665603ULL;
  hashBytes(identity, book.path.data(), book.path.size());
  hashBytes(identity, book.coverBmpPath.data(), book.coverBmpPath.size());
  return identity;
}

RecentBooksActivity::CarouselHqProbe& RecentBooksActivity::carouselHqProbe(const RecentBook& book) {
  const uint64_t identity = carouselCoverIdentity(book);
  for (auto& probe : carouselHqProbes) {
    if (probe.valid && probe.identity == identity) return probe;
  }
  for (auto& probe : carouselHqProbes) {
    if (!probe.valid) {
      probe = {true, identity, false};
      return probe;
    }
  }
  auto& replacement = carouselHqProbes[identity % carouselHqProbes.size()];
  replacement = {true, identity, false};
  return replacement;
}

bool RecentBooksActivity::hasMissingRecentCache() const {
  for (const auto& book : recentBooks) {
    if (!FsHelpers::hasEpubExtension(book.path) && !FsHelpers::hasXtcExtension(book.path) &&
        !FsHelpers::hasCbzExtension(book.path)) continue;
    // RecentBook can survive a firmware update/cache deletion, so a non-empty
    // title alone does not prove that the lightweight cache still exists.
    // Probe only the at-most-ten Recent entries during the one-time bootstrap;
    // normal visits never run this path.
    if (book.title.empty()) return true;

    if (FsHelpers::hasEpubExtension(book.path)) {
      Epub epub(book.path, "/.crosspoint");
      if (!epub.loadCachedMetadataOnly()) return true;
    } else if (FsHelpers::hasCbzExtension(book.path)) {
      Cbz cbz(book.path, "/.crosspoint");
      if (!cbz.loadCachedMetadataOnly()) return true;
    }
    // A missing thumbnail is intentionally not a bootstrap condition. Recent
    // can render its lightweight placeholder immediately; only the explicit
    // Refresh Book Cache action is allowed to decode a cover.
  }
  return false;
}

void RecentBooksActivity::rebuildVisibleBooks() {
  visibleBookCount = 0;
  for (size_t i = 0; i < recentBooks.size() && visibleBookCount < sizeof(visibleBookIndexes); ++i) {
    if (isClippingsExport(recentBooks[i].path)) continue;
    const BookState* state = BOOK_STATES.find(recentBooks[i].path);
    const bool finished = state ? state->status == BookStatus::Finished : recentBooks[i].progressPercent >= 100;
    const bool show = activeTab == 0 || (activeTab == 1 && !finished) || (activeTab == 2 && finished);
    if (show) visibleBookIndexes[visibleBookCount++] = static_cast<uint8_t>(i);
  }
  if (visibleBookCount == 0) {
    selectorIndex = 0;
  } else if (selectorIndex >= visibleBookCount) {
    selectorIndex = visibleBookCount - 1;
  }
}

size_t RecentBooksActivity::selectedRecentIndex() const {
  return visibleBookCount == 0 ? 0 : visibleBookIndexes[selectorIndex];
}

uint8_t RecentBooksActivity::activeBookLayout() const {
  // The standalone Carousel theme has its own layout preference; the Folio
  // Recent/Finished preferences are intentionally not consulted here.
  if (SETTINGS.uiTheme == CrossPointSettings::UI_THEME::CAROUSEL) {
    return SETTINGS.carouselLayout == CrossPointSettings::CAROUSEL_LAYOUT_THREE_COVER
               ? CrossPointSettings::FOLIO_LAYOUT_THREE_COVER_CAROUSEL
               : CrossPointSettings::FOLIO_LAYOUT_FIVE_COVER_CAROUSEL;
  }
  if (SETTINGS.uiTheme != CrossPointSettings::UI_THEME::FOLIO_NOOIR) {
    return CrossPointSettings::FOLIO_LAYOUT_GRID;
  }
  const uint8_t layout = activeTab == 2 ? SETTINGS.finishedBookLayout : SETTINGS.recentBookLayout;
  return layout < CrossPointSettings::FOLIO_BOOK_LAYOUT_COUNT ? layout : CrossPointSettings::FOLIO_LAYOUT_GRID;
}

bool RecentBooksActivity::usesCarouselLayout() const {
  const uint8_t layout = activeBookLayout();
  return layout == CrossPointSettings::FOLIO_LAYOUT_THREE_COVER_CAROUSEL ||
         layout == CrossPointSettings::FOLIO_LAYOUT_FIVE_COVER_CAROUSEL;
}

bool RecentBooksActivity::usesThreeCoverGrid() const {
  return SETTINGS.uiTheme == CrossPointSettings::UI_THEME::FOLIO_NOOIR &&
         activeBookLayout() == CrossPointSettings::FOLIO_LAYOUT_THREE_COVERS;
}

bool RecentBooksActivity::usesFourByTwoGrid() const {
  return SETTINGS.uiTheme == CrossPointSettings::UI_THEME::FOLIO_NOOIR &&
         activeBookLayout() == CrossPointSettings::FOLIO_LAYOUT_GRID;
}

int RecentBooksActivity::activePageItems() const {
  if (usesThreeCoverGrid()) return 3;
  if (usesCarouselLayout()) return 1;
  return BOOKS_PER_PAGE;
}

uint64_t RecentBooksActivity::snapshotKey() const {
  uint64_t hash = 1469598103934665603ULL;
  const int width = renderer.getScreenWidth();
  const int height = renderer.getScreenHeight();
  hashBytes(hash, &width, sizeof(width));
  hashBytes(hash, &height, sizeof(height));
  hashBytes(hash, &activeTab, sizeof(activeTab));
  const uint8_t layout = activeBookLayout();
  hashBytes(hash, &layout, sizeof(layout));
  for (const auto& book : recentBooks) {
    hashBytes(hash, book.path.data(), book.path.size());
    hashBytes(hash, book.title.data(), book.title.size());
    hashBytes(hash, book.author.data(), book.author.size());
    hashBytes(hash, book.coverBmpPath.data(), book.coverBmpPath.size());
    hashBytes(hash, book.synopsis.data(), book.synopsis.size());
    hashBytes(hash, &book.progressPercent, sizeof(book.progressPercent));
    hashBytes(hash, &book.readingSeconds, sizeof(book.readingSeconds));
    hashBytes(hash, &book.lastSessionSeconds, sizeof(book.lastSessionSeconds));
    hashBytes(hash, &book.dailyReadingSeconds, sizeof(book.dailyReadingSeconds));
    hashBytes(hash, &book.dailyReadingDateKey, sizeof(book.dailyReadingDateKey));
    hashBytes(hash, &book.readingSessions, sizeof(book.readingSessions));
    hashBytes(hash, &book.pagesTurned, sizeof(book.pagesTurned));
    const BookState* state = BOOK_STATES.find(book.path);
    const uint8_t status = state ? static_cast<uint8_t>(state->status) : 0;
    hashBytes(hash, &status, sizeof(status));
  }
  return hash;
}

bool RecentBooksActivity::restoreSnapshot() {
  if (!usesFourByTwoGrid() || !renderer.hasFrameBuffer()) return false;
  HalFile file;
  if (!Storage.openFileForRead("SHELF", FOLIO_HOME_SNAPSHOT, file)) return false;

  uint32_t magic = 0;
  uint16_t version = 0;
  uint16_t width = 0;
  uint16_t height = 0;
  uint32_t dataSize = 0;
  uint64_t key = 0;
  uint32_t pageStart = 0;
  if (file.read(&magic, sizeof(magic)) != sizeof(magic) || file.read(&version, sizeof(version)) != sizeof(version) ||
      file.read(&width, sizeof(width)) != sizeof(width) || file.read(&height, sizeof(height)) != sizeof(height) ||
      file.read(&dataSize, sizeof(dataSize)) != sizeof(dataSize) || file.read(&key, sizeof(key)) != sizeof(key)) {
    return false;
  }
  if (magic != FOLIO_HOME_MAGIC || version != FOLIO_HOME_VERSION || width != renderer.getScreenWidth() ||
      height != renderer.getScreenHeight() || dataSize != renderer.getBufferSize() || key != snapshotKey() ||
      file.read(&pageStart, sizeof(pageStart)) != sizeof(pageStart)) {
    return false;
  }
  const bool loaded = file.read(renderer.getFrameBuffer(), dataSize) == static_cast<int>(dataSize);
  if (loaded) {
    snapshotPageStart = pageStart;
    snapshotSelectorIndex = SIZE_MAX;
    LOG_DBG("SHELF", "Restored %u-byte Home snapshot", dataSize);
  }
  return loaded;
}

void RecentBooksActivity::writeSnapshot() {
  if (!renderer.hasFrameBuffer()) return;
  Storage.ensureDirectoryExists("/.crosspoint");
  HalFile file;
  if (!Storage.openFileForWrite("SHELF", FOLIO_HOME_SNAPSHOT, file)) return;
  const uint32_t magic = FOLIO_HOME_MAGIC;
  const uint16_t version = FOLIO_HOME_VERSION;
  const uint16_t width = static_cast<uint16_t>(renderer.getScreenWidth());
  const uint16_t height = static_cast<uint16_t>(renderer.getScreenHeight());
  const uint32_t dataSize = renderer.getBufferSize();
  const uint64_t key = snapshotKey();
  const uint32_t pageStart = static_cast<uint32_t>((selectorIndex / BOOKS_PER_PAGE) * BOOKS_PER_PAGE);
  const bool ok = file.write(&magic, sizeof(magic)) == sizeof(magic) &&
                  file.write(&version, sizeof(version)) == sizeof(version) &&
                  file.write(&width, sizeof(width)) == sizeof(width) &&
                  file.write(&height, sizeof(height)) == sizeof(height) &&
                  file.write(&dataSize, sizeof(dataSize)) == sizeof(dataSize) &&
                  file.write(&key, sizeof(key)) == sizeof(key) &&
                  file.write(&pageStart, sizeof(pageStart)) == sizeof(pageStart) &&
                  file.write(renderer.getFrameBuffer(), dataSize) == dataSize;
  if (ok) {
    snapshotPageStart = pageStart;
    snapshotSelectorIndex = selectorIndex;
  } else {
    LOG_ERR("SHELF", "Failed to write Home snapshot");
  }
}

void RecentBooksActivity::generateNextCover() {
  if (coverGenerationActive) return;
  coverGenerationActive = true;
  // First-run warmup and manual Refresh Book Cache only need the lightweight
  // bookshelf metadata path.  Do not build the reader's spine/TOC cache here;
  // that work is deferred until the user opens the book.
  // Warm-up must process only entries whose metadata cache probe found a gap.
  // Manual Refresh Book Cache is the only path that intentionally rereads a
  // source cover or generates a missing thumbnail.
  const bool forceRebuild = coverGenerationRequested && !recentCacheWarmupActive;
  const bool metadataOnly = forceRebuild || recentCacheWarmupActive;
  while (nextCoverToGenerate < recentBooks.size()) {
    RecentBook& book = recentBooks[nextCoverToGenerate++];
    if (!FsHelpers::hasEpubExtension(book.path) && !FsHelpers::hasXtcExtension(book.path) &&
        !FsHelpers::hasCbzExtension(book.path)) continue;
    bool metadataMissing = book.title.empty() || book.coverBmpPath.empty();
    if (FsHelpers::hasCbzExtension(book.path)) {
      Cbz cached(book.path, "/.crosspoint");
      metadataMissing = metadataMissing || !cached.loadCachedMetadataOnly();
    }
    // A manual Refresh Book Cache must reread metadata even when an old
    // thumbnail survived the cache cleanup. Normal warm-up only handles
    // missing metadata; it must not inspect or regenerate thumbnails.
    if (!forceRebuild && !metadataMissing) continue;
    bool attempted = false;
    if (FsHelpers::hasEpubExtension(book.path)) {
      Epub epub(book.path, "/.crosspoint");
      // During the one-time bootstrap, reuse metadata.bin/book.bin whenever
      // possible. Only books whose lightweight cache is absent are parsed
      // from the EPUB package; manual Refresh remains an explicit source
      // reread.
      bool loaded = false;
      if (recentCacheWarmupActive && !metadataMissing) loaded = epub.loadCachedMetadataOnly();
      if (!loaded) loaded = metadataOnly ? epub.loadMetadataOnly() : epub.load(false, true);
      if (loaded) {
        attempted = true;
        const std::string title = epub.getTitle().empty() ? book.title : epub.getTitle();
        const std::string author = epub.getAuthor();
        const std::string cover = epub.getThumbBmpPath();
        // Some EPUBs expose their description through a metadata variant that
        // the lightweight pass cannot see. Never erase a working shelf synopsis
        // just because this refresh returned an empty description.
        std::string parsedSynopsis = epub.getDescription();
        // If the fresh lightweight OPF pass did not expose a description,
        // reuse the existing book.bin metadata when available. This recovers
        // a synopsis that was cached before a transient/partial refresh.
        if (parsedSynopsis.empty() && metadataOnly) {
          Epub cached(book.path, "/.crosspoint");
          if (cached.loadCachedMetadataOnly()) parsedSynopsis = cached.getDescription();
        }
        const std::string synopsis = parsedSynopsis.empty() ? book.synopsis : parsedSynopsis.substr(0, 384);
        if (book.title != title || book.author != author || book.coverBmpPath != cover || book.synopsis != synopsis) {
          book.title = title;
          book.author = author;
          book.coverBmpPath = cover;
          book.synopsis = synopsis;
          RECENT_BOOKS.refreshBookMetadata(book.path, book.title, book.author, book.coverBmpPath, book.synopsis);
        }
        // Never decode a missing cover during ordinary Recent bootstrap. A
        // placeholder keeps navigation responsive; manual refresh is the only
        // operation allowed to spend time and heap on image conversion.
        if (forceRebuild && !book.coverBmpPath.empty()) {
          const std::string thumb = UITheme::getCoverThumbPath(book.coverBmpPath, BOOKSHELF_COVER_HEIGHT);
          if (!isValidBookThumbnail(thumb)) {
            const size_t coverBytes = epub.getCoverImageSize();
            if (coverBytes > MAX_MANUAL_THUMBNAIL_SOURCE_BYTES) {
              LOG_DBG("SHELF", "Skipping oversized manual thumbnail source (%lu bytes): %s",
                      static_cast<unsigned long>(coverBytes), book.path.c_str());
            } else if (coverBytes == 0) {
              // No cover reference (or an unresolvable reference). Keep the
              // lightweight metadata and let the shelf use its placeholder.
              LOG_DBG("SHELF", "No usable EPUB cover for Recent: %s", book.path.c_str());
            } else {
              Storage.remove(thumb.c_str());
              if (!epub.generateThumbBmp(BOOKSHELF_COVER_HEIGHT) || !isValidBookThumbnail(thumb)) {
                LOG_ERR("SHELF", "Could not regenerate EPUB thumbnail: %s", book.path.c_str());
              }
            }
          }
          if (!refreshCarouselHqPath.empty() &&
              UITheme::getCoverThumbPath(book.coverBmpPath, CAROUSEL_HQ_COVER_HEIGHT) == refreshCarouselHqPath) {
            generateCarouselHqThumbnail(book, true);
          }
        }
      }
    } else if (FsHelpers::hasXtcExtension(book.path)) {
      Xtc xtc(book.path, "/.crosspoint");
      if (xtc.load()) {
        attempted = true;
        const std::string title = xtc.getTitle().empty() ? book.title : xtc.getTitle();
        const std::string author = xtc.getAuthor();
        const std::string cover = xtc.getThumbBmpPath();
        if (book.title != title || book.author != author || book.coverBmpPath != cover) {
          book.title = title;
          book.author = author;
          book.coverBmpPath = cover;
          RECENT_BOOKS.updateBook(book.path, book.title, book.author, book.coverBmpPath);
        }
        const std::string thumb = UITheme::getCoverThumbPath(book.coverBmpPath, BOOKSHELF_COVER_HEIGHT);
        if (forceRebuild && !isValidBookThumbnail(thumb)) {
          Storage.remove(thumb.c_str());
          if (!xtc.generateThumbBmp(BOOKSHELF_COVER_HEIGHT) || !isValidBookThumbnail(thumb)) {
            LOG_ERR("SHELF", "Could not regenerate XTC thumbnail: %s", book.path.c_str());
          }
        }
        if (forceRebuild && !refreshCarouselHqPath.empty() &&
            UITheme::getCoverThumbPath(book.coverBmpPath, CAROUSEL_HQ_COVER_HEIGHT) == refreshCarouselHqPath) {
          generateCarouselHqThumbnail(book, true);
        }
      }
    } else if (FsHelpers::hasCbzExtension(book.path)) {
      Cbz cbz(book.path, "/.crosspoint");
      if (cbz.loadMetadataOnly()) {
        attempted = true;
        const std::string title = cbz.getTitle().empty() ? book.title : cbz.getTitle();
        const std::string author = cbz.getAuthor();
        const std::string cover = cbz.getThumbBmpPath();
        const std::string synopsis = cbz.getSynopsis().empty() ? book.synopsis : cbz.getSynopsis().substr(0, 384);
        const std::string thumb = UITheme::getCoverThumbPath(cover, BOOKSHELF_COVER_HEIGHT);
        logCbzCacheLookup(thumb, Storage.exists(thumb.c_str()));
        if (book.title != title || book.author != author || book.coverBmpPath != cover || book.synopsis != synopsis) {
          book.title = title;
          book.author = author;
          book.coverBmpPath = cover;
          book.synopsis = synopsis;
          RECENT_BOOKS.refreshBookMetadata(book.path, book.title, book.author, book.coverBmpPath, book.synopsis);
        }
        if (forceRebuild && !book.coverBmpPath.empty()) {
          const bool thumbValid = isValidBookThumbnail(thumb);
          if (!thumbValid) {
            if (!cbz.generateThumbBmp(BOOKSHELF_COVER_HEIGHT) || !isValidBookThumbnail(thumb)) {
              LOG_ERR("SHELF", "Could not generate CBZ thumbnail: %s", book.path.c_str());
            }
          }
          if (!refreshCarouselHqPath.empty() &&
              UITheme::getCoverThumbPath(book.coverBmpPath, CAROUSEL_HQ_COVER_HEIGHT) == refreshCarouselHqPath) {
            generateCarouselHqThumbnail(book, true);
          }
        }
      }
    }
    // A manual refresh targets one highlighted book. If its source cannot be
    // opened, stop here instead of scanning every Recent entry and making the
    // shelf appear locked for a long time.
    if (forceRebuild && !attempted) break;
    if (attempted) {
      // A manual refresh changes only the selected book. Keep the in-memory
      // shelf snapshot when possible so the next frame does not decode all
      // eight covers again; the featured panel and selected card are redrawn.
      const bool hadShelfSnapshot = snapshotRestored;
      const size_t selectedPageStart = (selectedRecentIndex() / BOOKS_PER_PAGE) * BOOKS_PER_PAGE;
      const bool keepShelfSnapshot = manualSingleRefresh && hadShelfSnapshot && snapshotPageStart == selectedPageStart;
      if (keepShelfSnapshot) {
        snapshotRestored = true;
        initialRenderPending = false;
      } else {
        snapshotRestored = false;
        if (Storage.exists(FOLIO_HOME_SNAPSHOT)) Storage.remove(FOLIO_HOME_SNAPSHOT);
        initialRenderPending = true;
      }
      coverGenerationActive = false;
      refreshCarouselHqPath.clear();
      invalidateCarouselHqProbes();
      // Continue until every missing recent entry has been attempted during
      // first-run warmup. Manual refresh intentionally processes one book at
      // a time so the shelf stays responsive.
      coverGenerationRequested = recentCacheWarmupActive;
      if (recentCacheWarmupActive) recentCacheWarmupNextMs = millis() + 500;
      requestUpdate();
      return;  // At most one expensive extraction per activity cycle.
    }
  }
  coverGenerationActive = false;
  coverGenerationRequested = false;
  if (recentCacheWarmupActive) {
    recentCacheWarmupActive = false;
    recentCacheWarmupPopupRendered = false;
    // Do not repeat the SD probe on every Recent visit. A failed/missing
    // source is still safe to retry through the explicit Refresh action.
    Storage.writeFile(RECENT_CACHE_BOOTSTRAP_MARKER, String("1"));
  }
  requestUpdate();
}

bool RecentBooksActivity::generateCarouselHqThumbnail(const RecentBook& book, const bool force) {
  if (book.coverBmpPath.empty()) return false;

  const std::string output = UITheme::getCoverThumbPath(book.coverBmpPath, CAROUSEL_HQ_COVER_HEIGHT);
  if (!force && isValidBookThumbnail(output)) return true;
  if (Storage.exists(output.c_str())) Storage.remove(output.c_str());

  bool attempted = false;
  bool generated = false;
  if (FsHelpers::hasEpubExtension(book.path)) {
    Epub epub(book.path, "/.crosspoint");
    if (epub.loadMetadataOnly()) {
      attempted = true;
      const size_t coverBytes = epub.getCoverImageSize();
      if (coverBytes == 0) {
        LOG_DBG("SHELF", "No usable EPUB cover for Carousel HQ: %s", book.path.c_str());
      } else if (coverBytes > MAX_MANUAL_THUMBNAIL_SOURCE_BYTES) {
        LOG_DBG("SHELF", "Skipping oversized Carousel HQ source (%lu bytes): %s",
                static_cast<unsigned long>(coverBytes), book.path.c_str());
      } else {
        generated = epub.generateThumbBmp(CAROUSEL_HQ_COVER_HEIGHT);
      }
    }
  } else if (FsHelpers::hasXtcExtension(book.path)) {
    Xtc xtc(book.path, "/.crosspoint");
    if (xtc.load()) {
      attempted = true;
      generated = xtc.generateThumbBmp(CAROUSEL_HQ_COVER_HEIGHT);
    }
  } else if (FsHelpers::hasCbzExtension(book.path)) {
    Cbz cbz(book.path, "/.crosspoint");
    if (cbz.loadMetadataOnly()) {
      attempted = true;
      generated = cbz.generateThumbBmp(CAROUSEL_HQ_COVER_HEIGHT);
    }
  }

  if (!attempted || !generated || !isValidBookThumbnail(output)) {
    LOG_ERR("SHELF", "Could not prepare Carousel HQ thumbnail: %s", book.path.c_str());
    if (Storage.exists(output.c_str()) && !isValidBookThumbnail(output)) Storage.remove(output.c_str());
    return false;
  }
  return true;
}

void RecentBooksActivity::startCarouselHqPreparation() {
  // An explicit preparation pass is also the invalidation boundary for files
  // created or replaced since the last Carousel frame.
  invalidateCarouselHqProbes();
  carouselHqQueue.clear();
  carouselHqQueueIndex = 0;
  carouselHqPreparationActive = false;
  carouselHqPopupRendered = false;
  carouselHqCompletionPopupPending = false;

  std::vector<std::string> seenPaths;
  seenPaths.reserve(recentBooks.size());
  for (size_t index = 0; index < recentBooks.size(); ++index) {
    const RecentBook& book = recentBooks[index];
    if (book.coverBmpPath.empty() || RecentBooksStore::isMissing(book)) continue;
    if (!FsHelpers::hasEpubExtension(book.path) && !FsHelpers::hasXtcExtension(book.path) &&
        !FsHelpers::hasCbzExtension(book.path)) {
      continue;
    }
    if (std::find(seenPaths.begin(), seenPaths.end(), book.path) != seenPaths.end()) continue;
    seenPaths.push_back(book.path);

    const std::string hqPath = UITheme::getCoverThumbPath(book.coverBmpPath, CAROUSEL_HQ_COVER_HEIGHT);
    if (!isValidBookThumbnail(hqPath)) carouselHqQueue.push_back(index);
  }

  if (carouselHqQueue.empty()) {
    LOG_DBG("SHELF", "Carousel HQ preparation found no missing covers");
    // This menu action is confirmed on press, while normal book opening is
    // handled on the matching release. Consume that release and show a
    // completion message instead of opening the selected book.
    swallowBookConfirmRelease = true;
    longPressActionShown = true;
    carouselHqCompletionPopupPending = true;
    requestUpdate(true);
    return;
  }

  carouselHqPreparationActive = true;
  requestUpdate(true);
}

void RecentBooksActivity::prepareNextCarouselCover() {
  if (!carouselHqPreparationActive) return;
  if (carouselHqQueueIndex >= carouselHqQueue.size()) {
    carouselHqPreparationActive = false;
    carouselHqPopupRendered = false;
    carouselHqQueue.clear();
    requestUpdate(true);
    return;
  }
  if (!carouselHqPopupRendered) return;

  carouselHqPopupRendered = false;
  requestUpdateAndWait();
  const RecentBook& book = recentBooks[carouselHqQueue[carouselHqQueueIndex]];
  const bool success = generateCarouselHqThumbnail(book, true);
  if (!success) LOG_DBG("SHELF", "Carousel HQ fallback remains available: %s", book.path.c_str());
  invalidateCarouselHqProbes();
  ++carouselHqQueueIndex;
  if (carouselHqQueueIndex >= carouselHqQueue.size()) {
    carouselHqPreparationActive = false;
    carouselHqPopupRendered = false;
    carouselHqQueue.clear();
  }
  requestUpdate(true);
}

void RecentBooksActivity::showMenu() {
  std::vector<std::string> options = {tr(STR_FILE_TRANSFER), tr(STR_SETTINGS_TITLE), tr(STR_CLOCK_WEATHER),
                                      tr(STR_TODO_LIST), tr(STR_READING_STATS),
                                      "Bookmarks (all books)", "Clippings (all books)"};
  const bool carouselTheme = usesCarouselLayout();
  const int prepareCarouselCoversIndex = static_cast<int>(options.size());
  if (carouselTheme) options.emplace_back(tr(STR_PREPARE_CAROUSEL_COVERS));
  menuPopup.show(StrId::STR_MENU, options, 0, [this, carouselTheme, prepareCarouselCoversIndex](const int index) {
    // Always close the menu before starting another activity. This is
    // especially important for Settings, which replaces the shelf activity.
    menuPopup.dismiss();
    if (index == 0) {
      activityManager.goToFileTransfer();
    } else if (index == 1) {
      activityManager.goToSettings();
    } else if (index == 2) {
      startActivityForResult(std::make_unique<ClockWeatherActivity>(renderer, mappedInput), nullptr);
    } else if (index == 3) {
      startActivityForResult(std::make_unique<ToDoListActivity>(renderer, mappedInput), nullptr);
    } else if (index == 4) {
      startActivityForResult(std::make_unique<ReadingStatsActivity>(renderer, mappedInput), nullptr);
    } else if (index == 5) {
      startActivityForResult(
          std::make_unique<EpubReaderBookmarksActivity>(renderer, mappedInput, std::string{}, true),
          [this](const ActivityResult& result) {
            if (result.isCancelled) return;
            const auto* bookmark = std::get_if<ProgressChangeResult>(&result.data);
            if (bookmark && !bookmark->bookPath.empty()) {
              activityManager.goToReaderAtBookmark(bookmark->bookPath, *bookmark);
            }
          });
    } else if (index == 6) {
      startActivityForResult(std::make_unique<EpubReaderClippingListActivity>(
                                renderer, mappedInput, std::string{}, "All books", true),
                            nullptr);
    } else if (carouselTheme && index == prepareCarouselCoversIndex) {
      startCarouselHqPreparation();
    }
  });
  requestUpdate();
}

void RecentBooksActivity::showBookActions() {
  if (visibleBookCount == 0 || selectorIndex >= visibleBookCount) return;
  const RecentBook selectedBook = recentBooks[selectedRecentIndex()];
  std::vector<std::string> actions = {tr(STR_OPEN), tr(STR_MARK_READING), tr(STR_MARK_ON_HOLD), tr(STR_FINISHED),
                                      tr(STR_RESET_PROGRESS), tr(STR_REFRESH_BOOK_CACHE),
                                      tr(STR_DELETE_CACHE), tr(STR_REMOVE_FROM_LIST),
                                      tr(STR_READ_FULL_SYNOPSIS), tr(STR_BOOK_STATISTICS)};
  if (FsHelpers::hasEpubExtension(selectedBook.path)) {
    actions.emplace_back(tr(STR_BOOKMARKS));
    actions.emplace_back(tr(STR_CLIPPINGS));
  }
  bookActionsPopup.show(StrId::STR_BOOK_ACTIONS, actions, 0, [this](const int action) {
    if (visibleBookCount == 0 || selectorIndex >= visibleBookCount) return;
    const RecentBook selected = recentBooks[selectedRecentIndex()];
    if (action == 0) {
      logCbzPath("recent-book-selection", selected.path);
      onSelectBook(selected.path);
      return;
    }
    if (action == 1 || action == 2) {
      BOOK_STATES.setStatus(selected.path, action == 1 ? BookStatus::Reading : BookStatus::OnHold);
      if (selected.progressPercent >= 100) RECENT_BOOKS.recordReading(selected.path, 99, 0);
    }
    if (action == 3) {
      BOOK_STATES.setStatus(selected.path, BookStatus::Finished);
      RECENT_BOOKS.recordReading(selected.path, 100, 0);
    }
    if (action == 4) {
      BOOK_STATES.reset(selected.path);
      resetBookProgress(selected.path);
      RECENT_BOOKS.recordReading(selected.path, 0, 0);
    }
    if (action == 5) {
      // Refresh only the lightweight bookshelf metadata and thumbnail. Do not
      // clear the reader cache: progress, bookmarks, clippings, and cached
      // reading pages must remain usable while the EPUB is reread.
      refreshCarouselHqPath.clear();
      if (!selected.coverBmpPath.empty()) {
        const std::string hqPath = UITheme::getCoverThumbPath(selected.coverBmpPath, CAROUSEL_HQ_COVER_HEIGHT);
        if (Storage.exists(hqPath.c_str())) refreshCarouselHqPath = hqPath;
      }
      nextCoverToGenerate = selectedRecentIndex();
      coverGenerationActive = false;
      coverGenerationRequested = false;
      manualSingleRefresh = true;
      retrievingBookCache = true;
      retrievingBookCacheProgress = 5;
      retrievingBookCacheIndex = selectorIndex;
      retrievingBookCachePopupRendered = false;
    }
    if (action == 6) {
      // Preserve progress, but force CBZ reader pages to be rebuilt.
      clearBookCache(selected.path);
    }
    if (action == 7) {
      RECENT_BOOKS.removeByPath(selected.path);
      BOOK_STATES.removeByPath(selected.path);
    }
    if (action == 8) {
      startActivityForResult(
          std::make_unique<SynopsisActivity>(renderer, mappedInput, selected.title, selected.author, selected.synopsis,
                                             selected.path),
          nullptr);
      return;
    }
    if (action == 9) {
      startActivityForResult(std::make_unique<ReadingStatsActivity>(renderer, mappedInput, selected.path), nullptr);
      return;
    }
    if (action == 10 && FsHelpers::hasEpubExtension(selected.path)) {
      startActivityForResult(
          std::make_unique<EpubReaderBookmarksActivity>(renderer, mappedInput, selected.path),
          [this](const ActivityResult& result) {
            if (result.isCancelled) return;
            const auto* bookmark = std::get_if<ProgressChangeResult>(&result.data);
            if (bookmark && !bookmark->bookPath.empty()) {
              activityManager.goToReaderAtBookmark(bookmark->bookPath, *bookmark);
            }
          });
      return;
    }
    if (action == 11 && FsHelpers::hasEpubExtension(selected.path)) {
      startActivityForResult(std::make_unique<EpubReaderClippingListActivity>(
                                renderer, mappedInput, selected.path, selected.title),
                            nullptr);
      return;
    }
    loadRecentBooks();
    rebuildVisibleBooks();
    snapshotRestored = false;
    lastRenderedSelectorIndex = SIZE_MAX;
    lastRenderedPageStart = SIZE_MAX;
    overlayFrameShown = false;
    if (Storage.exists(FOLIO_HOME_SNAPSHOT)) Storage.remove(FOLIO_HOME_SNAPSHOT);
    requestUpdate(true);
  });
  requestUpdate();
}

void RecentBooksActivity::onEnter() {
  Activity::onEnter();

  // Load data
  loadRecentBooks();
  rebuildVisibleBooks();

  selectorIndex = 0;
  nextCoverToGenerate = 0;
  coverGenerationRequested = false;
  manualSingleRefresh = false;
  retrievingBookCache = false;
  retrievingBookCacheProgress = 0;
  retrievingBookCacheIndex = SIZE_MAX;
  retrievingBookCachePopupRendered = false;
  carouselHqQueue.clear();
  carouselHqQueueIndex = 0;
  carouselHqPreparationActive = false;
  carouselHqPopupRendered = false;
  carouselHqCompletionPopupPending = false;
  refreshCarouselHqPath.clear();
  // A pending write belongs to the previous shelf frame. Never carry it into
  // a new activity instance, where it could write unrelated framebuffer data
  // during the first interaction after boot.
  snapshotWritePending = false;
  snapshotWriteRequestedMs = 0;
  // Probe the persisted cache only once after install/update. This catches a
  // retained recent.json whose title fields survived while metadata.bin or a
  // thumbnail was removed, without adding SD work to ordinary Recent visits.
  const bool cacheBootstrapPending = !Storage.exists(RECENT_CACHE_BOOTSTRAP_MARKER);
  // Defer the potentially blocking probe until the shelf has rendered its
  // feedback popup. An empty Recent list has nothing to bootstrap.
  recentCacheBootstrapProbePending = cacheBootstrapPending && !recentBooks.empty();
  recentCacheWarmupActive = recentCacheBootstrapProbePending;
  if (cacheBootstrapPending && !recentCacheBootstrapProbePending) {
    Storage.writeFile(RECENT_CACHE_BOOTSTRAP_MARKER, String("1"));
  }
  recentCacheWarmupNextMs = 0;
  recentCacheWarmupPopupRendered = false;
  coverGenerationRequested = false;
  snapshotRestored = restoreSnapshot();
  if (!snapshotRestored) {
    snapshotPageStart = SIZE_MAX;
    snapshotSelectorIndex = SIZE_MAX;
  }
  snapshotFastPathUsed = false;
  lastRenderedSelectorIndex = SIZE_MAX;
  lastRenderedPageStart = SIZE_MAX;
  lastRenderedTab = 0;
  lastFeaturedPath.clear();
  lastFeaturedCoverPath.clear();
  overlayFrameShown = false;
  initialRenderPending = true;
  requestUpdate();
}

void RecentBooksActivity::onExit() {
  Activity::onExit();
  recentBooks.clear();
  // ActivityManager calls onExit() while holding RenderLock. Keep cache
  // destruction on the already-locked path so teardown cannot deadlock while
  // still guaranteeing no render is using a cached source.
  invalidateCarouselHqProbesLocked();
}

void RecentBooksActivity::loop() {
  const int pageItems = activePageItems();
  const auto& metrics = UITheme::getInstance().getMetrics();
  const bool carouselTheme = usesCarouselLayout();
  const bool standaloneCarousel = SETTINGS.uiTheme == CrossPointSettings::UI_THEME::CAROUSEL;
  const bool snapshotLayout = usesFourByTwoGrid();
  const bool threeCoverGrid = usesThreeCoverGrid();
  FolioShelfLayout layout{};
  if (carouselTheme) {
    static const FolioNooirTheme folioPresentation;
    layout = folioPresentation.shelfLayout(renderer, metrics);
  } else {
    const auto& folioTheme = static_cast<const FolioNooirTheme&>(GUI);
    layout = folioTheme.shelfLayout(renderer, metrics);
  }

  // Snapshot persistence is an optimization, not part of showing the shelf.
  // Defer the SD write until the user has been idle so the first boot render
  // and the first button press do not wait behind a 48 KB filesystem write.
  if (snapshotLayout && snapshotWritePending && millis() - snapshotWriteRequestedMs >= 500 &&
      !mappedInput.wasAnyPressed() && !mappedInput.wasAnyReleased() && !mappedInput.isPressed(MappedInputManager::Button::Confirm) &&
      !mappedInput.isPressed(MappedInputManager::Button::Back) && !menuPopup.isActive() &&
      !bookActionsPopup.isActive() && !retrievingBookCache && !recentCacheWarmupActive) {
    snapshotWritePending = false;
    RenderLock lock;
    // The on-screen frame has a focus outline, but the persisted base frame
    // must not contain it (or a popup overlay). Erase only that outline while
    // writing, then restore it in the in-memory framebuffer without another
    // panel refresh.
    bool focusOutlineErased = false;
    if (visibleBookCount > 0) {
      const int columns = layout.columns;
      const int gap = layout.gridGap;
      const int cardWidth = layout.cardWidth;
      const int cardHeight = layout.cardHeight;
      const size_t slot = selectorIndex % BOOKS_PER_PAGE;
      const int x = gap + static_cast<int>(slot % columns) * (cardWidth + gap);
      const int y = layout.gridTop + static_cast<int>(slot / columns) * cardHeight;
      const int coverHeight = std::max(40, std::min(cardHeight - 27, cardWidth * 3 / 2));
      renderer.drawRect(x - 3, y - 3, cardWidth + 6, coverHeight + 6, false);
      focusOutlineErased = true;
    }
    writeSnapshot();
    if (focusOutlineErased && visibleBookCount > 0) {
      const int columns = layout.columns;
      const int gap = layout.gridGap;
      const int cardWidth = layout.cardWidth;
      const int cardHeight = layout.cardHeight;
      const size_t slot = selectorIndex % BOOKS_PER_PAGE;
      const int x = gap + static_cast<int>(slot % columns) * (cardWidth + gap);
      const int y = layout.gridTop + static_cast<int>(slot / columns) * cardHeight;
      const int coverHeight = std::max(40, std::min(cardHeight - 27, cardWidth * 3 / 2));
      renderer.drawRect(x - 3, y - 3, cardWidth + 6, coverHeight + 6);
    }
  }

  const bool bookPopupActive = bookActionsPopup.isActive();
  const bool bookPopupConfirm = bookPopupActive && mappedInput.wasPressed(MappedInputManager::Button::Confirm);
  const bool bookPopupBack = bookPopupActive && mappedInput.wasPressed(MappedInputManager::Button::Back);
  if (bookActionsPopup.handleInput(mappedInput, [this] { requestUpdate(); })) {
    if (bookPopupConfirm) swallowBookConfirmRelease = true;
    if (bookPopupBack) swallowBookBackRelease = true;
    return;
  }
  if (swallowBookBackRelease) {
    if (mappedInput.wasReleased(MappedInputManager::Button::Back)) swallowBookBackRelease = false;
    return;
  }

  // A child activity can consume the Confirm release that opened it. Clear
  // the long-press guard when the shelf becomes active again, otherwise the
  // next long press is incorrectly treated as the original one.
  if (!bookActionsPopup.isActive() && !mappedInput.isPressed(MappedInputManager::Button::Confirm) &&
      !mappedInput.wasReleased(MappedInputManager::Button::Confirm)) {
    longPressActionShown = false;
    swallowBookConfirmRelease = false;
  }

  if (retrievingBookCache && mappedInput.wasPressed(MappedInputManager::Button::Back)) {
    if (visibleBookCount > 0 && retrievingBookCacheIndex < visibleBookCount) {
      clearBookCache(recentBooks[selectedRecentIndex()].path);
    }
    retrievingBookCache = false;
    retrievingBookCachePopupRendered = false;
    retrievingBookCacheProgress = 0;
    coverGenerationRequested = false;
    manualSingleRefresh = false;
    requestUpdate(true);
    return;
  }

  if (retrievingBookCache) {
    // Popup-first: wait for the visible retrieval dialog instead of using a
    // fixed timeout fallback that could start ZIP/image work before feedback
    // reached a slower panel.
    if (!retrievingBookCachePopupRendered) return;
    retrievingBookCacheProgress = 35;
    requestUpdateAndWait();
    const unsigned long retrievalWorkStartedMs = millis();
    const size_t selectedIndex = selectedRecentIndex();
    const std::string selectedPath = selectedIndex < recentBooks.size() ? recentBooks[selectedIndex].path : std::string{};
    retrievingBookCache = false;
    retrievingBookCachePopupRendered = false;
    coverGenerationRequested = true;
    // Keep the retrieval dialog as the last complete frame while the
    // synchronous ZIP/thumbnail work runs. This prevents slower X3 panels
    // from appearing frozen or showing an empty cover during extraction.
    generateNextCover();
    LOG_DBG("PERF", "Metadata retrieval book=%s elapsed=%lums", selectedPath.c_str(),
            static_cast<unsigned long>(millis() - retrievalWorkStartedMs));
    retrievingBookCacheProgress = 100;
    requestUpdate();
    return;
  }

  if (carouselHqPreparationActive && mappedInput.wasPressed(MappedInputManager::Button::Back)) {
    carouselHqPreparationActive = false;
    carouselHqPopupRendered = false;
    carouselHqQueue.clear();
    carouselHqQueueIndex = 0;
    requestUpdate(true);
    return;
  }

  if (carouselHqPreparationActive) {
    prepareNextCarouselCover();
    return;
  }

  if (visibleBookCount > 0 && mappedInput.isPressed(MappedInputManager::Button::Confirm) &&
      mappedInput.getHeldTime() >= 1000 && !longPressActionShown) {
    longPressActionShown = true;
    swallowBookConfirmRelease = true;
    showBookActions();
    return;
  }

  const bool menuWasActive = menuPopup.isActive();
  const bool menuBackPressed =
      menuWasActive && mappedInput.wasPressed(MappedInputManager::Button::Back);
  if (menuPopup.handleInput(mappedInput, [this] { requestUpdate(); })) {
    if (menuBackPressed) swallowMenuBackRelease = true;
    return;
  }

  // OptionPopup dismisses on press while this activity opens the menu on
  // release. Consume the matching release so one press cannot close and then
  // immediately reopen the menu.
  if (swallowMenuBackRelease) {
    if (mappedInput.wasReleased(MappedInputManager::Button::Back)) swallowMenuBackRelease = false;
    return;
  }

  if (mappedInput.wasReleased(MappedInputManager::Button::Confirm)) {
    longPressActionShown = false;
    if (swallowBookConfirmRelease) {
      swallowBookConfirmRelease = false;
      return;
    }
    if (visibleBookCount > 0 && selectorIndex < visibleBookCount) {
      const RecentBook& selected = recentBooks[selectedRecentIndex()];
      LOG_DBG("RBA", "Selected recent book: %s", selected.path.c_str());
      logCbzPath("recent-book-selection", selected.path);
      onSelectBook(selected.path);
      return;
    }
  }

  const bool previousTab = mappedInput.wasReleased(MappedInputManager::Button::Left);
  const bool nextTab = mappedInput.wasReleased(MappedInputManager::Button::Right);
  if (previousTab) {
    // Third button: always return to the Library, regardless of whether the
    // current shelf is Recent or Finished.
    activityManager.goToFileBrowser("/");
    return;
  }
  if (nextTab) {
    // Fourth button: switch directly between Recent and Finished.
    activeTab = activeTab == 2 ? 1 : 2;
    selectorIndex = 0;
    rebuildVisibleBooks();
    snapshotRestored = false;
    snapshotPageStart = SIZE_MAX;
    snapshotSelectorIndex = SIZE_MAX;
    lastRenderedSelectorIndex = SIZE_MAX;
    lastRenderedPageStart = SIZE_MAX;
    lastFeaturedPath.clear();
    lastFeaturedCoverPath.clear();
    overlayFrameShown = false;
    initialRenderPending = true;
    requestUpdate();
    return;
  }

  int touchX = 0;
  int touchY = 0;
  if (carouselTheme) {
    std::array<CoverStackSlot, 5> stackSlots{};
    if (standaloneCarousel) {
      const int synopsisLineHeight = std::max(1, renderer.getLineHeight(SMALL_FONT_ID));
      int synopsisLineCount = 5;
      if (visibleBookCount > 0) {
        const RecentBook& layoutSelected = recentBooks[selectedRecentIndex()];
        const std::string layoutSynopsisPreview = SynopsisPreview::firstWords(layoutSelected.synopsis);
        const char* layoutSynopsisText =
            layoutSynopsisPreview.empty() ? tr(STR_NO_SYNOPSIS) : layoutSynopsisPreview.c_str();
        const auto layoutSynopsisLines =
            renderer.wrappedText(SMALL_FONT_ID, layoutSynopsisText, std::max(1, renderer.getScreenWidth() - 48), 5);
        synopsisLineCount = std::clamp(static_cast<int>(layoutSynopsisLines.size()), 1, 5);
      }
      static const CarouselTheme carouselPresentation;
      const auto carouselLayout =
          carouselPresentation.carouselLayout(renderer, synopsisLineCount, synopsisLineHeight, layout.statsTop);
      stackSlots = activeBookLayout() == CrossPointSettings::FOLIO_LAYOUT_THREE_COVER_CAROUSEL
                       ? CoverStackGeometry::layoutThree(renderer.getScreenWidth(), visibleBookCount,
                                                         selectorIndex, carouselLayout.centerHeight)
                       : CoverStackGeometry::layout(renderer.getScreenWidth(), visibleBookCount, selectorIndex,
                                                    carouselLayout.centerHeight);
    } else {
      stackSlots = CoverStackGeometry::layoutFolioShelf(renderer.getScreenWidth(), layout.gridTop, layout.gridHeight,
                                                        visibleBookCount, selectorIndex,
                                                        activeBookLayout() ==
                                                            CrossPointSettings::FOLIO_LAYOUT_THREE_COVER_CAROUSEL);
    }
    if (mappedInput.wasScreenTouchDown(touchX, touchY)) {
      if (touchY >= metrics.topPadding && touchY < layout.contentTop) {
        const uint8_t touchedTab =
            static_cast<uint8_t>(std::min(2, std::max(0, touchX * 3 / renderer.getScreenWidth())));
        if (touchedTab == 0) {
          activityManager.goToFileBrowser("/");
          return;
        }
        activeTab = touchedTab;
        selectorIndex = 0;
        rebuildVisibleBooks();
        snapshotRestored = false;
        snapshotPageStart = SIZE_MAX;
        snapshotSelectorIndex = SIZE_MAX;
        lastRenderedSelectorIndex = SIZE_MAX;
        lastRenderedPageStart = SIZE_MAX;
        overlayFrameShown = false;
        initialRenderPending = true;
        requestUpdate();
        return;
      }
      for (int slot = static_cast<int>(stackSlots.size()) - 1; slot >= 0; --slot) {
        const auto& stackSlot = stackSlots[static_cast<size_t>(slot)];
        if (!stackSlot.valid) continue;
        const auto& rect = stackSlot.rect;
        if (touchX >= rect.x && touchX < rect.x + rect.width && touchY >= rect.y &&
            touchY < rect.y + rect.height) {
          if (selectorIndex != stackSlot.itemIndex) {
            selectorIndex = stackSlot.itemIndex;
            requestUpdate();
          }
          return;
        }
      }
    }
    for (int slot = static_cast<int>(stackSlots.size()) - 1; slot >= 0; --slot) {
      const auto& stackSlot = stackSlots[static_cast<size_t>(slot)];
      if (!stackSlot.valid) continue;
      const auto& rect = stackSlot.rect;
      if (mappedInput.wasTapInRect(rect.x, rect.y, rect.width, rect.height)) {
        selectorIndex = stackSlot.itemIndex;
        const std::string& path = recentBooks[visibleBookIndexes[selectorIndex]].path;
        logCbzPath("recent-book-selection", path);
        onSelectBook(path);
        return;
      }
    }
  } else {
    const int columns = threeCoverGrid ? 3 : layout.columns;
    const int gap = threeCoverGrid ? 10 : layout.gridGap;
    const int cardWidth = threeCoverGrid ? (renderer.getScreenWidth() - gap * (columns + 1)) / columns : layout.cardWidth;
    const int gridTop = layout.gridTop;
    const int cardHeight = threeCoverGrid ? layout.gridHeight : layout.cardHeight;
    const int pageItems = threeCoverGrid ? 3 : BOOKS_PER_PAGE;
    const size_t pageStart = (selectorIndex / pageItems) * pageItems;
    if (mappedInput.wasScreenTouchDown(touchX, touchY)) {
      if (touchY >= metrics.topPadding && touchY < layout.contentTop) {
        const uint8_t touchedTab =
            static_cast<uint8_t>(std::min(2, std::max(0, touchX * 3 / renderer.getScreenWidth())));
        if (touchedTab == 0) {
          activityManager.goToFileBrowser("/");
          return;
        }
        activeTab = touchedTab;
        selectorIndex = 0;
        rebuildVisibleBooks();
        snapshotRestored = false;
        snapshotPageStart = SIZE_MAX;
        snapshotSelectorIndex = SIZE_MAX;
        lastRenderedSelectorIndex = SIZE_MAX;
        lastRenderedPageStart = SIZE_MAX;
        overlayFrameShown = false;
        initialRenderPending = true;
        requestUpdate();
        return;
      }
      for (int slot = 0; slot < pageItems; ++slot) {
        const size_t index = pageStart + static_cast<size_t>(slot);
        if (index >= visibleBookCount) break;
        const int cardX = gap + (slot % columns) * (cardWidth + gap);
        const int cardY = gridTop + (slot / columns) * cardHeight;
        if (touchX >= cardX && touchX < cardX + cardWidth && touchY >= cardY && touchY < cardY + cardHeight) {
          if (selectorIndex != index) {
            selectorIndex = index;
            requestUpdate();
          }
          return;
        }
      }
    }
    for (int slot = 0; slot < pageItems; ++slot) {
      const size_t index = pageStart + static_cast<size_t>(slot);
      if (index >= visibleBookCount) break;
      const int cardX = gap + (slot % columns) * (cardWidth + gap);
      const int cardY = gridTop + (slot / columns) * cardHeight;
      if (mappedInput.wasTapInRect(cardX, cardY, cardWidth, cardHeight)) {
        selectorIndex = index;
        const std::string& path = recentBooks[visibleBookIndexes[index]].path;
        logCbzPath("recent-book-selection", path);
        onSelectBook(path);
        return;
      }
    }
  }

  if (mappedInput.wasReleased(MappedInputManager::Button::Back)) {
    if (SETTINGS.uiTheme == CrossPointSettings::UI_THEME::FOLIO_NOOIR || carouselTheme)
      showMenu();
    else
      onGoHome();
  }

  int listSize = visibleBookCount;
  const auto swipe = mappedInput.wasSwipe();
  if (swipe == MappedInputManager::SwipeDir::Up) {
    selectorIndex = carouselTheme ? ButtonNavigator::nextIndex(static_cast<int>(selectorIndex), listSize)
                                  : ButtonNavigator::nextPageIndex(static_cast<int>(selectorIndex), listSize, pageItems);
    requestUpdate();
    return;
  }
  if (swipe == MappedInputManager::SwipeDir::Down) {
    selectorIndex = carouselTheme ? ButtonNavigator::previousIndex(static_cast<int>(selectorIndex), listSize)
                                  : ButtonNavigator::previousPageIndex(static_cast<int>(selectorIndex), listSize, pageItems);
    requestUpdate();
    return;
  }

  buttonNavigator.onNextRelease([this, listSize] {
    selectorIndex = ButtonNavigator::nextIndex(static_cast<int>(selectorIndex), listSize);
    requestUpdate();
  });

  buttonNavigator.onPreviousRelease([this, listSize] {
    selectorIndex = ButtonNavigator::previousIndex(static_cast<int>(selectorIndex), listSize);
    requestUpdate();
  });

  buttonNavigator.onNextContinuous([this, listSize, pageItems, carouselTheme] {
    selectorIndex = carouselTheme ? ButtonNavigator::nextIndex(static_cast<int>(selectorIndex), listSize)
                                  : ButtonNavigator::nextPageIndex(static_cast<int>(selectorIndex), listSize, pageItems);
    requestUpdate();
  });

  buttonNavigator.onPreviousContinuous([this, listSize, pageItems, carouselTheme] {
    selectorIndex = carouselTheme ? ButtonNavigator::previousIndex(static_cast<int>(selectorIndex), listSize)
                                  : ButtonNavigator::previousPageIndex(static_cast<int>(selectorIndex), listSize, pageItems);
    requestUpdate();
  });

  if (recentCacheWarmupActive) {
    if (!recentCacheWarmupPopupRendered) return;
    // The first frame must be visible before probing the SD card. Otherwise
    // the initial cache check can make the device look frozen with no feedback.
    if (recentCacheBootstrapProbePending) {
      if (mappedInput.wasAnyPressed() || mappedInput.wasAnyReleased()) {
        recentCacheWarmupNextMs = millis() + 500;
        return;
      }
      recentCacheBootstrapProbePending = false;
      recentCacheWarmupActive = hasMissingRecentCache();
      if (!recentCacheWarmupActive) {
        coverGenerationRequested = false;
        Storage.writeFile(RECENT_CACHE_BOOTSTRAP_MARKER, String("1"));
        recentCacheWarmupPopupRendered = false;
        requestUpdate(true);
        return;
      }
      coverGenerationRequested = true;
      recentCacheWarmupNextMs = millis() + 500;
      requestUpdate(true);
      return;
    }
    if (mappedInput.wasAnyPressed() || mappedInput.wasAnyReleased()) {
      recentCacheWarmupNextMs = millis() + 500;
      return;
    }
    if (millis() < recentCacheWarmupNextMs) return;
    if (coverGenerationRequested) generateNextCover();
  } else if (coverGenerationRequested) {
    generateNextCover();
  }

  // Initial recent-cache extraction is deliberately limited to one book per
  // activity cycle so Home remains usable while the popup is visible.
}

void RecentBooksActivity::promptRemoveBook(const std::string& path, const std::string& title) {
  auto handler = [this, path](const ActivityResult& res) {
    if (res.isCancelled) {
      LOG_DBG("RBA", "Remove from recents cancelled");
      return;
    }
    if (RECENT_BOOKS.removeByPath(path)) {
      LOG_DBG("RBA", "Removed from recents: %s", path.c_str());
      loadRecentBooks();
      rebuildVisibleBooks();
      if (recentBooks.empty()) {
        selectorIndex = 0;
      } else if (selectorIndex >= visibleBookCount) {
        selectorIndex = visibleBookCount > 0 ? visibleBookCount - 1 : 0;
      }
      requestUpdate(true);
    }
  };

  startActivityForResult(
      std::make_unique<ConfirmationActivity>(renderer, mappedInput, tr(STR_REMOVE_FROM_RECENTS), title),
      std::move(handler));
}

#if 0
void RecentBooksActivity::render(RenderLock&&) {
  renderer.clearScreen();

  const auto pageWidth = renderer.getScreenWidth();
  const auto pageHeight = renderer.getScreenHeight();
  const auto& metrics = UITheme::getInstance().getMetrics();

  GUI.drawHeader(renderer, Rect{0, metrics.topPadding, pageWidth, metrics.headerHeight}, tr(STR_BOOKSHELF));

  const int contentTop = metrics.topPadding + metrics.headerHeight + metrics.verticalSpacing;
  const int contentHeight = pageHeight - contentTop - metrics.buttonHintsHeight - metrics.verticalSpacing;

  // Four-card bookshelf. Only cached BMP thumbnails are decoded; missing
  // thumbnails are generated one at a time from loop(), never inside render().
  if (recentBooks.empty()) {
    renderer.drawText(UI_10_FONT_ID, metrics.contentSidePadding, contentTop + 20, tr(STR_NO_RECENT_BOOKS));
  } else {
    constexpr int columns = 2;
    constexpr int gap = 10;
    const int cardWidth = (pageWidth - gap * 3) / columns;
    const int cardHeight = contentHeight / 2;
    const size_t pageStart = (selectorIndex / BOOKS_PER_PAGE) * BOOKS_PER_PAGE;
    for (int slot = 0; slot < BOOKS_PER_PAGE; ++slot) {
      const size_t index = pageStart + static_cast<size_t>(slot);
      if (index >= recentBooks.size()) break;
      const RecentBook& book = recentBooks[index];
      const int col = slot % columns;
      const int row = slot / columns;
      const int cardX = gap + col * (cardWidth + gap);
      const int cardY = contentTop + row * cardHeight;
      if (index == selectorIndex) renderer.drawRect(cardX, cardY, cardWidth, cardHeight - gap);

      const int coverHeight = std::min(BOOKSHELF_COVER_HEIGHT, cardHeight - 67);
      int coverWidth = coverHeight * 2 / 3;
      const std::string thumbPath = UITheme::getCoverThumbPath(book.coverBmpPath, BOOKSHELF_COVER_HEIGHT);
      HalFile file;
      bool drewCover = false;
      if (!book.coverBmpPath.empty() && Storage.openFileForRead("SHELF", thumbPath, file)) {
        Bitmap bitmap(file);
        if (bitmap.parseHeaders() == BmpReaderError::Ok && bitmap.getWidth() > 0 && bitmap.getHeight() > 0) {
          coverWidth = coverHeight * bitmap.getWidth() / bitmap.getHeight();
          coverWidth = std::min(coverWidth, cardWidth - 24);
          renderer.drawBitmap(bitmap, cardX + (cardWidth - coverWidth) / 2, cardY + 5, coverWidth, coverHeight);
          drewCover = true;
        }
      }
      if (!drewCover) {
        renderer.drawRect(cardX + (cardWidth - coverWidth) / 2, cardY + 5, coverWidth, coverHeight);
        const std::string placeholderTitle =
            renderer.truncatedText(UI_10_FONT_ID, book.title.c_str(), coverWidth - 12);
        const int placeholderX = cardX + (cardWidth - renderer.getTextWidth(UI_10_FONT_ID, placeholderTitle.c_str())) / 2;
        renderer.drawText(UI_10_FONT_ID, placeholderX, cardY + coverHeight / 2, placeholderTitle.c_str());
      }

      const int textY = cardY + coverHeight + 9;
      const std::string title = renderer.truncatedText(UI_10_FONT_ID, book.title.c_str(), cardWidth - 12);
      renderer.drawText(UI_10_FONT_ID, cardX + 6, textY, title.c_str(), true);
      char progress[24];
      snprintf(progress, sizeof(progress), "%u%% · %s", book.progressPercent,
               book.progressPercent >= 100 ? tr(STR_COMPLETE) : (book.progressPercent > 0 ? tr(STR_ONGOING) : tr(STR_NEW)));
      renderer.drawText(UI_10_FONT_ID, cardX + 6, textY + renderer.getLineHeight(UI_10_FONT_ID), progress);
      const int barY = cardY + cardHeight - gap - 7;
      renderer.drawRect(cardX + 6, barY, cardWidth - 12, 4);
      const int fillWidth = (cardWidth - 14) * book.progressPercent / 100;
      if (fillWidth > 0) renderer.fillRect(cardX + 7, barY + 1, fillWidth, 2);
    }
  }

  // Help text
  const auto labels = mappedInput.mapLabels(tr(STR_HOME), tr(STR_OPEN), tr(STR_DIR_UP), tr(STR_DIR_DOWN));
  GUI.drawButtonHints(renderer, labels.btn1, labels.btn2, labels.btn3, labels.btn4);

  renderer.displayBuffer();
}
#endif

void RecentBooksActivity::renderCarousel(const bool threeCover) {
  const int pageWidth = renderer.getScreenWidth();
  const auto& metrics = UITheme::getInstance().getMetrics();
  static const CarouselTheme carouselPresentation;
  const int synopsisLineHeight = std::max(1, renderer.getLineHeight(SMALL_FONT_ID));
  int synopsisLineCount = 5;
  if (visibleBookCount > 0) {
    const RecentBook& layoutSelected = recentBooks[selectedRecentIndex()];
    const std::string layoutSynopsisPreview = SynopsisPreview::firstWords(layoutSelected.synopsis);
    const char* layoutSynopsisText =
        layoutSynopsisPreview.empty() ? tr(STR_NO_SYNOPSIS) : layoutSynopsisPreview.c_str();
    const auto layoutSynopsisLines =
        renderer.wrappedText(SMALL_FONT_ID, layoutSynopsisText, std::max(1, pageWidth - 48), 5);
    synopsisLineCount = std::clamp(static_cast<int>(layoutSynopsisLines.size()), 1, 5);
  }
  // Carousel keeps Folio Nooir's existing header, battery, summary strip, and
  // controls. The presentation object is stateless; it is not the
  // active UITheme instance and therefore does not depend on an unsafe cast.
  static const FolioNooirTheme folioPresentation;
  const FolioShelfLayout folioLayout = folioPresentation.shelfLayout(renderer, metrics);
  const CarouselLayout carouselLayout =
      carouselPresentation.carouselLayout(renderer, synopsisLineCount, synopsisLineHeight, folioLayout.statsTop);
  folioPresentation.drawShelfTabs(renderer, folioLayout, activeTab);
  folioPresentation.drawShelfBattery(renderer, folioLayout, metrics);
  renderer.fillRect(carouselLayout.carouselRegion.x, carouselLayout.carouselRegion.y,
                    carouselLayout.carouselRegion.width, carouselLayout.carouselRegion.height, false);

  if (visibleBookCount == 0) {
    renderer.drawText(UI_10_FONT_ID, metrics.contentSidePadding, carouselLayout.carouselRegion.y + 45,
                      tr(STR_NO_RECENT_BOOKS));
  } else {
    const RecentBook& selected = recentBooks[selectedRecentIndex()];
    const std::string title = renderer.truncatedText(UI_12_FONT_ID, selected.title.c_str(),
                                                     carouselLayout.titleRegion.width);
    const std::string author = renderer.truncatedText(UI_10_FONT_ID, selected.author.c_str(),
                                                      carouselLayout.titleRegion.width);
    const int titleWidth = renderer.getTextWidth(UI_12_FONT_ID, title.c_str());
    const int authorWidth = renderer.getTextWidth(UI_10_FONT_ID, author.c_str());
    renderer.drawText(UI_12_FONT_ID,
                      carouselLayout.titleRegion.x + (carouselLayout.titleRegion.width - titleWidth) / 2,
                      carouselLayout.titleRegion.y, title.c_str(), true);
    renderer.drawText(UI_10_FONT_ID,
                      carouselLayout.titleRegion.x + (carouselLayout.titleRegion.width - authorWidth) / 2,
                      carouselLayout.titleRegion.y + 24, author.c_str());

    auto drawCoverOutline = [this](const CoverStackSlot& stackSlot) {
      const auto& rect = stackSlot.rect;
      const int maxHeight = std::max(stackSlot.leftHeight, stackSlot.rightHeight);
      const int rightX = rect.x + rect.width - 1;
      const int leftTop = rect.y + (maxHeight - stackSlot.leftHeight) / 2;
      const int rightTop = rect.y + (maxHeight - stackSlot.rightHeight) / 2;
      const int leftBottom = leftTop + stackSlot.leftHeight - 1;
      const int rightBottom = rightTop + stackSlot.rightHeight - 1;
      renderer.drawLine(rect.x, leftTop, rightX, rightTop);
      renderer.drawLine(rightX, rightTop, rightX, rightBottom);
      renderer.drawLine(rightX, rightBottom, rect.x, leftBottom);
      renderer.drawLine(rect.x, leftBottom, rect.x, leftTop);
    };

    CarouselSourceFrame frame;
    auto drawCover = [this, &drawCoverOutline, &frame](const RecentBook& book, const CoverStackSlot& stackSlot,
                                                      const size_t slotIndex) {
      const auto& rect = stackSlot.rect;
      const int maxHeight = std::max(stackSlot.leftHeight, stackSlot.rightHeight);
      const int fallbackWidth = std::min(rect.width, maxHeight * 2 / 3);
      const auto& planned = frame.slots[slotIndex];
      auto drawBitmap = [this, &rect, &stackSlot, &frame](const std::string& path) {
        CarouselSourceTiming sourceTiming;
        auto* source = getCarouselSource(path, sourceTiming, &frame);
        if (source == nullptr || source->bitmap == nullptr) return CarouselBitmapDrawResult::Unavailable;

        if (sourceTiming.reused) {
          const bool rewound = source->bitmap->rewindToData() == BmpReaderError::Ok;
          if (!rewound) {
            invalidateCarouselSourceLocked(path);
            return CarouselBitmapDrawResult::Failed;
          }
        }
        const CarouselBitmapDrawResult result =
            renderer.drawPerspectiveBitmap(*source->bitmap, rect.x, rect.y, rect.width, stackSlot.leftHeight,
                                           stackSlot.rightHeight)
                ? CarouselBitmapDrawResult::Drawn
                : CarouselBitmapDrawResult::Failed;
        if (result == CarouselBitmapDrawResult::Failed) invalidateCarouselSourceLocked(path);
        return result;
      };

      if (!planned.resolvedPath.empty()) {
        const auto primaryResult = drawBitmap(planned.resolvedPath);
        if (primaryResult == CarouselBitmapDrawResult::Drawn) {
          drawCoverOutline(stackSlot);
          return;
        }
        if (planned.resolvedHq && primaryResult == CarouselBitmapDrawResult::Unavailable) {
          carouselHqProbe(book).unavailable = true;
        }
        // Preserve the existing behavior if the HQ source opens correctly
        // but the actual row render fails: retry the normal source only then.
        if (planned.resolvedHq && drawBitmap(planned.fallbackPath) == CarouselBitmapDrawResult::Drawn) {
          drawCoverOutline(stackSlot);
          return;
        }
      }

      const int rightX = rect.x + rect.width - 1;
      const int leftTop = rect.y + (maxHeight - stackSlot.leftHeight) / 2;
      const int rightTop = rect.y + (maxHeight - stackSlot.rightHeight) / 2;
      const int leftBottom = leftTop + stackSlot.leftHeight - 1;
      const int rightBottom = rightTop + stackSlot.rightHeight - 1;
      const int silhouetteX[] = {rect.x, rightX, rightX, rect.x};
      const int silhouetteY[] = {leftTop, rightTop, rightBottom, leftBottom};
      renderer.fillPolygon(silhouetteX, silhouetteY, 4, false);
      drawCoverOutline(stackSlot);
      const int coverX = rect.x + (rect.width - fallbackWidth) / 2;
      const std::string placeholder = renderer.truncatedText(UI_10_FONT_ID, book.title.c_str(), fallbackWidth - 10);
      renderer.drawText(UI_10_FONT_ID, coverX + 5, rect.y + maxHeight / 2, placeholder.c_str());
    };

    const auto stackSlots = threeCover
                                ? CoverStackGeometry::layoutThree(pageWidth, visibleBookCount, selectorIndex,
                                                                  carouselLayout.centerHeight)
                                : CoverStackGeometry::layout(pageWidth, visibleBookCount, selectorIndex,
                                                             carouselLayout.centerHeight);
    prepareCarouselSourceFrame(stackSlots, frame);
    // Slots are returned in far-to-near order so a rear cover and its ribbon
    // can never paint over a nearer cover.
    for (size_t slotIndex = 0; slotIndex < stackSlots.size(); ++slotIndex) {
      const auto& stackSlot = stackSlots[slotIndex];
      if (!stackSlot.valid) continue;
      const RecentBook& book = recentBooks[visibleBookIndexes[stackSlot.itemIndex]];
      const auto& rect = stackSlot.rect;
      drawCover(book, stackSlot, slotIndex);
      folioPresentation.drawCoverProgressBadge(renderer, rect.x, rect.y, rect.width, rect.height,
                                               book.progressPercent);
    }
    const auto& center = stackSlots.back();
    if (center.valid) {
      renderer.drawRect(center.rect.x - 3, center.rect.y - 3, center.rect.width + 6, center.rect.height + 6);
    }

    const std::string synopsisPreview = SynopsisPreview::firstWords(selected.synopsis);
    const char* synopsisText = synopsisPreview.empty() ? tr(STR_NO_SYNOPSIS) : synopsisPreview.c_str();
    const int synopsisMaxLines = std::clamp(carouselLayout.synopsisRegion.height / synopsisLineHeight, 1, 5);
    const auto synopsisLines = renderer.wrappedText(SMALL_FONT_ID, synopsisText,
                                                    carouselLayout.synopsisRegion.width, synopsisMaxLines);
    int synopsisY = carouselLayout.synopsisRegion.y;
    for (const auto& line : synopsisLines) {
      renderer.drawText(SMALL_FONT_ID, carouselLayout.synopsisRegion.x, synopsisY, line.c_str());
      synopsisY += synopsisLineHeight;
    }

    const int statusY = synopsisY + 6;
    renderer.drawLine(carouselLayout.statusRegion.x, statusY - 6,
                      carouselLayout.statusRegion.x + carouselLayout.statusRegion.width - 1,
                      statusY - 6);

    const uint32_t readingMinutes = (selected.readingSeconds + 30) / 60;
    const char* readingState = selected.progressPercent >= 100 ? tr(STR_COMPLETE)
                                                                 : (selected.progressPercent > 0 ? tr(STR_ONGOING)
                                                                                                 : tr(STR_NEW));
    char progressValue[16];
    char readingTimeValue[32];
    char sessionsValue[32];
    snprintf(progressValue, sizeof(progressValue), "%u%%", selected.progressPercent);
    snprintf(readingTimeValue, sizeof(readingTimeValue), "%lu %s",
             static_cast<unsigned long>(readingMinutes), tr(STR_MIN_SHORT));
    const char* sessionLabel = selected.readingSessions == 1 ? tr(STR_SESSION_SINGULAR)
                                                              : tr(STR_SESSION_PLURAL);
    snprintf(sessionsValue, sizeof(sessionsValue), "%u %s", selected.readingSessions, sessionLabel);
    const char* statusValues[] = {readingState, progressValue, readingTimeValue, sessionsValue};
    for (int field = 0; field < 4; ++field) {
      const int fieldLeft = carouselLayout.statusRegion.x + carouselLayout.statusRegion.width * field / 4;
      const int fieldRight = carouselLayout.statusRegion.x + carouselLayout.statusRegion.width * (field + 1) / 4;
      const std::string value = renderer.truncatedText(SMALL_FONT_ID, statusValues[field],
                                                       std::max(1, fieldRight - fieldLeft - 4));
      const int valueWidth = renderer.getTextWidth(SMALL_FONT_ID, value.c_str());
      renderer.drawText(SMALL_FONT_ID, fieldLeft + (fieldRight - fieldLeft - valueWidth) / 2, statusY, value.c_str());
    }
  }

  const bool accumulated = !halClock.isAvailable();
  const uint32_t today = halClock.getDateKey();
  uint32_t middleSeconds = 0;
  uint16_t finishedCount = 0;
  for (const auto& book : recentBooks) {
    const uint32_t seconds = accumulated ? book.readingSeconds
                                         : (today != 0 && book.dailyReadingDateKey == today
                                                ? book.dailyReadingSeconds
                                                : 0);
    middleSeconds += std::min(seconds, UINT32_MAX - middleSeconds);
    if (book.progressPercent >= 100) ++finishedCount;
  }
  const uint32_t lastMinutes = recentBooks.empty() ? 0 : (recentBooks.front().lastSessionSeconds + 30) / 60;
  folioPresentation.drawShelfStats(renderer, folioLayout, lastMinutes, (middleSeconds + 30) / 60, finishedCount,
                                   accumulated);

  if (menuPopup.processRender(renderer, mappedInput)) {
    overlayFrameShown = true;
    return;
  }
  if (bookActionsPopup.processRender(renderer, mappedInput)) {
    overlayFrameShown = true;
    return;
  }
  if (carouselHqPreparationActive) {
    const int progress = carouselHqQueue.empty()
                             ? 100
                             : static_cast<int>((carouselHqQueueIndex * 100) / carouselHqQueue.size());
    const std::string message = std::string(tr(STR_PREPARE_CAROUSEL_COVERS)) + "\n" +
                                std::to_string(carouselHqQueueIndex) + " / " +
                                std::to_string(carouselHqQueue.size());
    const Rect popup = GUI.drawPopup(renderer, message.c_str(), true);
    GUI.fillPopupProgress(renderer, popup, progress);
    const auto cancelLabels = mappedInput.mapLabels(tr(STR_CANCEL), "", "", "");
    GUI.drawButtonHints(renderer, cancelLabels.btn1, cancelLabels.btn2, cancelLabels.btn3, cancelLabels.btn4);
    renderer.displayBuffer(HalDisplay::FAST_REFRESH);
    carouselHqPopupRendered = true;
    overlayFrameShown = true;
    return;
  }
  if (carouselHqCompletionPopupPending) {
    GUI.drawPopup(renderer, "All carousel covers are ready");
    renderer.displayBuffer(HalDisplay::FAST_REFRESH);
    carouselHqCompletionPopupPending = false;
    overlayFrameShown = true;
    return;
  }
  if (retrievingBookCache && retrievingBookCacheIndex == selectorIndex) {
    std::string bookName = "book";
    if (visibleBookCount > 0 && retrievingBookCacheIndex < visibleBookCount) {
      const RecentBook& book = recentBooks[selectedRecentIndex()];
      bookName = book.title.empty() ? book.path : book.title;
    }
    const std::string prefix = std::string(tr(STR_RETRIEVING_BOOK_DETAILS)) + ": ";
    const int nameWidth = std::max(40, renderer.getScreenWidth() -
                                           renderer.getTextWidth(UI_10_FONT_ID, prefix.c_str()) - 100);
    const std::string message = prefix + "\n" + renderer.truncatedText(UI_10_FONT_ID, bookName.c_str(), nameWidth) +
                                "\nProgress: " + std::to_string(retrievingBookCacheProgress) + "%";
    const Rect popup = GUI.drawPopup(renderer, message.c_str(), true);
    GUI.fillPopupProgress(renderer, popup, retrievingBookCacheProgress);
    const auto cancelLabels = mappedInput.mapLabels(tr(STR_CANCEL), "", "", "");
    GUI.drawButtonHints(renderer, cancelLabels.btn1, cancelLabels.btn2, cancelLabels.btn3, cancelLabels.btn4);
    renderer.displayBuffer(HalDisplay::FAST_REFRESH);
    retrievingBookCachePopupRendered = true;
    overlayFrameShown = true;
    return;
  }
  if (recentCacheWarmupActive) {
    GUI.drawPopup(renderer, tr(STR_RETRIEVING_BOOK_DETAILS));
    recentCacheWarmupPopupRendered = true;
    overlayFrameShown = true;
    return;
  }

  const auto labels = mappedInput.mapLabels(tr(STR_MENU), tr(STR_OPEN), tr(STR_LIBRARY),
                                            activeTab == 1 ? tr(STR_FINISHED) : tr(STR_RECENT));
  GUI.drawButtonHints(renderer, labels.btn1, labels.btn2, labels.btn3, labels.btn4);
  renderer.displayBuffer();
  if (manualSingleRefresh && !retrievingBookCache) manualSingleRefresh = false;
  initialRenderPending = false;
  lastRenderedSelectorIndex = selectorIndex;
  lastRenderedPageStart = (selectorIndex / BOOKS_PER_PAGE) * BOOKS_PER_PAGE;
  lastRenderedTab = activeTab;
  snapshotRestored = false;
}

void RecentBooksActivity::renderFolioCarouselShelf(const int shelfTop, const int shelfHeight,
                                                    const bool threeCover) {
  if (shelfHeight <= 0) return;

  const int pageWidth = renderer.getScreenWidth();
  renderer.fillRect(0, shelfTop, pageWidth, shelfHeight, false);
  if (visibleBookCount == 0) return;

  // This renderer deliberately uses the Folio shelf bounds only. The
  // independent Carousel theme keeps its own full-screen title, synopsis,
  // status, and button layout in renderCarousel().
  static const FolioNooirTheme folioPresentation;
  const auto stackSlots = CoverStackGeometry::layoutFolioShelf(pageWidth, shelfTop, shelfHeight,
                                                                visibleBookCount, selectorIndex, threeCover);
  CarouselSourceFrame frame;
  prepareCarouselSourceFrame(stackSlots, frame);

  auto drawCoverOutline = [this](const CoverStackSlot& stackSlot) {
    const auto& rect = stackSlot.rect;
    const int maxHeight = std::max(stackSlot.leftHeight, stackSlot.rightHeight);
    const int rightX = rect.x + rect.width - 1;
    const int leftTop = rect.y + (maxHeight - stackSlot.leftHeight) / 2;
    const int rightTop = rect.y + (maxHeight - stackSlot.rightHeight) / 2;
    const int leftBottom = leftTop + stackSlot.leftHeight - 1;
    const int rightBottom = rightTop + stackSlot.rightHeight - 1;
    renderer.drawLine(rect.x, leftTop, rightX, rightTop);
    renderer.drawLine(rightX, rightTop, rightX, rightBottom);
    renderer.drawLine(rightX, rightBottom, rect.x, leftBottom);
    renderer.drawLine(rect.x, leftBottom, rect.x, leftTop);
  };

  auto drawCover = [this, &drawCoverOutline, &frame](const RecentBook& book, const CoverStackSlot& stackSlot,
                                                    const size_t slotIndex) {
    const auto& rect = stackSlot.rect;
    const int maxHeight = std::max(stackSlot.leftHeight, stackSlot.rightHeight);
    const int fallbackWidth = std::max(1, std::min(rect.width, maxHeight * 2 / 3));
    const auto& planned = frame.slots[slotIndex];
    auto drawBitmap = [this, &rect, &stackSlot, &frame](const std::string& path) {
      CarouselSourceTiming sourceTiming;
      auto* source = getCarouselSource(path, sourceTiming, &frame);
      if (source == nullptr || source->bitmap == nullptr) return CarouselBitmapDrawResult::Unavailable;

      if (sourceTiming.reused) {
        const bool rewound = source->bitmap->rewindToData() == BmpReaderError::Ok;
        if (!rewound) {
          invalidateCarouselSourceLocked(path);
          return CarouselBitmapDrawResult::Failed;
        }
      }
      const CarouselBitmapDrawResult result =
          renderer.drawPerspectiveBitmap(*source->bitmap, rect.x, rect.y, rect.width, stackSlot.leftHeight,
                                         stackSlot.rightHeight)
              ? CarouselBitmapDrawResult::Drawn
              : CarouselBitmapDrawResult::Failed;
      if (result == CarouselBitmapDrawResult::Failed) invalidateCarouselSourceLocked(path);
      return result;
    };

    if (!planned.resolvedPath.empty()) {
      const auto primaryResult = drawBitmap(planned.resolvedPath);
      if (primaryResult == CarouselBitmapDrawResult::Drawn) {
        drawCoverOutline(stackSlot);
        return;
      }
      if (planned.resolvedHq && primaryResult == CarouselBitmapDrawResult::Unavailable) {
        carouselHqProbe(book).unavailable = true;
      }
      // Preserve the existing behavior if the HQ source opens correctly but
      // its row render fails: retry the normal source only then.
      if (planned.resolvedHq && drawBitmap(planned.fallbackPath) == CarouselBitmapDrawResult::Drawn) {
        drawCoverOutline(stackSlot);
        return;
      }
    }

    const int rightX = rect.x + rect.width - 1;
    const int leftTop = rect.y + (maxHeight - stackSlot.leftHeight) / 2;
    const int rightTop = rect.y + (maxHeight - stackSlot.rightHeight) / 2;
    const int leftBottom = leftTop + stackSlot.leftHeight - 1;
    const int rightBottom = rightTop + stackSlot.rightHeight - 1;
    const int silhouetteX[] = {rect.x, rightX, rightX, rect.x};
    const int silhouetteY[] = {leftTop, rightTop, rightBottom, leftBottom};
    renderer.fillPolygon(silhouetteX, silhouetteY, 4, false);
    drawCoverOutline(stackSlot);
    const int coverX = rect.x + (rect.width - fallbackWidth) / 2;
    const std::string placeholder = renderer.truncatedText(UI_10_FONT_ID, book.title.c_str(),
                                                            std::max(1, fallbackWidth - 10));
    renderer.drawText(UI_10_FONT_ID, coverX + 5, rect.y + maxHeight / 2, placeholder.c_str());
  };

  // CoverStackGeometry returns rear-to-front order; drawing in that order
  // keeps every nearer silhouette/bitmap opaque over the covers behind it.
  for (size_t slotIndex = 0; slotIndex < stackSlots.size(); ++slotIndex) {
    const auto& stackSlot = stackSlots[slotIndex];
    if (!stackSlot.valid) continue;
    const RecentBook& book = recentBooks[visibleBookIndexes[stackSlot.itemIndex]];
    drawCover(book, stackSlot, slotIndex);
    folioPresentation.drawCoverProgressBadge(renderer, stackSlot.rect.x, stackSlot.rect.y,
                                             stackSlot.rect.width, stackSlot.rect.height, book.progressPercent);
  }

  const auto& center = stackSlots.back();
  if (center.valid) {
    renderer.drawRect(center.rect.x - 3, center.rect.y - 3, center.rect.width + 6, center.rect.height + 6);
  }
}

void RecentBooksActivity::render(RenderLock&&) {
  // Keep the Folio bookshelf's featured book and 4x2 grid typography fixed;
  // UI Scale applies to the surrounding application screens instead.
  renderer.setUiScaleTextEnabled(false);
  // Popups are drawn over the shelf framebuffer. Once the popup closes, force
  // one clean shelf render; otherwise its dialog pixels would remain behind
  // the next selection.
  if (overlayFrameShown && !menuPopup.isActive() && !bookActionsPopup.isActive() && !retrievingBookCache &&
      !carouselHqPreparationActive && !recentCacheWarmupActive) {
    overlayFrameShown = false;
    snapshotRestored = false;
    lastRenderedSelectorIndex = SIZE_MAX;
    lastRenderedPageStart = SIZE_MAX;
    initialRenderPending = true;
  }

  if (SETTINGS.uiTheme == CrossPointSettings::UI_THEME::CAROUSEL) {
    renderer.clearScreen();
    renderCarousel(activeBookLayout() == CrossPointSettings::FOLIO_LAYOUT_THREE_COVER_CAROUSEL);
    return;
  }

  const bool threeCoverGrid = usesThreeCoverGrid();
  const bool snapshotLayout = usesFourByTwoGrid();
  // Grid3 can reuse the current in-memory frame on the same page, but it does
  // not participate in the persisted 4x2 snapshot file or its focus outline.
  const bool frameReuseLayout = snapshotLayout || threeCoverGrid;
  bool renderedFromSnapshot = frameReuseLayout && snapshotRestored;
  const int pageWidth = renderer.getScreenWidth();
  const int pageHeight = renderer.getScreenHeight();
  const auto& metrics = UITheme::getInstance().getMetrics();
  const int gridPageItems = threeCoverGrid ? 3 : BOOKS_PER_PAGE;
  const size_t currentPageStart = (selectorIndex / gridPageItems) * gridPageItems;
  // Settings and other non-library screens do not change the shelf data. If
  // the persisted frame matches the current book state, show that frame once
  // and add only the focus outline. This avoids a second full shelf rebuild
  // (cover decoding, synopsis wrapping, and eight-card geometry) on return.
  // The snapshot key includes presentation/progress fields, so a reader exit
  // or cache update invalidates this path automatically.
  if (snapshotLayout && renderedFromSnapshot &&
      initialRenderPending && !snapshotFastPathUsed && !overlayFrameShown && !menuPopup.isActive() &&
      !bookActionsPopup.isActive() && !retrievingBookCache && !recentCacheWarmupActive && !manualSingleRefresh &&
      snapshotPageStart == currentPageStart && visibleBookCount > 0) {
    const auto& folioTheme = static_cast<const FolioNooirTheme&>(GUI);
    const FolioShelfLayout layout = folioTheme.shelfLayout(renderer, metrics);
    const int columns = layout.columns;
    const int gap = layout.gridGap;
    const int cardWidth = layout.cardWidth;
    const int cardHeight = layout.cardHeight;
    const size_t slot = selectorIndex % BOOKS_PER_PAGE;
    const int x = gap + static_cast<int>(slot % columns) * (cardWidth + gap);
    const int y = layout.gridTop + static_cast<int>(slot / columns) * cardHeight;
    const int coverHeight = std::max(40, std::min(cardHeight - 27, cardWidth * 3 / 2));
    renderer.drawRect(x - 3, y - 3, cardWidth + 6, coverHeight + 6);
    renderer.displayBuffer();
    snapshotFastPathUsed = true;
    initialRenderPending = false;
    lastRenderedSelectorIndex = selectorIndex;
    lastRenderedPageStart = currentPageStart;
    lastRenderedTab = activeTab;
    const RecentBook& selected = recentBooks[selectedRecentIndex()];
    lastFeaturedPath = selected.path;
    lastFeaturedCoverPath = selected.coverBmpPath;
    snapshotRestored = true;
    snapshotPageStart = currentPageStart;
    snapshotSelectorIndex = selectorIndex;
    return;
  }

  // A deferred request can arrive after a popup or cache task has already
  // completed. If the same Folio shelf frame is still on the panel, avoid
  // rebuilding all synopsis text and cover geometry and avoid another e-ink
  // refresh. Explicit cache refreshes and tab/selection changes invalidate
  // this fast path below.
  if (frameReuseLayout && !initialRenderPending &&
      !overlayFrameShown && !menuPopup.isActive() && !bookActionsPopup.isActive() && !retrievingBookCache &&
      !recentCacheWarmupActive && !manualSingleRefresh && lastRenderedSelectorIndex == selectorIndex &&
      lastRenderedPageStart == currentPageStart && lastRenderedTab == activeTab && snapshotRestored) {
    const bool sameFeatured = visibleBookCount == 0 || recentBooks[selectedRecentIndex()].path == lastFeaturedPath;
    if (sameFeatured) return;
  }

  // The shelf frame is already on the panel and in the renderer framebuffer
  // when a menu/long-press popup opens. Do not redraw all covers, synopsis
  // lines, statistics, and progress badges just to place the dialog on top.
  // OptionPopup::processRender() performs the single required display update.
  const bool popupOnlyRender =
      (menuPopup.isActive() || bookActionsPopup.isActive()) && frameReuseLayout && snapshotRestored && !initialRenderPending &&
      lastRenderedSelectorIndex == selectorIndex && lastRenderedPageStart == currentPageStart;
  if (popupOnlyRender) {
    if (menuPopup.processRender(renderer, mappedInput) || bookActionsPopup.processRender(renderer, mappedInput)) {
      overlayFrameShown = true;
      return;
    }
  }

  // The snapshot is restored once in onEnter. Keep the framebuffer in RAM
  // while moving the selection; re-reading the full 48 KB snapshot from SD on
  // every button press was the source of the 1–2 second Recent/Finished lag.
  if (renderedFromSnapshot && snapshotPageStart != currentPageStart) {
    renderedFromSnapshot = false;
    snapshotRestored = false;
  }
  if (!renderedFromSnapshot) renderer.clearScreen();

  // The cover bookshelf is exclusive to the Folio Nooir UI theme. Other
  // themes retain CrossPoint's compact Recent Books list.
  if (SETTINGS.uiTheme != CrossPointSettings::UI_THEME::FOLIO_NOOIR) {
    GUI.drawHeader(renderer, Rect{0, metrics.topPadding, pageWidth, metrics.headerHeight},
                   tr(STR_MENU_RECENT_BOOKS));
    const int top = metrics.topPadding + metrics.headerHeight + metrics.verticalSpacing;
    const int height = pageHeight - top - metrics.buttonHintsHeight - metrics.verticalSpacing;
    if (recentBooks.empty()) {
      renderer.drawText(UI_10_FONT_ID, metrics.contentSidePadding, top + 20, tr(STR_NO_RECENT_BOOKS));
    } else {
      GUI.drawList(renderer, Rect{0, top, pageWidth, height}, recentBooks.size(), selectorIndex,
                   [this](int index) { return recentBooks[index].title; },
                   [this](int index) { return recentBooks[index].author; },
                   [this](int index) { return UITheme::getFileIcon(recentBooks[index].path); });
    }
    const auto labels = mappedInput.mapLabels(tr(STR_HOME), tr(STR_OPEN), tr(STR_DIR_UP), tr(STR_DIR_DOWN));
    GUI.drawButtonHints(renderer, labels.btn1, labels.btn2, labels.btn3, labels.btn4);
    renderer.displayBuffer();
    return;
  }

  const auto& folioTheme = static_cast<const FolioNooirTheme&>(GUI);
  const FolioShelfLayout layout = folioTheme.shelfLayout(renderer, metrics);
  auto drawThreeCoverSelection = [&](const size_t index, const bool selected) {
    if (!threeCoverGrid || index >= visibleBookCount) return;
    const size_t pageStart = (selectorIndex / gridPageItems) * gridPageItems;
    if (index < pageStart || index >= pageStart + gridPageItems) return;
    const int gap = 10;
    const int columns = 3;
    const int cardWidth = (pageWidth - gap * (columns + 1)) / columns;
    const int coverHeight = std::min(layout.gridHeight - 20, cardWidth * 3 / 2);
    const int slot = static_cast<int>(index - pageStart);
    const int x = gap + (slot % columns) * (cardWidth + gap);
    const int y = layout.gridTop + std::max(0, (layout.gridHeight - coverHeight) / 2);
    renderer.drawRect(x - 3, y - 3, cardWidth + 6, coverHeight + 6, selected);
  };
  folioTheme.drawShelfTabs(renderer, layout, activeTab);
  folioTheme.drawShelfBattery(renderer, layout, metrics);

  const int contentTop = layout.contentTop;
  const int contentHeight = layout.contentHeight;
  const int detailHeight = layout.detailHeight;
  auto drawStats = [&] {
    const bool accumulated = !halClock.isAvailable();
    const uint32_t today = halClock.getDateKey();
    uint32_t middleSeconds = 0;
    uint16_t finishedCount = 0;
    for (const auto& book : recentBooks) {
      const uint32_t seconds = accumulated ? book.readingSeconds
                                           : (today != 0 && book.dailyReadingDateKey == today
                                                  ? book.dailyReadingSeconds
                                                  : 0);
      middleSeconds += std::min(seconds, UINT32_MAX - middleSeconds);
      if (book.progressPercent >= 100) ++finishedCount;
    }
    const uint32_t lastMinutes = recentBooks.empty() ? 0 : (recentBooks.front().lastSessionSeconds + 30) / 60;
    folioTheme.drawShelfStats(renderer, layout, lastMinutes, (middleSeconds + 30) / 60, finishedCount, accumulated);
  };
  if (visibleBookCount == 0) {
    renderer.fillRect(0, contentTop, pageWidth, contentHeight, false);
    renderer.drawText(UI_10_FONT_ID, metrics.contentSidePadding, contentTop + 20, tr(STR_NO_RECENT_BOOKS));
  } else {
    auto drawCover = [this](const RecentBook& book, int x, int y, int maxWidth, int maxHeight, bool drawBitmap,
                            bool preferCarouselHq) {
      if (!drawBitmap) return;
      int width = std::min(maxWidth, maxHeight * 2 / 3);
      const std::string normalPath = UITheme::getCoverThumbPath(book.coverBmpPath, BOOKSHELF_COVER_HEIGHT);
      auto drawBitmapFromPath = [this, x, y, maxWidth, maxHeight](const std::string& path) -> CarouselBitmapDrawResult {
        HalFile file;
        if (path.empty() || !Storage.openFileForRead("SHELF", path, file)) {
          return CarouselBitmapDrawResult::Unavailable;
        }
        Bitmap bitmap(file);
        if (bitmap.parseHeaders() == BmpReaderError::Ok && bitmap.getWidth() > 0 && bitmap.getHeight() > 0) {
          // Fit inside the slot without distorting the cover, then center it
          // so the largest possible portion remains visible.
          const int sourceWidth = bitmap.getWidth();
          const int sourceHeight = bitmap.getHeight();
          int drawWidth = sourceWidth;
          int drawHeight = sourceHeight;
          if (sourceWidth > maxWidth || sourceHeight > maxHeight) {
            if (static_cast<long long>(sourceWidth) * maxHeight >
                static_cast<long long>(sourceHeight) * maxWidth) {
              drawWidth = maxWidth;
              drawHeight = std::max(1, sourceHeight * maxWidth / sourceWidth);
            } else {
              drawHeight = maxHeight;
              drawWidth = std::max(1, sourceWidth * maxHeight / sourceHeight);
            }
          }
          const int drawX = x + (maxWidth - drawWidth) / 2;
          const int drawY = y + (maxHeight - drawHeight) / 2;
          renderer.drawBitmap(bitmap, drawX, drawY, drawWidth, drawHeight);
          renderer.drawRect(x, y, maxWidth, maxHeight);
          return CarouselBitmapDrawResult::Drawn;
        }
        return CarouselBitmapDrawResult::Unavailable;
      };

      if (preferCarouselHq && !book.coverBmpPath.empty()) {
        const std::string hqPath = UITheme::getCoverThumbPath(book.coverBmpPath, CAROUSEL_HQ_COVER_HEIGHT);
        auto& hqProbe = carouselHqProbe(book);
        if (!hqProbe.unavailable) {
          const auto hqResult = drawBitmapFromPath(hqPath);
          if (hqResult == CarouselBitmapDrawResult::Drawn) return;
          if (hqResult == CarouselBitmapDrawResult::Unavailable) hqProbe.unavailable = true;
        }
      }
      if (drawBitmapFromPath(normalPath) == CarouselBitmapDrawResult::Drawn) return;

      const int coverX = x + (maxWidth - width) / 2;
      renderer.drawRect(coverX, y, width, maxHeight);
      const std::string title = renderer.truncatedText(UI_10_FONT_ID, book.title.c_str(), width - 10);
      renderer.drawText(UI_10_FONT_ID, coverX + 5, y + maxHeight / 2, title.c_str());
    };

    const RecentBook& selected = recentBooks[selectedRecentIndex()];
    constexpr int detailPadding = 12;
    // Keep the Featured cover close to the Folio 3-Cover card width.  The
    // same 10px gap and three-column calculation are used by the 3-Cover
    // shelf below, so this scales with X3/X4 instead of using an X4 width.
    constexpr int threeCoverGap = 10;
    const int threeCoverCardWidth = (pageWidth - threeCoverGap * 4) / 3;
    const int detailCoverWidth = std::max(1, threeCoverCardWidth);
    const int detailCoverHeight = detailHeight - 20;
    renderer.fillRect(detailPadding * 2 + detailCoverWidth, contentTop + 1,
                      pageWidth - detailPadding * 3 - detailCoverWidth, detailHeight - 2, false);
    const bool featuredCoverCached = !manualSingleRefresh && renderedFromSnapshot && lastFeaturedPath == selected.path &&
                                     lastFeaturedCoverPath == selected.coverBmpPath;
    if (!featuredCoverCached) {
      renderer.fillRect(detailPadding, contentTop + 1, detailCoverWidth, detailHeight - 2, false);
    }
    // Use the same 220px source as the Folio shelf cards.  The standalone
    // Carousel still owns the 360px preference; using it here would make the
    // same cover look darker after the separate HQ thumbnail is downsampled.
    drawCover(selected, detailPadding, contentTop + 7, detailCoverWidth, detailCoverHeight, !featuredCoverCached,
              false);
    lastFeaturedPath = selected.path;
    lastFeaturedCoverPath = selected.coverBmpPath;
    const int detailX = detailPadding * 2 + detailCoverWidth;
    const int detailWidth = pageWidth - detailX - detailPadding;
    const std::string title = renderer.truncatedText(UI_12_FONT_ID, selected.title.c_str(), detailWidth);
    const std::string author = renderer.truncatedText(UI_10_FONT_ID, selected.author.c_str(), detailWidth);
    renderer.drawText(UI_12_FONT_ID, detailX, contentTop + 20, title.c_str(), true);
    renderer.drawText(UI_10_FONT_ID, detailX, contentTop + 55, author.c_str());
    const std::string synopsisPreview = SynopsisPreview::firstWords(selected.synopsis);
    const char* synopsisText = synopsisPreview.empty() ? tr(STR_NO_SYNOPSIS) : synopsisPreview.c_str();
    const int synopsisY = contentTop + 79;
    // Keep the state row and progress bar at the bottom of the featured
    // panel, leaving enough vertical room for five synopsis lines above it.
    const int progressTextY = contentTop + detailHeight - 37;
    constexpr int SYNOPSIS_PROGRESS_GAP_PX = 2;
    const int synopsisLineHeight = std::max(1, renderer.getLineHeight(SMALL_FONT_ID));
    const int synopsisMaxLines = std::clamp(
        (progressTextY - synopsisY - SYNOPSIS_PROGRESS_GAP_PX) / synopsisLineHeight, 1, 5);
    const auto synopsisLines = renderer.wrappedText(SMALL_FONT_ID, synopsisText, detailWidth, synopsisMaxLines);
    int synopsisDrawY = synopsisY;
    for (const auto& line : synopsisLines) {
      renderer.drawText(SMALL_FONT_ID, detailX, synopsisDrawY, line.c_str());
      synopsisDrawY += synopsisLineHeight;
    }
    char state[96];
    const uint32_t readingMinutes = (selected.readingSeconds + 30) / 60;
    snprintf(state, sizeof(state), "%s - %u%% - %lu min - %u sessions",
             selected.progressPercent >= 100 ? tr(STR_COMPLETE)
                                             : (selected.progressPercent > 0 ? tr(STR_ONGOING) : tr(STR_NEW)),
             selected.progressPercent, static_cast<unsigned long>(readingMinutes), selected.readingSessions);
    const std::string stateText = renderer.truncatedText(SMALL_FONT_ID, state, detailWidth);
    renderer.drawText(SMALL_FONT_ID, detailX, progressTextY, stateText.c_str());
    const int progressY = contentTop + detailHeight - 14;
    renderer.drawRect(detailX, progressY, detailWidth, 12);
    const int fill = (detailWidth - 2) * selected.progressPercent / 100;
    if (fill > 0) renderer.fillRect(detailX + 1, progressY + 1, fill, 10);
    renderer.drawLine(0, contentTop + detailHeight - 1, pageWidth - 1, contentTop + detailHeight - 1);

    if (usesCarouselLayout()) {
      renderFolioCarouselShelf(layout.gridTop, layout.gridHeight,
                               activeBookLayout() == CrossPointSettings::FOLIO_LAYOUT_THREE_COVER_CAROUSEL);
    } else {
      const int columns = threeCoverGrid ? 3 : layout.columns;
      const int gap = threeCoverGrid ? 10 : layout.gridGap;
      const int gridTop = layout.gridTop;
      const int gridHeight = layout.gridHeight;
      const int cardWidth = threeCoverGrid ? (pageWidth - gap * (columns + 1)) / columns : layout.cardWidth;
      const int cardHeight = threeCoverGrid ? gridHeight : layout.cardHeight;
      const size_t pageStart = (selectorIndex / gridPageItems) * gridPageItems;
      if (threeCoverGrid && !renderedFromSnapshot) renderer.fillRect(0, gridTop, pageWidth, gridHeight, false);
      for (int slot = 0; slot < gridPageItems; ++slot) {
        const size_t index = pageStart + static_cast<size_t>(slot);
        if (index >= visibleBookCount) break;
        const RecentBook& book = recentBooks[visibleBookIndexes[index]];
        const int x = gap + (slot % columns) * (cardWidth + gap);
        const int y = threeCoverGrid
                          ? gridTop + std::max(0, (gridHeight - std::min(gridHeight - 20, cardWidth * 3 / 2)) / 2)
                          : gridTop + (slot / columns) * cardHeight;
        const int coverHeight = threeCoverGrid
                                    ? std::min(gridHeight - 20, cardWidth * 3 / 2)
                                    : std::max(40, std::min(cardHeight - 27, cardWidth * 3 / 2));
        if (!threeCoverGrid) {
          renderer.fillRect(x, y + coverHeight, cardWidth, std::max(1, cardHeight - coverHeight), false);
        }
        const bool refreshCard = !renderedFromSnapshot || (manualSingleRefresh && index == selectorIndex);
        drawCover(book, x, y, cardWidth, coverHeight, refreshCard, false);
        folioTheme.drawCoverProgressBadge(renderer, x, y, cardWidth, coverHeight, book.progressPercent);
      }
      const size_t pageCount = (visibleBookCount + gridPageItems - 1) / gridPageItems;
      folioTheme.drawPageIndicator(renderer, layout, selectorIndex / gridPageItems + 1, pageCount);
    }

    if (threeCoverGrid && !renderedFromSnapshot) {
      drawThreeCoverSelection(selectorIndex, true);
    }

  }
  drawStats();

  // The base frame is kept in RAM while navigating within a page. Erase only
  // the previous focus outline instead of restoring the whole SD snapshot.
  if (snapshotLayout && renderedFromSnapshot && lastRenderedSelectorIndex != SIZE_MAX &&
      lastRenderedSelectorIndex != selectorIndex && lastRenderedPageStart == currentPageStart) {
    const int columns = layout.columns;
    const int gap = layout.gridGap;
    const int cardWidth = layout.cardWidth;
    const int cardHeight = layout.cardHeight;
    const size_t oldSlot = lastRenderedSelectorIndex % BOOKS_PER_PAGE;
    const int oldX = gap + static_cast<int>(oldSlot % columns) * (cardWidth + gap);
    const int oldY = layout.gridTop + static_cast<int>(oldSlot / columns) * cardHeight;
    const int oldCoverHeight = std::max(40, std::min(cardHeight - 27, cardWidth * 3 / 2));
    renderer.drawRect(oldX - 3, oldY - 3, cardWidth + 6, oldCoverHeight + 6, false);
  } else if (threeCoverGrid && renderedFromSnapshot && lastRenderedSelectorIndex != SIZE_MAX &&
             lastRenderedSelectorIndex != selectorIndex && lastRenderedPageStart == currentPageStart) {
    drawThreeCoverSelection(lastRenderedSelectorIndex, false);
    drawThreeCoverSelection(selectorIndex, true);
  }

  // Persist the base frame before adding the current selection outline. The
  // next button press restores this clean frame and draws one outline only.
  if (snapshotLayout && !renderedFromSnapshot && (initialRenderPending || snapshotPageStart != currentPageStart)) {
    // Persist after the frame has become interactive; see the idle write in
    // loop(). Keeping the old snapshot invalid until then prevents a stale
    // page from being treated as the current one during rapid navigation.
    snapshotWritePending = true;
    snapshotWriteRequestedMs = millis();
    snapshotRestored = false;
    snapshotPageStart = SIZE_MAX;
  }
  if (snapshotLayout && visibleBookCount > 0) {
    const int columns = layout.columns;
    const int gap = layout.gridGap;
    const int cardWidth = layout.cardWidth;
    const int cardHeight = layout.cardHeight;
    const size_t slot = selectorIndex % BOOKS_PER_PAGE;
    const int x = gap + static_cast<int>(slot % columns) * (cardWidth + gap);
    const int y = layout.gridTop + static_cast<int>(slot / columns) * cardHeight;
    const int coverHeight = std::max(40, std::min(cardHeight - 27, cardWidth * 3 / 2));
    renderer.drawRect(x - 3, y - 3, cardWidth + 6, coverHeight + 6);
  }

  if (menuPopup.processRender(renderer, mappedInput)) {
    overlayFrameShown = true;
    return;
  }
  if (bookActionsPopup.processRender(renderer, mappedInput)) {
    overlayFrameShown = true;
    return;
  }
  if (carouselHqCompletionPopupPending) {
    GUI.drawPopup(renderer, "All carousel covers are ready");
    renderer.displayBuffer(HalDisplay::FAST_REFRESH);
    carouselHqCompletionPopupPending = false;
    overlayFrameShown = true;
    return;
  }
  if (retrievingBookCache && retrievingBookCacheIndex == selectorIndex) {
    std::string bookName = "book";
    if (visibleBookCount > 0 && retrievingBookCacheIndex < visibleBookCount) {
      const RecentBook& book = recentBooks[selectedRecentIndex()];
      bookName = book.title.empty() ? book.path : book.title;
    }
    const std::string prefix = std::string(tr(STR_RETRIEVING_BOOK_DETAILS)) + ": ";
    const int nameWidth = std::max(40, renderer.getScreenWidth() -
                                           renderer.getTextWidth(UI_10_FONT_ID, prefix.c_str()) - 100);
    const std::string message = prefix + "\n" + renderer.truncatedText(UI_10_FONT_ID, bookName.c_str(), nameWidth) +
                                "\nProgress: " + std::to_string(retrievingBookCacheProgress) + "%";
    const Rect popup = GUI.drawPopup(renderer, message.c_str(), true);
    GUI.fillPopupProgress(renderer, popup, retrievingBookCacheProgress);
    const auto cancelLabels = mappedInput.mapLabels(tr(STR_CANCEL), "", "", "");
    GUI.drawButtonHints(renderer, cancelLabels.btn1, cancelLabels.btn2, cancelLabels.btn3, cancelLabels.btn4);
    renderer.displayBuffer(HalDisplay::FAST_REFRESH);
    retrievingBookCachePopupRendered = true;
    overlayFrameShown = true;
    return;
  }
  if (recentCacheWarmupActive) {
    GUI.drawPopup(renderer, tr(STR_RETRIEVING_BOOK_DETAILS));
    recentCacheWarmupPopupRendered = true;
    overlayFrameShown = true;
    return;
  }
  const auto labels = mappedInput.mapLabels(tr(STR_MENU), tr(STR_OPEN), tr(STR_LIBRARY),
                                            activeTab == 1 ? tr(STR_FINISHED) : tr(STR_RECENT));
  GUI.drawButtonHints(renderer, labels.btn1, labels.btn2, labels.btn3, labels.btn4);
  renderer.displayBuffer();
  if (manualSingleRefresh && !retrievingBookCache) manualSingleRefresh = false;
  if (initialRenderPending) {
    initialRenderPending = false;
  }
  lastRenderedSelectorIndex = selectorIndex;
  lastRenderedPageStart = currentPageStart;
  lastRenderedTab = activeTab;
  // The framebuffer is already a valid in-memory base frame even when its
  // optional SD snapshot is still waiting for the idle write. Keep using it
  // for immediate button navigation; otherwise the first button press after
  // boot would decode all eight covers again before the deferred write.
  if (frameReuseLayout && visibleBookCount > 0) {
    snapshotRestored = true;
    snapshotPageStart = currentPageStart;
    snapshotSelectorIndex = selectorIndex;
  } else {
    snapshotRestored = false;
    snapshotPageStart = SIZE_MAX;
    snapshotSelectorIndex = SIZE_MAX;
  }
}
