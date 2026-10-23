#include <ArduinoJson.h>
#include <HalStorage.h>
#include <CrossPointSettings.h>

#include <Serialization/PersistableStore.h>
#include <Serialization/PersistableStorePolicy.h>

#include <gtest/gtest.h>

namespace {

constexpr const char* kSettingsPath = "/.crosspoint/settings.json";
constexpr const char* kBackupPath = "/.crosspoint/settings.json.bak";
constexpr const char* kTempPath = "/.crosspoint/settings.json.tmp";
constexpr const char* kRecentPath = "/.crosspoint/recent.json";

JsonDocument settingsDocument(const uint8_t theme) {
  JsonDocument doc;
  doc["fontFamily"] = 1;
  doc["fontSize"] = 1;
  doc["sleepScreen"] = 1;
  doc["sleepTimeout"] = 1;
  doc["orientation"] = 0;
  doc["fadingFix"] = 1;
  doc["uiTheme"] = theme;
  return doc;
}

String serializedSettings(const uint8_t theme) {
  String json;
  serializeJson(settingsDocument(theme), json);
  return json;
}

String serializedRecent(const uint32_t readingSeconds) {
  JsonDocument doc;
  JsonArray books = doc["books"].to<JsonArray>();
  JsonObject book = books.add<JsonObject>();
  book["path"] = "/manga/One Piece.xtch";
  book["title"] = "One Piece";
  book["series"] = "One Piece";
  book["xtcMetadataChecked"] = true;
  book["readingSeconds"] = readingSeconds;
  String json;
  serializeJson(doc, json);
  return json;
}

uint8_t loadedTheme() {
  JsonDocument loaded;
  EXPECT_TRUE(PersistableStoreBase::readDocFromFile(kSettingsPath, loaded));
  return loaded["uiTheme"].as<uint8_t>();
}

}  // namespace

class SettingsSaveProbe : public PersistableStore<SettingsSaveProbe> {
 public:
  static constexpr const char* getFilePath() { return kSettingsPath; }
  uint8_t uiTheme = 1;
  void toJson(JsonDocument& doc) const {
    doc["fontFamily"] = 1;
    doc["fontSize"] = 2;
    doc["sleepScreen"] = 3;
    doc["sleepTimeout"] = 2;
    doc["orientation"] = 0;
    doc["fadingFix"] = 1;
    doc["uiTheme"] = uiTheme;
  }
  bool fromJson(JsonVariantConst doc) {
    uiTheme = doc["uiTheme"] | uint8_t{1};
    return true;
  }
};

class SettingsPersistenceTest : public ::testing::Test {
 protected:
  void SetUp() override {
    Storage.reset();
    PersistableStoreBase::invalidateSettingsValidationCache();
  }
};

TEST_F(SettingsPersistenceTest, LoadsAnExistingValidMainSettingsFile) {
  Storage.files[kSettingsPath] = serializedSettings(3).c_str();

  EXPECT_EQ(loadedTheme(), 3);
  EXPECT_FALSE(persistable::settingsRecoveryRequired(true, true));
}

TEST_F(SettingsPersistenceTest, BinaryAndTruncatedFilesAreConfirmedCorruptionOnlyAfterCompleteReads) {
  Storage.files[kSettingsPath] = std::string("\x01\xFF\x00\xA5", 4);
  Storage.files[kBackupPath] = "{\"fontFamily\":1";

  JsonDocument loaded;
  persistable::SettingsLoadReport report;
  EXPECT_FALSE(PersistableStoreBase::readDocFromFile(kSettingsPath, loaded, &report));
  EXPECT_TRUE(report.confirmedCorruptionOnly());
  EXPECT_EQ(report.candidates[0], persistable::SettingsCandidateResult::Malformed);
  EXPECT_EQ(report.candidates[1], persistable::SettingsCandidateResult::Malformed);
  EXPECT_EQ(report.candidates[2], persistable::SettingsCandidateResult::Missing);
}

TEST_F(SettingsPersistenceTest, EmptySettingsFileIsMalformedButReadFailureIsNot) {
  Storage.files[kSettingsPath] = "";

  JsonDocument loaded;
  persistable::SettingsLoadReport report;
  EXPECT_FALSE(PersistableStoreBase::readDocFromFile(kSettingsPath, loaded, &report));
  EXPECT_TRUE(report.confirmedCorruptionOnly());

  Storage.failReadPaths[kSettingsPath] = true;
  EXPECT_FALSE(PersistableStoreBase::readDocFromFile(kSettingsPath, loaded, &report));
  EXPECT_FALSE(report.confirmedCorruptionOnly());
  EXPECT_TRUE(report.hasReadError());
}

TEST_F(SettingsPersistenceTest, RejectsMalformedMainAndRecoversValidBackup) {
  Storage.files[kSettingsPath] = "{not-json";
  Storage.files[kBackupPath] = serializedSettings(4).c_str();

  EXPECT_EQ(loadedTheme(), 4);
  JsonDocument loaded;
  persistable::SettingsLoadReport report;
  EXPECT_TRUE(PersistableStoreBase::readDocFromFile(kSettingsPath, loaded, &report));
  EXPECT_EQ(report.selected, 1u);
  EXPECT_FALSE(report.confirmedCorruptionOnly());
  EXPECT_EQ(Storage.files[kSettingsPath], "{not-json");
  EXPECT_TRUE(Storage.exists(kBackupPath));
}

TEST_F(SettingsPersistenceTest, UnknownValidSettingsDocumentIsPreservedAndNeverAutoReset) {
  const std::string original = "{\"schema\":999,\"futureSetting\":true}";
  Storage.files[kSettingsPath] = original;

  JsonDocument loaded;
  persistable::SettingsLoadReport report;
  EXPECT_FALSE(PersistableStoreBase::readDocFromFile(kSettingsPath, loaded, &report));
  EXPECT_EQ(report.candidates[0], persistable::SettingsCandidateResult::Incompatible);
  EXPECT_FALSE(report.confirmedCorruptionOnly());
  EXPECT_EQ(Storage.files[kSettingsPath], original);
}

TEST_F(SettingsPersistenceTest, ValidBackupDoesNotAuthorizeOverwritingUnknownMainFormat) {
  const std::string unknownMain = "{\"schema\":999,\"futureSetting\":true}";
  Storage.files[kSettingsPath] = unknownMain;
  Storage.files[kBackupPath] = serializedSettings(8).c_str();

  JsonDocument loaded;
  persistable::SettingsLoadReport report;
  EXPECT_TRUE(PersistableStoreBase::readDocFromFile(kSettingsPath, loaded, &report));
  EXPECT_EQ(loaded["uiTheme"].as<uint8_t>(), 8);
  EXPECT_EQ(report.selected, 1u);
  EXPECT_TRUE(report.hasIncompatibleCandidate());
  EXPECT_EQ(Storage.files[kSettingsPath], unknownMain);
}

TEST_F(SettingsPersistenceTest, CompatibleLegacyShapeIsAcceptedForMigration) {
  Storage.files[kSettingsPath] =
      "{\"fontFamily\":1,\"fontSize\":2,\"sleepScreen\":3,\"sleepTimeout\":2,\"orientation\":0,\"fadingFix\":1}";

  JsonDocument loaded;
  persistable::SettingsLoadReport report;
  EXPECT_TRUE(PersistableStoreBase::readDocFromFile(kSettingsPath, loaded, &report));
  EXPECT_EQ(report.selected, 0u);
  EXPECT_FALSE(report.confirmedCorruptionOnly());
}

TEST_F(SettingsPersistenceTest, RejectsIncompatibleProfileAndRecoversValidPendingWrite) {
  JsonDocument profile = settingsDocument(2);
  profile["_profileSchema"] = 1;
  String profileJson;
  serializeJson(profile, profileJson);
  Storage.files[kSettingsPath] = profileJson.c_str();
  Storage.files[kTempPath] = serializedSettings(5).c_str();

  EXPECT_EQ(loadedTheme(), 5);
  EXPECT_EQ(Storage.files[kSettingsPath], profileJson.c_str());
}

TEST_F(SettingsPersistenceTest, FailedLoadRequiresExplicitRecoveryOnlyWhenAFileExists) {
  EXPECT_FALSE(persistable::settingsRecoveryRequired(false, false));
  EXPECT_TRUE(persistable::settingsRecoveryRequired(false, true));
  EXPECT_FALSE(persistable::settingsRecoveryRequired(true, true));

  Storage.files[kSettingsPath] = "{broken";
  JsonDocument loaded;
  EXPECT_FALSE(PersistableStoreBase::readDocFromFile(kSettingsPath, loaded));
  EXPECT_EQ(Storage.files[kSettingsPath], "{broken");
  EXPECT_TRUE(persistable::settingsRecoveryRequired(false, Storage.exists(kSettingsPath)));
}

TEST(SettingsSerializationPolicy, RequiresCompleteNonOverflowedOutput) {
  EXPECT_TRUE(persistable::serializedDocumentIsComplete(false, 128, 128, 128));
  EXPECT_FALSE(persistable::serializedDocumentIsComplete(true, 128, 128, 128));
  EXPECT_FALSE(persistable::serializedDocumentIsComplete(false, 128, 127, 128));
  EXPECT_FALSE(persistable::serializedDocumentIsComplete(false, 128, 128, 127));
  EXPECT_FALSE(persistable::serializedDocumentIsComplete(false, 0, 0, 0));
}

TEST_F(SettingsPersistenceTest, AutoRecoveryQuarantinesCorruptCandidatesAndValidatesFreshDefaults) {
  const std::string original("\x01\xFF\x00\xA5", 4);
  Storage.files[kSettingsPath] = original;

  JsonDocument loaded;
  persistable::SettingsLoadReport report;
  ASSERT_FALSE(PersistableStoreBase::readDocFromFile(kSettingsPath, loaded, &report));
  ASSERT_TRUE(report.confirmedCorruptionOnly());
  const JsonDocument defaults = settingsDocument(7);

  EXPECT_TRUE(PersistableStoreBase::recoverMalformedSettings(kSettingsPath, defaults, report));
  EXPECT_EQ(Storage.files[std::string(kSettingsPath) + ".corrupt-main-0"], original);
  EXPECT_EQ(loadedTheme(), 7);
  EXPECT_FALSE(Storage.exists("/.crosspoint/settings.recovery-in-progress"));
}

TEST_F(SettingsPersistenceTest, ExistingRecoveryMarkerPreventsRepeatedAutomaticReset) {
  const std::string original = "{broken";
  Storage.files[kSettingsPath] = original;
  Storage.files["/.crosspoint/settings.recovery-in-progress"] = "settings-recovery-v1";
  JsonDocument loaded;
  persistable::SettingsLoadReport report;
  ASSERT_FALSE(PersistableStoreBase::readDocFromFile(kSettingsPath, loaded, &report));
  ASSERT_TRUE(report.confirmedCorruptionOnly());

  EXPECT_FALSE(PersistableStoreBase::recoverMalformedSettings(kSettingsPath, settingsDocument(6), report));
  EXPECT_EQ(Storage.files[kSettingsPath], original);
  EXPECT_FALSE(Storage.exists((std::string(kSettingsPath) + ".corrupt-main-0").c_str()));
}

TEST_F(SettingsPersistenceTest, RecoveryWriteFailureKeepsQuarantineAndBlocksFurtherAttempts) {
  Storage.files[kSettingsPath] = "{broken";
  JsonDocument loaded;
  persistable::SettingsLoadReport report;
  ASSERT_FALSE(PersistableStoreBase::readDocFromFile(kSettingsPath, loaded, &report));
  ASSERT_TRUE(report.confirmedCorruptionOnly());
  Storage.failWritePaths[kTempPath] = true;

  EXPECT_FALSE(PersistableStoreBase::recoverMalformedSettings(kSettingsPath, settingsDocument(6), report));
  EXPECT_EQ(Storage.files[std::string(kSettingsPath) + ".corrupt-main-0"], "{broken");
  EXPECT_TRUE(Storage.exists("/.crosspoint/settings.recovery-in-progress"));
  EXPECT_FALSE(PersistableStoreBase::recoverMalformedSettings(kSettingsPath, settingsDocument(7), report));
  EXPECT_FALSE(Storage.exists((std::string(kSettingsPath) + ".corrupt-main-1").c_str()));
}

TEST_F(SettingsPersistenceTest, SettingsRemainReadableAcrossRepeatedBootAndWakeLoads) {
  const JsonDocument saved = settingsDocument(9);
  ASSERT_TRUE(PersistableStoreBase::writeDocToFile(kSettingsPath, saved));

  for (int cycle = 0; cycle < 4; ++cycle) {
    JsonDocument loaded;
    persistable::SettingsLoadReport report;
    ASSERT_TRUE(PersistableStoreBase::readDocFromFile(kSettingsPath, loaded, &report));
    EXPECT_EQ(loaded["uiTheme"].as<uint8_t>(), 9);
    EXPECT_EQ(report.selected, 0u);
  }
}

TEST_F(SettingsPersistenceTest, InterruptedTemporaryWriteLeavesValidMainUntouched) {
  const String original = serializedSettings(1);
  Storage.files[kSettingsPath] = original.c_str();
  Storage.armCrash(TestStorage::CrashPoint::DuringTempWrite);
  const JsonDocument replacement = settingsDocument(6);

  EXPECT_THROW(PersistableStoreBase::writeDocToFile(kSettingsPath, replacement), SimulatedPowerLoss);
  EXPECT_EQ(Storage.files[kSettingsPath], original.c_str());
  EXPECT_EQ(loadedTheme(), 1);
}

TEST_F(SettingsPersistenceTest, InterruptedMainToBackupRenameRecoversOriginalSettings) {
  const String original = serializedSettings(1);
  Storage.files[kSettingsPath] = original.c_str();
  Storage.armCrash(TestStorage::CrashPoint::AfterFirstRename);
  const JsonDocument replacement = settingsDocument(6);

  EXPECT_THROW(PersistableStoreBase::writeDocToFile(kSettingsPath, replacement), SimulatedPowerLoss);
  EXPECT_FALSE(Storage.exists(kSettingsPath));
  EXPECT_EQ(Storage.files[kBackupPath], original.c_str());
  EXPECT_EQ(loadedTheme(), 1);
}

TEST_F(SettingsPersistenceTest, InterruptedAfterPromotionLoadsCompleteNewSettings) {
  const String original = serializedSettings(1);
  Storage.files[kSettingsPath] = original.c_str();
  Storage.armCrash(TestStorage::CrashPoint::AfterSecondRename);
  const JsonDocument replacement = settingsDocument(6);

  EXPECT_THROW(PersistableStoreBase::writeDocToFile(kSettingsPath, replacement), SimulatedPowerLoss);
  EXPECT_EQ(Storage.files[kBackupPath], original.c_str());
  EXPECT_EQ(loadedTheme(), 6);
}

TEST_F(SettingsPersistenceTest, FailedMainRenamePreservesPrimaryAndExistingBackup) {
  const std::string original = serializedSettings(2).c_str();
  const std::string priorBackup = serializedSettings(1).c_str();
  Storage.files[kSettingsPath] = original;
  Storage.files[kBackupPath] = priorBackup;
  Storage.failRenameFrom[kSettingsPath] = true;

  EXPECT_FALSE(PersistableStoreBase::writeDocToFile(kSettingsPath, settingsDocument(6)));
  EXPECT_EQ(Storage.files[kSettingsPath], original);
  EXPECT_EQ(Storage.files[kBackupPath], priorBackup);
  EXPECT_EQ(loadedTheme(), 2);
}

TEST_F(SettingsPersistenceTest, SavePromotesTheOnlyValidPendingSettingsBeforeReusingTempPath) {
  const std::string corruptPrimary = "{broken";
  Storage.files[kSettingsPath] = corruptPrimary;
  Storage.files[kTempPath] = serializedSettings(4).c_str();

  EXPECT_TRUE(PersistableStoreBase::writeDocToFile(kSettingsPath, settingsDocument(7)));
  EXPECT_EQ(loadedTheme(), 7);
  EXPECT_EQ(Storage.files[std::string(kSettingsPath) + ".corrupt-before-save-0"], corruptPrimary);
  EXPECT_FALSE(Storage.exists(kBackupPath));
  EXPECT_FALSE(Storage.exists(kTempPath));
}

TEST_F(SettingsPersistenceTest, SaveOverMalformedPrimaryPreservesTheOriginalCandidate) {
  const std::string corrupt = std::string("\x01\xFF\x00", 3);
  Storage.files[kSettingsPath] = corrupt;

  ASSERT_TRUE(PersistableStoreBase::writeDocToFile(kSettingsPath, settingsDocument(8)));
  EXPECT_EQ(loadedTheme(), 8);
  EXPECT_EQ(Storage.files[std::string(kSettingsPath) + ".corrupt-before-save-0"], corrupt);
}

TEST_F(SettingsPersistenceTest, ExplicitSavePreservesAnUnrecognizedValidSettingsFormat) {
  const std::string unknown = "{\"schema\":999,\"futureSetting\":true}";
  Storage.files[kSettingsPath] = unknown;

  ASSERT_TRUE(PersistableStoreBase::writeDocToFile(kSettingsPath, settingsDocument(8)));
  EXPECT_EQ(loadedTheme(), 8);
  EXPECT_EQ(Storage.files[std::string(kSettingsPath) + ".unrecognized-before-save-0"], unknown);
}

TEST_F(SettingsPersistenceTest, ReadFailureDuringPreflightLeavesEverySettingsCandidateUntouched) {
  const std::string original = serializedSettings(3).c_str();
  const std::string pending = serializedSettings(4).c_str();
  Storage.files[kSettingsPath] = original;
  Storage.files[kTempPath] = pending;
  Storage.failReadPaths[kSettingsPath] = true;

  EXPECT_FALSE(PersistableStoreBase::writeDocToFile(kSettingsPath, settingsDocument(8)));
  EXPECT_EQ(Storage.files[kSettingsPath], original);
  EXPECT_EQ(Storage.files[kTempPath], pending);
  EXPECT_FALSE(Storage.exists(kBackupPath));
}

TEST_F(SettingsPersistenceTest, FailedTemporaryReadbackPreservesTemporaryAndCommittedSettings) {
  const std::string original = serializedSettings(3).c_str();
  Storage.files[kSettingsPath] = original;
  Storage.failReadOnAttempt[kTempPath] = 2;  // Initial inventory is missing; fail the post-write readback.

  EXPECT_FALSE(PersistableStoreBase::writeDocToFile(kSettingsPath, settingsDocument(8)));
  EXPECT_EQ(Storage.files[kSettingsPath], original);
  EXPECT_EQ(Storage.files[kTempPath], serializedSettings(8).c_str());
  EXPECT_FALSE(Storage.exists(kBackupPath));
}

TEST_F(SettingsPersistenceTest, UncertainPrimaryReadbackKeepsBothNewAndPreviousSettings) {
  const std::string original = serializedSettings(3).c_str();
  Storage.files[kSettingsPath] = original;
  Storage.failReadOnAttempt[kSettingsPath] = 2;  // Preflight succeeds; fail final primary validation.

  EXPECT_FALSE(PersistableStoreBase::writeDocToFile(kSettingsPath, settingsDocument(8)));
  EXPECT_EQ(Storage.files[kSettingsPath], serializedSettings(8).c_str());
  EXPECT_EQ(Storage.files[kBackupPath], original);
  EXPECT_FALSE(Storage.exists((std::string(kSettingsPath) + ".failed-write-0").c_str()));
}

TEST_F(SettingsPersistenceTest, SuccessfulSaveReplacesMainOnlyAfterTemporaryValidation) {
  Storage.files[kSettingsPath] = serializedSettings(1).c_str();
  const JsonDocument replacement = settingsDocument(6);

  EXPECT_TRUE(PersistableStoreBase::writeDocToFile(kSettingsPath, replacement));
  EXPECT_FALSE(Storage.exists(kBackupPath));
  EXPECT_FALSE(Storage.exists(kTempPath));
  EXPECT_EQ(loadedTheme(), 6);
}

TEST_F(SettingsPersistenceTest, RecentSaveChecksCompleteTemporaryBytesAndRetainsPreviousPrimary) {
  const String original = serializedRecent(17);
  Storage.files[kRecentPath] = original.c_str();
  Storage.files[std::string(kRecentPath) + ".bak"] = serializedRecent(9).c_str();

  JsonDocument replacement;
  JsonArray books = replacement["books"].to<JsonArray>();
  JsonObject book = books.add<JsonObject>();
  book["path"] = "/manga/One Piece.xtch";
  book["title"] = "One Piece";
  book["series"] = "One Piece";
  book["xtcMetadataChecked"] = true;
  book["readingSeconds"] = 25;

  EXPECT_TRUE(PersistableStoreBase::writeDocToFile(kRecentPath, replacement));

  EXPECT_EQ(Storage.files[kRecentPath], serializedRecent(25).c_str());
  EXPECT_EQ(Storage.files[std::string(kRecentPath) + ".bak"], original.c_str());
  EXPECT_FALSE(Storage.files.contains(std::string(kRecentPath) + ".tmp"));
  EXPECT_FALSE(Storage.files.contains(std::string(kRecentPath) + ".previous-0"));
}

TEST_F(SettingsPersistenceTest, TruncatedRecentTemporaryWriteCannotReplaceHistoryOrBackup) {
  const String original = serializedRecent(17);
  const String backup = serializedRecent(9);
  Storage.files[kRecentPath] = original.c_str();
  Storage.files[std::string(kRecentPath) + ".bak"] = backup.c_str();
  Storage.truncateTempWrite = true;

  JsonDocument replacement;
  JsonArray books = replacement["books"].to<JsonArray>();
  JsonObject book = books.add<JsonObject>();
  book["path"] = "/manga/One Piece.xtch";
  book["title"] = "One Piece";
  book["series"] = "One Piece";
  book["xtcMetadataChecked"] = true;
  book["readingSeconds"] = 25;

  EXPECT_FALSE(PersistableStoreBase::writeDocToFile(kRecentPath, replacement));
  EXPECT_EQ(Storage.files[kRecentPath], original.c_str());
  EXPECT_EQ(Storage.files[std::string(kRecentPath) + ".bak"], backup.c_str());
  EXPECT_TRUE(Storage.files.contains(std::string(kRecentPath) + ".tmp"));
}

TEST_F(SettingsPersistenceTest, RecentRecoverySaveKeepsValidatedBackupAndPriorTemporaryCandidate) {
  const std::string damagedPrimary = "{\"books\":[{\"path\":\"/manga/partial";
  const String validBackup = serializedRecent(17);
  const String pendingCandidate = serializedRecent(22);
  Storage.files[kRecentPath] = damagedPrimary;
  Storage.files[std::string(kRecentPath) + ".bak"] = validBackup.c_str();
  Storage.files[std::string(kRecentPath) + ".tmp"] = pendingCandidate.c_str();

  JsonDocument replacement;
  JsonArray books = replacement["books"].to<JsonArray>();
  JsonObject book = books.add<JsonObject>();
  book["path"] = "/manga/One Piece.xtch";
  book["title"] = "One Piece";
  book["series"] = "One Piece";
  book["xtcMetadataChecked"] = true;
  book["readingSeconds"] = 25;

  ASSERT_TRUE(PersistableStoreBase::writeRecoveredRecentBooksDocToFile(kRecentPath, replacement));

  EXPECT_EQ(Storage.files[kRecentPath], serializedRecent(25).c_str());
  EXPECT_EQ(Storage.files[std::string(kRecentPath) + ".bak"], validBackup.c_str());
  EXPECT_EQ(Storage.files[std::string(kRecentPath) + ".recovered-primary-0"], damagedPrimary);
  EXPECT_EQ(Storage.files[std::string(kRecentPath) + ".tmp.recovery-candidate-0"], pendingCandidate.c_str());
  EXPECT_FALSE(Storage.files.contains(std::string(kRecentPath) + ".tmp"));
}

TEST_F(SettingsPersistenceTest, UnchangedSettingsSkipAtomicWritesAfterValidatedLoad) {
  Storage.files[kSettingsPath] = serializedSettings(3).c_str();
  SettingsSaveProbe settings;
  ASSERT_TRUE(settings.loadFromFile());
  ASSERT_EQ(settings.uiTheme, 3);
  const size_t writesBefore = Storage.writeCount;
  EXPECT_TRUE(settings.saveToFile());
  EXPECT_EQ(Storage.writeCount, writesBefore);
  EXPECT_EQ(Storage.readAttempts[kSettingsPath], 1u);
}

TEST_F(SettingsPersistenceTest, ChangedSettingsUseAtomicWriteThenSkipSubsequentNoOpSaves) {
  Storage.files[kSettingsPath] = serializedSettings(3).c_str();
  SettingsSaveProbe settings;
  ASSERT_TRUE(settings.loadFromFile());
  settings.uiTheme = 4;
  ASSERT_TRUE(settings.saveToFile());
  EXPECT_EQ(Storage.writeCount, 1u);
  EXPECT_EQ(loadedTheme(), 4);
  const size_t readsAfterWrite = Storage.readAttempts[kSettingsPath];
  EXPECT_TRUE(settings.saveToFile());
  EXPECT_EQ(Storage.writeCount, 1u);
  EXPECT_EQ(Storage.readAttempts[kSettingsPath], readsAfterWrite);
}

TEST_F(SettingsPersistenceTest, ChangedSaveAfterValidatedLoadDoesNotProbeSettingsCandidates) {
  Storage.files[kSettingsPath] = serializedSettings(3).c_str();
  SettingsSaveProbe settings;
  ASSERT_TRUE(settings.loadFromFile());
  const size_t mainReadsBefore = Storage.readAttempts[kSettingsPath];
  const size_t backupReadsBefore = Storage.readAttempts[kBackupPath];
  const size_t tempReadsBefore = Storage.readAttempts[kTempPath];
  const size_t existsBefore = Storage.existsCount;

  settings.uiTheme = 4;
  ASSERT_TRUE(settings.saveToFile());

  EXPECT_EQ(Storage.readAttempts[kSettingsPath], mainReadsBefore);
  EXPECT_EQ(Storage.readAttempts[kBackupPath], backupReadsBefore);
  // The fast path still reads back and validates the newly written temporary
  // document before its atomic rename into the primary path.
  EXPECT_EQ(Storage.readAttempts[kTempPath], tempReadsBefore + 1);
  EXPECT_EQ(Storage.existsCount, existsBefore);
  EXPECT_EQ(Storage.files[kBackupPath], serializedSettings(3).c_str());
  EXPECT_EQ(loadedTheme(), 4);
}

TEST_F(SettingsPersistenceTest, FirstSettingsSaveAfterMissingLoadDoesNotProbeCandidatesAgain) {
  SettingsSaveProbe settings;
  persistable::SettingsLoadReport report;
  EXPECT_FALSE(settings.loadFromFile(&report));
  EXPECT_FALSE(report.anyExisting);
  const size_t mainReadsBefore = Storage.readAttempts[kSettingsPath];
  const size_t backupReadsBefore = Storage.readAttempts[kBackupPath];
  const size_t tempReadsBefore = Storage.readAttempts[kTempPath];
  const size_t existsBefore = Storage.existsCount;

  settings.uiTheme = 4;
  ASSERT_TRUE(settings.saveToFile());

  EXPECT_EQ(Storage.readAttempts[kSettingsPath], mainReadsBefore);
  EXPECT_EQ(Storage.readAttempts[kBackupPath], backupReadsBefore);
  EXPECT_EQ(Storage.readAttempts[kTempPath], tempReadsBefore + 1);
  EXPECT_EQ(Storage.existsCount, existsBefore);
  EXPECT_EQ(loadedTheme(), 4);
}

TEST_F(SettingsPersistenceTest, ExternalSettingsMutationInvalidatesTheCandidateCache) {
  Storage.files[kSettingsPath] = serializedSettings(3).c_str();
  SettingsSaveProbe settings;
  ASSERT_TRUE(settings.loadFromFile());
  const size_t mainReadsBefore = Storage.readAttempts[kSettingsPath];
  const size_t backupReadsBefore = Storage.readAttempts[kBackupPath];
  const size_t tempReadsBefore = Storage.readAttempts[kTempPath];

  settings.invalidatePersistenceCache();
  settings.uiTheme = 5;
  ASSERT_TRUE(settings.saveToFile());

  EXPECT_GT(Storage.readAttempts[kSettingsPath], mainReadsBefore);
  EXPECT_GT(Storage.readAttempts[kBackupPath], backupReadsBefore);
  EXPECT_GT(Storage.readAttempts[kTempPath], tempReadsBefore);
}

TEST_F(SettingsPersistenceTest, XtcReaderSettingsKeepLegacyDefaultsAndNormalizeRange) {
  EXPECT_EQ(CrossPointSettings::missingXtcReadingDirection(false, CrossPointSettings::XTC_RIGHT_TO_LEFT),
            CrossPointSettings::XTC_LEFT_TO_RIGHT);
  EXPECT_EQ(CrossPointSettings::missingXtcPageFitMode(false, CrossPointSettings::XTC_CROP_FILL),
            CrossPointSettings::XTC_FIT_CONTAIN);
  EXPECT_EQ(CrossPointSettings::missingXtcReadingDirection(true, CrossPointSettings::XTC_RIGHT_TO_LEFT),
            CrossPointSettings::XTC_RIGHT_TO_LEFT);
  EXPECT_EQ(CrossPointSettings::missingXtcPageFitMode(true, CrossPointSettings::XTC_CROP_FILL),
            CrossPointSettings::XTC_CROP_FILL);
  EXPECT_EQ(CrossPointSettings::normalizeXtcReadingDirection(-1), CrossPointSettings::XTC_LEFT_TO_RIGHT);
  EXPECT_EQ(CrossPointSettings::normalizeXtcReadingDirection(2), CrossPointSettings::XTC_LEFT_TO_RIGHT);
  EXPECT_EQ(CrossPointSettings::normalizeXtcPageFitMode(-1), CrossPointSettings::XTC_FIT_CONTAIN);
  EXPECT_EQ(CrossPointSettings::normalizeXtcPageFitMode(2), CrossPointSettings::XTC_FIT_CONTAIN);
  EXPECT_EQ(CrossPointSettings::normalizeXtcReadingDirection(1), CrossPointSettings::XTC_RIGHT_TO_LEFT);
  EXPECT_EQ(CrossPointSettings::normalizeXtcPageFitMode(1), CrossPointSettings::XTC_CROP_FILL);
}

TEST_F(SettingsPersistenceTest, SettingsLoadedFromBackupDoNotSeedNoOpFingerprint) {
  Storage.files[kSettingsPath] = "{broken";
  Storage.files[kBackupPath] = serializedSettings(5).c_str();
  SettingsSaveProbe settings;
  persistable::SettingsLoadReport report;
  ASSERT_TRUE(settings.loadFromFile(&report));
  ASSERT_EQ(report.selected, 1u);
  EXPECT_TRUE(settings.saveToFile());
  EXPECT_GT(Storage.writeCount, 0u);
}

TEST_F(SettingsPersistenceTest, FailedChangedSettingsSaveRetriesThroughAtomicValidation) {
  Storage.files[kSettingsPath] = serializedSettings(2).c_str();
  SettingsSaveProbe settings;
  ASSERT_TRUE(settings.loadFromFile());
  settings.uiTheme = 6;
  Storage.failWritePaths[kTempPath] = true;

  EXPECT_FALSE(settings.saveToFile());
  EXPECT_EQ(Storage.writeCount, 1u);
  Storage.failWritePaths.clear();

  EXPECT_TRUE(settings.saveToFile());
  EXPECT_EQ(Storage.writeCount, 2u);
  EXPECT_EQ(loadedTheme(), 6);
}
