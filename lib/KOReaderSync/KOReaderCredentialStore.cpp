#include "KOReaderCredentialStore.h"

#include <Logging.h>
#include <MD5Builder.h>
#include <ObfuscationUtils.h>

namespace {
// The empty server URL follows the public KOReader sync service, matching
// KOReader and the original Nooir behavior.
constexpr char DEFAULT_SERVER_URL[] = "https://sync.koreader.rocks:443";

// Config version 2 changed the empty default to CrossPoint. Pin that value for
// existing v2 users before restoring the public default for new setups.
constexpr char CONFIG_V2_DEFAULT_SERVER_URL[] = "https://sync.crosspointreader.com";

// Bumped when a change to defaults would alter behavior for existing configs.
constexpr uint8_t CONFIG_VERSION = 3;
constexpr char DEFAULT_DEVICE_NAME[] = "Folio Nooir X4";
}  // namespace

void KOReaderCredentialStore::toJson(JsonDocument& doc) const {
  doc["cfgVersion"] = CONFIG_VERSION;
  doc["username"] = getUsername();
  doc["password_obf"] = obfuscation::obfuscateToBase64(getPassword());
  doc["serverUrl"] = getServerUrl();
  doc["deviceName"] = getDeviceName();
  doc["matchMethod"] = static_cast<uint8_t>(getMatchMethod());
  doc["sendMetadata"] = getSendMetadata();
  doc["syncBehavior"] = static_cast<uint8_t>(getSyncBehavior());
}

bool KOReaderCredentialStore::fromJson(JsonVariantConst doc) {
  std::string user = doc["username"] | "";

  bool needsResave = false;
  std::string pass = extractPassword(doc, needsResave);

  setCredentials(user, pass);
  setServerUrl(doc["serverUrl"] | "");
  const JsonVariantConst deviceNameValue = doc["deviceName"];
  const bool missingDeviceName = deviceNameValue.isNull();
  setDeviceName(deviceNameValue | DEFAULT_DEVICE_NAME);
  // Older config files have no deviceName. Save the migrated default once so
  // the editable value survives reboot and remains visible to web settings.
  needsResave = needsResave || missingDeviceName;

  // Config v2 made CrossPoint the empty-URL default. Preserve that behavior for
  // existing v2 users by pinning it explicitly, while v1/legacy users retain
  // the public KOReader default. Fresh setups use DEFAULT_SERVER_URL.
  const uint8_t cfgVersion = doc["cfgVersion"] | (uint8_t)1;
  if (cfgVersion < CONFIG_VERSION) {
    if (getServerUrl().empty() && hasCredentials()) {
      const char* previousDefault = cfgVersion >= 2 ? CONFIG_V2_DEFAULT_SERVER_URL : DEFAULT_SERVER_URL;
      LOG_DBG("KRS", "Preserving configured default server; pinning %s", previousDefault);
      setServerUrl(previousDefault);
    }
    needsResave = true;  // stamp cfgVersion so this migration runs once
  }

  uint8_t method = doc["matchMethod"] | (uint8_t)0;
  if (method <= static_cast<uint8_t>(DocumentMatchMethod::BINARY)) {
    setMatchMethod(static_cast<DocumentMatchMethod>(method));
  } else {
    LOG_DBG("KRS", "Invalid matchMethod %u in JSON, resetting to FILENAME", method);
    setMatchMethod(DocumentMatchMethod::FILENAME);
  }
  setSendMetadata(doc["sendMetadata"] | false);

  const JsonVariantConst behaviorValue = doc["syncBehavior"];
  const bool missingBehavior = behaviorValue.isNull();
  uint8_t behavior = behaviorValue | static_cast<uint8_t>(KOReaderSyncBehavior::ASK_EVERY_TIME);
  if (behavior <= static_cast<uint8_t>(KOReaderSyncBehavior::SMART)) {
    setSyncBehavior(static_cast<KOReaderSyncBehavior>(behavior));
    needsResave = needsResave || missingBehavior;
  } else {
    LOG_DBG("KRS", "Invalid syncBehavior %u in JSON, resetting to ASK_EVERY_TIME", behavior);
    setSyncBehavior(KOReaderSyncBehavior::ASK_EVERY_TIME);
    needsResave = true;
  }

  if (needsResave) {
    LOG_DBG("KRS", "Resaving KOReader credentials to update format");
    requestResave();
  }

  return true;
}

void KOReaderCredentialStore::setCredentials(const std::string& user, const std::string& pass) {
  username = user;
  password = pass;
  LOG_DBG("KRS", "Set credentials for user: %s", user.c_str());
}

std::string KOReaderCredentialStore::getMd5Password() const {
  if (password.empty()) {
    return "";
  }

  // Calculate MD5 hash of password using ESP32's MD5Builder
  MD5Builder md5;
  md5.begin();
  md5.add(password.c_str());
  md5.calculate();

  return md5.toString().c_str();
}

bool KOReaderCredentialStore::hasCredentials() const { return !username.empty() && !password.empty(); }

void KOReaderCredentialStore::clearCredentials() {
  username.clear();
  password.clear();
  saveToFile();
  LOG_DBG("KRS", "Cleared KOReader credentials");
}

void KOReaderCredentialStore::setServerUrl(const std::string& url) {
  serverUrl = url;
  LOG_DBG("KRS", "Set server URL: %s", url.empty() ? "(default)" : url.c_str());
}

void KOReaderCredentialStore::setDeviceName(const std::string& name) {
  // Keep the value small enough for the sync result screen and the server's
  // device field, while allowing normal user-entered names.
  deviceName = name.substr(0, 40);
  if (deviceName.empty()) deviceName = DEFAULT_DEVICE_NAME;
  LOG_DBG("KRS", "Set device name: %s", deviceName.c_str());
}

bool KOReaderCredentialStore::usesCrossPointSyncServer() const {
  const std::string base = getBaseUrl();
  // The empty URL resolves to the official CrossPoint service. A custom
  // CrossPoint deployment can opt into the extension simply by using a host
  // containing "crosspoint"; generic KOReader servers stay standard-only.
  return base.find("sync.crosspointreader.com") != std::string::npos ||
         base.find("crosspoint") != std::string::npos;
}

std::string KOReaderCredentialStore::getBaseUrl() const {
  std::string url;
  if (serverUrl.empty()) {
    url = DEFAULT_SERVER_URL;
  } else if (serverUrl.find("://") == std::string::npos) {
    // Normalize URL: add http:// if no protocol specified (local servers typically don't have SSL)
    url = "http://" + serverUrl;
  } else {
    url = serverUrl;
  }

  // Strip trailing slashes to avoid double-slash in API paths
  while (!url.empty() && url.back() == '/') {
    url.pop_back();
  }

  return url;
}

void KOReaderCredentialStore::setMatchMethod(DocumentMatchMethod method) {
  matchMethod = method;
  LOG_DBG("KRS", "Set match method: %s", method == DocumentMatchMethod::FILENAME ? "Filename" : "Binary");
}

void KOReaderCredentialStore::setSendMetadata(bool enabled) {
  sendMetadata = enabled;
  LOG_DBG("KRS", "Set send metadata: %s", enabled ? "true" : "false");
}

void KOReaderCredentialStore::setSyncBehavior(KOReaderSyncBehavior behavior) {
  if (static_cast<uint8_t>(behavior) > static_cast<uint8_t>(KOReaderSyncBehavior::SMART)) {
    behavior = KOReaderSyncBehavior::ASK_EVERY_TIME;
  }
  syncBehavior = behavior;
  LOG_DBG("KRS", "Set sync behavior: %s", behavior == KOReaderSyncBehavior::SMART ? "Smart" : "Ask");
}
