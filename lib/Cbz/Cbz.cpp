#include "Cbz.h"

#include <FsHelpers.h>
#include <HalStorage.h>
#include <Logging.h>
#include <Serialization.h>
#include <ZipFile.h>
#include <JpegToBmpConverter.h>
#include <PngToBmpConverter.h>

#include <algorithm>
#include <cctype>
#include <functional>
#include <cstdlib>
#include <cstring>
#include <vector>
#include <utility>

#include "../../src/util/CbzDiagnostics.h"

namespace {
constexpr size_t STREAM_CHUNK_SIZE = 8 * 1024;
// These limits bound the heap used by the full reader page index on X3. A
// normal manga archive is far below both limits; metadata-only shelf retrieval
// intentionally does not build this index and is not subject to them.
constexpr size_t MAX_PAGE_ENTRIES = 1024;
constexpr size_t MAX_PAGE_PATH_BYTES = 64u * 1024u;
// Keep decompression on SD, but reject pathological entries before writing a
// very large temporary file. Decoder-level dimension/heap guards remain the
// final authority for image safety.
constexpr size_t MAX_PAGE_ENTRY_BYTES = 32u * 1024u * 1024u;
// ComicInfo files are normally a few KB. Keep the parser's transient
// allocation bounded for X3 rather than accepting an archive-sized XML blob.
constexpr size_t MAX_COMICINFO_BYTES = 16u * 1024u;
constexpr size_t MAX_COVER_SOURCE_BYTES = 3u * 1024u * 1024u;
constexpr uint8_t METADATA_CACHE_VERSION = 1;
constexpr char METADATA_CACHE_FILE[] = "/metadata.bin";
constexpr char METADATA_CACHE_TEMP_FILE[] = "/metadata.bin.tmp";

std::string lowerCopy(std::string_view value) {
  std::string result(value);
  std::transform(result.begin(), result.end(), result.begin(), [](const unsigned char c) {
    return static_cast<char>(std::tolower(c));
  });
  return result;
}

std::string trimCopy(std::string value) {
  const auto notSpace = [](const unsigned char c) { return !std::isspace(c); };
  value.erase(value.begin(), std::find_if(value.begin(), value.end(), notSpace));
  value.erase(std::find_if(value.rbegin(), value.rend(), notSpace).base(), value.end());
  return value;
}

std::string xmlDecode(std::string value) {
  struct Entity {
    const char* encoded;
    const char* decoded;
  };
  constexpr Entity entities[] = {{"&amp;", "&"}, {"&lt;", "<"}, {"&gt;", ">"},
                                 {"&quot;", "\""}, {"&apos;", "'"}};
  for (const auto& entity : entities) {
    size_t pos = 0;
    while ((pos = value.find(entity.encoded, pos)) != std::string::npos) {
      value.replace(pos, std::strlen(entity.encoded), entity.decoded);
      pos += std::strlen(entity.decoded);
    }
  }
  return trimCopy(std::move(value));
}

std::string xmlText(const std::string& xml, const char* tag) {
  const std::string open = std::string("<") + tag;
  const std::string close = std::string("</") + tag + ">";
  size_t start = xml.find(open);
  if (start == std::string::npos) return {};
  start = xml.find('>', start);
  if (start == std::string::npos) return {};
  ++start;
  const size_t end = xml.find(close, start);
  if (end == std::string::npos || end <= start) return {};
  std::string text = xml.substr(start, end - start);
  // ComicInfo text is normally plain XML text. Strip any nested tags so a
  // malformed/extended producer cannot leak markup into the shelf synopsis.
  std::string plain;
  plain.reserve(text.size());
  bool inTag = false;
  for (const char c : text) {
    if (c == '<') inTag = true;
    else if (c == '>') inTag = false;
    else if (!inTag) plain.push_back(c);
  }
  return xmlDecode(std::move(plain));
}

std::string xmlAttribute(const std::string& element, const char* name) {
  const std::string key = std::string(name) + "=";
  size_t pos = element.find(key);
  if (pos == std::string::npos) return {};
  pos += key.size();
  if (pos >= element.size()) return {};
  const char quote = element[pos];
  if (quote != '\"' && quote != '\'') return {};
  const size_t end = element.find(quote, pos + 1);
  if (end == std::string::npos) return {};
  return xmlDecode(element.substr(pos + 1, end - pos - 1));
}

bool isNumeric(const std::string& value) {
  return !value.empty() && std::all_of(value.begin(), value.end(),
                                       [](const unsigned char c) { return std::isdigit(c); });
}

std::string fallbackTitleForPath(const std::string& path) {
  const size_t slash = path.find_last_of('/');
  const size_t start = slash == std::string::npos ? 0 : slash + 1;
  const size_t dot = path.find_last_of('.');
  const size_t end = dot == std::string::npos || dot <= start ? path.size() : dot;
  return path.substr(start, end - start);
}

// A shelf thumbnail is presentation state, not reader scratch data.  Treat a
// file as reusable only after its BMP header and payload are present; callers
// can then replace a stale/partial file without first deleting a valid one.
bool isUsableThumbnail(const std::string& path) {
  HalFile file;
  if (!Storage.openFileForRead("CBZ", path, file)) return false;
  const bool largeEnough = file.size() > 62;
  uint8_t signature[2]{};
  const bool header = largeEnough && file.read(signature, sizeof(signature)) == sizeof(signature) &&
                      signature[0] == 'B' && signature[1] == 'M';
  file.close();
  return header;
}

bool isReaderCacheFile(const char* name) {
  if (!name) return false;
  const std::string value(name);
  if (value == "page_cache.bin" || value == "page_cache.bin.tmp" || value == "render.pxc" ||
      value == "render.next.pxc" || value == "render.pending.pxc") {
    return true;
  }
  if (value.rfind("prefetch_", 0) == 0) return true;
  if (value.rfind("page_", 0) != 0) return false;
  return value.size() > 4 && (value.rfind(".pxc") == value.size() - 4 ||
                              value.rfind(".pxc.tmp") == value.size() - 8);
}
}  // namespace

Cbz::Cbz(std::string filepath, const std::string& cacheDir) : filepath(std::move(filepath)) {
  logCbzPath("cbz-construction", this->filepath);
  cachePath = cacheDir + "/cbz_" + std::to_string(std::hash<std::string>{}(this->filepath));
}

bool Cbz::isIgnoredEntry(const std::string_view path) {
  if (path.empty() || path.back() == '/') return true;

  size_t componentStart = 0;
  while (componentStart < path.size()) {
    const size_t slash = path.find('/', componentStart);
    const size_t componentEnd = slash == std::string_view::npos ? path.size() : slash;
    const std::string component = lowerCopy(path.substr(componentStart, componentEnd - componentStart));
    if (component == "__macosx" || component == ".ds_store" || component == "thumbs.db" ||
        (component.size() > 2 && component.rfind("._", 0) == 0)) {
      return true;
    }
    if (slash == std::string_view::npos) break;
    componentStart = slash + 1;
  }
  return false;
}

bool Cbz::isSupportedPage(const std::string_view path) {
  return !isIgnoredEntry(path) && (FsHelpers::hasJpgExtension(path) || FsHelpers::hasPngExtension(path));
}

bool Cbz::load() {
  std::vector<std::string>().swap(pageEntries);
  pageEntryPathBytes = 0;
  pageIndexLimitExceeded = false;
  loaded = false;
  if (!Storage.exists(filepath.c_str())) {
    LOG_ERR("CBZ", "File does not exist: %s", filepath.c_str());
    return false;
  }

  ZipFile zip(filepath);
  if (!zip.enumerateFilePaths([this](const std::string_view path) {
        if (!isSupportedPage(path)) return;
        const size_t pathBytes = path.size() + 1;
        if (pageEntries.size() >= MAX_PAGE_ENTRIES || pathBytes > MAX_PAGE_PATH_BYTES ||
            pageEntryPathBytes > MAX_PAGE_PATH_BYTES - pathBytes) {
          pageIndexLimitExceeded = true;
          return;
        }
        pageEntries.emplace_back(path);
        pageEntryPathBytes += pathBytes;
      })) {
    LOG_ERR("CBZ", "Could not read archive directory: %s", filepath.c_str());
    pageEntries.clear();
    pageEntryPathBytes = 0;
    return false;
  }

  if (pageIndexLimitExceeded) {
    pageEntries.clear();
    pageEntryPathBytes = 0;
    LOG_ERR("CBZ", "Archive too large for bounded page index (max %lu pages/%lu path bytes): %s",
            static_cast<unsigned long>(MAX_PAGE_ENTRIES), static_cast<unsigned long>(MAX_PAGE_PATH_BYTES),
            filepath.c_str());
    return false;
  }

  std::sort(pageEntries.begin(), pageEntries.end(), [](const std::string& left, const std::string& right) {
    if (FsHelpers::naturalLess(left, right)) return true;
    if (FsHelpers::naturalLess(right, left)) return false;
    return left < right;
  });
  loaded = true;
  if (!metadataLoaded) loadMetadataFromArchive();
  LOG_DBG("CBZ", "Loaded %s (%lu supported pages)", filepath.c_str(),
          static_cast<unsigned long>(pageEntries.size()));
  return true;
}

bool Cbz::loadCachedMetadataOnly() {
  std::vector<std::string>().swap(pageEntries);
  pageEntryPathBytes = 0;
  pageIndexLimitExceeded = false;
  loaded = false;
  metadataLoaded = false;
  return loadMetadataCache();
}

bool Cbz::loadMetadataOnly() {
  // This is the explicit source-inspection path used by Library retrieval and
  // Refresh Book Cache. Reparse ComicInfo.xml even if an older compact cache
  // exists, then atomically replace that cache with the new values.
  // Reset the in-memory flag as well so a caller that first inspected the
  // cached header cannot accidentally turn an explicit refresh into a no-op.
  std::vector<std::string>().swap(pageEntries);
  pageEntryPathBytes = 0;
  pageIndexLimitExceeded = false;
  loaded = false;
  metadataLoaded = false;
  forceMetadataReload = true;
  if (!Storage.exists(filepath.c_str())) {
    LOG_ERR("CBZ", "File does not exist: %s", filepath.c_str());
    return false;
  }
  return loadMetadataFromArchive();
}

bool Cbz::loadMetadataFromArchive() {
  metadata = Metadata{};
  const bool cached = !forceMetadataReload && loadMetadataCache();
  forceMetadataReload = false;
  if (cached) return true;

  const std::string comicInfoEntry = findComicInfoEntry();
  if (!comicInfoEntry.empty()) {
    ZipFile zipWithMetadata(filepath);
    size_t xmlSize = 0;
    if (zipWithMetadata.getInflatedFileSize(comicInfoEntry.c_str(), &xmlSize) && xmlSize > 0 &&
        xmlSize <= MAX_COMICINFO_BYTES) {
      size_t readSize = 0;
      uint8_t* data = zipWithMetadata.readFileToMemory(comicInfoEntry.c_str(), &readSize, true);
      if (data && readSize <= MAX_COMICINFO_BYTES + 1) {
        parseComicInfo(std::string(reinterpret_cast<char*>(data), readSize));
      }
      std::free(data);
    } else if (xmlSize > MAX_COMICINFO_BYTES) {
      LOG_DBG("CBZ", "Skipping oversized ComicInfo.xml (%lu bytes): %s",
              static_cast<unsigned long>(xmlSize), filepath.c_str());
    }
  }

  if (metadata.title.empty()) metadata.title = fallbackTitleForPath(filepath);
  if (pageEntries.empty()) {
    metadata.coverEntry = resolveMetadataOnlyCover(findFirstPageEntry());
  } else {
    metadata.coverEntry = resolveCoverEntry();
  }
  metadataLoaded = true;
  saveMetadataCache();
  return true;
}

std::string Cbz::findComicInfoEntry() const {
  ZipFile zip(filepath);
  std::string result;
  zip.enumerateFilePaths([&result](const std::string_view path) {
    if (!result.empty() || path.empty() || path.back() == '/') return;
    const std::string lower = lowerCopy(path);
    constexpr std::string_view suffix = "/comicinfo.xml";
    if (lower == "comicinfo.xml" ||
        (lower.size() > suffix.size() && lower.rfind(suffix) == lower.size() - suffix.size())) {
      result.assign(path);
    }
  });
  return result;
}

std::string Cbz::findFirstPageEntry() const {
  ZipFile zip(filepath);
  std::string result;
  zip.enumerateFilePaths([&result](const std::string_view path) {
    if (!isSupportedPage(path)) return;
    const std::string candidate(path);
    if (result.empty() || FsHelpers::naturalLess(candidate, result) ||
        (!FsHelpers::naturalLess(result, candidate) && candidate < result)) result = candidate;
  });
  return result;
}

std::string Cbz::resolveMetadataOnlyCover(const std::string& fallback) const {
  if (metadata.coverEntry.empty()) return fallback.empty() ? findFirstPageEntry() : fallback;

  std::string requested = lowerCopy(metadata.coverEntry);
  std::replace(requested.begin(), requested.end(), '\\', '/');
  if (isNumeric(requested)) {
    const size_t wanted = static_cast<size_t>(std::strtoul(requested.c_str(), nullptr, 10));
    if (wanted > 0) {
      ZipFile zip(filepath);
      size_t count = 0;
      std::string result;
      zip.enumerateFilePaths([&](const std::string_view path) {
        if (!result.empty() || !isSupportedPage(path)) return;
        if (++count == wanted) result.assign(path);
      });
      if (!result.empty()) return result;
    }
  } else {
    ZipFile zip(filepath);
    std::string result;
    zip.enumerateFilePaths([&](const std::string_view path) {
      if (!result.empty() || !isSupportedPage(path)) return;
      const std::string lower = lowerCopy(path);
      const size_t slash = lower.find_last_of('/');
      if (lower == requested || (slash != std::string::npos && lower.substr(slash + 1) == requested)) {
        result.assign(path);
      }
    });
    if (!result.empty()) return result;
  }
  return fallback.empty() ? findFirstPageEntry() : fallback;
}

bool Cbz::parseComicInfo(const std::string& xml) {
  if (xml.empty()) return false;
  metadata.title = xmlText(xml, "Title");
  metadata.series = xmlText(xml, "Series");
  metadata.number = xmlText(xml, "Number");
  metadata.writer = xmlText(xml, "Writer");
  metadata.penciller = xmlText(xml, "Penciller");
  metadata.inker = xmlText(xml, "Inker");
  metadata.colorist = xmlText(xml, "Colorist");
  metadata.letterer = xmlText(xml, "Letterer");
  metadata.coverArtist = xmlText(xml, "CoverArtist");
  metadata.editor = xmlText(xml, "Editor");
  metadata.publisher = xmlText(xml, "Publisher");
  metadata.genre = xmlText(xml, "Genre");
  metadata.summary = xmlText(xml, "Summary");
  metadata.year = xmlText(xml, "Year");
  metadata.month = xmlText(xml, "Month");
  metadata.day = xmlText(xml, "Day");
  metadata.languageIso = xmlText(xml, "LanguageISO");
  metadata.pageCount = xmlText(xml, "PageCount");

  size_t cursor = 0;
  while ((cursor = xml.find("<Page", cursor)) != std::string::npos) {
    const size_t end = xml.find('>', cursor);
    if (end == std::string::npos) break;
    const std::string element = xml.substr(cursor, end - cursor + 1);
    const std::string type = lowerCopy(xmlAttribute(element, "Type"));
    if (type == "frontcover" || type == "cover") {
      metadata.coverEntry = xmlAttribute(element, "Image");
      if (!metadata.coverEntry.empty()) break;
    }
    cursor = end + 1;
  }
  return true;
}

std::string Cbz::resolveCoverEntry() const {
  if (pageEntries.empty()) return {};
  if (!metadata.coverEntry.empty()) {
    std::string requested = lowerCopy(metadata.coverEntry);
    std::replace(requested.begin(), requested.end(), '\\', '/');
    if (isNumeric(requested)) {
      const size_t oneBased = static_cast<size_t>(std::strtoul(requested.c_str(), nullptr, 10));
      if (oneBased > 0 && oneBased <= pageEntries.size()) return pageEntries[oneBased - 1];
    }
    for (const auto& entry : pageEntries) {
      const std::string lower = lowerCopy(entry);
      if (lower == requested || lower.substr(lower.find_last_of('/') + 1) == requested) return entry;
    }
  }
  return pageEntries.front();
}

bool Cbz::loadMetadataCache() {
  HalFile file;
  if (!Storage.openFileForRead("CBZ", cachePath + METADATA_CACHE_FILE, file)) return false;
  uint8_t version = 0;
  serialization::readPod(file, version);
  if (version != METADATA_CACHE_VERSION) {
    file.close();
    return false;
  }
  constexpr uint32_t MAX_METADATA_FIELD_BYTES = 64u * 1024u;
  auto read = [&file](std::string& value) {
    uint32_t length = 0;
    if (file.read(&length, sizeof(length)) != sizeof(length) || length > MAX_METADATA_FIELD_BYTES ||
        length > file.size() - std::min<size_t>(file.position(), file.size())) {
      return false;
    }
    value.resize(length);
    return length == 0 || file.read(reinterpret_cast<uint8_t*>(&value[0]), length) == static_cast<int>(length);
  };
  if (!read(metadata.title) || !read(metadata.series) || !read(metadata.number) || !read(metadata.writer) ||
      !read(metadata.penciller) || !read(metadata.inker) || !read(metadata.colorist) ||
      !read(metadata.letterer) || !read(metadata.coverArtist) || !read(metadata.editor) ||
      !read(metadata.publisher) || !read(metadata.genre) || !read(metadata.summary) || !read(metadata.year) ||
      !read(metadata.month) || !read(metadata.day) || !read(metadata.languageIso) || !read(metadata.pageCount) ||
      !read(metadata.coverEntry)) {
    file.close();
    return false;
  }
  file.close();
  metadataLoaded = true;
  return true;
}

bool Cbz::saveMetadataCache() const {
  setupCacheDir();
  const std::string tempPath = cachePath + METADATA_CACHE_TEMP_FILE;
  const std::string finalPath = cachePath + METADATA_CACHE_FILE;
  if (Storage.exists(tempPath.c_str())) {
    logCbzCacheAction("remove", "metadata_rewrite_temp", tempPath);
    Storage.remove(tempPath.c_str());
  }
  HalFile file;
  if (!Storage.openFileForWrite("CBZ", tempPath, file)) return false;
  serialization::writePod(file, METADATA_CACHE_VERSION);
  const auto write = [&file](const std::string& value) { serialization::writeString(file, value); };
  write(metadata.title);
  write(metadata.series);
  write(metadata.number);
  write(metadata.writer);
  write(metadata.penciller);
  write(metadata.inker);
  write(metadata.colorist);
  write(metadata.letterer);
  write(metadata.coverArtist);
  write(metadata.editor);
  write(metadata.publisher);
  write(metadata.genre);
  write(metadata.summary);
  write(metadata.year);
  write(metadata.month);
  write(metadata.day);
  write(metadata.languageIso);
  write(metadata.pageCount);
  write(metadata.coverEntry);
  file.flush();
  const bool ok = file.close();
  if (!ok) {
    logCbzCacheAction("remove", "metadata_write_failed", tempPath);
    Storage.remove(tempPath.c_str());
    return false;
  }
  if (Storage.exists(finalPath.c_str())) {
    logCbzCacheAction("remove", "metadata_publish_replace", finalPath);
    Storage.remove(finalPath.c_str());
  }
  if (!Storage.rename(tempPath.c_str(), finalPath.c_str())) {
    logCbzCacheAction("remove", "metadata_publish_failed", tempPath);
    Storage.remove(tempPath.c_str());
    return false;
  }
  return true;
}

bool Cbz::clearCache() const {
  if (!Storage.exists(cachePath.c_str())) return true;

  // Keep the generated shelf thumbnail and progress.bin: the former is a
  // persistent presentation asset, while the cache utility restores the
  // latter after this operation. Reader page caches are deliberately removed
  // here so an explicit cache rebuild cannot replay an old render algorithm.
  const char* transientFiles[] = {"/metadata.bin", "/metadata.bin.tmp", "/current.jpg", "/current.jpeg",
                                  "/current.png", "/thumb_source.jpg", "/thumb_source.jpeg", "/thumb_source.png",
                                  "/render.pxc"};
  bool ok = true;
  for (const char* suffix : transientFiles) {
    const std::string path = cachePath + suffix;
    if (Storage.exists(path.c_str())) {
      LOG_DBG("CBZCACHE", "action=remove reason=clear_cache path=\"%s\"", path.c_str());
      if (!Storage.remove(path.c_str())) ok = false;
    }
  }

  std::vector<std::string> readerCacheFiles;
  HalFile directory = Storage.open(cachePath.c_str());
  if (directory && directory.isDirectory()) {
    directory.rewindDirectory();
    for (HalFile entry = directory.openNextFile(); entry; entry = directory.openNextFile()) {
      char name[96]{};
      entry.getName(name, sizeof(name));
      const bool remove = !entry.isDirectory() && isReaderCacheFile(name);
      entry.close();
      if (remove) readerCacheFiles.emplace_back(name);
    }
    directory.close();
  }
  for (const auto& name : readerCacheFiles) {
    const std::string path = cachePath + "/" + name;
    if (!Storage.exists(path.c_str())) continue;
    LOG_DBG("CBZCACHE", "action=remove reason=clear_reader_cache path=\"%s\"", path.c_str());
    if (!Storage.remove(path.c_str())) ok = false;
  }
  return ok;
}

void Cbz::setupCacheDir() const {
  if (Storage.exists(cachePath.c_str())) return;
  for (size_t i = 1; i < cachePath.length(); ++i) {
    if (cachePath[i] == '/') Storage.mkdir(cachePath.substr(0, i).c_str());
  }
  Storage.mkdir(cachePath.c_str());
}

std::string Cbz::getTitle() const {
  if (metadataLoaded && !metadata.title.empty()) {
    if (!metadata.series.empty() && !metadata.number.empty()) {
      return metadata.title + " (" + metadata.series + " #" + metadata.number + ")";
    }
    return metadata.title;
  }
  const size_t slash = filepath.find_last_of('/');
  const size_t start = slash == std::string::npos ? 0 : slash + 1;
  const size_t dot = filepath.find_last_of('.');
  const size_t end = dot == std::string::npos || dot <= start ? filepath.size() : dot;
  return filepath.substr(start, end - start);
}

std::string Cbz::getAuthor() const {
  if (!metadataLoaded) return {};
  if (!metadata.writer.empty()) return metadata.writer;
  if (!metadata.penciller.empty()) return metadata.penciller;
  if (!metadata.coverArtist.empty()) return metadata.coverArtist;
  if (!metadata.editor.empty()) return metadata.editor;
  return {};
}

std::string Cbz::getSynopsis() const { return metadataLoaded ? metadata.summary : std::string(); }

bool Cbz::generateThumbBmp(const int height) const {
  if (!metadataLoaded || height <= 0) return false;
  const std::string output = getThumbBmpPath(height);
  logCbzCacheLookup(output, Storage.exists(output.c_str()));
  if (isUsableThumbnail(output)) return true;
  const std::string cover = pageEntries.empty() ? metadata.coverEntry : resolveCoverEntry();
  if (cover.empty()) return false;
  size_t coverBytes = 0;
  ZipFile coverZip(filepath);
  if (coverZip.getInflatedFileSize(cover.c_str(), &coverBytes) &&
      (coverBytes == 0 || coverBytes > MAX_COVER_SOURCE_BYTES)) {
    LOG_DBG("CBZ", "Skipping oversized cover source (%lu bytes): %s", static_cast<unsigned long>(coverBytes),
            filepath.c_str());
    return false;
  }
  const size_t coverDot = cover.find_last_of('.');
  if (coverDot == std::string::npos) return false;
  const std::string coverExt = lowerCopy(std::string_view(cover).substr(coverDot));
  const std::string imagePath = cachePath + "/thumb_source" + coverExt;
  // Never use current.* for a shelf cover.  The reader owns current.* for
  // the active page and may still need it while a shelf refresh is running.
  if (!extractEntryToPath(cover, imagePath)) return false;
  const std::string tempOutput = output + ".tmp";
  if (Storage.exists(tempOutput.c_str())) {
    logCbzCacheAction("remove", "thumbnail_rewrite_temp", tempOutput);
    Storage.remove(tempOutput.c_str());
  }
  HalFile source;
  HalFile destination;
  const int targetWidth = std::max(1, height * 2 / 3);
  bool ok = false;
  if (FsHelpers::hasJpgExtension(imagePath) && Storage.openFileForRead("CBZ", imagePath, source) &&
      Storage.openFileForWrite("CBZ", tempOutput, destination)) {
    ok = JpegToBmpConverter::jpegFileTo1BitBmpStreamWithSize(source, destination, targetWidth, height);
  } else if (FsHelpers::hasPngExtension(imagePath) && Storage.openFileForRead("CBZ", imagePath, source) &&
             Storage.openFileForWrite("CBZ", tempOutput, destination)) {
    ok = PngToBmpConverter::pngFileTo1BitBmpStreamWithSize(source, destination, targetWidth, height);
  }
  if (source) source.close();
  bool outputClosed = true;
  if (destination) {
    destination.flush();
    outputClosed = destination.close();
  }
  logCbzCacheAction("remove", "thumbnail_source_done", imagePath);
  Storage.remove(imagePath.c_str());
  // Do not expose a partial BMP to Recent/Library.  Keep an older valid
  // thumbnail if conversion fails, and publish a replacement only after the
  // temporary file has closed and passed a lightweight BMP validation.
  if (!ok || !outputClosed || !isUsableThumbnail(tempOutput)) {
    logCbzCacheAction("remove", "thumbnail_publish_failed", tempOutput);
    Storage.remove(tempOutput.c_str());
    return false;
  }
  if (Storage.exists(output.c_str())) {
    logCbzCacheAction("remove", "thumbnail_publish_replace", output);
    if (!Storage.remove(output.c_str())) {
      logCbzCacheAction("remove", "thumbnail_publish_failed", tempOutput);
      Storage.remove(tempOutput.c_str());
      return false;
    }
  }
  if (!Storage.rename(tempOutput.c_str(), output.c_str())) {
    logCbzCacheAction("remove", "thumbnail_rename_failed", tempOutput);
    Storage.remove(tempOutput.c_str());
    return false;
  }
  return true;
}

bool Cbz::extractPage(const size_t index, std::string& outputPath) const {
  outputPath.clear();
  if (!loaded || index >= pageEntries.size()) return false;
  return extractEntry(pageEntries[index], outputPath);
}

bool Cbz::extractPageTo(const size_t index, const std::string& outputPath, const AbortCallback abortCallback,
                        void* const abortContext) const {
  if (!loaded || index >= pageEntries.size() || outputPath.empty()) return false;
  return extractEntryToPath(pageEntries[index], outputPath, abortCallback, abortContext);
}

bool Cbz::extractEntryToPath(const std::string& entry, const std::string& outputPath,
                             const AbortCallback abortCallback, void* const abortContext) const {
  const size_t dot = entry.find_last_of('.');
  if (dot == std::string::npos) return false;
  const std::string ext = lowerCopy(std::string_view(entry).substr(dot));
  if (ext != ".jpg" && ext != ".jpeg" && ext != ".png") return false;

  size_t uncompressedSize = 0;
  ZipFile zip(filepath);
  // Keep one ZIP handle through the size check and streamed extraction. The
  // previous closed/reopened sequence rescanned the central directory before
  // every uncached page without changing the safety decision.
  if (!zip.open()) return false;
  if (zip.getInflatedFileSize(entry.c_str(), &uncompressedSize) &&
      (uncompressedSize == 0 || uncompressedSize > MAX_PAGE_ENTRY_BYTES)) {
    LOG_ERR("CBZ", "Skipping unsafe page entry (%lu bytes): %s", static_cast<unsigned long>(uncompressedSize),
            entry.c_str());
    zip.close();
    return false;
  }

  setupCacheDir();
  if (Storage.exists(outputPath.c_str())) {
    LOG_DBG("CBZCACHE", "action=remove reason=replace_extract path=\"%s\"", outputPath.c_str());
    Storage.remove(outputPath.c_str());
  }

  HalFile output;
  if (!Storage.openFileForWrite("CBZ", outputPath, output)) {
    zip.close();
    return false;
  }
  const bool streamed = zip.readFileToStream(entry.c_str(), output, STREAM_CHUNK_SIZE, false, abortCallback,
                                             abortContext);
  output.flush();
  output.close();
  zip.close();
  if (!streamed) {
    LOG_DBG("CBZCACHE", "action=remove reason=extract_failed path=\"%s\"", outputPath.c_str());
    Storage.remove(outputPath.c_str());
    if (abortCallback && abortCallback(abortContext)) {
      LOG_DBG("CBZ", "Extraction cancelled: %s", entry.c_str());
    } else {
      LOG_ERR("CBZ", "Could not extract page: %s", entry.c_str());
    }
    return false;
  }

  HalFile check;
  const bool usable = Storage.openFileForRead("CBZ", outputPath, check) && check.size() > 16;
  if (check) check.close();
  if (!usable) {
    LOG_DBG("CBZCACHE", "action=remove reason=unusable_extract path=\"%s\"", outputPath.c_str());
    Storage.remove(outputPath.c_str());
    return false;
  }
  return true;
}

bool Cbz::extractEntry(const std::string& entry, std::string& outputPath) const {
  outputPath.clear();
  const size_t dot = entry.find_last_of('.');
  if (dot == std::string::npos) return false;
  const std::string ext = lowerCopy(std::string_view(entry).substr(dot));
  if (ext != ".jpg" && ext != ".jpeg" && ext != ".png") return false;

  outputPath = cachePath + "/current" + ext;
  // A page change owns the current source path. Remove only the old current
  // variants; thumbnail extraction uses thumb_source.* and is independent.
  const char* currentFiles[] = {"/current.jpg", "/current.jpeg", "/current.png"};
  for (const char* suffix : currentFiles) {
    const std::string path = cachePath + suffix;
    if (Storage.exists(path.c_str())) {
      LOG_DBG("CBZCACHE", "action=remove reason=replace_page path=\"%s\"", path.c_str());
      Storage.remove(path.c_str());
    }
  }
  if (!extractEntryToPath(entry, outputPath)) {
    outputPath.clear();
    return false;
  }
  return true;
}
