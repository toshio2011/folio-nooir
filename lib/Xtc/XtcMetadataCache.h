#pragma once

#include <cstdint>
#include <string>

namespace xtc_metadata_cache {

enum class ExtendedMetadataState : uint8_t { NotChecked = 0, NotPresent = 1, Parsed = 2, Unsupported = 3 };
enum class CoverState : uint8_t { Unknown = 0, Available = 1, NoCover = 2, Unsupported = 3 };

struct SourceFingerprint {
  uint64_t size = 0;
  uint16_t modifiedDate = 0;
  uint16_t modifiedTime = 0;
};

struct Record {
  std::string title;
  std::string author;
  std::string synopsis;
  std::string series;
  ExtendedMetadataState extendedMetadata = ExtendedMetadataState::NotChecked;
  CoverState cover = CoverState::Unknown;
};

enum class LoadResult : uint8_t { Hit, Missing, Invalid, Stale, SourceUnavailable };

// The fingerprint opens the FAT entry and reads its size and modification
// stamp only; it does not scan or parse the XTC source.
bool readSourceFingerprint(const std::string& sourcePath, SourceFingerprint& fingerprint);
LoadResult load(const std::string& metadataPath, const SourceFingerprint& source, Record& record);
bool store(const std::string& metadataPath, const SourceFingerprint& source, const Record& record);

}  // namespace xtc_metadata_cache
