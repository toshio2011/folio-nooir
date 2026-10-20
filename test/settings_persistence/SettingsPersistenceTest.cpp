#include <ArduinoJson.h>
#include <HalStorage.h>

#include <Serialization/PersistableStore.h>
#include <Serialization/PersistableStorePolicy.h>

#include <gtest/gtest.h>

namespace {

constexpr const char* kSettingsPath = "/.crosspoint/settings.json";
constexpr const char* kBackupPath = "/.crosspoint/settings.json.bak";
constexpr const char* kTempPath = "/.crosspoint/settings.json.tmp";

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

uint8_t loadedTheme() {
  JsonDocument loaded;
  EXPECT_TRUE(PersistableStoreBase::readDocFromFile(kSettingsPath, loaded));
  return loaded["uiTheme"].as<uint8_t>();
}

}  // namespace

class SettingsPersistenceTest : public ::testing::Test {
 protected:
  void SetUp() override { Storage.reset(); }
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
