#include "KOReaderSyncClient.h"

#include <ArduinoJson.h>
#include <Logging.h>
#include <SecureHttpClient.h>
#include <base64.h>

#include <string>

#include "KOReaderCredentialStore.h"

#ifndef NOOIR_KOSYNC_FONT_DIAGNOSTICS
#define NOOIR_KOSYNC_FONT_DIAGNOSTICS 0
#endif

int KOReaderSyncClient::lastHttpCode = 0;

namespace {
// Keep the identifier stable for existing sync records; the display name is
// user-editable in KOReader settings.
constexpr char DEVICE_ID[] = "crosspoint-reader";

// KOSync's TLS-1.3 servers can't be reached through the precompiled system
// mbedTLS (TLS 1.3 is stubbed out), so requests run over wolfSSL via
// SecureHttpClient. The handshake still needs working heap; gate on it. wolfSSL's
// footprint is smaller than mbedTLS's old ~48KB peak, but keep a conservative
// floor. Check both total free heap and largest contiguous block so fragmented
// heap does not fall through into a failed TLS allocation path.
// MEMFIX-PORT: TLS heap gate; portable
// Field data (July 2026): launching sync from a reader session lands at
// 51.9-58.2 KB free / 42-53 KB maxAlloc after WiFi comes up. wolfSSL handles
// allocation failure by returning MEMORY_E (no abort under -fno-exceptions),
// so an optimistic attempt degrades to the same clean "sync failed" as the
// gate — the gate only needs to keep out states where a doomed handshake
// would waste tens of seconds, not guarantee success.
//
// Free and largest-block have separate requirements: with SP ECC
// (WOLFSSL_HAVE_SP_ECC) the handshake's crypto uses fixed 256-bit arrays, so
// the largest single TLS allocation is the ~17 KB wolfSSL record buffer, not
// a run of fast-math bignums. A handshake was measured succeeding inside a
// 43 KB largest block; requiring 50 KB contiguous refused syncs that fit.
constexpr uint32_t MIN_FREE_FOR_TLS = 50000;
constexpr uint32_t MIN_BLOCK_FOR_TLS = 20000;

bool isHttpSuccess(const int status) { return status >= 200 && status < 300; }

#if NOOIR_KOSYNC_FONT_DIAGNOSTICS
void syncDiagnostic(const char* stage, const int status = 0, const char* document = "", const size_t progressBytes = 0,
                    const float percentage = 0.0f, const bool includeServer = false) {
  const std::string server = includeServer ? KOREADER_STORE.getBaseUrl() : "-";
  LOG_INF("KSDIAG", "stage=%s code=%d free=%u min=%u max=%u doc=%s plen=%u pct=%.3f srv=%s", stage, status,
          ESP.getFreeHeap(), ESP.getMinFreeHeap(), ESP.getMaxAllocHeap(), document ? document : "-",
          static_cast<unsigned>(progressBytes), percentage, server.c_str());
}
#endif

// Apply the shared KOSync auth headers after begin(). x-auth-* is the native
// KOSync scheme; Basic auth is added for Calibre-Web-Automated compatibility.
void applyAuthHeaders(freeink::SecureHttpClient& http) {
  http.addHeader("Accept", "application/vnd.koreader.v1+json");
  http.addHeader("x-auth-user", KOREADER_STORE.getUsername());
  http.addHeader("x-auth-key", KOREADER_STORE.getMd5Password());
  const std::string credentials = KOREADER_STORE.getUsername() + ":" + KOREADER_STORE.getPassword();
  const String encoded = base64::encode(credentials.c_str());
  http.addHeader("Authorization", std::string("Basic ") + encoded.c_str());
}

// True when free heap is too low to risk a TLS handshake.
bool insufficientHeap() {
  const uint32_t freeHeap = ESP.getFreeHeap();
  const uint32_t maxAllocHeap = ESP.getMaxAllocHeap();
  if (freeHeap < MIN_FREE_FOR_TLS || maxAllocHeap < MIN_BLOCK_FOR_TLS) {
    LOG_ERR("KOSync", "Insufficient heap for TLS handshake: %u bytes free (need %u), %u max alloc (need %u)", freeHeap,
            MIN_FREE_FOR_TLS, maxAllocHeap, MIN_BLOCK_FOR_TLS);
    return true;
  }
  return false;
}
}  // namespace

KOReaderSyncClient::Error KOReaderSyncClient::authenticate() {
  lastHttpCode = 0;
#if NOOIR_KOSYNC_FONT_DIAGNOSTICS
  syncDiagnostic("auth_begin", 0, "", 0, 0.0f, true);
#endif
  if (!KOREADER_STORE.hasCredentials()) {
    LOG_DBG("KOSync", "No credentials configured");
    return NO_CREDENTIALS;
  }

  const std::string url = KOREADER_STORE.getBaseUrl() + "/users/auth";
  LOG_DBG("KOSync", "Authenticating: %s (heap: %u)", url.c_str(), (unsigned)ESP.getFreeHeap());
  if (insufficientHeap()) {
#if NOOIR_KOSYNC_FONT_DIAGNOSTICS
    syncDiagnostic("auth_gate", -1);
#endif
    return LOW_MEMORY;
  }

  freeink::SecureHttpClient http;
  http.setInsecure();
  if (!http.begin(url)) {
    LOG_ERR("KOSync", "Bad URL: %s", url.c_str());
    return NETWORK_ERROR;
  }
  applyAuthHeaders(http);
  const int httpCode = http.GET();
  http.end();
  lastHttpCode = httpCode;

  LOG_DBG("KOSync", "Auth response: %d", httpCode);
#if NOOIR_KOSYNC_FONT_DIAGNOSTICS
  syncDiagnostic("auth_result", httpCode);
#endif

  if (httpCode <= 0) return NETWORK_ERROR;
  if (isHttpSuccess(httpCode)) return OK;
  if (httpCode == 401) return AUTH_FAILED;
  return SERVER_ERROR;
}

KOReaderSyncClient::Error KOReaderSyncClient::createUser() {
  lastHttpCode = 0;
#if NOOIR_KOSYNC_FONT_DIAGNOSTICS
  syncDiagnostic("create_begin", 0, "", 0, 0.0f, true);
#endif
  if (!KOREADER_STORE.hasCredentials()) {
    LOG_DBG("KOSync", "No credentials configured");
    return NO_CREDENTIALS;
  }

  const std::string url = KOREADER_STORE.getBaseUrl() + "/users/create";
  LOG_DBG("KOSync", "Creating account: %s (heap: %u)", url.c_str(), (unsigned)ESP.getFreeHeap());
  if (insufficientHeap()) {
#if NOOIR_KOSYNC_FONT_DIAGNOSTICS
    syncDiagnostic("create_gate", -1);
#endif
    return LOW_MEMORY;
  }

  JsonDocument doc;
  doc["username"] = KOREADER_STORE.getUsername();
  doc["password"] = KOREADER_STORE.getMd5Password();
  std::string body;
  serializeJson(doc, body);

  freeink::SecureHttpClient http;
  http.setInsecure();
  if (!http.begin(url)) {
    LOG_ERR("KOSync", "Bad URL: %s", url.c_str());
    return NETWORK_ERROR;
  }
  http.addHeader("Accept", "application/vnd.koreader.v1+json");
  http.addHeader("Content-Type", "application/json");
  const int httpCode = http.sendRequest("POST", body);
  http.end();
  lastHttpCode = httpCode;

  LOG_DBG("KOSync", "Create user response: %d", httpCode);
#if NOOIR_KOSYNC_FONT_DIAGNOSTICS
  syncDiagnostic("create_result", httpCode);
#endif

  if (httpCode <= 0) return NETWORK_ERROR;
  if (isHttpSuccess(httpCode)) return OK;
  if (httpCode == 402) return USER_EXISTS;
  return SERVER_ERROR;
}

KOReaderSyncClient::Error KOReaderSyncClient::getProgress(const std::string& documentHash,
                                                          KOReaderProgress& outProgress) {
  lastHttpCode = 0;
#if NOOIR_KOSYNC_FONT_DIAGNOSTICS
  syncDiagnostic("get_begin", 0, documentHash.c_str(), 0, 0.0f, true);
#endif
  if (!KOREADER_STORE.hasCredentials()) {
    LOG_DBG("KOSync", "No credentials configured");
    return NO_CREDENTIALS;
  }

  const std::string url = KOREADER_STORE.getBaseUrl() + "/syncs/progress/" + documentHash;
  LOG_DBG("KOSync", "Getting progress: %s (heap: %u)", url.c_str(), (unsigned)ESP.getFreeHeap());
  if (insufficientHeap()) {
#if NOOIR_KOSYNC_FONT_DIAGNOSTICS
    syncDiagnostic("get_gate", -1, documentHash.c_str());
#endif
    return LOW_MEMORY;
  }

  freeink::SecureHttpClient http;
  http.setInsecure();
  if (!http.begin(url)) {
    LOG_ERR("KOSync", "Bad URL: %s", url.c_str());
    return NETWORK_ERROR;
  }
  applyAuthHeaders(http);
  const int httpCode = http.GET();
  lastHttpCode = httpCode;

  LOG_DBG("KOSync", "Get progress response: %d", httpCode);
#if NOOIR_KOSYNC_FONT_DIAGNOSTICS
  syncDiagnostic("get_result", httpCode, documentHash.c_str());
#endif

  if (httpCode <= 0) {
    http.end();
    return NETWORK_ERROR;
  }

  // Some KOSync-compatible servers use the HTTP-semantic representation for
  // "no progress". Handle it before trying to parse a response body.
  if (httpCode == 204) {
    http.end();
    return NOT_FOUND;
  }

  if (isHttpSuccess(httpCode)) {
    const std::string responseBody = http.getString();
    http.end();

#if NOOIR_KOSYNC_FONT_DIAGNOSTICS
    syncDiagnostic("get_body", httpCode, documentHash.c_str(), responseBody.size());
#endif

    if (responseBody.length() == 0) {
      LOG_ERR("KOSync", "Successful progress response contained no payload");
      return JSON_ERROR;
    }

    JsonDocument doc;
    const DeserializationError error = deserializeJson(doc, responseBody.c_str());

    if (error) {
      LOG_ERR("KOSync", "JSON parse failed: %s", error.c_str());
      return JSON_ERROR;
    }

    // The reference public server has historically represented a valid
    // no-progress response as HTTP 200 with an empty JSON object. Keep that
    // distinct from an empty body or malformed/non-object JSON.
    const JsonObjectConst responseObject = doc.as<JsonObjectConst>();
    if (!responseObject.isNull() && responseObject.size() == 0) {
      return NOT_FOUND;
    }

    const char* progressValue = doc["progress"].as<const char*>();
    if (progressValue == nullptr || progressValue[0] == '\0') {
      LOG_ERR("KOSync", "Successful progress response contained no usable progress");
      return JSON_ERROR;
    }

    outProgress.document = documentHash;
    outProgress.progress = doc["progress"].as<std::string>();
    outProgress.percentage = doc["percentage"].as<float>();
    outProgress.device = doc["device"].as<std::string>();
    outProgress.deviceId = doc["device_id"].as<std::string>();
    outProgress.timestamp = doc["timestamp"].as<int64_t>();

    // Extended crosspoint-sync field; absent on plain kosync servers.
    outProgress.position.reset();
    const JsonObjectConst pos = doc["position"].as<JsonObjectConst>();
    if (!pos.isNull()) {
      KOReaderRichPosition rich;
      rich.pctQ = pos["pctQ"].as<uint32_t>();
      rich.spineIndex = pos["spine"].as<uint16_t>();
      rich.pageNumber = pos["page"].as<uint16_t>();
      const uint16_t pages = pos["pages"].as<uint16_t>();
      rich.totalPages = pages > 0 ? pages : 1;
      const uint16_t para = pos["para"].as<uint16_t>();
      if (para > 0) rich.paragraphIndex = para;
      rich.xpath = pos["xpath"].as<const char*>() ? pos["xpath"].as<const char*>() : "";
      LOG_DBG("KOSync", "Got rich position: spine=%u page=%u/%u para=%u", rich.spineIndex, rich.pageNumber,
              rich.totalPages, para);
      outProgress.position = std::move(rich);
    }

    LOG_DBG("KOSync", "Got progress: %.2f%% at %s", outProgress.percentage * 100, outProgress.progress.c_str());
    return OK;
  }

  http.end();
  if (httpCode == 401) return AUTH_FAILED;
  if (httpCode == 404) return NOT_FOUND;
  return SERVER_ERROR;
}

KOReaderSyncClient::Error KOReaderSyncClient::updateProgress(const KOReaderProgress& progress) {
  lastHttpCode = 0;
#if NOOIR_KOSYNC_FONT_DIAGNOSTICS
  syncDiagnostic("put_begin", 0, progress.document.c_str(), progress.progress.size(), progress.percentage, true);
#endif
  if (!KOREADER_STORE.hasCredentials()) {
    LOG_DBG("KOSync", "No credentials configured");
    return NO_CREDENTIALS;
  }

  const std::string url = KOREADER_STORE.getBaseUrl() + "/syncs/progress";
  LOG_DBG("KOSync", "Updating progress: %s (heap: %u)", url.c_str(), (unsigned)ESP.getFreeHeap());
  if (insufficientHeap()) {
#if NOOIR_KOSYNC_FONT_DIAGNOSTICS
    syncDiagnostic("put_gate", -1, progress.document.c_str(), progress.progress.size(), progress.percentage);
#endif
    return LOW_MEMORY;
  }

  // Build JSON body
  JsonDocument doc;
  doc["document"] = progress.document;
  if (progress.metadata.has_value()) {
    auto meta = doc["metadata"].to<JsonObject>();
    meta["filename"] = progress.metadata->filename;
    meta["title"] = progress.metadata->title;
    meta["authors"] = progress.metadata->authors;
  }
  doc["progress"] = progress.progress;
  doc["percentage"] = progress.percentage;
  doc["device"] = KOREADER_STORE.getDeviceName();
  doc["device_id"] = DEVICE_ID;
  if (progress.position.has_value() && KOREADER_STORE.usesCrossPointSyncServer()) {
    // Extended CrossPoint field. Do not send it to generic KOReader servers;
    // some self-hosted implementations reject unknown payload fields.
    const auto& p = *progress.position;
    auto pos = doc["position"].to<JsonObject>();
    pos["pctQ"] = p.pctQ;
    pos["spine"] = p.spineIndex;
    pos["page"] = p.pageNumber;
    pos["pages"] = p.totalPages;
    if (p.paragraphIndex.has_value()) pos["para"] = *p.paragraphIndex;
    // Server rejects the whole position object if xpath exceeds 120 bytes.
    if (!p.xpath.empty() && p.xpath.size() <= 120) pos["xpath"] = p.xpath;
  }

  std::string body;
  serializeJson(doc, body);

  LOG_DBG("KOSync", "Request body: %s", body.c_str());

  freeink::SecureHttpClient http;
  http.setInsecure();
  if (!http.begin(url)) {
    LOG_ERR("KOSync", "Bad URL: %s", url.c_str());
    return NETWORK_ERROR;
  }
  applyAuthHeaders(http);
  http.addHeader("Content-Type", "application/json");
  const int httpCode = http.sendRequest("PUT", body);
  http.end();
  lastHttpCode = httpCode;

  LOG_DBG("KOSync", "Update progress response: %d", httpCode);
#if NOOIR_KOSYNC_FONT_DIAGNOSTICS
  syncDiagnostic("put_result", httpCode, progress.document.c_str(), progress.progress.size(), progress.percentage);
#endif

  if (httpCode <= 0) return NETWORK_ERROR;
  if (isHttpSuccess(httpCode)) return OK;
  if (httpCode == 401) return AUTH_FAILED;
  return SERVER_ERROR;
}

const char* KOReaderSyncClient::errorString(Error error) {
  switch (error) {
    case OK:
      return "Success";
    case NO_CREDENTIALS:
      return "No credentials configured";
    case NETWORK_ERROR:
      return "Network error";
    case AUTH_FAILED:
      return "Authentication failed";
    case SERVER_ERROR:
      return "Server error (try again later)";
    case JSON_ERROR:
      return "JSON parse error";
    case NOT_FOUND:
      return "No progress found";
    case LOW_MEMORY:
      return "Not enough memory for sync — please retry";
    default:
      return "Unknown error";
  }
}
