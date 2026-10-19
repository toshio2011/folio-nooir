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

TEST_F(SettingsPersistenceTest, RejectsMalformedMainAndRecoversValidBackup) {
  Storage.files[kSettingsPath] = "{not-json";
  Storage.files[kBackupPath] = serializedSettings(4).c_str();

  EXPECT_EQ(loadedTheme(), 4);
  EXPECT_EQ(Storage.files[kSettingsPath], "{not-json");
  EXPECT_TRUE(Storage.exists(kBackupPath));
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

TEST_F(SettingsPersistenceTest, SuccessfulSaveReplacesMainOnlyAfterTemporaryValidation) {
  Storage.files[kSettingsPath] = serializedSettings(1).c_str();
  const JsonDocument replacement = settingsDocument(6);

  EXPECT_TRUE(PersistableStoreBase::writeDocToFile(kSettingsPath, replacement));
  EXPECT_FALSE(Storage.exists(kBackupPath));
  EXPECT_FALSE(Storage.exists(kTempPath));
  EXPECT_EQ(loadedTheme(), 6);
}
