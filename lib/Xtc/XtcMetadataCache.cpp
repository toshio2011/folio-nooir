#include "XtcMetadataCache.h"

#include <HalStorage.h>

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <utility>

namespace xtc_metadata_cache {
namespace {
constexpr size_t kHeaderBytes = 32;
constexpr size_t kMaxMetadataBytes = 16u * 1024u;
constexpr uint16_t kVersion = 1;
constexpr char kMagic[4] = {'X', 'T', 'M', '1'};
constexpr char kModule[] = "XTCMETA";
constexpr char kCompleteSuffix[] = ".tmp";
constexpr char kBackupSuffix[] = ".old";

enum HeaderOffset : size_t {
  MagicOffset = 0,
  VersionOffset = 4,
  StateOffset = 6,
  SourceSizeOffset = 8,
  ModifiedDateOffset = 16,
  ModifiedTimeOffset = 18,
  TitleLengthOffset = 20,
  AuthorLengthOffset = 22,
  SynopsisLengthOffset = 24,
  SeriesLengthOffset = 26,
  ChecksumOffset = 28,
};

void put16(uint8_t* out, const size_t offset, const uint16_t value) {
  out[offset] = static_cast<uint8_t>(value & 0xFFu);
  out[offset + 1] = static_cast<uint8_t>(value >> 8u);
}

void put32(uint8_t* out, const size_t offset, const uint32_t value) {
  for (size_t i = 0; i < sizeof(value); ++i) out[offset + i] = static_cast<uint8_t>(value >> (i * 8u));
}

void put64(uint8_t* out, const size_t offset, const uint64_t value) {
  for (size_t i = 0; i < sizeof(value); ++i) out[offset + i] = static_cast<uint8_t>(value >> (i * 8u));
}

uint16_t get16(const uint8_t* in, const size_t offset) {
  return static_cast<uint16_t>(in[offset]) | static_cast<uint16_t>(static_cast<uint16_t>(in[offset + 1]) << 8u);
}

uint32_t get32(const uint8_t* in, const size_t offset) {
  uint32_t value = 0;
  for (size_t i = 0; i < sizeof(value); ++i) value |= static_cast<uint32_t>(in[offset + i]) << (i * 8u);
  return value;
}

uint64_t get64(const uint8_t* in, const size_t offset) {
  uint64_t value = 0;
  for (size_t i = 0; i < sizeof(value); ++i) value |= static_cast<uint64_t>(in[offset + i]) << (i * 8u);
  return value;
}

uint32_t hashBytes(uint32_t hash, const uint8_t* bytes, const size_t count) {
  for (size_t i = 0; i < count; ++i) {
    hash ^= bytes[i];
    hash *= 16777619u;
  }
  return hash;
}

uint32_t checksum(const uint8_t* header, const Record& record) {
  uint32_t hash = hashBytes(2166136261u, header, ChecksumOffset);
  const std::string* fields[] = {&record.title, &record.author, &record.synopsis, &record.series};
  for (const std::string* field : fields) {
    hash = hashBytes(hash, reinterpret_cast<const uint8_t*>(field->data()), field->size());
  }
  return hash;
}

bool recordIsBoundedAndComplete(const Record& record) {
  if (record.extendedMetadata == ExtendedMetadataState::NotChecked ||
      record.extendedMetadata > ExtendedMetadataState::Unsupported || record.cover > CoverState::Unsupported) {
    return false;
  }
  const size_t maxField = std::numeric_limits<uint16_t>::max();
  if (record.title.size() > maxField || record.author.size() > maxField || record.synopsis.size() > maxField ||
      record.series.size() > maxField) {
    return false;
  }
  size_t total = 0;
  const size_t fieldSizes[] = {record.title.size(), record.author.size(), record.synopsis.size(), record.series.size()};
  for (const size_t size : fieldSizes) {
    if (size > kMaxMetadataBytes - total) return false;
    total += size;
  }
  return true;
}

bool exactRead(HalFile& file, void* bytes, const size_t count) {
  return count == 0 || file.read(bytes, count) == count;
}

bool exactWrite(HalFile& file, const void* bytes, const size_t count) {
  return count == 0 || file.write(bytes, count) == count;
}

uint8_t packState(const Record& record) {
  return static_cast<uint8_t>(0x01u | (static_cast<uint8_t>(record.cover) << 1u) |
                              (static_cast<uint8_t>(record.extendedMetadata) << 3u));
}

bool unpackState(const uint8_t packed, Record& record) {
  if ((packed & 0x01u) == 0 || (packed & 0x80u) != 0) return false;
  const uint8_t cover = static_cast<uint8_t>((packed >> 1u) & 0x03u);
  const uint8_t extended = static_cast<uint8_t>((packed >> 3u) & 0x03u);
  if (cover > static_cast<uint8_t>(CoverState::Unsupported) ||
      extended == static_cast<uint8_t>(ExtendedMetadataState::NotChecked) ||
      extended > static_cast<uint8_t>(ExtendedMetadataState::Unsupported) || (packed & 0xE0u) != 0) {
    return false;
  }
  record.cover = static_cast<CoverState>(cover);
  record.extendedMetadata = static_cast<ExtendedMetadataState>(extended);
  return true;
}

bool readRecord(const std::string& path, const SourceFingerprint& source, Record& record, LoadResult& result) {
  HalFile file;
  if (!Storage.openFileForRead(kModule, path, file)) {
    result = LoadResult::Missing;
    return false;
  }
  std::array<uint8_t, kHeaderBytes> header{};
  if (!exactRead(file, header.data(), header.size()) || file.fileSize64() < kHeaderBytes ||
      !std::equal(kMagic, kMagic + sizeof(kMagic), header.begin()) || get16(header.data(), VersionOffset) != kVersion) {
    file.close();
    result = LoadResult::Invalid;
    return false;
  }
  Record parsed;
  if (!unpackState(header[StateOffset], parsed)) {
    file.close();
    result = LoadResult::Invalid;
    return false;
  }

  const uint16_t titleBytes = get16(header.data(), TitleLengthOffset);
  const uint16_t authorBytes = get16(header.data(), AuthorLengthOffset);
  const uint16_t synopsisBytes = get16(header.data(), SynopsisLengthOffset);
  const uint16_t seriesBytes = get16(header.data(), SeriesLengthOffset);
  const size_t lengths[] = {titleBytes, authorBytes, synopsisBytes, seriesBytes};
  size_t payloadBytes = 0;
  for (const size_t length : lengths) {
    if (length > kMaxMetadataBytes - payloadBytes) {
      file.close();
      result = LoadResult::Invalid;
      return false;
    }
    payloadBytes += length;
  }
  if (file.fileSize64() != kHeaderBytes + payloadBytes) {
    file.close();
    result = LoadResult::Invalid;
    return false;
  }

  const uint64_t cachedSize = get64(header.data(), SourceSizeOffset);
  const uint16_t cachedDate = get16(header.data(), ModifiedDateOffset);
  const uint16_t cachedTime = get16(header.data(), ModifiedTimeOffset);
  if (cachedSize != source.size || cachedDate != source.modifiedDate || cachedTime != source.modifiedTime) {
    file.close();
    result = LoadResult::Stale;
    return false;
  }

  parsed.title.resize(titleBytes);
  parsed.author.resize(authorBytes);
  parsed.synopsis.resize(synopsisBytes);
  parsed.series.resize(seriesBytes);
  const std::string* fields[] = {&parsed.title, &parsed.author, &parsed.synopsis, &parsed.series};
  uint32_t hash = hashBytes(2166136261u, header.data(), ChecksumOffset);
  bool valid = true;
  for (const std::string* field : fields) {
    if (!exactRead(file, const_cast<char*>(field->data()), field->size())) {
      valid = false;
      break;
    }
    hash = hashBytes(hash, reinterpret_cast<const uint8_t*>(field->data()), field->size());
  }
  const bool closed = file.close();
  if (!valid || !closed || hash != get32(header.data(), ChecksumOffset)) {
    result = LoadResult::Invalid;
    return false;
  }
  record = std::move(parsed);
  result = LoadResult::Hit;
  return true;
}

}  // namespace

bool readSourceFingerprint(const std::string& sourcePath, SourceFingerprint& fingerprint) {
  HalFile file;
  if (!Storage.openFileForRead(kModule, sourcePath, file)) return false;
  SourceFingerprint next;
  next.size = file.fileSize64();
  const bool timestampValid = file.getModifyDateTime(&next.modifiedDate, &next.modifiedTime);
  const bool closed = file.close();
  if (!closed || !timestampValid || next.size == 0) return false;
  fingerprint = next;
  return true;
}

LoadResult load(const std::string& metadataPath, const SourceFingerprint& source, Record& record) {
  LoadResult result = LoadResult::Missing;
  if (readRecord(metadataPath, source, record, result)) return LoadResult::Hit;
  return result;
}

bool store(const std::string& metadataPath, const SourceFingerprint& source, const Record& record) {
  if (source.size == 0 || !recordIsBoundedAndComplete(record)) return false;

  std::array<uint8_t, kHeaderBytes> header{};
  std::copy(kMagic, kMagic + sizeof(kMagic), header.begin());
  put16(header.data(), VersionOffset, kVersion);
  header[StateOffset] = packState(record);
  put64(header.data(), SourceSizeOffset, source.size);
  put16(header.data(), ModifiedDateOffset, source.modifiedDate);
  put16(header.data(), ModifiedTimeOffset, source.modifiedTime);
  put16(header.data(), TitleLengthOffset, static_cast<uint16_t>(record.title.size()));
  put16(header.data(), AuthorLengthOffset, static_cast<uint16_t>(record.author.size()));
  put16(header.data(), SynopsisLengthOffset, static_cast<uint16_t>(record.synopsis.size()));
  put16(header.data(), SeriesLengthOffset, static_cast<uint16_t>(record.series.size()));
  put32(header.data(), ChecksumOffset, checksum(header.data(), record));

  const std::string temporaryPath = metadataPath + kCompleteSuffix;
  const std::string backupPath = metadataPath + kBackupSuffix;
  if (Storage.exists(temporaryPath.c_str()) && !Storage.remove(temporaryPath.c_str())) return false;
  HalFile file;
  if (!Storage.openFileForWrite(kModule, temporaryPath, file)) return false;
  bool written = exactWrite(file, header.data(), header.size());
  const std::string* fields[] = {&record.title, &record.author, &record.synopsis, &record.series};
  for (const std::string* field : fields) {
    written = written && exactWrite(file, field->data(), field->size());
  }
  file.flush();
  written = file.close() && written;
  if (!written) {
    Storage.remove(temporaryPath.c_str());
    return false;
  }

  Record verified;
  LoadResult verifyResult = LoadResult::Missing;
  if (!readRecord(temporaryPath, source, verified, verifyResult) || verifyResult != LoadResult::Hit ||
      verified.title != record.title || verified.author != record.author || verified.synopsis != record.synopsis ||
      verified.series != record.series || verified.extendedMetadata != record.extendedMetadata ||
      verified.cover != record.cover) {
    Storage.remove(temporaryPath.c_str());
    return false;
  }

  const bool hadOriginal = Storage.exists(metadataPath.c_str());
  if (hadOriginal) {
    if (Storage.exists(backupPath.c_str()) && !Storage.remove(backupPath.c_str())) {
      Storage.remove(temporaryPath.c_str());
      return false;
    }
    if (!Storage.rename(metadataPath.c_str(), backupPath.c_str())) {
      Storage.remove(temporaryPath.c_str());
      return false;
    }
  }
  if (!Storage.rename(temporaryPath.c_str(), metadataPath.c_str())) {
    if (hadOriginal) Storage.rename(backupPath.c_str(), metadataPath.c_str());
    Storage.remove(temporaryPath.c_str());
    return false;
  }
  if (hadOriginal) Storage.remove(backupPath.c_str());
  return true;
}

}  // namespace xtc_metadata_cache
