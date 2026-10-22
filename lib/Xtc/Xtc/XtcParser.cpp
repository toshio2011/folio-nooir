/**
 * XtcParser.cpp
 *
 * XTC file parsing implementation
 * XTC ebook support for CrossPoint Reader
 */

#include "XtcParser.h"

#include <FsHelpers.h>
#include <HalStorage.h>
#include <Logging.h>

#include <cstdlib>
#include <cstring>
#include <memory>
#include <utility>

namespace xtc {
namespace {
constexpr uint8_t kNooirMetadataMagic[8] = {'N', 'O', 'O', 'I', 'R', 'M', '0', '1'};
constexpr uint32_t kMaxNooirMetadataBytes = 4096;
constexpr uint8_t kMaxMetadataJsonDepth = 16;

class BoundedMetadataJsonParser {
 public:
  BoundedMetadataJsonParser(const char* data, const size_t length) : data_(data), length_(length) {}

  bool parse(std::string& description, std::string& series) {
    skipWhitespace();
    if (!parseObject(1, true)) return false;
    skipWhitespace();
    if (position_ != length_) return false;
    description = std::move(description_);
    series = std::move(series_);
    return true;
  }

 private:
  struct FixedString {
    char* data;
    size_t capacity;
    size_t length = 0;
    bool overflow = false;
  };

  bool atEnd() const { return position_ >= length_; }
  char peek() const { return atEnd() ? '\0' : data_[position_]; }

  void skipWhitespace() {
    while (!atEnd() && (data_[position_] == ' ' || data_[position_] == '\t' || data_[position_] == '\r' ||
                        data_[position_] == '\n')) {
      ++position_;
    }
  }

  bool consume(const char expected) {
    if (atEnd() || data_[position_] != expected) return false;
    ++position_;
    return true;
  }

  static void appendByte(const uint8_t byte, std::string* output, FixedString* fixed) {
    if (output) output->push_back(static_cast<char>(byte));
    if (!fixed) return;
    if (fixed->length < fixed->capacity - 1u) {
      fixed->data[fixed->length] = static_cast<char>(byte);
    } else {
      fixed->overflow = true;
    }
    ++fixed->length;
  }

  static void appendCodePoint(const uint32_t codePoint, std::string* output, FixedString* fixed) {
    if (codePoint <= 0x7Fu) {
      appendByte(static_cast<uint8_t>(codePoint), output, fixed);
    } else if (codePoint <= 0x7FFu) {
      appendByte(static_cast<uint8_t>(0xC0u | (codePoint >> 6u)), output, fixed);
      appendByte(static_cast<uint8_t>(0x80u | (codePoint & 0x3Fu)), output, fixed);
    } else if (codePoint <= 0xFFFFu) {
      appendByte(static_cast<uint8_t>(0xE0u | (codePoint >> 12u)), output, fixed);
      appendByte(static_cast<uint8_t>(0x80u | ((codePoint >> 6u) & 0x3Fu)), output, fixed);
      appendByte(static_cast<uint8_t>(0x80u | (codePoint & 0x3Fu)), output, fixed);
    } else {
      appendByte(static_cast<uint8_t>(0xF0u | (codePoint >> 18u)), output, fixed);
      appendByte(static_cast<uint8_t>(0x80u | ((codePoint >> 12u) & 0x3Fu)), output, fixed);
      appendByte(static_cast<uint8_t>(0x80u | ((codePoint >> 6u) & 0x3Fu)), output, fixed);
      appendByte(static_cast<uint8_t>(0x80u | (codePoint & 0x3Fu)), output, fixed);
    }
  }

  bool readHexQuad(uint32_t& value) {
    if (length_ - position_ < 4u) return false;
    value = 0;
    for (uint8_t i = 0; i < 4; ++i) {
      const char c = data_[position_++];
      uint8_t digit;
      if (c >= '0' && c <= '9') {
        digit = static_cast<uint8_t>(c - '0');
      } else if (c >= 'a' && c <= 'f') {
        digit = static_cast<uint8_t>(c - 'a' + 10);
      } else if (c >= 'A' && c <= 'F') {
        digit = static_cast<uint8_t>(c - 'A' + 10);
      } else {
        return false;
      }
      value = (value << 4u) | digit;
    }
    return true;
  }

  bool parseUnicodeEscape(uint32_t& codePoint) {
    uint32_t first;
    if (!readHexQuad(first)) return false;
    if (first >= 0xD800u && first <= 0xDBFFu) {
      if (length_ - position_ < 6u || data_[position_] != '\\' || data_[position_ + 1u] != 'u') return false;
      position_ += 2u;
      uint32_t second;
      if (!readHexQuad(second) || second < 0xDC00u || second > 0xDFFFu) return false;
      codePoint = 0x10000u + ((first - 0xD800u) << 10u) + (second - 0xDC00u);
      return true;
    }
    if (first >= 0xDC00u && first <= 0xDFFFu) return false;
    codePoint = first;
    return true;
  }

  bool parseRawUtf8(std::string* output, FixedString* fixed) {
    const size_t start = position_;
    const uint8_t first = static_cast<uint8_t>(data_[position_++]);
    uint32_t codePoint;
    uint8_t continuationCount;
    if (first >= 0xC2u && first <= 0xDFu) {
      codePoint = first & 0x1Fu;
      continuationCount = 1;
    } else if (first >= 0xE0u && first <= 0xEFu) {
      codePoint = first & 0x0Fu;
      continuationCount = 2;
    } else if (first >= 0xF0u && first <= 0xF4u) {
      codePoint = first & 0x07u;
      continuationCount = 3;
    } else {
      return false;
    }
    if (length_ - position_ < continuationCount) return false;
    for (uint8_t i = 0; i < continuationCount; ++i) {
      const uint8_t next = static_cast<uint8_t>(data_[position_++]);
      if ((next & 0xC0u) != 0x80u) return false;
      codePoint = (codePoint << 6u) | (next & 0x3Fu);
    }
    if ((continuationCount == 1 && codePoint < 0x80u) || (continuationCount == 2 && codePoint < 0x800u) ||
        (continuationCount == 3 && codePoint < 0x10000u) || codePoint > 0x10FFFFu ||
        (codePoint >= 0xD800u && codePoint <= 0xDFFFu)) {
      return false;
    }
    for (size_t i = start; i < position_; ++i) {
      appendByte(static_cast<uint8_t>(data_[i]), output, fixed);
    }
    return true;
  }

  bool parseString(std::string* output, FixedString* fixed = nullptr) {
    if (!consume('"')) return false;
    while (!atEnd()) {
      const uint8_t c = static_cast<uint8_t>(data_[position_++]);
      if (c == '"') {
        if (fixed && fixed->capacity > 0 && !fixed->overflow) fixed->data[fixed->length] = '\0';
        return true;
      }
      if (c < 0x20u) return false;
      if (c == '\\') {
        if (atEnd()) return false;
        const char escape = data_[position_++];
        switch (escape) {
          case '"':
          case '\\':
          case '/':
            appendByte(static_cast<uint8_t>(escape), output, fixed);
            break;
          case 'b':
            appendByte('\b', output, fixed);
            break;
          case 'f':
            appendByte('\f', output, fixed);
            break;
          case 'n':
            appendByte('\n', output, fixed);
            break;
          case 'r':
            appendByte('\r', output, fixed);
            break;
          case 't':
            appendByte('\t', output, fixed);
            break;
          case 'u': {
            uint32_t codePoint;
            if (!parseUnicodeEscape(codePoint)) return false;
            appendCodePoint(codePoint, output, fixed);
            break;
          }
          default:
            return false;
        }
      } else if (c < 0x80u) {
        appendByte(c, output, fixed);
      } else {
        --position_;
        if (!parseRawUtf8(output, fixed)) return false;
      }
    }
    return false;
  }

  static bool keyEquals(const FixedString& key, const char* expected, const size_t expectedLength) {
    return !key.overflow && key.length == expectedLength && std::memcmp(key.data, expected, expectedLength) == 0;
  }

  bool parseObject(const uint8_t depth, const bool root) {
    if (depth > kMaxMetadataJsonDepth || !consume('{')) return false;
    skipWhitespace();
    if (consume('}')) return true;
    for (;;) {
      char keyBuffer[16] = {};
      FixedString key{keyBuffer, sizeof(keyBuffer)};
      if (!parseString(nullptr, &key)) return false;
      skipWhitespace();
      if (!consume(':')) return false;
      skipWhitespace();

      const bool isDescription = root && keyEquals(key, "description", sizeof("description") - 1u);
      const bool isSeries = root && keyEquals(key, "series", sizeof("series") - 1u);
      if (isDescription || isSeries) {
        bool& seen = isDescription ? sawDescription_ : sawSeries_;
        if (seen || peek() != '"') return false;
        seen = true;
        if (isDescription) {
          if (!parseString(&description_)) return false;
        } else if (!parseString(&series_)) {
          return false;
        }
      } else if (!parseValue(static_cast<uint8_t>(depth + 1u))) {
        return false;
      }

      skipWhitespace();
      if (consume('}')) return true;
      if (!consume(',')) return false;
      skipWhitespace();
    }
  }

  bool parseArray(const uint8_t depth) {
    if (depth > kMaxMetadataJsonDepth || !consume('[')) return false;
    skipWhitespace();
    if (consume(']')) return true;
    for (;;) {
      if (!parseValue(static_cast<uint8_t>(depth + 1u))) return false;
      skipWhitespace();
      if (consume(']')) return true;
      if (!consume(',')) return false;
      skipWhitespace();
    }
  }

  bool parseNumber() {
    if (consume('-') && atEnd()) return false;
    if (consume('0')) {
      if (!atEnd() && data_[position_] >= '0' && data_[position_] <= '9') return false;
    } else {
      if (atEnd() || data_[position_] < '1' || data_[position_] > '9') return false;
      while (!atEnd() && data_[position_] >= '0' && data_[position_] <= '9') ++position_;
    }
    if (consume('.')) {
      if (atEnd() || data_[position_] < '0' || data_[position_] > '9') return false;
      while (!atEnd() && data_[position_] >= '0' && data_[position_] <= '9') ++position_;
    }
    if (consume('e') || consume('E')) {
      if (!consume('+')) consume('-');
      if (atEnd() || data_[position_] < '0' || data_[position_] > '9') return false;
      while (!atEnd() && data_[position_] >= '0' && data_[position_] <= '9') ++position_;
    }
    return true;
  }

  bool parseValue(const uint8_t depth) {
    if (depth > kMaxMetadataJsonDepth || atEnd()) return false;
    switch (peek()) {
      case '"':
        return parseString(nullptr);
      case '{':
        return parseObject(depth, false);
      case '[':
        return parseArray(depth);
      case 't':
        return consume('t') && consume('r') && consume('u') && consume('e');
      case 'f':
        return consume('f') && consume('a') && consume('l') && consume('s') && consume('e');
      case 'n':
        return consume('n') && consume('u') && consume('l') && consume('l');
      default:
        return parseNumber();
    }
  }

  const char* data_;
  size_t length_;
  size_t position_ = 0;
  bool sawDescription_ = false;
  bool sawSeries_ = false;
  std::string description_;
  std::string series_;
};
}  // namespace

XtcParser::XtcParser()
    : m_isOpen(false),
      m_defaultWidth(DISPLAY_WIDTH),
      m_defaultHeight(DISPLAY_HEIGHT),
      m_bitDepth(1),
      m_hasChapters(false),
      m_chaptersLoaded(false),
      m_extendedMetadataLoaded(false),
      m_lastError(XtcError::OK) {
  memset(&m_header, 0, sizeof(m_header));
}

XtcParser::~XtcParser() { close(); }

XtcError XtcParser::open(const char* filepath) {
  // Close if already open
  if (m_isOpen) {
    close();
  }

  m_filepath = filepath;

  // Open file
  if (!Storage.openFileForRead("XTC", filepath, m_file)) {
    m_lastError = XtcError::FILE_NOT_FOUND;
    return m_lastError;
  }

  // Read header
  m_lastError = readHeader();
  if (m_lastError != XtcError::OK) {
    LOG_DBG("XTC", "Failed to read header: %s", errorToString(m_lastError));
    // Explicit close() required: member variable persists beyond function scope
    m_file.close();
    return m_lastError;
  }

  // Read title & author if available
  if (m_header.hasMetadata) {
    m_lastError = readTitle();
    if (m_lastError != XtcError::OK) {
      LOG_DBG("XTC", "Failed to read title: %s", errorToString(m_lastError));
      // Explicit close() required: member variable persists beyond function scope
      m_file.close();
      return m_lastError;
    }
    m_lastError = readAuthor();
    if (m_lastError != XtcError::OK) {
      LOG_DBG("XTC", "Failed to read author: %s", errorToString(m_lastError));
      // Explicit close() required: member variable persists beyond function scope
      m_file.close();
      return m_lastError;
    }
    // Trim excess capacity from metadata strings
    m_title.shrink_to_fit();
    m_author.shrink_to_fit();
  }

  // Read first page info for default dimensions (no bulk page table allocation)
  m_lastError = readFirstPageInfo();
  if (m_lastError != XtcError::OK) {
    LOG_DBG("XTC", "Failed to read first page info: %s", errorToString(m_lastError));
    // Explicit close() required: member variable persists beyond function scope
    m_file.close();
    return m_lastError;
  }

  // Defer chapter parsing until actually needed (lazy load).
  // Chapter strings can use significant heap; keeping them out of memory
  // during rendering leaves more room for the page bitmap buffer.
  // Older XTC files start the page table at 0x30, so they do not have the later
  // chapterOffset field even if the bytes read into that slot are non-zero.
  m_hasChapters = (m_header.hasChapters == 1 && m_header.pageTableOffset >= sizeof(XtcHeader));
  m_chaptersLoaded = false;

  // Close the source file to free its internal SdFat buffers.
  // It will be reopened on-demand for page table lookups and bitmap reads.
  m_file.close();

  m_isOpen = true;
  LOG_DBG("XTC", "Opened file: %s (%u pages, %dx%d)", filepath, m_header.pageCount, m_defaultWidth, m_defaultHeight);
  return XtcError::OK;
}

void XtcParser::close() {
  closeFile();
  m_isOpen = false;
  m_chaptersLoaded = false;
  m_chapters.clear();
  m_title.clear();
  m_author.clear();
  m_description.clear();
  m_series.clear();
  std::string().swap(m_description);
  std::string().swap(m_series);
  m_hasChapters = false;
  memset(&m_header, 0, sizeof(m_header));
  m_extendedMetadataLoaded = false;
}

bool XtcParser::ensureFileOpen() {
  if (m_file.isOpen()) {
    return true;
  }
  return Storage.openFileForRead("XTC", m_filepath.c_str(), m_file);
}

void XtcParser::closeFile() {
  if (m_file.isOpen()) {
    m_file.close();
  }
}

XtcError XtcParser::readHeader() {
  // Read first 56 bytes of header
  size_t bytesRead = m_file.read(reinterpret_cast<uint8_t*>(&m_header), sizeof(XtcHeader));
  if (bytesRead != sizeof(XtcHeader)) {
    return XtcError::READ_ERROR;
  }

  // Verify magic number (accept both XTC and XTCH)
  if (m_header.magic != XTC_MAGIC && m_header.magic != XTCH_MAGIC) {
    LOG_DBG("XTC", "Invalid magic: 0x%08X (expected 0x%08X or 0x%08X)", m_header.magic, XTC_MAGIC, XTCH_MAGIC);
    return XtcError::INVALID_MAGIC;
  }

  // Determine bit depth from file magic
  m_bitDepth = (m_header.magic == XTCH_MAGIC) ? 2 : 1;

  // Check version
  // Currently, version 1.0 is the only valid version, however some generators are swapping the bytes around, so we
  // accept both 1.0 and 0.1 for compatibility
  const bool validVersion = m_header.versionMajor == 1 && m_header.versionMinor == 0 ||
                            m_header.versionMajor == 0 && m_header.versionMinor == 1;
  if (!validVersion) {
    LOG_DBG("XTC", "Unsupported version: %u.%u", m_header.versionMajor, m_header.versionMinor);
    return XtcError::INVALID_VERSION;
  }

  // Basic validation
  if (m_header.pageCount == 0) {
    return XtcError::CORRUPTED_HEADER;
  }

  LOG_DBG("XTC", "Header: magic=0x%08X (%s), ver=%u.%u, pages=%u, bitDepth=%u", m_header.magic,
          (m_header.magic == XTCH_MAGIC) ? "XTCH" : "XTC", m_header.versionMajor, m_header.versionMinor,
          m_header.pageCount, m_bitDepth);

  return XtcError::OK;
}

XtcError XtcParser::readTitle() {
  constexpr auto titleOffset = 0x38;
  if (!m_file.seek(titleOffset)) {
    return XtcError::READ_ERROR;
  }

  char titleBuf[128] = {0};
  m_file.read(titleBuf, sizeof(titleBuf) - 1);
  m_title = titleBuf;

  LOG_DBG("XTC", "Title: %s", m_title.c_str());
  return XtcError::OK;
}

XtcError XtcParser::readAuthor() {
  // Read author as null-terminated UTF-8 string with max length 64, directly following title
  constexpr auto authorOffset = 0xB8;
  if (!m_file.seek(authorOffset)) {
    return XtcError::READ_ERROR;
  }

  char authorBuf[64] = {0};
  m_file.read(authorBuf, sizeof(authorBuf) - 1);
  m_author = authorBuf;

  LOG_DBG("XTC", "Author: %s", m_author.c_str());
  return XtcError::OK;
}

const std::string& XtcParser::getDescription() {
  if (m_isOpen && !m_extendedMetadataLoaded) loadExtendedMetadata();
  return m_description;
}

const std::string& XtcParser::getSeries() {
  if (m_isOpen && !m_extendedMetadataLoaded) loadExtendedMetadata();
  return m_series;
}

void XtcParser::loadExtendedMetadata() {
  if (m_extendedMetadataLoaded || !m_isOpen) return;
  if (!ensureFileOpen()) return;

  struct CloseFileOnExit {
    HalFile& file;
    ~CloseFileOnExit() { file.close(); }
  } closeFileOnExit{m_file};

  constexpr size_t footerSize = sizeof(kNooirMetadataMagic) + sizeof(uint32_t);
  const uint64_t fileSize = m_file.fileSize64();
  if (fileSize < footerSize) {
    m_extendedMetadataLoaded = true;
    return;
  }
  if (!m_file.seek64(fileSize - footerSize)) return;

  uint8_t footer[footerSize];
  if (m_file.read(footer, sizeof(footer)) != sizeof(footer)) return;
  m_extendedMetadataLoaded = true;
  if (std::memcmp(footer, kNooirMetadataMagic, sizeof(kNooirMetadataMagic)) != 0) return;
  const size_t lengthOffset = sizeof(kNooirMetadataMagic);
  const uint32_t payloadLength = static_cast<uint32_t>(footer[lengthOffset]) |
                                (static_cast<uint32_t>(footer[lengthOffset + 1u]) << 8u) |
                                (static_cast<uint32_t>(footer[lengthOffset + 2u]) << 16u) |
                                (static_cast<uint32_t>(footer[lengthOffset + 3u]) << 24u);
  if (payloadLength == 0 || payloadLength > kMaxNooirMetadataBytes || payloadLength > fileSize - footerSize) return;
  const uint64_t payloadOffset = fileSize - footerSize - payloadLength;

  if (m_header.pageCount == 0 || m_header.pageTableOffset > fileSize) return;
  const uint64_t lastEntryIndex = static_cast<uint64_t>(m_header.pageCount - 1u) * sizeof(PageTableEntry);
  if (lastEntryIndex > fileSize - m_header.pageTableOffset ||
      sizeof(PageTableEntry) > fileSize - m_header.pageTableOffset - lastEntryIndex) {
    return;
  }
  const uint64_t pageTableEnd = m_header.pageTableOffset +
                                static_cast<uint64_t>(m_header.pageCount) * sizeof(PageTableEntry);
  PageTableEntry lastPage;
  const uint64_t lastEntryOffset = m_header.pageTableOffset + lastEntryIndex;
  if (!m_file.seek64(lastEntryOffset)) {
    m_extendedMetadataLoaded = false;
    return;
  }
  if (m_file.read(reinterpret_cast<uint8_t*>(&lastPage), sizeof(lastPage)) != sizeof(lastPage)) {
    m_extendedMetadataLoaded = false;
    return;
  }
  if (lastPage.dataOffset < pageTableEnd || lastPage.dataOffset > fileSize ||
      lastPage.dataSize > fileSize - lastPage.dataOffset) return;
  const uint64_t pageDataEnd = lastPage.dataOffset + lastPage.dataSize;
  if (payloadOffset != pageDataEnd) return;
  if (!m_file.seek64(payloadOffset)) {
    m_extendedMetadataLoaded = false;
    return;
  }

  char* payload = static_cast<char*>(malloc(payloadLength));
  if (!payload) {
    m_extendedMetadataLoaded = false;
    return;
  }
  const bool readComplete = m_file.read(reinterpret_cast<uint8_t*>(payload), payloadLength) == payloadLength;
  if (readComplete) {
    BoundedMetadataJsonParser parser(payload, payloadLength);
    std::string description;
    std::string series;
    if (parser.parse(description, series)) {
      m_description = std::move(description);
      m_series = std::move(series);
    }
  } else {
    m_extendedMetadataLoaded = false;
  }
  free(payload);
}

XtcError XtcParser::readFirstPageInfo() {
  if (m_header.pageTableOffset == 0) {
    LOG_DBG("XTC", "Page table offset is 0, cannot read");
    return XtcError::CORRUPTED_HEADER;
  }

  // Verify the file is large enough to contain the full page table
  const uint64_t fileSize = m_file.fileSize64();
  const uint64_t pageTableSize = static_cast<uint64_t>(m_header.pageCount) * sizeof(PageTableEntry);
  if (m_header.pageTableOffset < XTC_LEGACY_HEADER_SIZE || m_header.pageTableOffset > fileSize ||
      pageTableSize > fileSize - m_header.pageTableOffset) {
    LOG_DBG("XTC",
            "Page table exceeds file bounds: file=%llu tableOffset=%llu tableSize=%llu pages=%u entrySize=%u "
            "dataOffset=%llu minTableOffset=%llu",
            static_cast<unsigned long long>(fileSize), static_cast<unsigned long long>(m_header.pageTableOffset),
            static_cast<unsigned long long>(pageTableSize), m_header.pageCount,
            static_cast<unsigned int>(sizeof(PageTableEntry)), static_cast<unsigned long long>(m_header.dataOffset),
            static_cast<unsigned long long>(XTC_LEGACY_HEADER_SIZE));
    return XtcError::CORRUPTED_HEADER;
  }

  // Read only the first entry to get default page dimensions
  // All other entries are read on-demand via readPageTableEntry()
  // This avoids allocating pageCount * 16 bytes (e.g. 65KB for 4000+ pages)
  PageTableEntry entry;
  if (!m_file.seek64(m_header.pageTableOffset)) {
    LOG_DBG("XTC", "Failed to seek to page table at %llu", m_header.pageTableOffset);
    return XtcError::READ_ERROR;
  }
  size_t bytesRead = m_file.read(reinterpret_cast<uint8_t*>(&entry), sizeof(PageTableEntry));
  if (bytesRead != sizeof(PageTableEntry)) {
    LOG_DBG("XTC", "Failed to read first page table entry");
    return XtcError::READ_ERROR;
  }

  m_defaultWidth = entry.width;
  m_defaultHeight = entry.height;

  LOG_DBG("XTC", "Page table validated: %u pages, default %dx%d", m_header.pageCount, m_defaultWidth, m_defaultHeight);
  return XtcError::OK;
}

bool XtcParser::readPageTableEntry(uint32_t pageIndex, PageInfo& info) {
  if (pageIndex >= m_header.pageCount) {
    return false;
  }

  if (!ensureFileOpen()) {
    LOG_DBG("XTC", "Failed to reopen file for page table read");
    return false;
  }

  // Seek to the specific page table entry on the SD card
  const uint64_t entryOffset = m_header.pageTableOffset + static_cast<uint64_t>(pageIndex) * sizeof(PageTableEntry);
  if (!m_file.seek64(entryOffset)) {
    LOG_DBG("XTC", "Failed to seek to page table entry %lu at %llu", pageIndex, entryOffset);
    return false;
  }

  PageTableEntry entry;
  size_t bytesRead = m_file.read(reinterpret_cast<uint8_t*>(&entry), sizeof(PageTableEntry));
  if (bytesRead != sizeof(PageTableEntry)) {
    LOG_DBG("XTC", "Failed to read page table entry %lu", pageIndex);
    return false;
  }

  info.offset = entry.dataOffset;
  info.size = entry.dataSize;
  info.width = entry.width;
  info.height = entry.height;
  info.bitDepth = m_bitDepth;
  return true;
}

XtcError XtcParser::readChapters() {
  m_chapters.clear();

  if (!ensureFileOpen()) {
    return XtcError::READ_ERROR;
  }

  uint8_t hasChaptersFlag = 0;
  if (!m_file.seek(0x0B)) {
    return XtcError::READ_ERROR;
  }
  if (m_file.read(&hasChaptersFlag, sizeof(hasChaptersFlag)) != sizeof(hasChaptersFlag)) {
    return XtcError::READ_ERROR;
  }

  if (hasChaptersFlag != 1) {
    return XtcError::OK;
  }

  uint64_t chapterOffset = 0;
  if (!m_file.seek(0x30)) {
    return XtcError::READ_ERROR;
  }
  if (m_file.read(reinterpret_cast<uint8_t*>(&chapterOffset), sizeof(chapterOffset)) != sizeof(chapterOffset)) {
    return XtcError::READ_ERROR;
  }

  if (chapterOffset == 0) {
    return XtcError::OK;
  }

  const uint64_t fileSize = m_file.fileSize64();
  if (chapterOffset < sizeof(XtcHeader) || chapterOffset >= fileSize || chapterOffset + 96 > fileSize) {
    return XtcError::OK;
  }

  // Clamp maxOffset to fileSize so bogus header values can't inflate chapterCount
  uint64_t maxOffset = fileSize;
  if (m_header.pageTableOffset > chapterOffset && m_header.pageTableOffset <= fileSize) {
    maxOffset = m_header.pageTableOffset;
  } else if (m_header.dataOffset > chapterOffset && m_header.dataOffset <= fileSize) {
    maxOffset = m_header.dataOffset;
  }

  if (maxOffset <= chapterOffset) {
    return XtcError::OK;
  }

  constexpr size_t chapterSize = 96;
  const uint64_t available = maxOffset - chapterOffset;
  const size_t chapterCount = static_cast<size_t>(available / chapterSize);
  if (chapterCount == 0) {
    return XtcError::OK;
  }

  if (!m_file.seek64(chapterOffset)) {
    return XtcError::READ_ERROR;
  }

  m_chapters.reserve(chapterCount);
  std::vector<uint8_t> chapterBuf(chapterSize);
  for (size_t i = 0; i < chapterCount; i++) {
    if (m_file.read(chapterBuf.data(), chapterSize) != chapterSize) {
      return XtcError::READ_ERROR;
    }

    char nameBuf[81];
    memcpy(nameBuf, chapterBuf.data(), 80);
    nameBuf[80] = '\0';
    const size_t nameLen = strnlen(nameBuf, 80);
    std::string name(nameBuf, nameLen);

    uint16_t startPage = 0;
    uint16_t endPage = 0;
    memcpy(&startPage, chapterBuf.data() + 0x50, sizeof(startPage));
    memcpy(&endPage, chapterBuf.data() + 0x52, sizeof(endPage));

    if (name.empty() && startPage == 0 && endPage == 0) {
      break;
    }

    if (startPage > 0) {
      startPage--;
    }
    if (endPage > 0) {
      endPage--;
    }

    if (startPage >= m_header.pageCount) {
      continue;
    }

    if (endPage >= m_header.pageCount) {
      endPage = m_header.pageCount - 1;
    }

    if (startPage > endPage) {
      continue;
    }

    ChapterInfo chapter{std::move(name), startPage, endPage};
    m_chapters.push_back(std::move(chapter));
  }

  m_hasChapters = !m_chapters.empty();
  LOG_DBG("XTC", "Chapters: %u", static_cast<unsigned int>(m_chapters.size()));
  return XtcError::OK;
}

const std::vector<ChapterInfo>& XtcParser::getChapters() {
  // Lazy load chapters on first access
  if (!m_chaptersLoaded && m_hasChapters) {
    const XtcError err = readChapters();
    if (err != XtcError::OK) {
      LOG_ERR("XTC", "Failed to lazy-load chapters: %s", errorToString(err));
      m_hasChapters = false;
      m_chapters.clear();
    }
    m_chaptersLoaded = true;
    // Close file after chapter read to free buffers for rendering
    closeFile();
  }
  return m_chapters;
}

bool XtcParser::getPageInfo(uint32_t pageIndex, PageInfo& info) { return readPageTableEntry(pageIndex, info); }

size_t XtcParser::loadPage(uint32_t pageIndex, uint8_t* buffer, size_t bufferSize) {
  if (!m_isOpen) {
    m_lastError = XtcError::FILE_NOT_FOUND;
    return 0;
  }

  if (pageIndex >= m_header.pageCount) {
    m_lastError = XtcError::PAGE_OUT_OF_RANGE;
    return 0;
  }

  PageInfo page;
  if (!readPageTableEntry(pageIndex, page)) {
    m_lastError = XtcError::READ_ERROR;
    return 0;
  }

  if (!ensureFileOpen()) {
    m_lastError = XtcError::FILE_NOT_FOUND;
    return 0;
  }

  // Seek to page data
  if (!m_file.seek64(page.offset)) {
    LOG_DBG("XTC", "Failed to seek to page %u at offset %llu", pageIndex, static_cast<unsigned long long>(page.offset));
    m_lastError = XtcError::READ_ERROR;
    return 0;
  }

  // Read page header (XTG for 1-bit, XTH for 2-bit - same structure)
  XtgPageHeader pageHeader;
  size_t headerRead = m_file.read(reinterpret_cast<uint8_t*>(&pageHeader), sizeof(XtgPageHeader));
  if (headerRead != sizeof(XtgPageHeader)) {
    LOG_DBG("XTC", "Failed to read page header for page %u", pageIndex);
    m_lastError = XtcError::READ_ERROR;
    return 0;
  }

  // Verify page magic (XTG for 1-bit, XTH for 2-bit)
  const uint32_t expectedMagic = (m_bitDepth == 2) ? XTH_MAGIC : XTG_MAGIC;
  if (pageHeader.magic != expectedMagic) {
    LOG_DBG("XTC", "Invalid page magic for page %u: 0x%08X (expected 0x%08X)", pageIndex, pageHeader.magic,
            expectedMagic);
    m_lastError = XtcError::INVALID_MAGIC;
    return 0;
  }

  // Calculate bitmap size based on bit depth
  // XTG (1-bit): Row-major, ((width+7)/8) * height bytes
  // XTH (2-bit): Two bit planes, column-major, ((width * height + 7) / 8) * 2 bytes
  size_t bitmapSize;
  if (m_bitDepth == 2) {
    // XTH: two bit planes, each containing (width * height) bits rounded up to bytes
    bitmapSize = ((static_cast<size_t>(pageHeader.width) * pageHeader.height + 7) / 8) * 2;
  } else {
    bitmapSize = ((pageHeader.width + 7) / 8) * pageHeader.height;
  }

  // Check buffer size
  if (bufferSize < bitmapSize) {
    LOG_DBG("XTC", "Buffer too small: need %u, have %u", bitmapSize, bufferSize);
    m_lastError = XtcError::MEMORY_ERROR;
    return 0;
  }

  // Read bitmap data
  size_t bytesRead = m_file.read(buffer, bitmapSize);
  if (bytesRead != bitmapSize) {
    LOG_DBG("XTC", "Page read error: expected %u, got %u", bitmapSize, bytesRead);
    m_lastError = XtcError::READ_ERROR;
    return 0;
  }

  m_lastError = XtcError::OK;
  return bytesRead;
}

XtcError XtcParser::loadPageStreaming(uint32_t pageIndex,
                                      std::function<void(const uint8_t* data, size_t size, size_t offset)> callback,
                                      size_t chunkSize) {
  if (!m_isOpen) {
    return XtcError::FILE_NOT_FOUND;
  }

  if (pageIndex >= m_header.pageCount) {
    return XtcError::PAGE_OUT_OF_RANGE;
  }

  // Keep streaming reads bounded on memory-constrained targets. A caller may
  // request up to 8 KiB, but allocation failure is reported instead of
  // throwing/terminating on X3 when contiguous heap is fragmented.
  if (!xtc::isPageStreamingChunkSizeSupported(chunkSize) || !callback) {
    return chunkSize == 0 || !callback ? XtcError::READ_ERROR : XtcError::MEMORY_ERROR;
  }

  PageInfo page;
  if (!readPageTableEntry(pageIndex, page)) {
    return XtcError::READ_ERROR;
  }

  if (!ensureFileOpen()) {
    return XtcError::FILE_NOT_FOUND;
  }

  // Seek to page data
  if (!m_file.seek64(page.offset)) {
    return XtcError::READ_ERROR;
  }

  // Read and skip page header (XTG for 1-bit, XTH for 2-bit)
  XtgPageHeader pageHeader;
  size_t headerRead = m_file.read(reinterpret_cast<uint8_t*>(&pageHeader), sizeof(XtgPageHeader));
  const uint32_t expectedMagic = (m_bitDepth == 2) ? XTH_MAGIC : XTG_MAGIC;
  if (headerRead != sizeof(XtgPageHeader) || pageHeader.magic != expectedMagic) {
    return XtcError::READ_ERROR;
  }

  // Calculate bitmap size based on bit depth
  // XTG (1-bit): Row-major, ((width+7)/8) * height bytes
  // XTH (2-bit): Two bit planes, ((width * height + 7) / 8) * 2 bytes
  if (pageHeader.width == 0 || pageHeader.height == 0) {
    return XtcError::READ_ERROR;
  }
  size_t bitmapSize;
  if (m_bitDepth == 2) {
    const size_t planeBytes = (static_cast<size_t>(pageHeader.width) * pageHeader.height + 7u) / 8u;
    if (planeBytes > SIZE_MAX / 2u) return XtcError::READ_ERROR;
    bitmapSize = planeBytes * 2u;
  } else {
    const size_t rowBytes = (static_cast<size_t>(pageHeader.width) + 7u) / 8u;
    if (rowBytes > SIZE_MAX / pageHeader.height) return XtcError::READ_ERROR;
    bitmapSize = rowBytes * pageHeader.height;
  }

  // Read in chunks
  std::unique_ptr<uint8_t, decltype(&free)> chunk(static_cast<uint8_t*>(malloc(chunkSize)), &free);
  if (!chunk) {
    m_lastError = XtcError::MEMORY_ERROR;
    return XtcError::MEMORY_ERROR;
  }
  size_t totalRead = 0;

  while (totalRead < bitmapSize) {
    size_t toRead = std::min(chunkSize, bitmapSize - totalRead);
    size_t bytesRead = m_file.read(chunk.get(), toRead);

    if (bytesRead == 0) {
      return XtcError::READ_ERROR;
    }

    callback(chunk.get(), bytesRead, totalRead);
    totalRead += bytesRead;
  }

  m_lastError = XtcError::OK;
  return XtcError::OK;
}

bool XtcParser::isValidXtcFile(const char* filepath) {
  HalFile file;
  if (!Storage.openFileForRead("XTC", filepath, file)) {
    return false;
  }

  uint32_t magic = 0;
  size_t bytesRead = file.read(reinterpret_cast<uint8_t*>(&magic), sizeof(magic));
  file.close();

  if (bytesRead != sizeof(magic)) {
    return false;
  }

  return (magic == XTC_MAGIC || magic == XTCH_MAGIC);
}

}  // namespace xtc
