#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <cstring>

#ifndef NOOIR_DEVICE_ID
// Legacy C3 image family: one runtime-detected X3/X4 build.
#define NOOIR_DEVICE_ID "X3X4"
#endif

#ifndef NOOIR_EXPERIMENTAL_DEVICE_BUILD
#define NOOIR_EXPERIMENTAL_DEVICE_BUILD 0
#endif

#define NOOIR_FIRMWARE_TARGET_MARKER_LITERAL "NOOIR_FIRMWARE_TARGET=" NOOIR_DEVICE_ID

namespace nooir::firmware_identity {

inline constexpr char kMarkerPrefix[] = "NOOIR_FIRMWARE_TARGET=";
inline constexpr char kExpectedDeviceId[] = NOOIR_DEVICE_ID;
inline constexpr char kExpectedMarker[] = NOOIR_FIRMWARE_TARGET_MARKER_LITERAL;
inline constexpr size_t kMaxDeviceIdLength = 32;
inline constexpr bool kRequireTargetMarker = NOOIR_EXPERIMENTAL_DEVICE_BUILD != 0;

enum class CandidateTarget { Match, LegacyUnmarked, Missing, Mismatch };

// Streaming parser so a firmware image can be checked in the same bounded-RAM
// pass as its ESP checksum/SHA validation. It recognizes a generic marker
// prefix, then reads the target ID, without embedding a list of other device
// IDs into each firmware image.
class TargetMarkerScanner {
 public:
  void feed(const uint8_t* bytes, const size_t length) {
    for (size_t i = 0; i < length; ++i) feedByte(static_cast<char>(bytes[i]));
  }

  bool sawMarker() const { return sawMarker_; }
  bool matchedExpected() const { return matchedExpected_; }
  bool sawMismatch() const { return sawMismatch_; }

  CandidateTarget result(const bool requireMarker) const {
    if (sawMismatch_) return CandidateTarget::Mismatch;
    if (matchedExpected_) return CandidateTarget::Match;
    return requireMarker ? CandidateTarget::Missing : CandidateTarget::LegacyUnmarked;
  }

 private:
  static constexpr size_t kPrefixLength = sizeof(kMarkerPrefix) - 1;
  std::array<char, kPrefixLength> prefixWindow_{};
  std::array<char, kMaxDeviceIdLength> id_{};
  size_t prefixMatched_ = 0;
  size_t idLength_ = 0;
  size_t prefixWrite_ = 0;
  bool readingId_ = false;
  bool sawMarker_ = false;
  bool matchedExpected_ = false;
  bool sawMismatch_ = false;

  static bool isIdChar(const char c) {
    return (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '_' || c == '-';
  }

  bool prefixWindowMatches() const {
    for (size_t i = 0; i < kPrefixLength; ++i) {
      if (prefixWindow_[(prefixWrite_ + i) % kPrefixLength] != kMarkerPrefix[i]) return false;
    }
    return true;
  }

  void finishId() {
    if (idLength_ == 0) return;
    sawMarker_ = true;
    const size_t expectedLength = sizeof(kExpectedDeviceId) - 1;
    const bool matches = idLength_ == expectedLength &&
                         std::memcmp(id_.data(), kExpectedDeviceId, expectedLength) == 0;
    matchedExpected_ = matchedExpected_ || matches;
    sawMismatch_ = sawMismatch_ || !matches;
    idLength_ = 0;
  }

  void feedByte(const char c) {
    if (readingId_) {
      if (isIdChar(c) && idLength_ < id_.size()) {
        id_[idLength_++] = c;
        return;
      }
      finishId();
      readingId_ = false;
      // Continue below so a non-ID byte can also begin the next prefix.
    }

    prefixWindow_[prefixWrite_] = c;
    prefixWrite_ = (prefixWrite_ + 1) % kPrefixLength;
    if (prefixMatched_ < kPrefixLength && c == kMarkerPrefix[prefixMatched_]) {
      ++prefixMatched_;
    } else {
      prefixMatched_ = c == kMarkerPrefix[0] ? 1 : 0;
    }
    if (prefixMatched_ == kPrefixLength || prefixWindowMatches()) {
      readingId_ = true;
      idLength_ = 0;
      prefixMatched_ = 0;
    }
  }
};

}  // namespace nooir::firmware_identity
