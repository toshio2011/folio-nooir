#include "NooirFirmwareIdentity.h"

#include <gtest/gtest.h>

namespace {

using nooir::firmware_identity::CandidateTarget;
using nooir::firmware_identity::TargetMarkerScanner;

template <size_t N>
void feed(TargetMarkerScanner& scanner, const char (&value)[N]) {
  scanner.feed(reinterpret_cast<const uint8_t*>(value), N - 1);
}

TEST(NooirFirmwareIdentity, AcceptsMatchingTargetMarker) {
  TargetMarkerScanner scanner;
  feed(scanner, "ESP32 image NOOIR_FIRMWARE_TARGET=X4PRO\0tail");
  EXPECT_EQ(scanner.result(true), CandidateTarget::Match);
  EXPECT_TRUE(scanner.sawMarker());
  EXPECT_TRUE(scanner.matchedExpected());
  EXPECT_FALSE(scanner.sawMismatch());
}

TEST(NooirFirmwareIdentity, HandlesMarkerSplitAcrossInputChunks) {
  TargetMarkerScanner scanner;
  feed(scanner, "prefix NOOIR_FIRMWARE_TAR");
  feed(scanner, "GET=X4");
  feed(scanner, "PRO\0suffix");
  EXPECT_EQ(scanner.result(true), CandidateTarget::Match);
}

TEST(NooirFirmwareIdentity, RejectsDifferentTarget) {
  TargetMarkerScanner scanner;
  feed(scanner, "NOOIR_FIRMWARE_TARGET=X4CLASSIC\0");
  EXPECT_EQ(scanner.result(true), CandidateTarget::Mismatch);
  EXPECT_TRUE(scanner.sawMismatch());
}

TEST(NooirFirmwareIdentity, StrictBuildRejectsMissingMarker) {
  TargetMarkerScanner scanner;
  feed(scanner, "ordinary ESP image with no target identity");
  EXPECT_EQ(scanner.result(true), CandidateTarget::Missing);
  EXPECT_EQ(scanner.result(false), CandidateTarget::LegacyUnmarked);
}

TEST(NooirFirmwareIdentity, RejectsAnyImageContainingConflictingMarkers) {
  TargetMarkerScanner scanner;
  feed(scanner, "NOOIR_FIRMWARE_TARGET=X4PRO\0NOOIR_FIRMWARE_TARGET=X4CLASSIC\0");
  EXPECT_EQ(scanner.result(true), CandidateTarget::Mismatch);
}

}  // namespace
