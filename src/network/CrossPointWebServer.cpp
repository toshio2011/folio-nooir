#include "CrossPointWebServer.h"

#include <ArduinoJson.h>
#include <Cbz.h>
#include <Epub.h>
#include <FsHelpers.h>
#include <HalGPIO.h>
#include <HalStorage.h>
#include <Logging.h>
#include <WiFi.h>
#include <Xtc.h>
#include <esp_efuse.h>
#include <esp_efuse_table.h>
#include <freertos/task.h>
#include <lwip/sockets.h>

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdlib>
#include <cstdio>
#include <cstring>
#include <cstdint>
#include <string_view>

#include "CrossPointSettings.h"
#include "BleInput.h"
#include "ClockWeatherSyncService.h"
#include "components/UITheme.h"
#include "FontInstaller.h"
#include "OpdsServerStore.h"
#include "BookStateStore.h"
#include "BookMetadataOverridesStore.h"
#include "RecentBooksStore.h"
#include "ReadingStatsStore.h"
#include "HalClock.h"
#include "SdCardFontSystem.h"
#include "SettingsList.h"
#include "WebDAVHandler.h"
#include "WebClippingsHandler.h"
#include "WifiCredentialStore.h"
#include "WeatherStore.h"
#include "ToDoStore.h"
#include "html/FilesPageHtml.generated.h"
#include "html/FontsPageHtml.generated.h"
#include "html/HomePageHtml.generated.h"
#include "html/LibraryPageHtml.generated.h"
#include "html/StatsPageHtml.generated.h"
#include "html/SettingsPageHtml.generated.h"
#include "html/ToDoPageHtml.generated.h"
#include "html/js/jszip_minJs.generated.h"
#include "util/BookCacheUtils.h"
#include <TaskWatchdog.h>

namespace {
// Folders/files to hide from the web interface file browser
// Note: Items starting with "." are automatically hidden
constexpr const char* HIDDEN_ITEMS[] = {"System Volume Information", "XTCache"};
constexpr uint16_t UDP_PORTS[] = {54982, 48123, 39001, 44044, 59678};
constexpr uint16_t LOCAL_UDP_PORT = 8134;

// Static pointer for WebSocket callback (WebSocketsServer requires C-style callback)
CrossPointWebServer* wsInstance = nullptr;

void closeFileIfOpen(HalFile& file) {
  // HalFile deliberately asserts on operations against a default-constructed
  // or moved-from object.  isOpen() is the ownership-safe probe for the
  // persistent upload members below; it also avoids a second close after the
  // underlying FsFile has already been released.
  if (file.isOpen()) file.close();
}

// WebSocket upload state
HalFile wsUploadFile;
String wsUploadFileName;
String wsUploadPath;
size_t wsUploadSize = 0;
size_t wsUploadReceived = 0;
unsigned long wsUploadStartTime = 0;
bool wsUploadInProgress = false;
uint8_t wsUploadClientNum = 255;  // 255 = no active upload client
size_t wsLastProgressSent = 0;
String wsLastCompleteName;
size_t wsLastCompleteSize = 0;
unsigned long wsLastCompleteAt = 0;

void logWebOwnerDelta(const char* owner, const size_t beforeFree, const size_t beforeLargest) {
  LOG_INF("WEBMEM", "owner=%s free=%u->%u largest=%u->%u", owner ? owner : "unknown",
          static_cast<unsigned>(beforeFree), static_cast<unsigned>(ESP.getFreeHeap()),
          static_cast<unsigned>(beforeLargest), static_cast<unsigned>(ESP.getMaxAllocHeap()));
}

String normalizeWebPath(const String& inputPath) {
  if (inputPath.isEmpty() || inputPath == "/") {
    return "/";
  }
  std::string normalized = FsHelpers::normalisePath(inputPath.c_str());
  String result = normalized.c_str();
  if (result.isEmpty()) {
    return "/";
  }
  if (!result.startsWith("/")) {
    result = "/" + result;
  }
  if (result.length() > 1 && result.endsWith("/")) {
    result = result.substring(0, result.length() - 1);
  }
  return result;
}

bool isProtectedItemName(const String& name) {
  if (name.startsWith(".")) {
    return true;
  }
  for (const auto* item : HIDDEN_ITEMS) {
    if (name.equals(item)) {
      return true;
    }
  }
  return false;
}

bool isBookFile(const String& filename) {
  const std::string_view view(filename.c_str(), filename.length());
  return FsHelpers::hasEpubExtension(view) || FsHelpers::hasXtcExtension(view) || FsHelpers::hasCbzExtension(view) ||
         FsHelpers::hasTxtExtension(view) ||
         FsHelpers::hasMarkdownExtension(view);
}

// Web requests can ask for the same absent cover repeatedly while the browser
// refreshes its library view. Keep this small, fixed-size, and session-scoped:
// a miss must not cause another SD stat/open on every request, and no thumbnail
// generation is permitted from the WebServer path.
constexpr size_t kWebThumbnailNegativeCacheSize = 24;
constexpr unsigned long kWebThumbnailNegativeTtlMs = 5UL * 60UL * 1000UL;
struct WebThumbnailNegativeEntry {
  uint64_t key = 0;
  unsigned long expiresAt = 0;
};
WebThumbnailNegativeEntry webThumbnailNegativeCache[kWebThumbnailNegativeCacheSize];
size_t webThumbnailNegativeCursor = 0;

uint64_t webThumbnailKey(const std::string& path) {
  uint64_t hash = 1469598103934665603ULL;
  for (const unsigned char c : path) {
    hash ^= c;
    hash *= 1099511628211ULL;
  }
  return hash;
}

void clearWebThumbnailNegativeCache() {
  for (auto& entry : webThumbnailNegativeCache) entry = {};
  webThumbnailNegativeCursor = 0;
}

bool isWebThumbnailKnownMissing(const uint64_t key) {
  const unsigned long now = millis();
  for (auto& entry : webThumbnailNegativeCache) {
    if (entry.key == 0) continue;
    if (static_cast<long>(now - entry.expiresAt) >= 0) {
      entry = {};
      continue;
    }
    if (entry.key == key) return true;
  }
  return false;
}

void rememberWebThumbnailMissing(const uint64_t key) {
  if (key == 0) return;
  const unsigned long expiresAt = millis() + kWebThumbnailNegativeTtlMs;
  for (auto& entry : webThumbnailNegativeCache) {
    if (entry.key == key) {
      entry.expiresAt = expiresAt;
      return;
    }
  }
  for (auto& entry : webThumbnailNegativeCache) {
    if (entry.key == 0) {
      entry.key = key;
      entry.expiresAt = expiresAt;
      return;
    }
  }
  webThumbnailNegativeCache[webThumbnailNegativeCursor] = {key, expiresAt};
  webThumbnailNegativeCursor = (webThumbnailNegativeCursor + 1) % kWebThumbnailNegativeCacheSize;
}

void forgetWebThumbnailMissing(const uint64_t key) {
  for (auto& entry : webThumbnailNegativeCache) {
    if (entry.key == key) entry = {};
  }
}

bool checkWebThumbnail(const std::string& path, const char* route) {
  const uint64_t key = webThumbnailKey(path);
  if (isWebThumbnailKnownMissing(key)) {
    LOG_INF("WEBRT", "route=%s phase=thumbnail result=negative_cached key=%s", route ? route : "unknown",
            path.c_str());
    return false;
  }
  const bool valid = isValidBookThumbnail(path);
  if (valid) {
    forgetWebThumbnailMissing(key);
  } else {
    rememberWebThumbnailMissing(key);
  }
  LOG_INF("WEBRT", "route=%s phase=thumbnail result=%s key=%s", route ? route : "unknown",
          valid ? "valid" : "missing_cached", path.c_str());
  return valid;
}

struct WebRouteTrace {
  WebServer* server;
  const char* route;
  const char* strategy;
  unsigned long startedAt;

  WebRouteTrace(WebServer* server, const char* route, const char* strategy)
      : server(server), route(route), strategy(strategy), startedAt(millis()) {
    LOG_INF("WEBRT", "route=%s phase=begin strategy=%s free=%lu largest=%lu min=%lu client=%d", route, strategy,
            static_cast<unsigned long>(ESP.getFreeHeap()), static_cast<unsigned long>(ESP.getMaxAllocHeap()),
            static_cast<unsigned long>(ESP.getMinFreeHeap()), server && server->client().connected() ? 1 : 0);
  }

  ~WebRouteTrace() {
    LOG_INF("WEBRT", "route=%s phase=complete strategy=%s elapsed=%lu free=%lu largest=%lu min=%lu client=%d",
            route, strategy, millis() - startedAt, static_cast<unsigned long>(ESP.getFreeHeap()),
            static_cast<unsigned long>(ESP.getMaxAllocHeap()), static_cast<unsigned long>(ESP.getMinFreeHeap()),
            server && server->client().connected() ? 1 : 0);
  }
};

void logSettingsStage(const char* phase, const unsigned index = 0, const char* key = nullptr, WebServer* server = nullptr) {
  LOG_INF("WEBRT",
          "route=/api/settings phase=%s index=%u key=%.24s free=%lu largest=%lu min=%lu stack=%u client=%d", phase,
          index, key ? key : "-", static_cast<unsigned long>(ESP.getFreeHeap()),
          static_cast<unsigned long>(ESP.getMaxAllocHeap()), static_cast<unsigned long>(ESP.getMinFreeHeap()),
          static_cast<unsigned>(uxTaskGetStackHighWaterMark(nullptr)), server && server->client().connected() ? 1 : 0);
}

// Bounded JSON writer for GET /api/settings. ArduinoJson's dynamic document
// grows to the largest option list and its serializeJson path adds another
// temporary String. Emit directly from the effective descriptor into one small
// reusable chunk instead; the descriptor visitor remains the only per-setting
// object lifetime.
struct SettingsJsonStream {
  static constexpr size_t kBufferSize = 256;
  WebServer* server;
  char buffer[kBufferSize];
  size_t length = 0;
  size_t totalBytes = 0;
  size_t transportBytesAccepted = 0;
  size_t transportChunks = 0;
  size_t transportWriteCalls = 0;
  bool closed = false;
  bool firstField = true;
  bool countOnly = false;

  explicit SettingsJsonStream(WebServer* server, const bool countOnly = false)
      : server(server), countOnly(countOnly) {}

  size_t bytesWritten() const { return totalBytes; }

  void flush() {
    if (countOnly) return;
    if (closed || length == 0) return;
    const size_t requested = length;
    const size_t freeBefore = ESP.getFreeHeap();
    const size_t largestBefore = ESP.getMaxAllocHeap();
    const bool connectedBefore = server->client().connected();
    size_t sent = 0;
    // NetworkClient::write retries EAGAIN internally but can still return a
    // partial count after its bounded select window. Resume only the unsent
    // suffix a few times while this same synchronous client remains live.
    // A peer close is still a transport failure, never a successful/truncated
    // JSON response.
    constexpr unsigned kResumeAttempts = 3;
    for (unsigned attempt = 0; sent < requested && attempt <= kResumeAttempts; ++attempt) {
      if (!server->client().connected()) break;
      const size_t written = server->client().write(
          reinterpret_cast<const uint8_t*>(buffer) + sent, requested - sent);
      sent += written;
      if (written == 0 && !server->client().connected()) break;
    }
    transportBytesAccepted += sent;
    if (sent == requested) ++transportChunks;
    ++transportWriteCalls;
    length = 0;
    const size_t freeAfter = ESP.getFreeHeap();
    const size_t largestAfter = ESP.getMaxAllocHeap();
    const bool connectedAfter = server->client().connected();
    LOG_INF("WEBRT", "route=/api/settings phase=write call=%u requested=%u accepted=%u total=%u/%u free=%u->%u largest=%u->%u client=%d->%d tx_pending=unavailable",
            static_cast<unsigned>(transportWriteCalls), static_cast<unsigned>(requested),
            static_cast<unsigned>(sent), static_cast<unsigned>(transportBytesAccepted),
            static_cast<unsigned>(totalBytes), static_cast<unsigned>(freeBefore),
            static_cast<unsigned>(freeAfter), static_cast<unsigned>(largestBefore),
            static_cast<unsigned>(largestAfter), connectedBefore ? 1 : 0, connectedAfter ? 1 : 0);
    if (sent != requested || !server->client().connected()) {
      closed = true;
      LOG_ERR("WEBRT", "route=/api/settings phase=transport_backpressure requested=%u sent=%u total=%u/%u free=%u largest=%u",
              static_cast<unsigned>(requested), static_cast<unsigned>(sent),
              static_cast<unsigned>(transportBytesAccepted), static_cast<unsigned>(totalBytes),
              static_cast<unsigned>(ESP.getFreeHeap()), static_cast<unsigned>(ESP.getMaxAllocHeap()));
      server->client().stop();
    }
  }

  void append(const char c) {
    if (countOnly) {
      ++totalBytes;
      return;
    }
    if (length + 2 >= kBufferSize) flush();
    if (closed) return;
    buffer[length++] = c;
    ++totalBytes;
  }

  void raw(const char* text) {
    if (!text) return;
    while (*text && !closed) append(*text++);
  }

  void quoted(const char* text) {
    append('"');
    if (text) {
      for (const unsigned char* p = reinterpret_cast<const unsigned char*>(text); *p && !closed; ++p) {
        switch (*p) {
          case '"': raw("\\\""); break;
          case '\\': raw("\\\\"); break;
          case '\b': raw("\\b"); break;
          case '\f': raw("\\f"); break;
          case '\n': raw("\\n"); break;
          case '\r': raw("\\r"); break;
          case '\t': raw("\\t"); break;
          default:
            if (*p < 0x20) {
              static constexpr char kHex[] = "0123456789ABCDEF";
              char escaped[7] = {'\\', 'u', '0', '0', kHex[*p >> 4], kHex[*p & 0x0F], '\0'};
              raw(escaped);
            } else {
              append(static_cast<char>(*p));
            }
            break;
        }
      }
    }
    append('"');
  }

  void fieldName(const char* name) {
    if (!firstField) append(',');
    quoted(name);
    append(':');
    firstField = false;
  }

  void fieldString(const char* name, const char* value) {
    fieldName(name);
    quoted(value);
  }

  void fieldString(const char* name, const std::string& value) { fieldString(name, value.c_str()); }

  void fieldInt(const char* name, const int value) {
    char number[16];
    snprintf(number, sizeof(number), "%d", value);
    fieldName(name);
    raw(number);
  }

  void beginObject() {
    append('{');
    firstField = true;
  }

  void endObject() { append('}'); }

  void beginArray(const char* name) {
    fieldName(name);
    append('[');
    firstField = true;
  }

  void arrayString(const char* value) {
    if (!firstField) append(',');
    quoted(value);
    firstField = false;
  }

  void endArray() { append(']'); }
};

// Wi-Fi/server buffers are already active when a route starts. This floor is
// intentionally Web-specific: it is derived from the observed settings
// request's lowest stable working point, not from the BLE admission floors.
// The settings response uses a 256-byte stack writer and static descriptor
// references. Physical measurements showed up to 24,548 bytes of transient
// heap pressure for a 9,489-byte response, so budget a conservative 3x body
// allowance plus 12 KiB reserve. This applies per page and is not a BLE floor.
constexpr size_t kWebSettingsMinFreeHeap = 20 * 1024;
constexpr size_t kWebSettingsMinLargestBlock = 8 * 1024;
constexpr size_t kWebSettingsSafetyReserve = 12 * 1024;
constexpr size_t kWebSettingsObservedPeakMultiplier = 3;

bool webSettingsMemoryAdmitted(WebServer* server, const size_t responseBytes) {
  const size_t freeHeap = ESP.getFreeHeap();
  const size_t largest = ESP.getMaxAllocHeap();
  const size_t maxSize = static_cast<size_t>(-1);
  const size_t projectedTransport =
      responseBytes > (maxSize - kWebSettingsSafetyReserve) / kWebSettingsObservedPeakMultiplier
          ? maxSize
          : kWebSettingsSafetyReserve + responseBytes * kWebSettingsObservedPeakMultiplier;
  const size_t requiredFree = std::max(kWebSettingsMinFreeHeap, projectedTransport);
  const bool admitted = freeHeap >= requiredFree && largest >= kWebSettingsMinLargestBlock;
  LOG_INF("WEBMEM", "stage=settings_guard result=%s bytes=%u free=%u largest=%u required=%u reserve=%u ratio=%u largest_floor=%u",
          admitted ? "allow" : "reject", static_cast<unsigned>(responseBytes),
          static_cast<unsigned>(freeHeap), static_cast<unsigned>(largest), static_cast<unsigned>(requiredFree),
          static_cast<unsigned>(kWebSettingsSafetyReserve),
          static_cast<unsigned>(kWebSettingsObservedPeakMultiplier),
          static_cast<unsigned>(kWebSettingsMinLargestBlock));
  if (!admitted && server) {
    server->send(503, "text/plain", "Web memory temporarily unavailable");
  }
  return admitted;
}
}  // namespace

// File listing page template - now using generated headers:
// - HomePageHtml (from html/HomePage.html)
// - FilesPageHeaderHtml (from html/FilesPageHeader.html)
// - FilesPageFooterHtml (from html/FilesPageFooter.html)
CrossPointWebServer::CrossPointWebServer() {
  LOG_DBG("WEB", "[LIFECYCLE] CrossPointWebServer constructed this=%p free=%lu min=%lu largest=%lu", this,
          static_cast<unsigned long>(ESP.getFreeHeap()), static_cast<unsigned long>(ESP.getMinFreeHeap()),
          static_cast<unsigned long>(ESP.getMaxAllocHeap()));
}

CrossPointWebServer::~CrossPointWebServer() {
  LOG_DBG("WEB", "[LIFECYCLE] CrossPointWebServer destructing this=%p running=%d server=%p ws=%p free=%lu min=%lu largest=%lu",
          this, running, server.get(), wsServer.get(), static_cast<unsigned long>(ESP.getFreeHeap()),
          static_cast<unsigned long>(ESP.getMinFreeHeap()), static_cast<unsigned long>(ESP.getMaxAllocHeap()));
  stop();
  LOG_DBG("WEB", "[LIFECYCLE] CrossPointWebServer destroyed this=%p free=%lu min=%lu largest=%lu", this,
          static_cast<unsigned long>(ESP.getFreeHeap()), static_cast<unsigned long>(ESP.getMinFreeHeap()),
          static_cast<unsigned long>(ESP.getMaxAllocHeap()));
}

void CrossPointWebServer::begin() {
  stopCompleted = false;
  LOG_DBG("WEB", "[LIFECYCLE] begin enter this=%p running=%d server=%p ws=%p free=%lu min=%lu largest=%lu", this,
          running, server.get(), wsServer.get(), static_cast<unsigned long>(ESP.getFreeHeap()),
          static_cast<unsigned long>(ESP.getMinFreeHeap()), static_cast<unsigned long>(ESP.getMaxAllocHeap()));
  if (running) {
    LOG_DBG("WEB", "Web server already running");
    return;
  }

  // Check if we have a valid network connection (either STA connected or AP mode)
  const wifi_mode_t wifiMode = WiFi.getMode();
  const bool isStaConnected = (wifiMode & WIFI_MODE_STA) && (WiFi.status() == WL_CONNECTED);
  const bool isInApMode = (wifiMode & WIFI_MODE_AP) && (WiFi.softAPgetStationNum() >= 0);  // AP is running

  if (!isStaConnected && !isInApMode) {
    LOG_DBG("WEB", "Cannot start webserver - no valid network (mode=%d, status=%d, free=%lu, min=%lu, largest=%lu)",
            wifiMode, WiFi.status(), static_cast<unsigned long>(ESP.getFreeHeap()),
            static_cast<unsigned long>(ESP.getMinFreeHeap()), static_cast<unsigned long>(ESP.getMaxAllocHeap()));
    return;
  }

  // Store AP mode flag for later use (e.g., in handleStatus)
  apMode = isInApMode;

  lastHttpClientConnected = false;
  lastClientDiagnosticAt = 0;
  closeFileIfOpen(wsUploadFile);
  wsUploadFileName = String();
  wsUploadPath = String();
  wsUploadSize = 0;
  wsUploadReceived = 0;
  wsUploadStartTime = 0;
  wsUploadInProgress = false;
  wsUploadClientNum = 255;
  wsLastProgressSent = 0;
  wsLastCompleteName = String();
  wsLastCompleteSize = 0;
  wsLastCompleteAt = 0;
  clearWebThumbnailNegativeCache();
  LOG_DBG("WEB", "[MEM] stage=begin free=%lu min=%lu largest=%lu", static_cast<unsigned long>(ESP.getFreeHeap()),
          static_cast<unsigned long>(ESP.getMinFreeHeap()), static_cast<unsigned long>(ESP.getMaxAllocHeap()));
  LOG_DBG("WEB", "Network mode: %s", apMode ? "AP" : "STA");

  LOG_DBG("WEB", "Creating web server on port %d (this=%p)...", port, this);
  server.reset(new WebServer(port));

  // Disable WiFi sleep to improve responsiveness and prevent 'unreachable' errors.
  // This is critical for reliable web server operation on ESP32.
  WiFi.setSleep(false);
  // Default varies by ESP32 core version. The activity's loss-recovery loop
  // relies on driver retries during transient disconnects.
  WiFi.setAutoReconnect(true);

  // Note: WebServer class doesn't have setNoDelay() in the standard ESP32 library.
  // We rely on disabling WiFi sleep for responsiveness.

  LOG_DBG("WEB", "[LIFECYCLE] WebServer object created server=%p", server.get());
  LOG_DBG("WEB", "[MEM] stage=after-http-allocation free=%lu min=%lu largest=%lu",
          static_cast<unsigned long>(ESP.getFreeHeap()), static_cast<unsigned long>(ESP.getMinFreeHeap()),
          static_cast<unsigned long>(ESP.getMaxAllocHeap()));

  if (!server) {
    LOG_ERR("WEB", "Failed to create WebServer!");
    return;
  }

  // Add Access-Control-Allow-* headers to every response so web-based clients
  // and PWAs on other origins can use the HTTP API. Preflight OPTIONS requests
  // are answered in handleNotFound().
  server->enableCORS(true);

  // Setup routes
  LOG_DBG("WEB", "Setting up routes...");
  server->on("/", HTTP_GET, [this] { handleRoot(); });
  server->on("/files", HTTP_GET, [this] { handleFileList(); });
  server->on("/js/jszip.min.js", HTTP_GET, [this] { handleJszip(); });

  server->on("/api/status", HTTP_GET, [this] { handleStatus(); });
  server->on("/library", HTTP_GET, [this] { handleLibrary(); });
  server->on("/api/library", HTTP_GET, [this] { handleLibraryData(); });
  server->on("/api/library/cover", HTTP_GET, [this] { handleLibraryCover(); });
  server->on("/stats", HTTP_GET, [this] { handleStatsPage(); });
  server->on("/api/stats", HTTP_GET, [this] { handleStatsData(); });
  server->on("/api/stats/export", HTTP_GET, [this] { handleStatsExport(); });
  server->on("/api/book", HTTP_GET, [this] { handleGetBookInfo(); });
  server->on("/api/book", HTTP_POST, [this] { handlePostBookUpdate(); });
  WebClippingsHandler::registerRoutes(server.get());
  server->on("/api/files", HTTP_GET, [this] { handleFileListData(); });
  server->on("/download", HTTP_GET, [this] { handleDownload(); });

  // Upload endpoint with special handling for multipart form data
  server->on("/upload", HTTP_POST, [this] { handleUploadPost(upload); }, [this] { handleUpload(upload); });

  // Create folder endpoint
  server->on("/mkdir", HTTP_POST, [this] { handleCreateFolder(); });

  // Rename file endpoint
  server->on("/rename", HTTP_POST, [this] { handleRename(); });

  // Move file endpoint
  server->on("/move", HTTP_POST, [this] { handleMove(); });

  // Delete file/folder endpoint
  server->on("/delete", HTTP_POST, [this] { handleDelete(); });

  // Settings endpoints
  server->on("/settings", HTTP_GET, [this] { handleSettingsPage(); });
  server->on("/todo", HTTP_GET, [this] { handleToDoPage(); });
  server->on("/api/todo", HTTP_GET, [this] { handleGetToDo(); });
  server->on("/api/todo", HTTP_POST, [this] { handlePostToDo(); });
  server->on("/api/settings", HTTP_GET, [this] { handleGetSettings(); });
  server->on("/api/settings", HTTP_POST, [this] { handlePostSettings(); });

  // Font management endpoints
  server->on("/fonts", HTTP_GET, [this] { handleFontsPage(); });
  server->on("/api/fonts", HTTP_GET, [this] { handleFontList(); });
  server->on("/api/fonts/upload", HTTP_POST, [this] { handleFontUpload(); }, [this] { handleFontUploadData(); });
  server->on("/api/fonts/delete", HTTP_POST, [this] { handleFontDelete(); });

  // OPDS server endpoints
  server->on("/api/opds", HTTP_GET, [this] { handleGetOpdsServers(); });
  server->on("/api/opds", HTTP_POST, [this] { handlePostOpdsServer(); });
  server->on("/api/opds/delete", HTTP_POST, [this] { handleDeleteOpdsServer(); });

  // Wi-Fi credential endpoints
  server->on("/api/wifi", HTTP_GET, [this] { handleGetWifiNetworks(); });
  server->on("/api/wifi", HTTP_POST, [this] { handlePostWifiNetwork(); });
  server->on("/api/wifi/delete", HTTP_POST, [this] { handleDeleteWifiNetwork(); });
  server->on("/api/clock-weather", HTTP_GET, [this] { handleGetClockWeather(); });
  server->on("/api/clock-weather", HTTP_POST, [this] { handlePostClockWeather(); });

  server->onNotFound([this] { handleNotFound(); });
  LOG_DBG("WEB", "[MEM] stage=after-route-setup free=%lu min=%lu largest=%lu",
          static_cast<unsigned long>(ESP.getFreeHeap()), static_cast<unsigned long>(ESP.getMinFreeHeap()),
          static_cast<unsigned long>(ESP.getMaxAllocHeap()));

  // Collect WebDAV headers and register handler
  const char* davHeaders[] = {"Depth", "Destination", "Overwrite", "If", "Lock-Token", "Timeout"};
  server->collectHeaders(davHeaders, 6);
  server->addHandler(new WebDAVHandler());  // Note: WebDAVHandler will be deleted by WebServer when server is stopped
  LOG_DBG("WEB", "WebDAV handler initialized");

  LOG_DBG("WEB", "[LIFECYCLE] Calling WebServer::begin server=%p port=%d", server.get(), port);
  server->begin();
  LOG_DBG("WEB", "[LIFECYCLE] WebServer::begin returned server=%p free=%lu min=%lu largest=%lu", server.get(),
          static_cast<unsigned long>(ESP.getFreeHeap()), static_cast<unsigned long>(ESP.getMinFreeHeap()),
          static_cast<unsigned long>(ESP.getMaxAllocHeap()));

  // Start WebSocket server for fast binary uploads
  LOG_DBG("WEB", "Starting WebSocket server on port %d...", wsPort);
  wsServer.reset(new WebSocketsServer(wsPort));
  LOG_DBG("WEB", "[LIFECYCLE] WebSocketsServer object created ws=%p free=%lu min=%lu largest=%lu", wsServer.get(),
          static_cast<unsigned long>(ESP.getFreeHeap()), static_cast<unsigned long>(ESP.getMinFreeHeap()),
          static_cast<unsigned long>(ESP.getMaxAllocHeap()));
  wsInstance = const_cast<CrossPointWebServer*>(this);
  wsServer->begin();
  wsServer->onEvent(wsEventCallback);
  LOG_DBG("WEB", "WebSocket server started ws=%p free=%lu min=%lu largest=%lu", wsServer.get(),
          static_cast<unsigned long>(ESP.getFreeHeap()), static_cast<unsigned long>(ESP.getMinFreeHeap()),
          static_cast<unsigned long>(ESP.getMaxAllocHeap()));

  udpActive = udp.begin(LOCAL_UDP_PORT);
  LOG_DBG("WEB", "Discovery UDP %s on port %d free=%lu min=%lu largest=%lu", udpActive ? "enabled" : "failed",
          LOCAL_UDP_PORT, static_cast<unsigned long>(ESP.getFreeHeap()), static_cast<unsigned long>(ESP.getMinFreeHeap()),
          static_cast<unsigned long>(ESP.getMaxAllocHeap()));

  // All request handlers run on the task that calls handleClient(). Register
  // that task before any handler can call esp_task_wdt_reset().
  LOG_DBG("WEB", "[TASK] Registering current task name=%s handle=%p stackHighWater=%lu", pcTaskGetName(nullptr),
          xTaskGetCurrentTaskHandle(), static_cast<unsigned long>(uxTaskGetStackHighWaterMark(nullptr)));
  const esp_err_t watchdogResult = esp_task_wdt_add(nullptr);
  watchdogTaskRegistered = watchdogResult == ESP_OK;
  LOG_DBG("WEB", "[TASK] Watchdog registration result=%s registered=%d stackHighWater=%lu", esp_err_to_name(watchdogResult),
          watchdogTaskRegistered, static_cast<unsigned long>(uxTaskGetStackHighWaterMark(nullptr)));
  if (!watchdogTaskRegistered) {
    LOG_ERR("WEB", "Failed to register web server task with watchdog: %s", esp_err_to_name(watchdogResult));
  }

  running = true;

  LOG_DBG("WEB", "Web server started on port %d", port);
  // Show the correct IP based on network mode
  const String ipAddr = apMode ? WiFi.softAPIP().toString() : WiFi.localIP().toString();
  LOG_DBG("WEB", "Access at http://%s/", ipAddr.c_str());
  LOG_DBG("WEB", "WebSocket at ws://%s:%d/", ipAddr.c_str(), wsPort);
  LOG_DBG("WEB", "[MEM] stage=server-started free=%lu min=%lu largest=%lu",
          static_cast<unsigned long>(ESP.getFreeHeap()), static_cast<unsigned long>(ESP.getMinFreeHeap()),
          static_cast<unsigned long>(ESP.getMaxAllocHeap()));
}

void CrossPointWebServer::abortWsUpload(const char* tag) {
  LOG_DBG(tag, "[WS] Aborting upload client=%u received=%lu total=%lu free=%lu min=%lu largest=%lu", wsUploadClientNum,
          static_cast<unsigned long>(wsUploadReceived), static_cast<unsigned long>(wsUploadSize),
          static_cast<unsigned long>(ESP.getFreeHeap()), static_cast<unsigned long>(ESP.getMinFreeHeap()),
          static_cast<unsigned long>(ESP.getMaxAllocHeap()));
  // Explicit close() required: file-scope global persists beyond function scope
  closeFileIfOpen(wsUploadFile);
  if (!wsUploadFileName.isEmpty()) {
    String filePath = wsUploadPath;
    if (!filePath.endsWith("/")) filePath += "/";
    filePath += wsUploadFileName;
    if (Storage.remove(filePath.c_str())) {
      LOG_DBG(tag, "Deleted incomplete upload: %s", filePath.c_str());
    } else {
      LOG_DBG(tag, "Failed to delete incomplete upload: %s", filePath.c_str());
    }
  }
  wsUploadInProgress = false;
  wsUploadClientNum = 255;
  wsLastProgressSent = 0;
  wsUploadSize = 0;
  wsUploadReceived = 0;
  wsUploadStartTime = 0;
  wsUploadFileName = String();
  wsUploadPath = String();
  LOG_DBG(tag, "[WS] Upload abort cleanup complete free=%lu min=%lu largest=%lu",
          static_cast<unsigned long>(ESP.getFreeHeap()), static_cast<unsigned long>(ESP.getMinFreeHeap()),
          static_cast<unsigned long>(ESP.getMaxAllocHeap()));
}

void CrossPointWebServer::stop() {
  if (stopCompleted) return;
  stopCompleted = true;
  LOG_DBG("WEB", "[LIFECYCLE] stop enter this=%p running=%d server=%p ws=%p watchdog=%d free=%lu min=%lu largest=%lu",
          this, running, server.get(), wsServer.get(), watchdogTaskRegistered,
          static_cast<unsigned long>(ESP.getFreeHeap()), static_cast<unsigned long>(ESP.getMinFreeHeap()),
          static_cast<unsigned long>(ESP.getMaxAllocHeap()));
  LOG_DBG("WEB", "STOP INITIATED - setting running=false first");
  running = false;  // Set this FIRST to prevent handleClient from using server

  LOG_DBG("WEB", "[MEM] stage=stop-enter free=%lu min=%lu largest=%lu",
          static_cast<unsigned long>(ESP.getFreeHeap()), static_cast<unsigned long>(ESP.getMinFreeHeap()),
          static_cast<unsigned long>(ESP.getMaxAllocHeap()));

  // Close any in-progress WebSocket upload and remove partial file
  if (wsUploadInProgress || wsUploadFile) {
    abortWsUpload("WEB");
  }

  // Stop WebSocket server
  if (wsServer) {
    const size_t beforeFree = ESP.getFreeHeap();
    const size_t beforeLargest = ESP.getMaxAllocHeap();
    LOG_DBG("WEB", "Stopping WebSocket server...");
    // WebSocketsServer::close() synchronously emits disconnect callbacks for
    // connected clients. Clear the trampoline first so teardown callbacks
    // cannot re-enter this object after running has been invalidated.
    if (wsInstance == this) wsInstance = nullptr;
    wsServer->close();
    wsServer.reset();
    logWebOwnerDelta("websocket_server_clients", beforeFree, beforeLargest);
    LOG_DBG("WEB", "WebSocket server stopped ws=%p free=%lu min=%lu largest=%lu", wsServer.get(),
            static_cast<unsigned long>(ESP.getFreeHeap()), static_cast<unsigned long>(ESP.getMinFreeHeap()),
            static_cast<unsigned long>(ESP.getMaxAllocHeap()));
  }

  if (udpActive) {
    const size_t beforeFree = ESP.getFreeHeap();
    const size_t beforeLargest = ESP.getMaxAllocHeap();
    udp.stop();
    udpActive = false;
    logWebOwnerDelta("discovery_udp", beforeFree, beforeLargest);
    LOG_DBG("WEB", "Discovery UDP stopped free=%lu min=%lu largest=%lu", static_cast<unsigned long>(ESP.getFreeHeap()),
            static_cast<unsigned long>(ESP.getMinFreeHeap()), static_cast<unsigned long>(ESP.getMaxAllocHeap()));
  }

  if (server) {
    const size_t beforeFree = ESP.getFreeHeap();
    const size_t beforeLargest = ESP.getMaxAllocHeap();
    const int activeClientFd = server->client().fd();
    const bool activeClientConnected = server->client().connected();
    server->client().stop();
    LOG_INF("WEBMEM", "owner=http_active_client fd=%d connected=%u closed=%u free=%u->%u largest=%u->%u",
            activeClientFd, activeClientConnected ? 1U : 0U, activeClientFd >= 0 ? 1U : 0U,
            static_cast<unsigned>(beforeFree), static_cast<unsigned>(ESP.getFreeHeap()),
            static_cast<unsigned>(beforeLargest), static_cast<unsigned>(ESP.getMaxAllocHeap()));
    server->stop();
    LOG_DBG("WEB", "[LIFECYCLE] WebServer::stop returned server=%p free=%lu min=%lu largest=%lu", server.get(),
            static_cast<unsigned long>(ESP.getFreeHeap()), static_cast<unsigned long>(ESP.getMinFreeHeap()),
            static_cast<unsigned long>(ESP.getMaxAllocHeap()));
    server.reset();
    logWebOwnerDelta("http_server_request_handlers_webdav_client", beforeFree, beforeLargest);
  }
  LOG_DBG("WEB", "Web server stopped and deleted server=%p free=%lu min=%lu largest=%lu", server.get(),
          static_cast<unsigned long>(ESP.getFreeHeap()), static_cast<unsigned long>(ESP.getMinFreeHeap()),
          static_cast<unsigned long>(ESP.getMaxAllocHeap()));

  if (watchdogTaskRegistered) {
    LOG_DBG("WEB", "[TASK] Removing watchdog registration task=%s handle=%p stackHighWater=%lu", pcTaskGetName(nullptr),
            xTaskGetCurrentTaskHandle(), static_cast<unsigned long>(uxTaskGetStackHighWaterMark(nullptr)));
    const esp_err_t watchdogResult = esp_task_wdt_delete(nullptr);
    watchdogTaskRegistered = false;
    LOG_DBG("WEB", "[TASK] Watchdog removal result=%s", esp_err_to_name(watchdogResult));
  }
  lastHttpClientConnected = false;
  lastClientDiagnosticAt = 0;
  const size_t beforeUploadFree = ESP.getFreeHeap();
  const size_t beforeUploadLargest = ESP.getMaxAllocHeap();
  closeFileIfOpen(upload.file);
  upload.releaseBuffer();
  upload.fileName = String();
  upload.path = String();
  upload.size = 0;
  upload.success = false;
  upload.error = String();
  closeFileIfOpen(fontUpload.file);
  fontUpload.releaseBuffer();
  std::string().swap(fontUpload.familyName);
  std::string().swap(fontUpload.filePath);
  fontUpload.valid = false;
  fontUpload.magicChecked = false;
  fontUpload.bytesWritten = 0;
  wsLastCompleteName = String();
  wsLastCompleteSize = 0;
  wsLastCompleteAt = 0;
  closeFileIfOpen(wsUploadFile);
  wsUploadFileName = String();
  wsUploadPath = String();
  wsUploadSize = 0;
  wsUploadReceived = 0;
  wsUploadStartTime = 0;
  wsUploadInProgress = false;
  wsUploadClientNum = 255;
  wsLastProgressSent = 0;
  logWebOwnerDelta("upload_buffers_and_global_websocket_state", beforeUploadFree, beforeUploadLargest);
  // Signal once after all server-owned allocations are gone. The lifecycle
  // gate will defer admission while Wi-Fi is still active or the heap is below
  // its unchanged BLE floors.
  bleinput::requestLifecycleReevaluation();
  LOG_DBG("WEB", "[MEM] stage=stop-complete free=%lu min=%lu largest=%lu",
          static_cast<unsigned long>(ESP.getFreeHeap()), static_cast<unsigned long>(ESP.getMinFreeHeap()),
          static_cast<unsigned long>(ESP.getMaxAllocHeap()));
}

void CrossPointWebServer::handleClient() {
  static unsigned long lastDebugPrint = 0;

  // Check running flag FIRST before accessing server
  if (!running) {
    return;
  }

  // Double-check server pointer is valid
  if (!server) {
    LOG_DBG("WEB", "WARNING: handleClient called with null server!");
    return;
  }

  const unsigned long now = millis();
  if (now - lastDebugPrint > 10000) {
    LOG_DBG("WEB", "[HTTP] handleClient active port=%d connected=%d task=%s stackHighWater=%lu free=%lu min=%lu largest=%lu",
            port, lastHttpClientConnected, pcTaskGetName(nullptr),
            static_cast<unsigned long>(uxTaskGetStackHighWaterMark(nullptr)), static_cast<unsigned long>(ESP.getFreeHeap()),
            static_cast<unsigned long>(ESP.getMinFreeHeap()), static_cast<unsigned long>(ESP.getMaxAllocHeap()));
    lastDebugPrint = now;
  }

  const unsigned long handleStart = now;
  const bool clientConnectedBefore = server->client().connected();
  if (clientConnectedBefore && !lastHttpClientConnected) {
    LOG_INF("WEBRT", "stage=dispatch_begin uri=%s method=%d task=%s stack=%lu free=%lu largest=%lu min=%lu",
            server->uri().c_str(), static_cast<int>(server->method()), pcTaskGetName(nullptr),
            static_cast<unsigned long>(uxTaskGetStackHighWaterMark(nullptr)),
            static_cast<unsigned long>(ESP.getFreeHeap()), static_cast<unsigned long>(ESP.getMaxAllocHeap()),
            static_cast<unsigned long>(ESP.getMinFreeHeap()));
  }
  server->handleClient();
  const unsigned long handleElapsed = millis() - handleStart;
  const bool clientConnectedAfter = server->client().connected();
  const unsigned long after = millis();
  if (clientConnectedAfter != lastHttpClientConnected ||
      (handleElapsed >= 10 && after - lastClientDiagnosticAt > 1000)) {
    LOG_DBG("WEB", "[HTTP] handleClient exit elapsed=%lu connected=%d uri=%s method=%d free=%lu min=%lu largest=%lu",
            handleElapsed, clientConnectedAfter, server->uri().c_str(),
            static_cast<int>(server->method()), static_cast<unsigned long>(ESP.getFreeHeap()),
            static_cast<unsigned long>(ESP.getMinFreeHeap()), static_cast<unsigned long>(ESP.getMaxAllocHeap()));
    lastClientDiagnosticAt = after;
  }
  lastHttpClientConnected = clientConnectedAfter;

  // Handle WebSocket events
  if (wsServer) {
    wsServer->loop();
  }

  // Respond to discovery broadcasts
  if (udpActive) {
    int packetSize = udp.parsePacket();
    if (packetSize > 0) {
      char buffer[16];
      int len = udp.read(buffer, sizeof(buffer) - 1);
      if (len > 0) {
        buffer[len] = '\0';
        if (strcmp(buffer, "hello") == 0) {
          String hostname = WiFi.getHostname();
          if (hostname.isEmpty()) {
            hostname = "crosspoint";
          }
          String message = "crosspoint (on " + hostname + ");" + String(wsPort);
          udp.beginPacket(udp.remoteIP(), udp.remotePort());
          udp.write(reinterpret_cast<const uint8_t*>(message.c_str()), message.length());
          udp.endPacket();
        }
      }
    }
  }
}

CrossPointWebServer::WsUploadStatus CrossPointWebServer::getWsUploadStatus() const {
  WsUploadStatus status;
  status.inProgress = wsUploadInProgress;
  status.received = wsUploadReceived;
  status.total = wsUploadSize;
  status.filename = wsUploadFileName.c_str();
  status.lastCompleteName = wsLastCompleteName.c_str();
  status.lastCompleteSize = wsLastCompleteSize;
  status.lastCompleteAt = wsLastCompleteAt;
  return status;
}

static void sendHtmlContent(WebServer* server, const char* data, size_t len) {
  server->sendHeader("Content-Encoding", "gzip");
  server->send_P(200, "text/html", data, len);
}

void CrossPointWebServer::handleRoot() const {
  LOG_DBG("WEB", "[HTTP] request start uri=%s method=%d client=%d", server->uri().c_str(),
          static_cast<int>(server->method()), server->client().connected());
  sendHtmlContent(server.get(), HomePageHtml, sizeof(HomePageHtml));
  LOG_DBG("WEB", "Served root page");
}

void CrossPointWebServer::handleLibrary() const {
  sendHtmlContent(server.get(), LibraryPageHtml, sizeof(LibraryPageHtml));
}

void CrossPointWebServer::handleLibraryData() const {
  constexpr int WEB_LIBRARY_PAGE_SIZE = 10;
  WebRouteTrace routeTrace(server.get(), "/api/library", "streamed_bounded");
  const unsigned long requestStart = millis();
  size_t thumbnailChecks = 0;
  size_t thumbnailHits = 0;
  LOG_DBG("WEB", "[HTTP] library request start uri=%s method=%d client=%d free=%lu min=%lu largest=%lu",
          server->uri().c_str(), static_cast<int>(server->method()), server->client().connected(),
          static_cast<unsigned long>(ESP.getFreeHeap()), static_cast<unsigned long>(ESP.getMinFreeHeap()),
          static_cast<unsigned long>(ESP.getMaxAllocHeap()));
  const bool paginatedRequest = server->hasArg("offset") || server->hasArg("limit");
  int offset = 0;
  if (paginatedRequest && server->hasArg("offset")) {
    offset = std::max(0, static_cast<int>(server->arg("offset").toInt()));
  }
  int limit = WEB_LIBRARY_PAGE_SIZE;
  if (paginatedRequest && server->hasArg("limit")) {
    limit = std::clamp(static_cast<int>(server->arg("limit").toInt()), 1, WEB_LIBRARY_PAGE_SIZE);
  }

  server->setContentLength(CONTENT_LENGTH_UNKNOWN);
  server->send(200, "application/json", "");
  server->sendContent("{\"books\":[");
  bool first = true;
  size_t visibleIndex = 0;
  size_t emitted = 0;
  bool hasMore = false;
  JsonDocument doc;
  String row;
  for (const auto& book : RECENT_BOOKS.getBooks()) {
    if (RecentBooksStore::isMissing(book)) continue;
    if (visibleIndex < static_cast<size_t>(offset)) {
      ++visibleIndex;
      continue;
    }
    if (paginatedRequest && emitted >= static_cast<size_t>(limit)) {
      hasMore = true;
      break;
    }
    doc.clear();
    doc["index"] = static_cast<uint32_t>(visibleIndex);
    doc["path"] = book.path;
    doc["title"] = book.title;
    doc["author"] = book.author;
    doc["progress"] = book.progressPercent;
    doc["seconds"] = book.readingSeconds;
    doc["sessions"] = book.readingSessions;
    // Only advertise an already-valid thumbnail. The web Bookshelf must not
    // trigger cover extraction while a Transfer upload is using the SD/SPI
    // bus; missing covers fall back to the title card in the browser.
    bool hasCover = false;
    if (!book.coverBmpPath.empty()) {
      const int coverHeight = UITheme::getInstance().getMetrics().homeCoverHeight;
      const std::string thumbnailPath = UITheme::getCoverThumbPath(book.coverBmpPath, coverHeight);
      ++thumbnailChecks;
      hasCover = checkWebThumbnail(thumbnailPath, "/api/library");
      if (hasCover) ++thumbnailHits;
    }
    doc["hasCover"] = hasCover;
    row.clear();
    serializeJson(doc, row);
    if (!first) server->sendContent(",");
    first = false;
    server->sendContent(row);
    ++visibleIndex;
    ++emitted;
  }
  char footer[128];
  if (paginatedRequest) {
    snprintf(footer, sizeof(footer), "],\"offset\":%d,\"limit\":%d,\"hasMore\":%s}", offset, limit,
             hasMore ? "true" : "false");
  } else {
    // Preserve the pre-pagination contract for legacy callers that do not
    // provide query parameters: return the complete library response.
    snprintf(footer, sizeof(footer), "]}");
  }
  server->sendContent(footer);
  server->sendContent("");
  LOG_DBG("WEB", "[THUMB] library response emitted=%lu checks=%lu hits=%lu elapsed=%lu free=%lu min=%lu largest=%lu",
          static_cast<unsigned long>(emitted), static_cast<unsigned long>(thumbnailChecks),
          static_cast<unsigned long>(thumbnailHits), millis() - requestStart,
          static_cast<unsigned long>(ESP.getFreeHeap()), static_cast<unsigned long>(ESP.getMinFreeHeap()),
          static_cast<unsigned long>(ESP.getMaxAllocHeap()));
}

void CrossPointWebServer::handleLibraryCover() const {
  WebRouteTrace routeTrace(server.get(), "/api/library/cover", "streamed_1kb");
  const unsigned long requestStart = millis();
  LOG_DBG("WEB", "[THUMB] request start uri=%s method=%d client=%d free=%lu min=%lu largest=%lu", server->uri().c_str(),
          static_cast<int>(server->method()), server->client().connected(), static_cast<unsigned long>(ESP.getFreeHeap()),
          static_cast<unsigned long>(ESP.getMinFreeHeap()), static_cast<unsigned long>(ESP.getMaxAllocHeap()));
  const auto sendMissingThumbnail = [this](const char* message) {
    // Cache negative thumbnail results briefly so a missing cached cover does
    // not become a browser retry loop while preserving cover retrieval.
    server->sendHeader("Cache-Control", "public, max-age=300");
    server->send(404, "text/plain", message);
  };
  if (!server->hasArg("index")) {
    LOG_DBG("WEB", "[THUMB] request rejected: missing index");
    server->send(400, "text/plain", "Missing index");
    return;
  }
  const int wanted = server->arg("index").toInt();
  int index = 0;
  const RecentBook* selected = nullptr;
  for (const auto& book : RECENT_BOOKS.getBooks()) {
    if (RecentBooksStore::isMissing(book)) continue;
    if (index++ == wanted) {
      selected = &book;
      break;
    }
  }
  if (!selected || selected->coverBmpPath.empty()) {
    sendMissingThumbnail("Cover not found");
    return;
  }
  const int coverHeight = UITheme::getInstance().getMetrics().homeCoverHeight;
  const std::string coverPath = UITheme::getCoverThumbPath(selected->coverBmpPath, coverHeight);
  LOG_DBG("WEB", "[THUMB] resolved index=%d cachePath=%s", wanted, coverPath.c_str());
  // The Web Bookshelf is presentation-only. Never parse an EPUB or decode an
  // image from this request: opening the page should not compete with a file
  // transfer for the SD/SPI bus or Wi-Fi task. Device-side Library retrieval
  // remains available when the user highlights a book or uses Retrieve All.
  const bool validThumbnail = checkWebThumbnail(coverPath, "/api/library/cover");
  LOG_DBG("WEB", "[THUMB] cache validation path=%s valid=%d free=%lu min=%lu largest=%lu", coverPath.c_str(),
          validThumbnail, static_cast<unsigned long>(ESP.getFreeHeap()), static_cast<unsigned long>(ESP.getMinFreeHeap()),
          static_cast<unsigned long>(ESP.getMaxAllocHeap()));
  if (!validThumbnail) {
    sendMissingThumbnail("Cover not cached");
    return;
  }
  HalFile file = Storage.open(coverPath.c_str());
  if (!file || file.isDirectory()) {
    LOG_DBG("WEB", "[THUMB] cache open failed path=%s", coverPath.c_str());
    sendMissingThumbnail("Cover not found");
    return;
  }
  const size_t contentLength = file.size();
  LOG_DBG("WEB", "[THUMB] serving path=%s bytes=%lu client=%d free=%lu min=%lu largest=%lu", coverPath.c_str(),
          static_cast<unsigned long>(contentLength), server->client().connected(),
          static_cast<unsigned long>(ESP.getFreeHeap()), static_cast<unsigned long>(ESP.getMinFreeHeap()),
          static_cast<unsigned long>(ESP.getMaxAllocHeap()));
  server->setContentLength(contentLength);
  server->sendHeader("Cache-Control", "public, max-age=3600");
  server->send(200, "image/bmp", "");
  NetworkClient client = server->client();
  uint8_t buffer[1024];
  size_t bytesSent = 0;
  bool clientWriteFailed = false;
  while (file.available()) {
    const int count = file.read(buffer, sizeof(buffer));
    if (count <= 0) break;
    const size_t expected = static_cast<size_t>(count);
    if (client.write(buffer, expected) != expected) {
      clientWriteFailed = true;
      break;
    }
    bytesSent += expected;
    resetTaskWatchdogIfSubscribed();
    yield();
  }
  client.clear();
  file.close();
  LOG_DBG("WEB", "[THUMB] serving complete path=%s sent=%lu expected=%lu writeFailed=%d elapsed=%lu client=%d free=%lu min=%lu largest=%lu",
          coverPath.c_str(), static_cast<unsigned long>(bytesSent), static_cast<unsigned long>(contentLength),
          clientWriteFailed, millis() - requestStart, client.connected(), static_cast<unsigned long>(ESP.getFreeHeap()),
          static_cast<unsigned long>(ESP.getMinFreeHeap()), static_cast<unsigned long>(ESP.getMaxAllocHeap()));
}

void CrossPointWebServer::handleStatsPage() const {
  sendHtmlContent(server.get(), StatsPageHtml, sizeof(StatsPageHtml));
}

void CrossPointWebServer::handleStatsExport() const {
  server->sendHeader("Content-Disposition", "attachment; filename=folio-nooir-reading-stats.json");
  handleStatsData();
}

void CrossPointWebServer::handleStatsData() const {
  WebRouteTrace routeTrace(server.get(), "/api/stats", "streamed_bounded");
  LOG_DBG("WEB", "[STATS] request start free=%lu min=%lu largest=%lu", static_cast<unsigned long>(ESP.getFreeHeap()),
          static_cast<unsigned long>(ESP.getMinFreeHeap()), static_cast<unsigned long>(ESP.getMaxAllocHeap()));
  const uint32_t today = halClock.getDateKey();
  // Older installations already have cumulative per-book counters but do
  // not have the newer day-bucket file yet. Keep those users' totals visible
  // until the aggregate store catches up through the next completed session.
  uint32_t bookSeconds = 0;
  uint32_t bookSessions = 0;
  uint32_t bookPages = 0;
  for (const auto& book : BOOK_STATES.getBooks()) {
    bookSeconds += std::min(book.readingSeconds, UINT32_MAX - bookSeconds);
    bookSessions += std::min<uint32_t>(book.readingSessions, UINT32_MAX - bookSessions);
    bookPages += std::min(book.pagesTurned, UINT32_MAX - bookPages);
  }
  const uint32_t totalSeconds = std::max(bookSeconds, READING_STATS.totalSeconds());
  const uint32_t totalSessions = std::max(bookSessions, READING_STATS.totalSessions());
  const uint32_t totalPages = std::max(bookPages, READING_STATS.totalPagesTurned());

  // Stream the response in bounded pieces. The previous implementation kept
  // the complete document in ArduinoJson and then serialized a second complete
  // copy into String before WebServer copied it again for transmission. With
  // unlimited Recent Books that transient triple-copy is what drives the X4
  // heap into the 1-2KB largest-block failure seen during Web UI sessions.
  server->setContentLength(CONTENT_LENGTH_UNKNOWN);
  server->send(200, "application/json", "");
  char summary[512];
  snprintf(summary, sizeof(summary),
           "{\"totalSeconds\":%lu,\"sessions\":%lu,\"pagesTurned\":%lu,\"pagesPerMinute\":%lu,"
           "\"streak\":%u,\"bestStreak\":%u,\"today\":%lu,\"todaySeconds\":%lu,\"todayPages\":%lu,\"days\":[",
           static_cast<unsigned long>(totalSeconds), static_cast<unsigned long>(totalSessions),
           static_cast<unsigned long>(totalPages),
           static_cast<unsigned long>(totalSeconds == 0 ? 0 : totalPages / std::max<uint32_t>(1, (totalSeconds + 30) / 60)),
           static_cast<unsigned>(READING_STATS.currentStreakDays()), static_cast<unsigned>(READING_STATS.longestStreakDays()),
           static_cast<unsigned long>(today), static_cast<unsigned long>(today == 0 ? 0 : READING_STATS.secondsForDate(today)),
           static_cast<unsigned long>(today == 0 ? 0 : READING_STATS.pagesForDate(today)));
  server->sendContent(summary);

  const auto& dayRows = READING_STATS.getDays();
  const size_t firstDay = dayRows.size() > 90 ? dayRows.size() - 90 : 0;
  char dayRow[160];
  for (size_t i = firstDay; i < dayRows.size(); ++i) {
    if (i != firstDay) server->sendContent(",");
    snprintf(dayRow, sizeof(dayRow), "{\"date\":%lu,\"seconds\":%lu,\"sessions\":%u,\"pages\":%lu}",
             static_cast<unsigned long>(dayRows[i].dateKey), static_cast<unsigned long>(dayRows[i].seconds),
             static_cast<unsigned>(dayRows[i].sessions), static_cast<unsigned long>(dayRows[i].pagesTurned));
    server->sendContent(dayRow);
  }
  server->sendContent("],\"books\":[");

  JsonDocument rowDoc;
  String rowJson;
  bool seenBook = false;
  auto appendBook = [&](const std::string& path, const BookState* state, const RecentBook* recent, const int coverIndex) {
    const uint8_t progress = std::max<uint8_t>(state ? state->progressPercent : 0,
                                                recent ? recent->progressPercent : 0);
    const uint32_t seconds = std::max<uint32_t>(state ? state->readingSeconds : 0,
                                                recent ? recent->readingSeconds : 0);
    const uint16_t sessions = std::max<uint16_t>(state ? state->readingSessions : 0,
                                                 recent ? recent->readingSessions : 0);
    const uint32_t pages = std::max<uint32_t>(state ? state->pagesTurned : 0,
                                               recent ? recent->pagesTurned : 0);
    uint8_t status = state ? static_cast<uint8_t>(state->status) : static_cast<uint8_t>(BookStatus::New);
    if (progress >= 100) status = static_cast<uint8_t>(BookStatus::Finished);
    else if (status == static_cast<uint8_t>(BookStatus::New) && progress > 0)
      status = static_cast<uint8_t>(BookStatus::Reading);
    rowDoc.clear();
    JsonObject row = rowDoc.to<JsonObject>();
    row["path"] = path;
    row["title"] = recent && !recent->title.empty() ? recent->title : path;
    row["author"] = recent ? recent->author : "";
    row["synopsis"] = recent ? recent->synopsis : "";
    row["progress"] = progress;
    row["status"] = status;
    row["seconds"] = seconds;
    row["sessions"] = sessions;
    row["pagesTurned"] = pages;
    row["pagesPerMinute"] = seconds == 0 ? 0 : pages / std::max<uint32_t>(1, (seconds + 30) / 60);
    row["startDate"] = state ? state->startDate : 0;
    row["finishDate"] = state ? state->finishDate : 0;
    row["lastOpenedDate"] = state ? state->lastOpenedDate : 0;
    row["coverIndex"] = coverIndex;
    rowJson.clear();
    serializeJson(rowDoc, rowJson);
    if (seenBook) server->sendContent(",");
    server->sendContent(rowJson);
    seenBook = true;
  };
  // Walk Recent Books once so each entry performs at most one SD existence
  // check and receives its cover index. Recent order is also the useful
  // presentation order for the web page.
  int visibleIndex = 0;
  for (const auto& recent : RECENT_BOOKS.getBooks()) {
    const bool missing = RecentBooksStore::isMissing(recent);
    const int coverIndex = missing ? -1 : visibleIndex++;
    appendBook(recent.path, BOOK_STATES.find(recent.path), &recent, coverIndex);
  }
  // A book can exist in Book States without a Recent Books entry. Include it
  // too, preserving the full statistics feature without another SD scan.
  for (const auto& book : BOOK_STATES.getBooks()) {
    bool inRecent = false;
    for (const auto& recent : RECENT_BOOKS.getBooks()) {
      if (recent.path == book.path) {
        inRecent = true;
        break;
      }
    }
    if (!inRecent) appendBook(book.path, &book, nullptr, -1);
  }

  server->sendContent("]}");
  server->sendContent("");
  LOG_DBG("WEB", "[STATS] streamed response free=%lu min=%lu largest=%lu",
          static_cast<unsigned long>(ESP.getFreeHeap()), static_cast<unsigned long>(ESP.getMinFreeHeap()),
          static_cast<unsigned long>(ESP.getMaxAllocHeap()));
}

void CrossPointWebServer::handleGetBookInfo() const {
  if (!server->hasArg("path")) {
    server->send(400, "application/json", "{\"error\":\"Missing path\"}");
    return;
  }
  const String itemPath = normalizeWebPath(server->arg("path"));
  if (itemPath == "/" || !Storage.exists(itemPath.c_str()) || !isBookFile(itemPath)) {
    server->send(404, "application/json", "{\"error\":\"Book not found\"}");
    return;
  }

  const std::string path = itemPath.c_str();
  const std::string_view pathView(itemPath.c_str(), itemPath.length());
  const BookMetadataOverride* overrideData = BOOK_METADATA_OVERRIDES.find(path);
  const RecentBook* recent = nullptr;
  for (const auto& candidate : RECENT_BOOKS.getBooks()) {
    if (candidate.path == path) {
      recent = &candidate;
      break;
    }
  }

  std::string title;
  std::string author;
  std::string synopsis;
  if (recent) {
    title = recent->title;
    author = recent->author;
    synopsis = recent->synopsis;
  } else if (FsHelpers::hasEpubExtension(itemPath)) {
    Epub epub(path, "/.crosspoint");
    bool loaded = epub.loadCachedMetadataOnly();
    if (!loaded) loaded = epub.loadMetadataOnly();
    if (loaded) {
      title = epub.getTitle();
      author = epub.getAuthor();
      synopsis = epub.getDescription();
    }
  } else if (FsHelpers::hasXtcExtension(pathView)) {
    Xtc xtc(path, "/.crosspoint");
    if (xtc.load()) {
      title = xtc.getTitle();
      author = xtc.getAuthor();
    }
  } else if (FsHelpers::hasCbzExtension(pathView)) {
    Cbz cbz(path, "/.crosspoint");
    bool loaded = cbz.loadCachedMetadataOnly();
    if (!loaded) loaded = cbz.loadMetadataOnly();
    if (loaded) {
      title = cbz.getTitle();
      author = cbz.getAuthor();
      synopsis = cbz.getSynopsis();
    }
  }
  if (title.empty()) {
    const int slash = itemPath.lastIndexOf('/');
    title = itemPath.substring(slash + 1).c_str();
  }
  if (overrideData) {
    title = overrideData->title;
    author = overrideData->author;
    synopsis = overrideData->synopsis;
  }

  JsonDocument response;
  const BookState* state = BOOK_STATES.find(path);
  uint8_t progress = state ? state->progressPercent : (recent ? recent->progressPercent : 0);
  uint8_t status = state ? static_cast<uint8_t>(state->status) : static_cast<uint8_t>(BookStatus::New);
  if (progress >= 100) status = static_cast<uint8_t>(BookStatus::Finished);
  else if (status == static_cast<uint8_t>(BookStatus::New) && progress > 0)
    status = static_cast<uint8_t>(BookStatus::Reading);
  response["path"] = path;
  response["title"] = title;
  response["author"] = author;
  response["synopsis"] = synopsis;
  response["edited"] = overrideData != nullptr;
  response["status"] = status;
  response["progress"] = progress;
  response["startDate"] = state ? state->startDate : 0;
  response["finishDate"] = state ? state->finishDate : 0;
  String body;
  serializeJson(response, body);
  server->send(200, "application/json", body);
}

void CrossPointWebServer::handlePostBookUpdate() {
  if (!server->hasArg("plain")) {
    server->send(400, "application/json", "{\"error\":\"Missing JSON body\"}");
    return;
  }
  JsonDocument doc;
  const DeserializationError err = deserializeJson(doc, server->arg("plain"));
  if (err) {
    server->send(400, "application/json", String("{\"error\":\"Invalid JSON: ") + err.c_str() + "\"}");
    return;
  }

  const std::string path = doc["path"] | "";
  bool knownBook = BOOK_STATES.find(path) != nullptr;
  if (!knownBook) {
    for (const auto& candidate : RECENT_BOOKS.getBooks()) {
      if (candidate.path == path) {
        knownBook = true;
        break;
      }
    }
  }
  const bool metadataRequest = doc["title"].is<const char*>() || doc["author"].is<const char*>() ||
                               doc["synopsis"].is<const char*>();
  const bool stateRequest = !doc["progress"].isNull() || !doc["status"].isNull() || !doc["startDate"].isNull() ||
                            !doc["finishDate"].isNull();
  if (path.empty() || !Storage.exists(path.c_str()) || !isBookFile(path.c_str()) ||
      (!knownBook && !metadataRequest)) {
    server->send(404, "application/json", "{\"error\":\"Book not found\"}");
    return;
  }

  const std::string action = doc["action"] | "update";
  if (action == "reset") {
    if (!knownBook) {
      server->send(404, "application/json", "{\"error\":\"Book has no reading state\"}");
      return;
    }
    BOOK_STATES.reset(path);
    RECENT_BOOKS.resetReading(path);
    server->send(200, "application/json", "{\"ok\":true,\"action\":\"reset\"}");
    return;
  }

  const BookState* existing = BOOK_STATES.find(path);
  const RecentBook* recent = nullptr;
  for (const auto& candidate : RECENT_BOOKS.getBooks()) {
    if (candidate.path == path) {
      recent = &candidate;
      break;
    }
  }

  // Metadata edits are stored separately from the EPUB/XTCH container. This
  // keeps uploads fast and makes edits available to the device Library even
  // before the book has been opened or added to Recent Books.
  bool metadataUpdated = false;
  if (metadataRequest) {
    std::string title = recent ? recent->title : "";
    std::string author = recent ? recent->author : "";
    std::string synopsis = recent ? recent->synopsis : "";
    if (doc["title"].is<const char*>()) title = doc["title"].as<const char*>();
    if (doc["author"].is<const char*>()) author = doc["author"].as<const char*>();
    if (doc["synopsis"].is<const char*>()) synopsis = doc["synopsis"].as<const char*>();
    metadataUpdated = BOOK_METADATA_OVERRIDES.update(path, title, author, synopsis);
    if (recent) metadataUpdated = RECENT_BOOKS.updateMetadata(path, title, author, synopsis) && metadataUpdated;
  }

  // A transfer-page metadata edit does not need to create a reading-state
  // row. Return immediately for a newly uploaded/unopened book.
  if (metadataRequest && !stateRequest) {
    server->send(200, "application/json", metadataUpdated ? "{\"ok\":true,\"metadata\":true}"
                                                           : "{\"ok\":false,\"metadata\":false}");
    return;
  }

  uint8_t progress = existing ? existing->progressPercent : (recent ? recent->progressPercent : 0);
  uint32_t startDate = existing ? existing->startDate : 0;
  uint32_t finishDate = existing ? existing->finishDate : 0;
  BookStatus status = existing ? existing->status : BookStatus::New;
  if (!doc["progress"].isNull())
    progress = static_cast<uint8_t>(std::max(0, std::min(100, doc["progress"].as<int>())));
  if (!doc["startDate"].isNull()) startDate = doc["startDate"].as<uint32_t>();
  if (!doc["finishDate"].isNull()) finishDate = doc["finishDate"].as<uint32_t>();
  if (!doc["status"].isNull() && doc["status"].is<int>()) {
    status = static_cast<BookStatus>(std::min(3, std::max(0, doc["status"].as<int>())));
  } else if (!doc["status"].isNull() && doc["status"].is<const char*>()) {
    const String value = doc["status"].as<const char*>();
    if (value.equalsIgnoreCase("reading")) status = BookStatus::Reading;
    else if (value.equalsIgnoreCase("on hold")) status = BookStatus::OnHold;
    else if (value.equalsIgnoreCase("finished")) status = BookStatus::Finished;
    else status = BookStatus::New;
  }
  if (!BOOK_STATES.updateEditable(path, status, progress, startDate, finishDate)) {
    server->send(500, "application/json", "{\"error\":\"Could not save book state\"}");
    return;
  }
  if (recent) RECENT_BOOKS.recordReading(path, progress, 0);

  server->send(200, "application/json", metadataUpdated ? "{\"ok\":true,\"metadata\":true}"
                                                          : "{\"ok\":true,\"metadata\":false}");
}

void CrossPointWebServer::handleJszip() const {
  server->sendHeader("Content-Encoding", "gzip");
  server->send_P(200, "application/javascript", jszip_minJs, jszip_minJsCompressedSize);
  LOG_DBG("WEB", "Served jszip.min.js");
}

void CrossPointWebServer::handleNotFound() const {
  LOG_DBG("WEB", "[HTTP] not-found method=%d uri=%s client=%d free=%lu min=%lu largest=%lu",
          static_cast<int>(server->method()), server->uri().c_str(), server->client().connected(),
          static_cast<unsigned long>(ESP.getFreeHeap()), static_cast<unsigned long>(ESP.getMinFreeHeap()),
          static_cast<unsigned long>(ESP.getMaxAllocHeap()));
  // CORS preflight: routes are registered per-method, so OPTIONS requests land
  // here. The Access-Control-Allow-* headers are added by enableCORS().
  if (server->method() == HTTP_OPTIONS) {
    server->send(204, "text/plain", "");
    return;
  }

  // in AP mode, redirect unmatched browser/captive-portal requests to "/" so the OS auto-opens the browser
  // API requests (/api/*) still return 404 so XHR errors surface correctly
  // see https://en.wikipedia.org/wiki/Captive_portal#Detection
  if (apMode && !server->uri().startsWith("/api/")) {
    server->sendHeader("Location", "/", true);
    server->send(302, "text/plain", "");
    return;
  }

  String message = "404 Not Found\n\n";
  message += "URI: " + server->uri() + "\n";
  server->send(404, "text/plain", message);
}

void CrossPointWebServer::handleStatus() const {
  // Get correct IP based on AP vs STA mode
  const String ipAddr = apMode ? WiFi.softAPIP().toString() : WiFi.localIP().toString();

  JsonDocument doc;
  doc["version"] = CROSSPOINT_VERSION;
  doc["ip"] = ipAddr;
  doc["mode"] = apMode ? "AP" : "STA";
  doc["rssi"] = apMode ? 0 : WiFi.RSSI();
  doc["freeHeap"] = ESP.getFreeHeap();
  doc["uptime"] = millis() / 1000;
  doc["device"] = gpio.deviceIsX3() ? "X3" : "X4";

  char snBuf[33] = {0};
  bool valid = false;
#if !CONFIG_IDF_TARGET_ESP32
  // Classic ESP32's efuse table has no USER_DATA block (C3/S3 only)
  if (esp_efuse_read_field_blob(ESP_EFUSE_USER_DATA, snBuf, 256) == ESP_OK) {
    valid = snBuf[0] != '\0' && snBuf[0] != (char)0xFF;
    for (int i = 0; i < 32 && snBuf[i] != '\0'; i++) {
      if (!std::isprint(static_cast<unsigned char>(snBuf[i]))) {
        valid = false;
        break;
      }
    }
  }
#endif

  if (valid) {
    doc["serial"] = snBuf;
  } else {
    doc["serial"] = "Not found";
  }

  String response;
  serializeJson(doc, response);
  server->send(200, "application/json", response);
}

void CrossPointWebServer::scanFiles(const char* path, const std::function<void(FileInfo)>& callback) const {
  HalFile root = Storage.open(path);
  if (!root) {
    LOG_DBG("WEB", "Failed to open directory: %s", path);
    return;
  }

  if (!root.isDirectory()) {
    LOG_DBG("WEB", "Not a directory: %s", path);
    root.close();
    return;
  }

  LOG_DBG("WEB", "Scanning files in: %s", path);

  HalFile file = root.openNextFile();
  char name[500];
  while (file) {
    file.getName(name, sizeof(name));
    auto fileName = String(name);

    // Skip hidden items (starting with ".")
    bool shouldHide = !SETTINGS.showHiddenFiles && fileName.startsWith(".");

    // Check against explicitly hidden items list
    if (!shouldHide) {
      for (const auto* item : HIDDEN_ITEMS) {
        if (fileName.equals(item)) {
          shouldHide = true;
          break;
        }
      }
    }

    if (!shouldHide) {
      FileInfo info;
      info.name = fileName;
      info.isDirectory = file.isDirectory();

      if (info.isDirectory) {
        info.size = 0;
        info.isEpub = false;
        info.isBook = false;
      } else {
        info.size = file.size();
        info.isEpub = isEpubFile(info.name);
        info.isBook = isBookFile(info.name);
      }

      callback(info);
    }

    file.close();
    yield();                          // Yield to allow WiFi and other tasks to process during long scans
    resetTaskWatchdogIfSubscribed();  // Reset watchdog to prevent timeout on large directories
    file = root.openNextFile();
  }
  root.close();
}

bool CrossPointWebServer::isEpubFile(const String& filename) const { return FsHelpers::hasEpubExtension(filename); }

void CrossPointWebServer::handleFileList() const {
  sendHtmlContent(server.get(), FilesPageHtml, sizeof(FilesPageHtml));
}

void CrossPointWebServer::handleFileListData() const {
  // Get current path from query string (default to root)
  String currentPath = "/";
  if (server->hasArg("path")) {
    currentPath = server->arg("path");
    // Ensure path starts with /
    if (!currentPath.startsWith("/")) {
      currentPath = "/" + currentPath;
    }
    // Remove trailing slash unless it's root
    if (currentPath.length() > 1 && currentPath.endsWith("/")) {
      currentPath = currentPath.substring(0, currentPath.length() - 1);
    }
  }

  server->setContentLength(CONTENT_LENGTH_UNKNOWN);
  server->send(200, "application/json", "");
  server->sendContent("[");
  char output[512];
  constexpr size_t outputSize = sizeof(output);
  bool seenFirst = false;
  JsonDocument doc;

  scanFiles(currentPath.c_str(), [this, &output, &doc, seenFirst](const FileInfo& info) mutable {
    doc.clear();
    doc["name"] = info.name;
    doc["size"] = info.size;
    doc["isDirectory"] = info.isDirectory;
    doc["isEpub"] = info.isEpub;
    doc["isBook"] = info.isBook;

    const size_t written = serializeJson(doc, output, outputSize);
    if (written >= outputSize) {
      // JSON output truncated; skip this entry to avoid sending malformed JSON
      LOG_DBG("WEB", "Skipping file entry with oversized JSON for name: %s", info.name.c_str());
      return;
    }

    if (seenFirst) {
      server->sendContent(",");
    } else {
      seenFirst = true;
    }
    server->sendContent(output);
  });
  server->sendContent("]");
  // End of streamed response, empty chunk to signal client
  server->sendContent("");
  LOG_DBG("WEB", "Served file listing page for path: %s", currentPath.c_str());
}

void CrossPointWebServer::handleDownload() const {
  const unsigned long requestStart = millis();
  LOG_DBG("WEB", "[HTTP] download request start uri=%s method=%d client=%d free=%lu min=%lu largest=%lu",
          server->uri().c_str(), static_cast<int>(server->method()), server->client().connected(),
          static_cast<unsigned long>(ESP.getFreeHeap()), static_cast<unsigned long>(ESP.getMinFreeHeap()),
          static_cast<unsigned long>(ESP.getMaxAllocHeap()));
  if (!server->hasArg("path")) {
    server->send(400, "text/plain", "Missing path");
    return;
  }

  String itemPath = server->arg("path");
  if (itemPath.isEmpty() || itemPath == "/") {
    server->send(400, "text/plain", "Invalid path");
    return;
  }
  if (!itemPath.startsWith("/")) {
    itemPath = "/" + itemPath;
  }

  const String itemName = itemPath.substring(itemPath.lastIndexOf('/') + 1);
  if (itemName.startsWith(".")) {
    server->send(403, "text/plain", "Cannot access system files");
    return;
  }
  for (const auto* item : HIDDEN_ITEMS) {
    if (itemName.equals(item)) {
      server->send(403, "text/plain", "Cannot access protected items");
      return;
    }
  }

  if (!Storage.exists(itemPath.c_str())) {
    server->send(404, "text/plain", "Item not found");
    return;
  }

  HalFile file = Storage.open(itemPath.c_str());
  if (!file) {
    server->send(500, "text/plain", "Failed to open file");
    return;
  }
  if (file.isDirectory()) {
    file.close();
    server->send(400, "text/plain", "Path is a directory");
    return;
  }

  String contentType = "application/octet-stream";
  if (isEpubFile(itemPath)) {
    contentType = "application/epub+zip";
  }

  char nameBuf[128] = {0};
  String filename = "download";
  if (file.getName(nameBuf, sizeof(nameBuf))) {
    filename = nameBuf;
  }

  const size_t contentLength = file.size();
  LOG_DBG("WEB", "[HTTP] download serving path=%s bytes=%lu client=%d", itemPath.c_str(),
          static_cast<unsigned long>(contentLength), server->client().connected());
  server->setContentLength(contentLength);
  server->sendHeader("Content-Disposition", "attachment; filename=\"" + filename + "\"");
  server->send(200, contentType.c_str(), "");

  NetworkClient client = server->client();
  const size_t chunkSize = 4096;
  uint8_t buffer[chunkSize];

  bool downloadOk = true;
  size_t bytesSent = 0;
  while (downloadOk && file.available()) {
    int result = file.read(buffer, chunkSize);
    if (result <= 0) break;
    size_t bytesRead = static_cast<size_t>(result);
    size_t totalWritten = 0;
    while (totalWritten < bytesRead) {
      resetTaskWatchdogIfSubscribed();
      size_t wrote = client.write(buffer + totalWritten, bytesRead - totalWritten);
      if (wrote == 0) {
        downloadOk = false;
        break;
      }
      totalWritten += wrote;
      bytesSent += wrote;
    }
    yield();
  }
  LOG_DBG("WEB", "[HTTP] download client write complete path=%s sent=%lu expected=%lu ok=%d elapsed=%lu client=%d "
                 "free=%lu min=%lu largest=%lu",
          itemPath.c_str(), static_cast<unsigned long>(bytesSent), static_cast<unsigned long>(contentLength), downloadOk,
          millis() - requestStart, client.connected(), static_cast<unsigned long>(ESP.getFreeHeap()),
          static_cast<unsigned long>(ESP.getMinFreeHeap()), static_cast<unsigned long>(ESP.getMaxAllocHeap()));
  client.clear();
  LOG_DBG("WEB", "[HTTP] download client cleared path=%s connected=%d free=%lu min=%lu largest=%lu", itemPath.c_str(),
          client.connected(), static_cast<unsigned long>(ESP.getFreeHeap()), static_cast<unsigned long>(ESP.getMinFreeHeap()),
          static_cast<unsigned long>(ESP.getMaxAllocHeap()));
  file.close();
}

// Diagnostic counters for upload performance analysis
static unsigned long uploadStartTime = 0;
static unsigned long totalWriteTime = 0;
static size_t writeCount = 0;

static bool flushUploadBuffer(CrossPointWebServer::UploadState& state) {
  if (state.bufferPos > 0 && state.file) {
    resetTaskWatchdogIfSubscribed();  // Reset watchdog before potentially slow SD write
    const unsigned long writeStart = millis();
    const size_t written = state.file.write(state.buffer.get(), state.bufferPos);
    totalWriteTime += millis() - writeStart;
    writeCount++;
    resetTaskWatchdogIfSubscribed();  // Reset watchdog after SD write

    if (written != state.bufferPos) {
      LOG_DBG("WEB", "[UPLOAD] Buffer flush failed: expected %d, wrote %d", state.bufferPos, written);
      state.bufferPos = 0;
      return false;
    }
    state.bufferPos = 0;
    // Let the Wi-Fi driver run between SD writes during long uploads. This is
    // intentionally a scheduler yield only; it does not add a fixed delay to
    // normal small transfers.
    yield();
  }
  return true;
}

void CrossPointWebServer::handleUpload(UploadState& state) const {
  static size_t lastLoggedSize = 0;

  // Reset watchdog at start of every upload callback - HTTP parsing can be slow
  resetTaskWatchdogIfSubscribed();

  // Safety check: ensure server is still valid
  if (!running || !server) {
    LOG_DBG("WEB", "[UPLOAD] ERROR: handleUpload called but server not running!");
    return;
  }

  const HTTPUpload& upload = server->upload();

  if (upload.status == UPLOAD_FILE_START) {
    // Reset watchdog - this is the critical 1% crash point
    resetTaskWatchdogIfSubscribed();

    state.fileName = upload.filename;
    state.size = 0;
    state.success = false;
    state.error = "";
    uploadStartTime = millis();
    lastLoggedSize = 0;
    state.bufferPos = 0;
    if (!state.ensureBuffer()) {
      state.error = "Not enough memory for upload buffer";
      LOG_ERR("WEB", "[UPLOAD] Failed to allocate upload buffer");
      return;
    }
    totalWriteTime = 0;
    writeCount = 0;

    // Get upload path from query parameter (defaults to root if not specified)
    // Note: We use query parameter instead of form data because multipart form
    // fields aren't available until after file upload completes
    if (server->hasArg("path")) {
      state.path = server->arg("path");
      // Ensure path starts with /
      if (!state.path.startsWith("/")) {
        state.path = "/" + state.path;
      }
      // Remove trailing slash unless it's root
      if (state.path.length() > 1 && state.path.endsWith("/")) {
        state.path = state.path.substring(0, state.path.length() - 1);
      }
    } else {
      state.path = "/";
    }

    LOG_DBG("WEB", "[UPLOAD] START: %s to path: %s", state.fileName.c_str(), state.path.c_str());
    LOG_DBG("WEB", "[UPLOAD] request client=%d free=%lu min=%lu largest=%lu", server->client().connected(),
            static_cast<unsigned long>(ESP.getFreeHeap()), static_cast<unsigned long>(ESP.getMinFreeHeap()),
            static_cast<unsigned long>(ESP.getMaxAllocHeap()));

    String filePath = state.path;
    if (!filePath.endsWith("/")) filePath += "/";
    filePath += state.fileName;

    // Check if file already exists - SD operations can be slow
    resetTaskWatchdogIfSubscribed();
    if (Storage.exists(filePath.c_str())) {
      state.error = "File already exists: " + state.fileName;
      state.releaseBuffer();
      LOG_DBG("WEB", "[UPLOAD] Collision: %s", filePath.c_str());
      return;
    }

    // Open file for writing - this can be slow due to FAT cluster allocation
    resetTaskWatchdogIfSubscribed();
    if (!Storage.openFileForWrite("WEB", filePath, state.file)) {
      state.error = "Failed to create file on SD card";
      state.releaseBuffer();
      LOG_DBG("WEB", "[UPLOAD] FAILED to create file: %s", filePath.c_str());
      return;
    }
    resetTaskWatchdogIfSubscribed();

    LOG_DBG("WEB", "[UPLOAD] File created successfully: %s free=%lu min=%lu largest=%lu", filePath.c_str(),
            static_cast<unsigned long>(ESP.getFreeHeap()), static_cast<unsigned long>(ESP.getMinFreeHeap()),
            static_cast<unsigned long>(ESP.getMaxAllocHeap()));
  } else if (upload.status == UPLOAD_FILE_WRITE) {
    if (state.file && state.error.isEmpty()) {
      // Buffer incoming data and flush when buffer is full
      // This reduces SD card write operations and improves throughput
      const uint8_t* data = upload.buf;
      size_t remaining = upload.currentSize;

      while (remaining > 0) {
        const size_t space = UploadState::UPLOAD_BUFFER_SIZE - state.bufferPos;
        const size_t toCopy = (remaining < space) ? remaining : space;

        memcpy(state.buffer.get() + state.bufferPos, data, toCopy);
        state.bufferPos += toCopy;
        data += toCopy;
        remaining -= toCopy;

        // Flush buffer when full
        if (state.bufferPos >= UploadState::UPLOAD_BUFFER_SIZE) {
          if (!flushUploadBuffer(state)) {
            state.error = "Failed to write to SD card - disk may be full";
            state.file.close();
            state.releaseBuffer();
            return;
          }
        }
      }

      state.size += upload.currentSize;

      // Log progress every 100KB
      if (state.size - lastLoggedSize >= 102400) {
        const unsigned long elapsed = millis() - uploadStartTime;
        const float kbps = (elapsed > 0) ? (state.size / 1024.0) / (elapsed / 1000.0) : 0;
        LOG_DBG("WEB", "[UPLOAD] %d bytes (%.1f KB), %.1f KB/s, %d writes free=%lu min=%lu largest=%lu", state.size,
                state.size / 1024.0, kbps, writeCount, static_cast<unsigned long>(ESP.getFreeHeap()),
                static_cast<unsigned long>(ESP.getMinFreeHeap()), static_cast<unsigned long>(ESP.getMaxAllocHeap()));
        lastLoggedSize = state.size;
      }
    }
  } else if (upload.status == UPLOAD_FILE_END) {
    if (state.file) {
      // Flush any remaining buffered data
      if (!flushUploadBuffer(state)) {
        state.error = "Failed to write final data to SD card";
      }
      state.file.close();
      state.releaseBuffer();

      if (state.error.isEmpty()) {
        state.success = true;
        const unsigned long elapsed = millis() - uploadStartTime;
        const float avgKbps = (elapsed > 0) ? (state.size / 1024.0) / (elapsed / 1000.0) : 0;
        const float writePercent = (elapsed > 0) ? (totalWriteTime * 100.0 / elapsed) : 0;
        LOG_DBG("WEB", "[UPLOAD] Complete: %s (%d bytes in %lu ms, avg %.1f KB/s)", state.fileName.c_str(), state.size,
                elapsed, avgKbps);
        LOG_DBG("WEB", "[UPLOAD] Diagnostics: %d writes, total write time: %lu ms (%.1f%%)", writeCount, totalWriteTime,
                writePercent);

        // Clear book cache after uploading the file
        String filePath = state.path;
        if (!filePath.endsWith("/")) filePath += "/";
        filePath += state.fileName;
        LOG_DBG("WEB", "[BOOKCACHE] invalidate after HTTP upload path=%s free=%lu min=%lu largest=%lu", filePath.c_str(),
                static_cast<unsigned long>(ESP.getFreeHeap()), static_cast<unsigned long>(ESP.getMinFreeHeap()),
                static_cast<unsigned long>(ESP.getMaxAllocHeap()));
        clearBookCache(filePath.c_str());
        LOG_DBG("WEB", "[BOOKCACHE] invalidate complete after HTTP upload path=%s free=%lu min=%lu largest=%lu",
                filePath.c_str(), static_cast<unsigned long>(ESP.getFreeHeap()), static_cast<unsigned long>(ESP.getMinFreeHeap()),
                static_cast<unsigned long>(ESP.getMaxAllocHeap()));
      }
    }
    // The multipart callback may reach FILE_END after an earlier open/write
    // failure, so release the buffer outside the file-open branch as well.
    state.releaseBuffer();
  } else if (upload.status == UPLOAD_FILE_ABORTED) {
    state.bufferPos = 0;  // Discard buffered data
    if (state.file) {
      state.file.close();
      // Try to delete the incomplete file
      String filePath = state.path;
      if (!filePath.endsWith("/")) filePath += "/";
      filePath += state.fileName;
      Storage.remove(filePath.c_str());
    }
    state.error = "Upload aborted";
    state.releaseBuffer();
    LOG_DBG("WEB", "Upload aborted client=%d free=%lu min=%lu largest=%lu", server->client().connected(),
            static_cast<unsigned long>(ESP.getFreeHeap()), static_cast<unsigned long>(ESP.getMinFreeHeap()),
            static_cast<unsigned long>(ESP.getMaxAllocHeap()));
  }
}

void CrossPointWebServer::handleUploadPost(UploadState& state) const {
  LOG_DBG("WEB", "[HTTP] upload request complete success=%d bytes=%lu client=%d free=%lu min=%lu largest=%lu",
          state.success, static_cast<unsigned long>(state.size), server->client().connected(),
          static_cast<unsigned long>(ESP.getFreeHeap()), static_cast<unsigned long>(ESP.getMinFreeHeap()),
          static_cast<unsigned long>(ESP.getMaxAllocHeap()));
  if (state.success) {
    server->send(200, "text/plain", "File uploaded successfully: " + state.fileName);
  } else {
    const String error = state.error.isEmpty() ? "Unknown error during upload" : state.error;
    server->send(400, "text/plain", error);
  }
  state.fileName = String();
  state.path = String("/");
  state.error = String();
  state.size = 0;
  state.success = false;
}

void CrossPointWebServer::handleCreateFolder() const {
  // Get folder name from form data
  if (!server->hasArg("name")) {
    server->send(400, "text/plain", "Missing folder name");
    return;
  }

  const String folderName = server->arg("name");

  // Validate folder name
  if (folderName.isEmpty()) {
    server->send(400, "text/plain", "Folder name cannot be empty");
    return;
  }

  // Get parent path
  String parentPath = "/";
  if (server->hasArg("path")) {
    parentPath = server->arg("path");
    if (!parentPath.startsWith("/")) {
      parentPath = "/" + parentPath;
    }
    if (parentPath.length() > 1 && parentPath.endsWith("/")) {
      parentPath = parentPath.substring(0, parentPath.length() - 1);
    }
  }

  // Build full folder path
  String folderPath = parentPath;
  if (!folderPath.endsWith("/")) folderPath += "/";
  folderPath += folderName;

  LOG_DBG("WEB", "Creating folder: %s", folderPath.c_str());

  // Check if already exists
  if (Storage.exists(folderPath.c_str())) {
    server->send(400, "text/plain", "Folder already exists");
    return;
  }

  // Create the folder
  if (Storage.mkdir(folderPath.c_str())) {
    LOG_DBG("WEB", "Folder created successfully: %s", folderPath.c_str());
    server->send(200, "text/plain", "Folder created: " + folderName);
  } else {
    LOG_DBG("WEB", "Failed to create folder: %s", folderPath.c_str());
    server->send(500, "text/plain", "Failed to create folder");
  }
}

void CrossPointWebServer::handleRename() const {
  if (!server->hasArg("path") || !server->hasArg("name")) {
    server->send(400, "text/plain", "Missing path or new name");
    return;
  }

  String itemPath = normalizeWebPath(server->arg("path"));
  String newName = server->arg("name");
  newName.trim();

  if (itemPath.isEmpty() || itemPath == "/") {
    server->send(400, "text/plain", "Invalid path");
    return;
  }
  if (newName.isEmpty()) {
    server->send(400, "text/plain", "New name cannot be empty");
    return;
  }
  if (newName.indexOf('/') >= 0 || newName.indexOf('\\') >= 0) {
    server->send(400, "text/plain", "Invalid file name");
    return;
  }
  if (isProtectedItemName(newName)) {
    server->send(403, "text/plain", "Cannot rename to protected name");
    return;
  }

  const String itemName = itemPath.substring(itemPath.lastIndexOf('/') + 1);
  if (isProtectedItemName(itemName)) {
    server->send(403, "text/plain", "Cannot rename protected item");
    return;
  }
  if (newName == itemName) {
    server->send(200, "text/plain", "Name unchanged");
    return;
  }

  if (!Storage.exists(itemPath.c_str())) {
    server->send(404, "text/plain", "Item not found");
    return;
  }

  HalFile file = Storage.open(itemPath.c_str());
  if (!file) {
    server->send(500, "text/plain", "Failed to open file");
    return;
  }
  if (file.isDirectory()) {
    file.close();
    server->send(400, "text/plain", "Only files can be renamed");
    return;
  }

  String parentPath = itemPath.substring(0, itemPath.lastIndexOf('/'));
  if (parentPath.isEmpty()) {
    parentPath = "/";
  }
  String newPath = parentPath;
  if (!newPath.endsWith("/")) {
    newPath += "/";
  }
  newPath += newName;

  if (Storage.exists(newPath.c_str())) {
    file.close();
    server->send(409, "text/plain", "Target already exists");
    return;
  }

  LOG_DBG("WEB", "[BOOKCACHE] invalidate before rename path=%s free=%lu min=%lu largest=%lu", itemPath.c_str(),
          static_cast<unsigned long>(ESP.getFreeHeap()), static_cast<unsigned long>(ESP.getMinFreeHeap()),
          static_cast<unsigned long>(ESP.getMaxAllocHeap()));
  clearBookCache(itemPath.c_str());
  LOG_DBG("WEB", "[BOOKCACHE] invalidate complete before rename path=%s free=%lu min=%lu largest=%lu", itemPath.c_str(),
          static_cast<unsigned long>(ESP.getFreeHeap()), static_cast<unsigned long>(ESP.getMinFreeHeap()),
          static_cast<unsigned long>(ESP.getMaxAllocHeap()));
  const bool success = file.rename(newPath.c_str());
  file.close();

  if (success) {
    LOG_DBG("WEB", "Renamed file: %s -> %s", itemPath.c_str(), newPath.c_str());
    BOOK_METADATA_OVERRIDES.updatePath(itemPath.c_str(), newPath.c_str());
    server->send(200, "text/plain", "Renamed successfully");
  } else {
    LOG_ERR("WEB", "Failed to rename file: %s -> %s", itemPath.c_str(), newPath.c_str());
    server->send(500, "text/plain", "Failed to rename file");
  }
}

void CrossPointWebServer::handleMove() const {
  if (!server->hasArg("path") || !server->hasArg("dest")) {
    server->send(400, "text/plain", "Missing path or destination");
    return;
  }

  String itemPath = normalizeWebPath(server->arg("path"));
  String destPath = normalizeWebPath(server->arg("dest"));

  if (itemPath.isEmpty() || itemPath == "/") {
    server->send(400, "text/plain", "Invalid path");
    return;
  }
  if (destPath.isEmpty()) {
    server->send(400, "text/plain", "Invalid destination");
    return;
  }

  const String itemName = itemPath.substring(itemPath.lastIndexOf('/') + 1);
  if (isProtectedItemName(itemName)) {
    server->send(403, "text/plain", "Cannot move protected item");
    return;
  }
  if (destPath != "/") {
    const String destName = destPath.substring(destPath.lastIndexOf('/') + 1);
    if (isProtectedItemName(destName)) {
      server->send(403, "text/plain", "Cannot move into protected folder");
      return;
    }
  }

  if (!Storage.exists(itemPath.c_str())) {
    server->send(404, "text/plain", "Item not found");
    return;
  }

  HalFile file = Storage.open(itemPath.c_str());
  if (!file) {
    server->send(500, "text/plain", "Failed to open file");
    return;
  }
  if (file.isDirectory()) {
    file.close();
    server->send(400, "text/plain", "Only files can be moved");
    return;
  }

  if (!Storage.exists(destPath.c_str())) {
    file.close();
    server->send(404, "text/plain", "Destination not found");
    return;
  }
  HalFile destDir = Storage.open(destPath.c_str());
  if (!destDir || !destDir.isDirectory()) {
    if (destDir) {
      destDir.close();
    }
    file.close();
    server->send(400, "text/plain", "Destination is not a folder");
    return;
  }
  destDir.close();

  String newPath = destPath;
  if (!newPath.endsWith("/")) {
    newPath += "/";
  }
  newPath += itemName;

  if (newPath == itemPath) {
    file.close();
    server->send(200, "text/plain", "Already in destination");
    return;
  }
  if (Storage.exists(newPath.c_str())) {
    file.close();
    server->send(409, "text/plain", "Target already exists");
    return;
  }

  LOG_DBG("WEB", "[BOOKCACHE] invalidate before move path=%s free=%lu min=%lu largest=%lu", itemPath.c_str(),
          static_cast<unsigned long>(ESP.getFreeHeap()), static_cast<unsigned long>(ESP.getMinFreeHeap()),
          static_cast<unsigned long>(ESP.getMaxAllocHeap()));
  clearBookCache(itemPath.c_str());
  LOG_DBG("WEB", "[BOOKCACHE] invalidate complete before move path=%s free=%lu min=%lu largest=%lu", itemPath.c_str(),
          static_cast<unsigned long>(ESP.getFreeHeap()), static_cast<unsigned long>(ESP.getMinFreeHeap()),
          static_cast<unsigned long>(ESP.getMaxAllocHeap()));
  const bool success = file.rename(newPath.c_str());
  file.close();

  if (success) {
    LOG_DBG("WEB", "Moved file: %s -> %s", itemPath.c_str(), newPath.c_str());
    BOOK_METADATA_OVERRIDES.updatePath(itemPath.c_str(), newPath.c_str());
    server->send(200, "text/plain", "Moved successfully");
  } else {
    LOG_ERR("WEB", "Failed to move file: %s -> %s", itemPath.c_str(), newPath.c_str());
    server->send(500, "text/plain", "Failed to move file");
  }
}

void CrossPointWebServer::handleDelete() const {
  // To ensure backwards compatibility, plain `path` is mapped
  // to a single element JSON array.
  bool hasPathArg = server->hasArg("path");
  bool hasPathsArg = server->hasArg("paths");
  // Check 'paths' or `path` argument is provided
  if (!(hasPathArg || hasPathsArg)) {
    server->send(400, "text/plain", "Missing `path` or `paths` argument");
    return;
  }
  if (hasPathArg && hasPathsArg) {
    server->send(400, "text/plain", "Provide either 'path' or 'paths', not both");
    return;
  }

  // Parse paths
  String pathsArg;
  JsonDocument doc;
  DeserializationError error = DeserializationError(DeserializationError::Code::Ok);
  if (hasPathsArg) {
    pathsArg = server->arg("paths");
    error = deserializeJson(doc, pathsArg);
  } else {
    pathsArg = server->arg("path");
    doc.add(pathsArg);
  }
  if (error) {
    server->send(400, "text/plain", "Invalid paths format");
    return;
  }

  auto paths = doc.as<JsonArray>();
  if (paths.isNull() || paths.size() == 0) {
    server->send(400, "text/plain", "No paths provided");
    return;
  }

  // Iterate over paths and delete each item
  bool allSuccess = true;
  String failedItems;

  for (const auto& p : paths) {
    auto itemPath = p.as<String>();

    // Validate path
    if (itemPath.isEmpty() || itemPath == "/") {
      failedItems += itemPath + " (cannot delete root); ";
      allSuccess = false;
      continue;
    }

    // Ensure path starts with /
    if (!itemPath.startsWith("/")) {
      itemPath = "/" + itemPath;
    }

    // Security check: prevent deletion of protected items
    const String itemName = itemPath.substring(itemPath.lastIndexOf('/') + 1);

    // Hidden/system files are protected
    if (itemName.startsWith(".")) {
      failedItems += itemPath + " (hidden/system file); ";
      allSuccess = false;
      continue;
    }

    // Check against explicitly protected items
    bool isProtected = false;
    for (const auto* item : HIDDEN_ITEMS) {
      if (itemName.equals(item)) {
        isProtected = true;
        break;
      }
    }
    if (isProtected) {
      failedItems += itemPath + " (protected file); ";
      allSuccess = false;
      continue;
    }

    // Check if item exists
    if (!Storage.exists(itemPath.c_str())) {
      failedItems += itemPath + " (not found); ";
      allSuccess = false;
      continue;
    }

    // Decide whether it's a directory or file by opening it
    bool success = false;
    HalFile f = Storage.open(itemPath.c_str());
    if (f && f.isDirectory()) {
      // For folders, ensure empty before removing
      HalFile entry = f.openNextFile();
      if (entry) {
        entry.close();
        f.close();
        failedItems += itemPath + " (folder not empty); ";
        allSuccess = false;
        continue;
      }
      f.close();
      success = Storage.rmdir(itemPath.c_str());
    } else {
      // It's a file (or couldn't open as dir) — remove file
      if (f) f.close();
      success = Storage.remove(itemPath.c_str());
      LOG_DBG("WEB", "[BOOKCACHE] invalidate after delete path=%s removed=%d free=%lu min=%lu largest=%lu",
              itemPath.c_str(), success, static_cast<unsigned long>(ESP.getFreeHeap()),
              static_cast<unsigned long>(ESP.getMinFreeHeap()), static_cast<unsigned long>(ESP.getMaxAllocHeap()));
      clearBookCache(itemPath.c_str());
      LOG_DBG("WEB", "[BOOKCACHE] invalidate complete after delete path=%s free=%lu min=%lu largest=%lu", itemPath.c_str(),
              static_cast<unsigned long>(ESP.getFreeHeap()), static_cast<unsigned long>(ESP.getMinFreeHeap()),
              static_cast<unsigned long>(ESP.getMaxAllocHeap()));
      if (success) BOOK_METADATA_OVERRIDES.removeByPath(itemPath.c_str());
    }

    if (!success) {
      failedItems += itemPath + " (deletion failed); ";
      allSuccess = false;
    }
  }

  if (allSuccess) {
    server->send(200, "text/plain", "All items deleted successfully");
  } else {
    server->send(500, "text/plain", "Failed to delete some items: " + failedItems);
  }
}

void CrossPointWebServer::handleSettingsPage() const {
  sendHtmlContent(server.get(), SettingsPageHtml, sizeof(SettingsPageHtml));
  LOG_DBG("WEB", "Served settings page");
}

void CrossPointWebServer::handleGetSettings() const {
  WebRouteTrace routeTrace(server.get(), "/api/settings", "bounded_paged_stream");
  logSettingsStage("handler_entered", 0, nullptr, server.get());
  LOG_DBG("WEB", "[SETTINGS] GET start free=%lu min=%lu largest=%lu", static_cast<unsigned long>(ESP.getFreeHeap()),
          static_cast<unsigned long>(ESP.getMinFreeHeap()), static_cast<unsigned long>(ESP.getMaxAllocHeap()));
  if (!webSettingsMemoryAdmitted(server.get(), 0)) return;

  const bool paged = server->hasArg("offset") || server->hasArg("limit");
  size_t pageOffset = 0;
  size_t pageEnd = static_cast<size_t>(-1);
  if (paged) {
    const long parsedOffset = server->hasArg("offset") ? server->arg("offset").toInt() : 0;
    const long parsedLimit = server->hasArg("limit") ? server->arg("limit").toInt() : 6;
    if (parsedOffset < 0 || parsedLimit <= 0) {
      server->send(400, "text/plain", "Invalid settings page");
      return;
    }
    pageOffset = static_cast<size_t>(parsedOffset);
    const size_t pageLimit = std::min<size_t>(static_cast<size_t>(parsedLimit), 8);
    const size_t maxSize = static_cast<size_t>(-1);
    pageEnd = pageLimit > maxSize - pageOffset ? maxSize : pageOffset + pageLimit;
  }

  // Count the exact response length, then emit through one reused 256-byte
  // buffer with a fixed Content-Length. Static settings are visited by
  // reference. The dynamic font and sleep-screen rows are written directly
  // from their owners instead of constructing SettingInfo copies with
  // duplicated option vectors and captured family-name copies.
  const auto emitSettings = [this, pageOffset, pageEnd](SettingsJsonStream& output, WebServer* web,
                                                        const bool diagnostics) {
    output.raw("[");
    bool seenFirst = false;
    unsigned index = 0;
    if (diagnostics) logSettingsStage("descriptors_acquired", 0, nullptr, web);

    const auto beginDescriptor = [&](const char* key, const StrId nameId, const StrId category,
                                     unsigned& currentIndex) {
      if (output.closed || !key) return false;
      currentIndex = index++;
      if (currentIndex < pageOffset || currentIndex >= pageEnd) return false;
      if (diagnostics && (currentIndex & 7u) == 0u) logSettingsStage("descriptor", currentIndex, key, web);
      if (seenFirst) output.raw(",");
      seenFirst = true;
      output.beginObject();
      output.fieldString("key", key);
      output.fieldString("name", I18N.get(nameId));
      output.fieldString("category", I18N.get(category));
      return true;
    };

    const auto emitBaseDescriptor = [&](const SettingInfo& s) {
      if (!s.key || s.type == SettingType::ACTION) return;  // Skip non-JSON entries
      unsigned currentIndex = 0;
      if (!beginDescriptor(s.key, s.nameId, s.category, currentIndex)) return;
      switch (s.type) {
        case SettingType::TOGGLE: {
          output.fieldString("type", "toggle");
          if (s.valuePtr) output.fieldInt("value", static_cast<int>(SETTINGS.*(s.valuePtr)));
          break;
        }
        case SettingType::ENUM: {
          output.fieldString("type", "enum");
          if (s.valueGetter) {
            output.fieldInt("value", static_cast<int>(s.valueGetter()));
          } else if (s.valuePtr) {
            output.fieldInt("value", static_cast<int>(SETTINGS.*(s.valuePtr)));
          }
          output.beginArray("options");
          if (!s.enumStringValues.empty()) {
            for (const auto& opt : s.enumStringValues) output.arrayString(opt.c_str());
          } else {
            for (const auto& opt : s.enumValues) output.arrayString(I18N.get(opt));
          }
          output.endArray();
          break;
        }
        case SettingType::VALUE: {
          output.fieldString("type", "value");
          if (s.valuePtr) output.fieldInt("value", static_cast<int>(SETTINGS.*(s.valuePtr)));
          output.fieldInt("min", s.valueRange.min);
          output.fieldInt("max", s.valueRange.max);
          output.fieldInt("step", s.valueRange.step);
          break;
        }
        case SettingType::STRING: {
          output.fieldString("type", "string");
          if (s.stringGetter) {
            const std::string value = s.stringGetter();
            output.fieldString("value", value);
          } else if (s.stringMaxLen > 0) {
            output.fieldString("value", reinterpret_cast<const char*>(&SETTINGS) + s.stringOffset);
          }
          break;
        }
        default:
          return;
      }
      output.endObject();
      if (!output.closed && !output.countOnly && (currentIndex == 0u || (currentIndex & 7u) == 0u)) output.flush();
    };

    const auto emitSleepScreen = [&]() {
      unsigned currentIndex = 0;
      if (!beginDescriptor("sleepScreen", StrId::STR_SLEEP_SCREEN, StrId::STR_CAT_DISPLAY, currentIndex)) return;
      const auto sleepValue = []() -> uint8_t {
        switch (SETTINGS.sleepScreen) {
          case CrossPointSettings::SLEEP_SCREEN_MODE::COVER_CUSTOM: return 3;
          case CrossPointSettings::SLEEP_SCREEN_MODE::BLANK: return 4;
          case CrossPointSettings::SLEEP_SCREEN_MODE::QUICK_RESUME: return 5;
          case CrossPointSettings::SLEEP_SCREEN_MODE::OVERLAY: return 6;
          case CrossPointSettings::SLEEP_SCREEN_MODE::COVER_OVERLAY: return 7;
          case CrossPointSettings::SLEEP_SCREEN_MODE::READING_STATS_SLEEP: return 8;
          case CrossPointSettings::SLEEP_SCREEN_MODE::MINIMAL_STATS: return 9;
          case CrossPointSettings::SLEEP_SCREEN_MODE::CLIPPING_COVER: return 10;
          case CrossPointSettings::SLEEP_SCREEN_MODE::TODO_LIST: return 11;
          case CrossPointSettings::SLEEP_SCREEN_MODE::READING_CALENDAR_SLEEP: return 12;
          case CrossPointSettings::SLEEP_SCREEN_MODE::READING_SUMMARY_SLEEP: return 13;
          case CrossPointSettings::SLEEP_SCREEN_MODE::COVER_PROGRESS: return 14;
          case CrossPointSettings::SLEEP_SCREEN_MODE::RANDOM: return 15;
          default:
            return SETTINGS.sleepScreen <= CrossPointSettings::SLEEP_SCREEN_MODE::COVER
                       ? static_cast<uint8_t>(SETTINGS.sleepScreen)
                       : 3;
        }
      };
      const auto sleepOption = [](const uint8_t option) -> const char* {
        switch (option) {
          case 0: return I18N.get(StrId::STR_DARK);
          case 1: return I18N.get(StrId::STR_LIGHT);
          case 2: return I18N.get(StrId::STR_CUSTOM);
          case 3: return I18N.get(StrId::STR_COVER);
          case 4: return I18N.get(StrId::STR_NONE_OPT);
          case 5: return I18N.get(StrId::STR_QUICK_RESUME);
          case 6: return I18N.get(StrId::STR_PAGE_OVERLAY);
          case 7: return "Cover + Overlay";
          case 8: return "Reading Stats";
          case 9: return "Minimal Stats";
          case 10: return "Clipping + Cover";
          case 11: return I18N.get(StrId::STR_TODO_LIST);
          case 12: return I18N.get(StrId::STR_READING_CALENDAR_SLEEP);
          case 13: return I18N.get(StrId::STR_READING_SUMMARY_SLEEP);
          case 14: return I18N.get(StrId::STR_SLEEP_COVER_PROGRESS);
          case 15: return I18N.get(StrId::STR_SLEEP_RANDOM);
          default: return I18N.get(StrId::STR_READING_SUMMARY_SLEEP);
        }
      };
      output.fieldString("type", "enum");
      output.fieldInt("value", sleepValue());
      output.beginArray("options");
      for (uint8_t option = 0; option < 16; ++option) output.arrayString(sleepOption(option));
      output.endArray();
      output.endObject();
      if (!output.closed && !output.countOnly && (currentIndex == 0u || (currentIndex & 7u) == 0u)) output.flush();
    };

    const auto emitReaderFontFamily = [&]() {
      unsigned currentIndex = 0;
      if (!beginDescriptor("fontFamily", StrId::STR_FONT_FAMILY, StrId::STR_CAT_READER, currentIndex)) return;
      const auto& families = sdFontSystem.registry().getFamilies();
      uint8_t value = FontSelectionCompatibility::readerVisibleIndex(SETTINGS.fontFamily);
      if (SETTINGS.sdFontFamilyName[0] != '\0') {
        for (size_t i = 0; i < families.size(); ++i) {
          if (families[i].name == SETTINGS.sdFontFamilyName) {
            value = static_cast<uint8_t>(CrossPointSettings::VISIBLE_BUILTIN_FONT_COUNT + i);
            break;
          }
        }
      }
      output.fieldString("type", "enum");
      output.fieldInt("value", value);
      output.beginArray("options");
      output.arrayString(I18N.get(StrId::STR_NOTO_SERIF));
      for (const auto& family : families) output.arrayString(family.name.c_str());
      output.endArray();
      output.endObject();
      if (!output.closed && !output.countOnly && (currentIndex == 0u || (currentIndex & 7u) == 0u)) output.flush();
    };

    const auto emitDictionaryFont = [&]() {
      unsigned currentIndex = 0;
      if (!beginDescriptor("dictionaryFontFamily", StrId::STR_DICTIONARY_FONT, StrId::STR_CAT_READER,
                           currentIndex)) {
        return;
      }
      output.fieldString("type", "enum");
      output.fieldInt("value", FontSelectionCompatibility::dictionaryFamilyForUi(SETTINGS.dictionaryFontFamily));
      output.beginArray("options");
      output.arrayString(I18N.get(StrId::STR_USE_READER_FONT));
      output.arrayString(I18N.get(StrId::STR_NOTO_SERIF));
      output.endArray();
      output.endObject();
      if (!output.closed && !output.countOnly && (currentIndex == 0u || (currentIndex & 7u) == 0u)) output.flush();
    };

    const auto& baseSettings = getBaseSettingsList();
    for (const auto& setting : baseSettings) {
      if (!BoardConfig::hasTouch() && setting.nameId == StrId::STR_TOUCH_READER_CONTROLS) continue;
      if (BoardConfig::hasTouch() &&
          (setting.nameId == StrId::STR_FRONT_BTN_FOLLOW_ORIENTATION ||
           setting.nameId == StrId::STR_SUNLIGHT_FADING_FIX)) {
        continue;
      }
      if (setting.nameId == StrId::STR_SLEEP_SCREEN) {
        emitSleepScreen();
      } else if (isSleepComposerSetting(setting)) {
        continue;
      } else if (setting.nameId == StrId::STR_FONT_FAMILY) {
        emitReaderFontFamily();
      } else if (setting.nameId == StrId::STR_DICTIONARY_FONT) {
        emitDictionaryFont();
      } else {
        // The interface-font descriptor intentionally has no API key today;
        // emitBaseDescriptor preserves the existing skip behavior.
        emitBaseDescriptor(setting);
      }
    }

    output.raw("]");
    output.flush();
    return index;
  };

  SettingsJsonStream counter(nullptr, true);
  const unsigned countedDescriptors = emitSettings(counter, nullptr, false);
  const size_t contentLength = counter.bytesWritten();
  const size_t selectedDescriptorStart = std::min<size_t>(pageOffset, countedDescriptors);
  const size_t selectedDescriptorEnd = std::min<size_t>(pageEnd, countedDescriptors);
  const unsigned pageDescriptorCount = static_cast<unsigned>(
      selectedDescriptorEnd > selectedDescriptorStart ? selectedDescriptorEnd - selectedDescriptorStart : 0);
  logSettingsStage("counted_response", countedDescriptors, nullptr, server.get());
  if (!webSettingsMemoryAdmitted(server.get(), contentLength)) return;

  // NetworkClient::write waits for socket writability but its return count is
  // bytes accepted by lwIP, not proof of remote ACK. The socket queue depth is
  // not exposed here, so paged callers keep each response's live transport
  // footprint bounded; the no-query API retains the complete-array contract
  // and is admitted only when its measured-size budget fits.
  const int clientFd = server->client().fd();
  int sendBufferBytes = 0;
  socklen_t sendBufferLength = sizeof(sendBufferBytes);
  const int sendBufferResult = clientFd >= 0
                                   ? getsockopt(clientFd, SOL_SOCKET, SO_SNDBUF, &sendBufferBytes,
                                                &sendBufferLength)
                                   : -1;
  LOG_INF("WEBRT", "route=/api/settings phase=transport fd=%d sndbuf_query=%s sndbuf=%d queue_depth=not_exposed mode=lwip_send_queue chunk=%u page=%d offset=%u end=%u",
          clientFd, sendBufferResult == 0 ? "ok" : "unavailable", sendBufferBytes,
          static_cast<unsigned>(SettingsJsonStream::kBufferSize), paged ? 1 : 0,
          static_cast<unsigned>(pageOffset), static_cast<unsigned>(pageEnd));
  if (paged) server->sendHeader("X-Settings-Total", String(countedDescriptors));
  server->setContentLength(contentLength);
  logSettingsStage("before_response_object", 0, nullptr, server.get());
  server->send(200, "application/json", "");

  SettingsJsonStream output(server.get());
  const unsigned emittedTotalDescriptors = emitSettings(output, server.get(), true);
  const bool responseComplete = !output.closed && output.totalBytes == contentLength &&
                                output.transportBytesAccepted == contentLength;
  if (!responseComplete && server->client().connected()) server->client().stop();
  LOG_INF("WEBRT", "route=/api/settings phase=response_complete result=%s page_descriptors=%u total_descriptors=%u chunks=%u accepted=%u/%u free=%lu largest=%lu",
          responseComplete ? "ok" : "incomplete", pageDescriptorCount, emittedTotalDescriptors,
          static_cast<unsigned>(output.transportChunks),
          static_cast<unsigned>(output.transportBytesAccepted), static_cast<unsigned>(contentLength),
          static_cast<unsigned long>(ESP.getFreeHeap()), static_cast<unsigned long>(ESP.getMaxAllocHeap()));
  LOG_DBG("WEB", "[SETTINGS] GET complete free=%lu min=%lu largest=%lu", static_cast<unsigned long>(ESP.getFreeHeap()),
          static_cast<unsigned long>(ESP.getMinFreeHeap()), static_cast<unsigned long>(ESP.getMaxAllocHeap()));
}

void CrossPointWebServer::handlePostSettings() {
  WebRouteTrace routeTrace(server.get(), "/api/settings", "buffered_request_bounded");
  LOG_DBG("WEB", "[SETTINGS] POST start free=%lu min=%lu largest=%lu", static_cast<unsigned long>(ESP.getFreeHeap()),
          static_cast<unsigned long>(ESP.getMinFreeHeap()), static_cast<unsigned long>(ESP.getMaxAllocHeap()));
  if (!server->hasArg("plain")) {
    server->send(400, "text/plain", "Missing JSON body");
    return;
  }

  String body = server->arg("plain");
  JsonDocument doc;
  const DeserializationError err = deserializeJson(doc, body);
  if (err) {
    server->send(400, "text/plain", String("Invalid JSON: ") + err.c_str());
    return;
  }
  // WebServer and ArduinoJson both need the request while parsing, but the
  // settings walk does not. Release the body before applying settings.
  body = String();

  int applied = 0;
  bool changed = false;

  const auto applySetting = [&](const SettingInfo& s) {
    if (!s.key) return;
    if (!doc[s.key].is<JsonVariant>()) return;

    switch (s.type) {
      case SettingType::TOGGLE: {
        int val = 0;
        if (strcmp(s.key, "featuredShowFileType") == 0) {
          if (doc[s.key].is<bool>()) {
            val = doc[s.key].as<bool>() ? 1 : 0;
          } else if (doc[s.key].is<int>()) {
            val = doc[s.key].as<int>();
          } else {
            break;
          }
          if (val != 0 && val != 1) break;
        } else {
          val = doc[s.key].as<int>() ? 1 : 0;
        }
        if (s.valuePtr) {
          changed |= SETTINGS.*(s.valuePtr) != val;
          SETTINGS.*(s.valuePtr) = val;
        }
        applied++;
        break;
      }
      case SettingType::ENUM: {
        const int val = doc[s.key].as<int>();
        const int maxVal = s.enumStringValues.empty() ? static_cast<int>(s.enumValues.size())
                                                      : static_cast<int>(s.enumStringValues.size());
        if (val >= 0 && val < maxVal) {
          const uint8_t previous = s.valueGetter ? s.valueGetter()
                                                 : (s.valuePtr ? SETTINGS.*(s.valuePtr) : static_cast<uint8_t>(val));
          if (s.valueSetter) {
            s.valueSetter(static_cast<uint8_t>(val));
          } else if (s.valuePtr) {
            SETTINGS.*(s.valuePtr) = static_cast<uint8_t>(val);
          }
          const uint8_t current = s.valueGetter ? s.valueGetter()
                                                : (s.valuePtr ? SETTINGS.*(s.valuePtr) : static_cast<uint8_t>(val));
          changed |= previous != current;
          applied++;
        }
        break;
      }
      case SettingType::VALUE: {
        const int val = doc[s.key].as<int>();
        if (val >= s.valueRange.min && val <= s.valueRange.max) {
          if (s.valuePtr) {
            changed |= SETTINGS.*(s.valuePtr) != val;
            SETTINGS.*(s.valuePtr) = static_cast<uint8_t>(val);
          }
          applied++;
        }
        break;
      }
      case SettingType::STRING: {
        const std::string val = doc[s.key].as<std::string>();
        if (s.stringSetter) {
          changed = true;
          s.stringSetter(val);
        } else if (s.stringMaxLen > 0) {
          char* ptr = reinterpret_cast<char*>(&SETTINGS) + s.stringOffset;
          const size_t copiedLength = std::min(val.size(), s.stringMaxLen - 1);
          changed |= strncmp(ptr, val.c_str(), copiedLength) != 0 || ptr[copiedLength] != '\0';
          strncpy(ptr, val.c_str(), s.stringMaxLen - 1);
          ptr[s.stringMaxLen - 1] = '\0';
        }
        applied++;
        break;
      }
      default:
        break;
    }
  };

  // Apply the static descriptors by reference. The effective descriptor
  // builder deep-copies SD-font names into vectors and std::function captures;
  // the Web API needs only the selected indexes, not those UI objects.
  for (const auto& setting : getBaseSettingsList()) {
    if (setting.nameId == StrId::STR_SLEEP_SCREEN || setting.nameId == StrId::STR_FONT_FAMILY ||
        setting.nameId == StrId::STR_INTERFACE_FONT || setting.nameId == StrId::STR_DICTIONARY_FONT) {
      continue;
    }
    applySetting(setting);
  }

  if (doc["sleepScreen"].is<JsonVariant>()) {
    const int value = doc["sleepScreen"].as<int>();
    if (value >= 0 && value < 17) {
      const auto previous = SETTINGS.sleepScreen;
      switch (value) {
        case 0: SETTINGS.sleepScreen = CrossPointSettings::SLEEP_SCREEN_MODE::DARK; break;
        case 1: SETTINGS.sleepScreen = CrossPointSettings::SLEEP_SCREEN_MODE::LIGHT; break;
        case 2: SETTINGS.sleepScreen = CrossPointSettings::SLEEP_SCREEN_MODE::CUSTOM; break;
        case 3: SETTINGS.sleepScreen = CrossPointSettings::SLEEP_SCREEN_MODE::COVER; break;
        case 4: SETTINGS.sleepScreen = CrossPointSettings::SLEEP_SCREEN_MODE::BLANK; break;
        case 5: SETTINGS.sleepScreen = CrossPointSettings::SLEEP_SCREEN_MODE::QUICK_RESUME; break;
        case 6: SETTINGS.sleepScreen = CrossPointSettings::SLEEP_SCREEN_MODE::OVERLAY; break;
        case 7: SETTINGS.sleepScreen = CrossPointSettings::SLEEP_SCREEN_MODE::COVER_OVERLAY; break;
        case 8: SETTINGS.sleepScreen = CrossPointSettings::SLEEP_SCREEN_MODE::READING_STATS_SLEEP; break;
        case 9: SETTINGS.sleepScreen = CrossPointSettings::SLEEP_SCREEN_MODE::MINIMAL_STATS; break;
        case 10: SETTINGS.sleepScreen = CrossPointSettings::SLEEP_SCREEN_MODE::CLIPPING_COVER; break;
        case 11: SETTINGS.sleepScreen = CrossPointSettings::SLEEP_SCREEN_MODE::TODO_LIST; break;
        case 12: SETTINGS.sleepScreen = CrossPointSettings::SLEEP_SCREEN_MODE::READING_CALENDAR_SLEEP; break;
        case 13: SETTINGS.sleepScreen = CrossPointSettings::SLEEP_SCREEN_MODE::READING_SUMMARY_SLEEP; break;
        case 14: SETTINGS.sleepScreen = CrossPointSettings::SLEEP_SCREEN_MODE::COVER_PROGRESS; break;
        case 15: SETTINGS.sleepScreen = CrossPointSettings::SLEEP_SCREEN_MODE::RANDOM; break;
        case 16: SETTINGS.sleepScreen = CrossPointSettings::SLEEP_SCREEN_MODE::FLEXIBLE_COMPOSITE; break;
      }
      changed |= SETTINGS.sleepScreen != previous;
      ++applied;
    }
  }

  if (doc["fontFamily"].is<JsonVariant>()) {
    const int value = doc["fontFamily"].as<int>();
    const auto& families = sdFontSystem.registry().getFamilies();
    const int optionCount = static_cast<int>(CrossPointSettings::VISIBLE_BUILTIN_FONT_COUNT + families.size());
    if (value >= 0 && value < optionCount) {
      if (value < CrossPointSettings::VISIBLE_BUILTIN_FONT_COUNT) {
        changed |= SETTINGS.fontFamily != value || SETTINGS.sdFontFamilyName[0] != '\0';
        SETTINGS.fontFamily = static_cast<uint8_t>(value);
        SETTINGS.sdFontFamilyName[0] = '\0';
      } else {
        const size_t familyIndex = static_cast<size_t>(value - CrossPointSettings::VISIBLE_BUILTIN_FONT_COUNT);
        changed |= strncmp(SETTINGS.sdFontFamilyName, families[familyIndex].name.c_str(),
                           sizeof(SETTINGS.sdFontFamilyName)) != 0;
        strncpy(SETTINGS.sdFontFamilyName, families[familyIndex].name.c_str(), sizeof(SETTINGS.sdFontFamilyName) - 1);
        SETTINGS.sdFontFamilyName[sizeof(SETTINGS.sdFontFamilyName) - 1] = '\0';
      }
      ++applied;
    }
  }

  if (doc["dictionaryFontFamily"].is<JsonVariant>()) {
    const int value = doc["dictionaryFontFamily"].as<int>();
    if (value >= 0 && value < FontSelectionCompatibility::kVisibleDictionaryFontCount) {
      changed |= SETTINGS.dictionaryFontFamily != value;
      SETTINGS.dictionaryFontFamily = static_cast<uint8_t>(value);
      ++applied;
    }
  }

  if (changed) SETTINGS.saveToFile();

  LOG_DBG("WEB", "[SETTINGS] applied=%d complete free=%lu min=%lu largest=%lu", applied,
          static_cast<unsigned long>(ESP.getFreeHeap()), static_cast<unsigned long>(ESP.getMinFreeHeap()),
          static_cast<unsigned long>(ESP.getMaxAllocHeap()));
  server->send(200, "text/plain", String("Applied ") + String(applied) + " setting(s)");
}

// ---- OPDS Server API ----

void CrossPointWebServer::handleGetOpdsServers() const {
  const auto& servers = OPDS_STORE.getServers();

  // Stream JSON array incrementally to avoid allocating the full response in memory
  server->setContentLength(CONTENT_LENGTH_UNKNOWN);
  server->send(200, "application/json", "");
  server->sendContent("[");

  char output[512];
  constexpr size_t outputSize = sizeof(output);
  JsonDocument doc;

  for (size_t i = 0; i < servers.size(); i++) {
    doc.clear();
    doc["index"] = i;
    doc["name"] = servers[i].name;
    doc["url"] = servers[i].url;
    doc["username"] = servers[i].username;
    // Never expose passwords over the API — only indicate whether one is set
    doc["hasPassword"] = !servers[i].password.empty();

    const size_t written = serializeJson(doc, output, outputSize);
    if (written >= outputSize) continue;

    if (i > 0) server->sendContent(",");
    server->sendContent(output);
  }

  server->sendContent("]");
  server->sendContent("");
  LOG_DBG("WEB", "Served OPDS servers API (%zu servers)", servers.size());
}

void CrossPointWebServer::handlePostOpdsServer() {
  if (!server->hasArg("plain")) {
    server->send(400, "text/plain", "Missing JSON body");
    return;
  }

  const String body = server->arg("plain");
  JsonDocument doc;
  const DeserializationError err = deserializeJson(doc, body);
  if (err) {
    server->send(400, "text/plain", String("Invalid JSON: ") + err.c_str());
    return;
  }

  OpdsServer opdsServer;
  opdsServer.name = doc["name"] | std::string("");
  opdsServer.url = doc["url"] | std::string("");
  opdsServer.username = doc["username"] | std::string("");

  // The password field is optional in the JSON payload. When absent (vs. present but empty),
  // we preserve the existing password — the web UI omits it when the user hasn't changed it.
  bool hasPasswordField = doc["password"].is<const char*>() || doc["password"].is<std::string>();
  std::string password = doc["password"] | std::string("");

  if (doc["index"].is<int>()) {
    int idx = doc["index"].as<int>();
    if (idx < 0 || idx >= static_cast<int>(OPDS_STORE.getCount())) {
      server->send(400, "text/plain", "Invalid server index");
      return;
    }
    // Preserve existing password if not explicitly provided
    if (!hasPasswordField) {
      const auto* existing = OPDS_STORE.getServer(static_cast<size_t>(idx));
      if (existing) password = existing->password;
    }
    opdsServer.password = password;
    OPDS_STORE.updateServer(static_cast<size_t>(idx), opdsServer);
    LOG_DBG("WEB", "Updated OPDS server at index %d", idx);
  } else {
    opdsServer.password = password;
    if (!OPDS_STORE.addServer(opdsServer)) {
      server->send(400, "text/plain", "Cannot add server (limit reached)");
      return;
    }
    LOG_DBG("WEB", "Added new OPDS server: %s", opdsServer.name.c_str());
  }

  server->send(200, "text/plain", "OK");
}

// Uses POST (not HTTP DELETE) because ESP32 WebServer doesn't support DELETE with body.
void CrossPointWebServer::handleDeleteOpdsServer() {
  if (!server->hasArg("plain")) {
    server->send(400, "text/plain", "Missing JSON body");
    return;
  }

  const String body = server->arg("plain");
  JsonDocument doc;
  const DeserializationError err = deserializeJson(doc, body);
  if (err) {
    server->send(400, "text/plain", String("Invalid JSON: ") + err.c_str());
    return;
  }

  if (!doc["index"].is<int>()) {
    server->send(400, "text/plain", "Missing index");
    return;
  }

  int idx = doc["index"].as<int>();
  if (idx < 0 || idx >= static_cast<int>(OPDS_STORE.getCount())) {
    server->send(400, "text/plain", "Invalid server index");
    return;
  }

  OPDS_STORE.removeServer(static_cast<size_t>(idx));
  LOG_DBG("WEB", "Deleted OPDS server at index %d", idx);
  server->send(200, "text/plain", "OK");
}

// ---- Wi-Fi Credentials API ----

void CrossPointWebServer::handleGetWifiNetworks() const {
  const auto& credentials = WIFI_STORE.getCredentials();
  const std::string& lastConnectedSsid = WIFI_STORE.getLastConnectedSsid();

  // Stream JSON array incrementally to avoid allocating the full response in memory
  server->setContentLength(CONTENT_LENGTH_UNKNOWN);
  server->send(200, "application/json", "");
  server->sendContent("[");

  char output[320];
  constexpr size_t outputSize = sizeof(output);
  JsonDocument doc;

  for (size_t i = 0; i < credentials.size(); i++) {
    doc.clear();
    doc["index"] = i;
    doc["ssid"] = credentials[i].ssid;
    // Never expose Wi-Fi passwords over the API — only indicate whether one is set
    doc["hasPassword"] = !credentials[i].password.empty();
    doc["isLastConnected"] = credentials[i].ssid == lastConnectedSsid;

    const size_t written = serializeJson(doc, output, outputSize);
    if (written >= outputSize) continue;

    if (i > 0) server->sendContent(",");
    server->sendContent(output);
  }

  server->sendContent("]");
  server->sendContent("");
  LOG_DBG("WEB", "Served Wi-Fi credentials API (%zu network(s))", credentials.size());
}

void CrossPointWebServer::handlePostWifiNetwork() {
  if (!server->hasArg("plain")) {
    server->send(400, "text/plain", "Missing JSON body");
    return;
  }

  const String body = server->arg("plain");
  JsonDocument doc;
  const DeserializationError err = deserializeJson(doc, body);
  if (err) {
    server->send(400, "text/plain", String("Invalid JSON: ") + err.c_str());
    return;
  }

  std::string ssid = doc["ssid"] | std::string("");
  if (ssid.empty()) {
    server->send(400, "text/plain", "SSID is required");
    return;
  }

  // The password field is optional in the JSON payload. When absent (vs. present but empty),
  // preserve the existing password for updates. Empty passwords are valid for open networks.
  bool hasPasswordField = doc["password"].is<const char*>() || doc["password"].is<std::string>();
  std::string password = doc["password"] | std::string("");

  if (doc["index"].is<int>()) {
    int idx = doc["index"].as<int>();
    const auto& credentials = WIFI_STORE.getCredentials();
    if (idx < 0 || idx >= static_cast<int>(credentials.size())) {
      server->send(400, "text/plain", "Invalid network index");
      return;
    }

    const std::string oldSsid = credentials[static_cast<size_t>(idx)].ssid;
    if (!hasPasswordField) {
      password = credentials[static_cast<size_t>(idx)].password;
    }

    bool ok = true;
    if (oldSsid != ssid) {
      ok = WIFI_STORE.removeCredential(oldSsid) && WIFI_STORE.addCredential(ssid, password);
    } else {
      ok = WIFI_STORE.addCredential(ssid, password);
    }

    if (!ok) {
      server->send(400, "text/plain", "Failed to update Wi-Fi network");
      return;
    }

    LOG_DBG("WEB", "Updated Wi-Fi network at index %d (SSID: %s)", idx, ssid.c_str());
  } else {
    if (!WIFI_STORE.addCredential(ssid, password)) {
      server->send(400, "text/plain", "Cannot add network (limit reached)");
      return;
    }
    LOG_DBG("WEB", "Added Wi-Fi network: %s", ssid.c_str());
  }

  server->send(200, "text/plain", "OK");
}

// Uses POST (not HTTP DELETE) because ESP32 WebServer doesn't support DELETE with body.
void CrossPointWebServer::handleDeleteWifiNetwork() {
  if (!server->hasArg("plain")) {
    server->send(400, "text/plain", "Missing JSON body");
    return;
  }

  const String body = server->arg("plain");
  JsonDocument doc;
  const DeserializationError err = deserializeJson(doc, body);
  if (err) {
    server->send(400, "text/plain", String("Invalid JSON: ") + err.c_str());
    return;
  }

  if (!doc["index"].is<int>()) {
    server->send(400, "text/plain", "Missing index");
    return;
  }

  int idx = doc["index"].as<int>();
  const auto& credentials = WIFI_STORE.getCredentials();
  if (idx < 0 || idx >= static_cast<int>(credentials.size())) {
    server->send(400, "text/plain", "Invalid network index");
    return;
  }

  const std::string ssid = credentials[static_cast<size_t>(idx)].ssid;
  if (!WIFI_STORE.removeCredential(ssid)) {
    server->send(400, "text/plain", "Failed to delete Wi-Fi network");
    return;
  }

  LOG_DBG("WEB", "Deleted Wi-Fi network at index %d (SSID: %s)", idx, ssid.c_str());
  server->send(200, "text/plain", "OK");
}

namespace {
bool validWeatherCoordinate(const char* text, const double minValue, const double maxValue) {
  if (!text || !*text) return false;
  char* end = nullptr;
  const double value = std::strtod(text, &end);
  return end != text && *end == '\0' && std::isfinite(value) && value >= minValue && value <= maxValue;
}
}  // namespace

void CrossPointWebServer::handleGetClockWeather() const {
  JsonDocument doc;
  doc["location"] = WEATHER_STORE.location;
  doc["latitude"] = WEATHER_STORE.latitude;
  doc["longitude"] = WEATHER_STORE.longitude;
  doc["timezone"] = WEATHER_STORE.timezone;
  doc["utcOffsetSeconds"] = WEATHER_STORE.locationUtcOffsetSeconds;
  doc["fahrenheit"] = WEATHER_STORE.fahrenheit != 0;
  // Legacy compatibility field. Clock sync is now always attempted once per
  // station Wi-Fi session; expose the effective behavior, not the old opt-out.
  doc["clockSyncEnabled"] = true;
  doc["weatherSyncEnabled"] = SETTINGS.weatherSyncEnabled != 0;
  doc["wifiConnected"] = WiFi.status() == WL_CONNECTED;
  doc["clockLastSyncDate"] = WEATHER_STORE.lastClockSyncDateKey;
  doc["weatherLastSyncDate"] = WEATHER_STORE.lastWeatherSyncDateKey;
  doc["clockLastSyncEpoch"] = WEATHER_STORE.lastClockSyncEpoch;
  doc["weatherLastSyncEpoch"] = WEATHER_STORE.lastWeatherSyncEpoch;
  doc["hasWeather"] = WEATHER_STORE.hasWeather != 0;
  if (WEATHER_STORE.hasWeather) {
    doc["temperature"] = WEATHER_STORE.temperatureTenths / 10.0f;
    doc["weatherCode"] = WEATHER_STORE.weatherCode;
    doc["description"] = ClockWeatherSyncService::weatherDescription(WEATHER_STORE.weatherCode);
  }
  String response;
  serializeJson(doc, response);
  server->send(200, "application/json", response);
}

void CrossPointWebServer::handlePostClockWeather() {
  if (!server->hasArg("plain")) {
    server->send(400, "text/plain", "Missing JSON body");
    return;
  }

  JsonDocument doc;
  const DeserializationError error = deserializeJson(doc, server->arg("plain"));
  if (error) {
    server->send(400, "text/plain", String("Invalid JSON: ") + error.c_str());
    return;
  }

  if (!doc["location"].isNull()) {
    const char* value = doc["location"] | "";
    if (strlen(value) >= sizeof(WEATHER_STORE.location)) {
      server->send(400, "text/plain", "Location is too long");
      return;
    }
    strncpy(WEATHER_STORE.location, value, sizeof(WEATHER_STORE.location) - 1);
    WEATHER_STORE.location[sizeof(WEATHER_STORE.location) - 1] = '\0';
  }
  if (!doc["latitude"].isNull()) {
    const char* value = doc["latitude"] | "";
    if (strlen(value) >= sizeof(WEATHER_STORE.latitude) || !validWeatherCoordinate(value, -90.0, 90.0)) {
      server->send(400, "text/plain", "Invalid latitude");
      return;
    }
    strncpy(WEATHER_STORE.latitude, value, sizeof(WEATHER_STORE.latitude) - 1);
    WEATHER_STORE.latitude[sizeof(WEATHER_STORE.latitude) - 1] = '\0';
  }
  if (!doc["longitude"].isNull()) {
    const char* value = doc["longitude"] | "";
    if (strlen(value) >= sizeof(WEATHER_STORE.longitude) || !validWeatherCoordinate(value, -180.0, 180.0)) {
      server->send(400, "text/plain", "Invalid longitude");
      return;
    }
    strncpy(WEATHER_STORE.longitude, value, sizeof(WEATHER_STORE.longitude) - 1);
    WEATHER_STORE.longitude[sizeof(WEATHER_STORE.longitude) - 1] = '\0';
  }
  if (!doc["fahrenheit"].isNull()) WEATHER_STORE.fahrenheit = (doc["fahrenheit"] | false) ? 1 : 0;
  // Ignore the legacy clockSyncEnabled field if an older client still posts
  // it. Keep the persisted member for settings-file compatibility only.
  if (!doc["weatherSyncEnabled"].isNull()) SETTINGS.weatherSyncEnabled = (doc["weatherSyncEnabled"] | false) ? 1 : 0;

  WEATHER_STORE.saveToFile();
  SETTINGS.saveToFile();

  const bool shouldSync = doc["sync"] | false;
  ClockWeatherSyncResult result;
  if (shouldSync) {
    if (WiFi.status() != WL_CONNECTED) {
      server->send(409, "text/plain", "Wi-Fi is not connected");
      return;
    }
    // Keep Wi-Fi alive: this request is being served by the web session.
    result = ClockWeatherSyncService::sync(true, true);
  }

  JsonDocument response;
  response["ok"] = true;
  response["clockSynced"] = result.clockSynced;
  response["weatherSynced"] = result.weatherSynced;
  response["weatherSkippedNoLocation"] = result.weatherSkippedNoLocation;
  response["failed"] = result.failed;
  String output;
  serializeJson(response, output);
  server->send(200, "application/json", output);
}

void CrossPointWebServer::handleToDoPage() const {
  // ToDoPageHtml is gzip-compressed like the other embedded pages.  Set the
  // encoding header through the shared helper so browsers inflate it instead
  // of displaying the compressed bytes as plain text.
  sendHtmlContent(server.get(), ToDoPageHtml, ToDoPageHtmlCompressedSize);
}

void CrossPointWebServer::handleGetToDo() const {
  JsonDocument response;
  JsonArray items = response["items"].to<JsonArray>();
  for (const auto& item : TODO_STORE.getItems()) {
    JsonObject row = items.add<JsonObject>();
    row["id"] = item.id;
    row["text"] = item.text;
    row["completed"] = item.completed;
    row["priority"] = item.priority;
    row["order"] = item.order;
    row["createdAt"] = item.createdAt;
    row["updatedAt"] = item.updatedAt;
  }
  String output;
  serializeJson(response, output);
  server->send(200, "application/json", output);
}

void CrossPointWebServer::handlePostToDo() {
  if (!server->hasArg("plain")) {
    server->send(400, "application/json", "{\"error\":\"Missing JSON body\"}");
    return;
  }
  JsonDocument doc;
  const DeserializationError error = deserializeJson(doc, server->arg("plain"));
  if (error) {
    server->send(400, "application/json", String("{\"error\":\"") + error.c_str() + "\"}");
    return;
  }
  const std::string action = doc["action"] | "";
  bool ok = false;
  if (action == "add") {
    ok = TODO_STORE.add(doc["text"] | "");
  } else if (action == "update") {
    ok = TODO_STORE.update(doc["id"] | 0, doc["text"] | "", doc["completed"] | false);
  } else if (action == "toggle") {
    ok = TODO_STORE.toggle(doc["id"] | 0);
  } else if (action == "togglePriority") {
    ok = TODO_STORE.togglePriority(doc["id"] | 0);
  } else if (action == "delete") {
    ok = TODO_STORE.remove(doc["id"] | 0);
  } else if (action == "move") {
    ok = TODO_STORE.move(doc["id"] | 0, doc["direction"] | 0);
  } else if (action == "clearCompleted") {
    ok = TODO_STORE.clearCompleted();
  }
  if (!ok) {
    server->send(400, "application/json", "{\"error\":\"Invalid or failed to-do action\"}");
    return;
  }
  server->send(200, "application/json", "{\"ok\":true}");
}

// WebSocket callback trampoline
void CrossPointWebServer::wsEventCallback(uint8_t num, WStype_t type, uint8_t* payload, size_t length) {
  if (wsInstance && wsInstance->running) {
    wsInstance->onWebSocketEvent(num, type, payload, length);
  }
}

// WebSocket event handler for fast binary uploads
// Protocol:
//   1. Client sends TEXT message: "START:<filename>:<size>:<path>"
//   2. Client sends BINARY messages with file data chunks
//   3. Server sends TEXT "PROGRESS:<received>:<total>" after each chunk
//   4. Server sends TEXT "DONE" or "ERROR:<message>" when complete
void CrossPointWebServer::onWebSocketEvent(uint8_t num, WStype_t type, uint8_t* payload, size_t length) {
  if (!running) return;
  switch (type) {
    case WStype_DISCONNECTED:
      LOG_DBG("WS", "Client %u disconnected uploadClient=%u inProgress=%d free=%lu min=%lu largest=%lu", num,
              wsUploadClientNum, wsUploadInProgress, static_cast<unsigned long>(ESP.getFreeHeap()),
              static_cast<unsigned long>(ESP.getMinFreeHeap()), static_cast<unsigned long>(ESP.getMaxAllocHeap()));
      // Only clean up if this is the client that owns the active upload.
      // A new client may have already started a fresh upload before this
      // DISCONNECTED event fires (race condition on quick cancel + retry).
      if (num == wsUploadClientNum && wsUploadInProgress && wsUploadFile) {
        abortWsUpload("WS");
      }
      break;

    case WStype_CONNECTED: {
      LOG_DBG("WS", "Client %u connected free=%lu min=%lu largest=%lu", num,
              static_cast<unsigned long>(ESP.getFreeHeap()), static_cast<unsigned long>(ESP.getMinFreeHeap()),
              static_cast<unsigned long>(ESP.getMaxAllocHeap()));
      break;
    }

    case WStype_TEXT: {
      // Parse control messages
      String msg = String((char*)payload);
      LOG_DBG("WS", "Text from client %u: %s", num, msg.c_str());

      if (msg.startsWith("START:")) {
        // Reject any START while an upload is already active to prevent
        // leaking the open wsUploadFile handle (owning client re-START included)
        if (wsUploadInProgress) {
          wsServer->sendTXT(num, "ERROR:Upload already in progress");
          break;
        }

        // Parse: START:<filename>:<size>:<path>
        int firstColon = msg.indexOf(':', 6);
        int secondColon = msg.indexOf(':', firstColon + 1);

        if (firstColon > 0 && secondColon > 0) {
          wsUploadFileName = msg.substring(6, firstColon);
          String sizeToken = msg.substring(firstColon + 1, secondColon);
          bool sizeValid = sizeToken.length() > 0;
          int digitStart = (sizeValid && sizeToken[0] == '+') ? 1 : 0;
          if (digitStart > 0 && sizeToken.length() < 2) sizeValid = false;
          for (int i = digitStart; i < (int)sizeToken.length() && sizeValid; i++) {
            if (!isdigit((unsigned char)sizeToken[i])) sizeValid = false;
          }
          if (!sizeValid) {
            LOG_DBG("WS", "START rejected: invalid size token '%s'", sizeToken.c_str());
            wsServer->sendTXT(num, "ERROR:Invalid START format");
            return;
          }
          wsUploadSize = sizeToken.toInt();
          wsUploadPath = msg.substring(secondColon + 1);
          wsUploadReceived = 0;
          wsLastProgressSent = 0;
          wsUploadStartTime = millis();

          // Ensure path is valid
          if (!wsUploadPath.startsWith("/")) wsUploadPath = "/" + wsUploadPath;
          if (wsUploadPath.length() > 1 && wsUploadPath.endsWith("/")) {
            wsUploadPath = wsUploadPath.substring(0, wsUploadPath.length() - 1);
          }

          String filePath = wsUploadPath;
          if (!filePath.endsWith("/")) filePath += "/";
          filePath += wsUploadFileName;

          resetTaskWatchdogIfSubscribed();
          if (Storage.exists(filePath.c_str())) {
            LOG_DBG("WS", "Upload collision: %s", filePath.c_str());
            wsServer->sendTXT(num, "ERROR:File already exists: " + wsUploadFileName);
            return;
          }

          LOG_DBG("WS", "Starting upload: %s (%d bytes) to %s client=%u free=%lu min=%lu largest=%lu",
                  wsUploadFileName.c_str(), wsUploadSize, filePath.c_str(), num, static_cast<unsigned long>(ESP.getFreeHeap()),
                  static_cast<unsigned long>(ESP.getMinFreeHeap()), static_cast<unsigned long>(ESP.getMaxAllocHeap()));

          // Open file for writing
          resetTaskWatchdogIfSubscribed();
          if (!Storage.openFileForWrite("WS", filePath, wsUploadFile)) {
            wsServer->sendTXT(num, "ERROR:Failed to create file");
            wsUploadInProgress = false;
            wsUploadClientNum = 255;
            return;
          }
          resetTaskWatchdogIfSubscribed();
          LOG_DBG("WS", "Upload file opened path=%s client=%u free=%lu min=%lu largest=%lu", filePath.c_str(), num,
                  static_cast<unsigned long>(ESP.getFreeHeap()), static_cast<unsigned long>(ESP.getMinFreeHeap()),
                  static_cast<unsigned long>(ESP.getMaxAllocHeap()));

          // Zero-byte upload: complete immediately without waiting for BIN frames
          if (wsUploadSize == 0) {
            // Explicit close() required: file-scope global persists beyond function scope
            closeFileIfOpen(wsUploadFile);
            wsLastCompleteName = wsUploadFileName;
            wsLastCompleteSize = 0;
            wsLastCompleteAt = millis();
            LOG_DBG("WS", "Zero-byte upload complete: %s free=%lu min=%lu largest=%lu", filePath.c_str(),
                    static_cast<unsigned long>(ESP.getFreeHeap()), static_cast<unsigned long>(ESP.getMinFreeHeap()),
                    static_cast<unsigned long>(ESP.getMaxAllocHeap()));
            LOG_DBG("WEB", "[BOOKCACHE] invalidate after WebSocket upload path=%s free=%lu min=%lu largest=%lu",
                    filePath.c_str(), static_cast<unsigned long>(ESP.getFreeHeap()),
                    static_cast<unsigned long>(ESP.getMinFreeHeap()), static_cast<unsigned long>(ESP.getMaxAllocHeap()));
            clearBookCache(filePath.c_str());
            LOG_DBG("WEB", "[BOOKCACHE] invalidate complete after WebSocket upload path=%s free=%lu min=%lu largest=%lu",
                    filePath.c_str(), static_cast<unsigned long>(ESP.getFreeHeap()),
                    static_cast<unsigned long>(ESP.getMinFreeHeap()), static_cast<unsigned long>(ESP.getMaxAllocHeap()));
            wsServer->sendTXT(num, "DONE");
            wsLastProgressSent = 0;
            wsUploadSize = 0;
            wsUploadReceived = 0;
            wsUploadStartTime = 0;
            wsUploadFileName = String();
            wsUploadPath = String();
            break;
          }

          wsUploadClientNum = num;
          wsUploadInProgress = true;
          wsServer->sendTXT(num, "READY");
        } else {
          wsServer->sendTXT(num, "ERROR:Invalid START format");
        }
      }
      break;
    }

    case WStype_BIN: {
      if (!wsUploadInProgress || !wsUploadFile || num != wsUploadClientNum) {
        wsServer->sendTXT(num, "ERROR:No upload in progress");
        return;
      }

      // Write binary data directly to file
      size_t remaining = wsUploadSize - wsUploadReceived;
      if (length > remaining) {
        abortWsUpload("WS");
        wsServer->sendTXT(num, "ERROR:Upload overflow");
        return;
      }
      resetTaskWatchdogIfSubscribed();
      size_t written = wsUploadFile.write(payload, length);
      resetTaskWatchdogIfSubscribed();
      yield();

      if (written != length) {
        abortWsUpload("WS");
        wsServer->sendTXT(num, "ERROR:Write failed - disk full?");
        return;
      }

      wsUploadReceived += written;

      // Send progress update (every 64KB or at end)
      if (wsUploadReceived - wsLastProgressSent >= 65536 || wsUploadReceived >= wsUploadSize) {
        String progress = "PROGRESS:" + String(wsUploadReceived) + ":" + String(wsUploadSize);
        wsServer->sendTXT(num, progress);
        wsLastProgressSent = wsUploadReceived;
        LOG_DBG("WS", "Upload progress client=%u received=%lu total=%lu free=%lu min=%lu largest=%lu", num,
                static_cast<unsigned long>(wsUploadReceived), static_cast<unsigned long>(wsUploadSize),
                static_cast<unsigned long>(ESP.getFreeHeap()), static_cast<unsigned long>(ESP.getMinFreeHeap()),
                static_cast<unsigned long>(ESP.getMaxAllocHeap()));
      }

      // Check if upload complete
      if (wsUploadReceived >= wsUploadSize) {
        // Explicit close() required: file-scope global persists beyond function scope
        closeFileIfOpen(wsUploadFile);
        wsUploadInProgress = false;
        wsUploadClientNum = 255;

        wsLastCompleteName = wsUploadFileName;
        wsLastCompleteSize = wsUploadSize;
        wsLastCompleteAt = millis();

        unsigned long elapsed = millis() - wsUploadStartTime;
        float kbps = (elapsed > 0) ? (wsUploadSize / 1024.0) / (elapsed / 1000.0) : 0;

        LOG_DBG("WS", "Upload complete: %s (%d bytes in %lu ms, %.1f KB/s) free=%lu min=%lu largest=%lu",
                wsUploadFileName.c_str(), wsUploadSize, elapsed, kbps, static_cast<unsigned long>(ESP.getFreeHeap()),
                static_cast<unsigned long>(ESP.getMinFreeHeap()), static_cast<unsigned long>(ESP.getMaxAllocHeap()));

        // Clear book cache after uploading the file
        String filePath = wsUploadPath;
        if (!filePath.endsWith("/")) filePath += "/";
        filePath += wsUploadFileName;
        LOG_DBG("WEB", "[BOOKCACHE] invalidate after WebSocket upload path=%s free=%lu min=%lu largest=%lu",
                filePath.c_str(), static_cast<unsigned long>(ESP.getFreeHeap()),
                static_cast<unsigned long>(ESP.getMinFreeHeap()), static_cast<unsigned long>(ESP.getMaxAllocHeap()));
        clearBookCache(filePath.c_str());
        LOG_DBG("WEB", "[BOOKCACHE] invalidate complete after WebSocket upload path=%s free=%lu min=%lu largest=%lu",
                filePath.c_str(), static_cast<unsigned long>(ESP.getFreeHeap()),
                static_cast<unsigned long>(ESP.getMinFreeHeap()), static_cast<unsigned long>(ESP.getMaxAllocHeap()));

        wsServer->sendTXT(num, "DONE");
        wsLastProgressSent = 0;
        wsUploadSize = 0;
        wsUploadReceived = 0;
        wsUploadStartTime = 0;
        wsUploadFileName = String();
        wsUploadPath = String();
      }
      break;
    }

    default:
      break;
  }
}

// --- Font management handlers ---

void CrossPointWebServer::handleFontsPage() const {
  sendHtmlContent(server.get(), FontsPageHtml, sizeof(FontsPageHtml));
  LOG_DBG("WEB", "Served fonts page");
}

void CrossPointWebServer::handleFontList() const {
  // Pick up any uploads/deletes that happened since the last reader load.
  const_cast<SdCardFontSystem&>(sdFontSystem).refreshIfDirty();
  const auto& families = sdFontSystem.registry().getFamilies();

  JsonDocument doc;
  JsonArray arr = doc["families"].to<JsonArray>();
  doc["maxFamilies"] = SdCardFontRegistry::MAX_SD_FAMILIES;

  for (const auto& family : families) {
    JsonObject fObj = arr.add<JsonObject>();
    fObj["name"] = family.name;

    JsonArray sizes = fObj["sizes"].to<JsonArray>();
    for (uint8_t s : family.availableSizes()) {
      sizes.add(s);
    }

    JsonArray files = fObj["files"].to<JsonArray>();
    for (const auto& file : family.files) {
      JsonObject fileObj = files.add<JsonObject>();
      // Extract filename from full path
      const char* name = strrchr(file.path.c_str(), '/');
      fileObj["name"] = name ? name + 1 : file.path.c_str();

      // Stat the file for size
      HalFile f;
      if (Storage.openFileForRead("WEB", file.path.c_str(), f)) {
        fileObj["size"] = static_cast<unsigned long>(f.size());
        f.close();
      } else {
        fileObj["size"] = 0;
      }
    }
  }

  String json;
  serializeJson(doc, json);
  server->send(200, "application/json", json);
}

void CrossPointWebServer::handleFontUploadData() {
  HTTPUpload& upload = server->upload();

  switch (upload.status) {
    case UPLOAD_FILE_START: {
      resetTaskWatchdogIfSubscribed();
      String family = server->arg("family");
      fontUpload.file = HalFile();
      fontUpload.familyName.clear();
      fontUpload.filePath.clear();
      fontUpload.valid = false;
      fontUpload.magicChecked = false;
      fontUpload.bytesWritten = 0;
      fontUpload.bufferPos = 0;

      if (!FontInstaller::isValidFamilyName(family.c_str())) {
        LOG_ERR("WEB", "Invalid font family name: %s", family.c_str());
        fontUpload.releaseBuffer();
        break;
      }

      String filename = upload.filename;
      filename.replace(' ', '_');
      // Validate filename: rejects path traversal (../, /, \) and enforces
      // a .cpfont basename of alphanumeric + hyphen + underscore. Without
      // this an attacker could supply "../../.crosspoint/settings.json" as
      // a "filename" and have it written outside the fonts directory.
      if (!FontInstaller::isValidCpfontFilename(filename.c_str())) {
        LOG_ERR("WEB", "Invalid font filename: %s", filename.c_str());
        fontUpload.releaseBuffer();
        break;
      }

      fontUpload.familyName = family.c_str();

      // Create a temporary FontInstaller for directory creation
      FontInstaller installer(sdFontSystem.registry());
      if (!installer.ensureFamilyDir(family.c_str())) {
        LOG_ERR("WEB", "Failed to create font family dir");
        fontUpload.releaseBuffer();
        break;
      }

      char path[128];
      FontInstaller::buildFontPath(family.c_str(), filename.c_str(), path, sizeof(path));
      fontUpload.filePath = path;

      if (!Storage.openFileForWrite("WEB", path, fontUpload.file)) {
        LOG_ERR("WEB", "Failed to open font file for write: %s", path);
        fontUpload.releaseBuffer();
        break;
      }

      if (!fontUpload.ensureBuffer()) {
        closeFileIfOpen(fontUpload.file);
        Storage.remove(path);
        LOG_ERR("WEB", "Failed to allocate font upload buffer");
        break;
      }

      fontUpload.valid = true;
      LOG_DBG("WEB", "Font upload started: %s -> %s", filename.c_str(), path);
      break;
    }

    case UPLOAD_FILE_WRITE: {
      if (!fontUpload.valid) break;
      resetTaskWatchdogIfSubscribed();

      // Validate magic bytes on first chunk only
      if (!fontUpload.magicChecked && upload.currentSize >= 8) {
        if (memcmp(upload.buf, "CPFONT\0\0", 8) != 0) {
          LOG_ERR("WEB", "Invalid .cpfont magic bytes");
          fontUpload.valid = false;
          break;
        }
        fontUpload.magicChecked = true;
      }

      // Buffer writes for efficiency
      size_t remaining = upload.currentSize;
      const uint8_t* src = upload.buf;
      while (remaining > 0) {
        size_t space = FontUploadState::BUFFER_SIZE - fontUpload.bufferPos;
        size_t chunk = (remaining < space) ? remaining : space;
        memcpy(fontUpload.buffer.get() + fontUpload.bufferPos, src, chunk);
        fontUpload.bufferPos += chunk;
        src += chunk;
        remaining -= chunk;

        if (fontUpload.bufferPos >= FontUploadState::BUFFER_SIZE) {
          fontUpload.file.write(fontUpload.buffer.get(), fontUpload.bufferPos);
          fontUpload.bytesWritten += fontUpload.bufferPos;
          fontUpload.bufferPos = 0;
          resetTaskWatchdogIfSubscribed();
        }
      }
      break;
    }

    case UPLOAD_FILE_END: {
      // Flush remaining buffer
      if (fontUpload.valid && fontUpload.bufferPos > 0) {
        fontUpload.file.write(fontUpload.buffer.get(), fontUpload.bufferPos);
        fontUpload.bytesWritten += fontUpload.bufferPos;
        fontUpload.bufferPos = 0;
      }
      closeFileIfOpen(fontUpload.file);

      if (!fontUpload.valid && !fontUpload.filePath.empty()) {
        Storage.remove(fontUpload.filePath.c_str());
      }

      LOG_DBG("WEB", "Font upload end: valid=%d, %zu bytes", fontUpload.valid, fontUpload.bytesWritten);
      fontUpload.releaseBuffer();
      break;
    }

    case UPLOAD_FILE_ABORTED: {
      closeFileIfOpen(fontUpload.file);
      if (!fontUpload.filePath.empty()) {
        Storage.remove(fontUpload.filePath.c_str());
      }
      fontUpload.valid = false;
      fontUpload.releaseBuffer();
      LOG_DBG("WEB", "Font upload aborted");
      break;
    }
  }
}

void CrossPointWebServer::handleFontUpload() {
  if (fontUpload.valid) {
    sdFontSystem.markRegistryDirty();
    server->send(200, "application/json", "{\"ok\":true}");
    LOG_DBG("WEB", "Font upload complete: %s", fontUpload.filePath.c_str());
  } else {
    server->send(400, "application/json", "{\"error\":\"Invalid .cpfont file\"}");
  }
  std::string().swap(fontUpload.familyName);
  std::string().swap(fontUpload.filePath);
  fontUpload.valid = false;
  fontUpload.magicChecked = false;
  fontUpload.bytesWritten = 0;
}

void CrossPointWebServer::handleFontDelete() {
  String body = server->arg("plain");
  JsonDocument doc;
  DeserializationError err = deserializeJson(doc, body);

  if (err || !doc["family"].is<const char*>()) {
    server->send(400, "application/json", "{\"error\":\"Invalid request\"}");
    return;
  }

  const char* familyName = doc["family"];
  FontInstaller installer(sdFontSystem.registry());
  auto result = installer.deleteFamily(familyName);

  if (result == FontInstaller::Error::OK) {
    sdFontSystem.markRegistryDirty();
    server->send(200, "application/json", "{\"ok\":true}");
    LOG_DBG("WEB", "Deleted font family: %s", familyName);
  } else {
    server->send(500, "application/json", "{\"error\":\"Delete failed\"}");
    LOG_ERR("WEB", "Failed to delete font family: %s", familyName);
  }
}
