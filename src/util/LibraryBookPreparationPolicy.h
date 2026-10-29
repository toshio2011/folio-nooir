#pragma once

#include <cstddef>
#include <cstdint>
#include <string>

namespace library_book_preparation {

// Cover inputs are streamed to SD and decoded with bounded row/work buffers.
// Keep the same 3 MiB uncompressed-source ceiling used by Recent refresh and
// CBZ, while allowing the converters to reject unsupported image dimensions.
inline constexpr size_t MAX_COVER_SOURCE_BYTES = 3u * 1024u * 1024u;

constexpr bool cacheIsComplete(const bool metadataValid, const bool thumbnailValid,
                               const bool sourceKnownToHaveNoCover) {
  return metadataValid && (thumbnailValid || sourceKnownToHaveNoCover);
}

constexpr bool shouldStartHoverWork(const bool attemptedThisActivity, const bool supportedBook,
                                   const bool cacheComplete) {
  return supportedBook && !attemptedThisActivity && !cacheComplete;
}

constexpr bool shouldQueueThumbnail(const bool thumbnailValid, const bool sourceKnownToHaveNoCover,
                                    const bool sourceSupported) {
  return !thumbnailValid && !sourceKnownToHaveNoCover && sourceSupported;
}

enum class ThumbnailOutcome : uint8_t {
  CacheHit,
  Generated,
  SkippedNoSource,
  SkippedUnsupported,
  SkippedOversize,
  Failed
};

struct RetrieveAllSummary {
  uint32_t metadataCacheHits = 0;
  uint32_t metadataCacheMisses = 0;
  uint32_t metadataPrepared = 0;
  uint32_t metadataFailed = 0;
  uint32_t thumbnailCacheHits = 0;
  uint32_t thumbnailCacheMisses = 0;
  uint32_t thumbnailsGenerated = 0;
  uint32_t thumbnailsSkipped = 0;
  uint32_t thumbnailsFailed = 0;
  uint32_t queueFailures = 0;

  void noteMetadataCache(const bool hit) { hit ? ++metadataCacheHits : ++metadataCacheMisses; }
  void noteMetadataResult(const bool ready) { ready ? ++metadataPrepared : ++metadataFailed; }
  void noteThumbnailCache(const bool hit) { hit ? ++thumbnailCacheHits : ++thumbnailCacheMisses; }
  void noteThumbnailOutcome(const ThumbnailOutcome outcome) {
    switch (outcome) {
      case ThumbnailOutcome::CacheHit:
        break;
      case ThumbnailOutcome::Generated:
        ++thumbnailsGenerated;
        break;
      case ThumbnailOutcome::SkippedNoSource:
      case ThumbnailOutcome::SkippedUnsupported:
      case ThumbnailOutcome::SkippedOversize:
        ++thumbnailsSkipped;
        break;
      case ThumbnailOutcome::Failed:
        ++thumbnailsFailed;
        break;
    }
  }

  std::string completionMessage() const {
    return "Finished: meta " + std::to_string(metadataCacheHits) + " hit/" +
           std::to_string(metadataCacheMisses) + " miss, " + std::to_string(metadataPrepared) + " ready/" +
           std::to_string(metadataFailed) + " failed; covers " + std::to_string(thumbnailCacheHits) + " cached, " +
           std::to_string(thumbnailsGenerated) + " made/" + std::to_string(thumbnailsSkipped) + " skipped/" +
           std::to_string(thumbnailsFailed + queueFailures) + " failed.";
  }
};

}  // namespace library_book_preparation
