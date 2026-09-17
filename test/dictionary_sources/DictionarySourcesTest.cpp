#include <gtest/gtest.h>

#include <string>
#include <vector>

#include "util/DictionaryNameResolver.h"
#include "util/DictionaryResultModel.h"

namespace {

struct SourceCandidate {
  std::string name;
  bool matched;
};

DictionarySourceMatches collectSuccessfulSources(const std::vector<SourceCandidate>& candidates) {
  DictionarySourceMatches matches;
  for (const auto& candidate : candidates) {
    if (!candidate.matched) continue;
    if (!matches.add(candidate.name, "word", false)) break;
  }
  return matches;
}

}  // namespace

TEST(DictionarySources, KeepsPreferredAndFallbackMetadataWithoutBodies) {
  DictionarySourceMatches matches;

  ASSERT_TRUE(matches.add("Primary", "word", true));
  ASSERT_TRUE(matches.add("Fallback", "word", false));

  ASSERT_EQ(matches.size(), 2u);
  EXPECT_EQ(matches[0].name, "Primary");
  EXPECT_TRUE(matches[0].preferred);
  EXPECT_EQ(matches[1].name, "Fallback");
  EXPECT_FALSE(matches[1].preferred);
}

TEST(DictionarySources, DeduplicatesARepeatedSource) {
  DictionarySourceMatches matches;

  ASSERT_TRUE(matches.add("English", "old", false));
  ASSERT_TRUE(matches.add("English", "new", true));

  ASSERT_EQ(matches.size(), 1u);
  EXPECT_EQ(matches[0].headword, "new");
  EXPECT_TRUE(matches[0].preferred);
}

TEST(DictionarySources, CapsAtSixSources) {
  DictionarySourceMatches matches;
  for (int i = 0; i < 6; i++) {
    ASSERT_TRUE(matches.add("Dictionary " + std::to_string(i), "word", i == 0));
  }

  EXPECT_EQ(matches.size(), DictionarySourceMatches::MAX_MATCHES);
  EXPECT_FALSE(matches.add("Dictionary 6", "word", false));
  EXPECT_EQ(matches.size(), DictionarySourceMatches::MAX_MATCHES);
}

TEST(DictionarySources, PickerCapAppliesAfterSkippingMisses) {
  std::vector<SourceCandidate> candidates;
  candidates.push_back({"invalid folder", false});
  candidates.push_back({"no match", false});
  for (int i = 0; i < 6; i++) candidates.push_back({"match " + std::to_string(i), true});
  candidates.push_back({"seventh match", true});

  const DictionarySourceMatches matches = collectSuccessfulSources(candidates);

  ASSERT_EQ(matches.size(), DictionarySourceMatches::MAX_MATCHES);
  EXPECT_EQ(matches[0].name, "match 0");
  EXPECT_EQ(matches[5].name, "match 5");
  EXPECT_FALSE(matches.contains("invalid folder"));
  EXPECT_FALSE(matches.contains("no match"));
  EXPECT_FALSE(matches.contains("seventh match"));
}

TEST(DictionarySources, FailureDiagnosticsAreDeduplicatedAndBounded) {
  DictionaryOpenFailureLedger ledger;

  EXPECT_FALSE(ledger.contains("stale"));
  ASSERT_TRUE(ledger.canTrack());
  ledger.remember("stale");
  EXPECT_TRUE(ledger.contains("stale"));
  ledger.remember("stale");

  for (size_t i = 1; i < DictionaryOpenFailureLedger::MAX_TRACKED_FAILURES; i++) {
    ledger.remember("invalid-" + std::to_string(i));
  }
  EXPECT_FALSE(ledger.canTrack());
  ledger.remember("overflow");
  EXPECT_FALSE(ledger.contains("overflow"));
  EXPECT_TRUE(ledger.contains("stale"));
}

TEST(DictionarySources, ResolvesOneLegacyTruncatedName) {
  const std::vector<std::string> installed = {
      "English-English Wiktionary dictionary",
      "French-English dictionary",
  };
  std::string resolved;

  ASSERT_TRUE(resolveUniqueDictionaryName(installed, "English-English Wiktionary d", resolved));
  EXPECT_EQ(resolved, "English-English Wiktionary dictionary");
}

TEST(DictionarySources, DoesNotGuessAnAmbiguousLegacyName) {
  const std::vector<std::string> installed = {
      "Indonesian-English Wiktionary dictionary",
      "Indonesian-English Wiktionary definitions",
  };
  std::string resolved;

  EXPECT_FALSE(resolveUniqueDictionaryName(installed, "Indonesian-English Wiktionary d", resolved));
  EXPECT_TRUE(resolved.empty());
}

TEST(DictionarySources, ExactNameWinsOverLongerPrefixMatch) {
  const std::vector<std::string> installed = {
      "English",
      "English-English Wiktionary dictionary",
  };
  std::string resolved;

  ASSERT_TRUE(resolveUniqueDictionaryName(installed, "English", resolved));
  EXPECT_EQ(resolved, "English");
}
