#pragma once

#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

/**
 * Lightweight CBZ archive reader and ComicInfo shelf metadata cache.
 *
 * The archive directory is kept as a small list of supported image names. A
 * page is streamed to a temporary SD-card file only when it is displayed; no
 * archive entry or decoded image is retained in heap memory between pages.
 * ComicInfo metadata and cover choice are persisted in the per-book cache.
 */
class Cbz {
 public:
  struct Metadata {
    std::string title;
    std::string series;
    std::string number;
    std::string writer;
    std::string penciller;
    std::string inker;
    std::string colorist;
    std::string letterer;
    std::string coverArtist;
    std::string editor;
    std::string publisher;
    std::string genre;
    std::string summary;
    std::string year;
    std::string month;
    std::string day;
    std::string languageIso;
    std::string pageCount;
    std::string coverEntry;
  };

 private:
  std::string filepath;
  std::string cachePath;
  std::vector<std::string> pageEntries;
  // Full reader indexing is bounded for the physical X3 heap. Metadata-only
  // retrieval never builds this vector and can handle larger archives.
  size_t pageEntryPathBytes = 0;
  bool pageIndexLimitExceeded = false;
  bool loaded = false;
  bool metadataLoaded = false;
  bool forceMetadataReload = false;
  Metadata metadata;

  static bool isSupportedPage(std::string_view path);
  static bool isIgnoredEntry(std::string_view path);
  bool parseComicInfo(const std::string& xml);
  bool loadMetadataFromArchive();
  bool loadMetadataCache();
  bool saveMetadataCache() const;
  std::string findComicInfoEntry() const;
  std::string findFirstPageEntry() const;
  std::string resolveMetadataOnlyCover(const std::string& fallback) const;
  std::string resolveCoverEntry() const;
  // Extract an archive image to a caller-selected transient path.  Reader
  // pages use current.* while shelf thumbnails use thumb_source.* so cover
  // generation can never remove the page that an active reader is using.
  bool extractEntryToPath(const std::string& entry, const std::string& outputPath) const;
  bool extractEntry(const std::string& entry, std::string& outputPath) const;

 public:
  explicit Cbz(std::string filepath, const std::string& cacheDir);
  ~Cbz() = default;

  bool load();
  // Load only the compact metadata/cover cache. No archive scan is performed
  // when the cache is valid; callers use loadMetadataOnly() for on-demand
  // library retrieval when it is missing.
  bool loadCachedMetadataOnly();
  bool loadMetadataOnly();
  bool clearCache() const;
  void setupCacheDir() const;

  const std::string& getPath() const { return filepath; }
  const std::string& getCachePath() const { return cachePath; }
  // Session-scoped current-page pixel cache; the reader removes it whenever
  // the page or render geometry changes.
  std::string getRenderCachePath() const { return cachePath + "/render.pxc"; }
  std::string getTitle() const;
  std::string getAuthor() const;
  std::string getSynopsis() const;
  const Metadata& getMetadata() const { return metadata; }
  std::string getThumbBmpPath() const { return cachePath + "/thumb_[HEIGHT].bmp"; }
  std::string getThumbBmpPath(int height) const { return cachePath + "/thumb_" + std::to_string(height) + ".bmp"; }
  bool generateThumbBmp(int height) const;

  size_t getPageCount() const { return pageEntries.size(); }
  std::string_view getPageEntry(size_t index) const { return pageEntries[index]; }

  // Streams one archive image to a temporary SD file. The caller owns the
  // returned path and may remove it after decoding.
  bool extractPage(size_t index, std::string& outputPath) const;
  // Streams one archive image to a caller-selected temporary path without
  // touching the reader's current.* page. Readers use this for atomic page
  // transitions so a failed next-page extraction cannot destroy the page
  // currently being displayed.
  bool extractPageTo(size_t index, const std::string& outputPath) const;

  bool isLoaded() const { return loaded; }
  bool pageIndexTooLarge() const { return pageIndexLimitExceeded; }
};
