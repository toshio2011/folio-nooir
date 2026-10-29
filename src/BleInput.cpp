#include "BleInput.h"

#include <HalPowerManager.h>
#include <I18n.h>
#include <Logging.h>
#if FREEINK_CAP_BLE_HID_HOST
#include <WiFi.h>
#include <esp_heap_caps.h>
#endif

#include <cstdio>
#include <cstdarg>
#include <cstring>
#include <algorithm>
#include <atomic>

#include "util/BleKeyMapping.h"

#if FREEINK_CAP_BLE_HID_HOST
#include "CrossPointSettings.h"
#include "activities/Activity.h"  // completes Activity for ActivityManager's unique_ptr members
#include "activities/RenderLock.h"
#include "util/BleMemoryPolicy.h"
#endif

namespace bleinput {

#if FREEINK_CAP_BLE_HID_HOST
namespace {

DiagnosticRecord g_diagnosticRing[kDiagnosticCapacity] = {};
portMUX_TYPE g_diagnosticMux = portMUX_INITIALIZER_UNLOCKED;
uint8_t g_diagnosticHead = 0;
uint8_t g_diagnosticCount = 0;
bool g_lifecycleKnown = false;
bool g_lastRunning = false;
bool g_lastConnected = false;
bool g_lastConnecting = false;
bool g_lastScanning = false;
uint8_t g_lastDeviceCount = 0;
bool g_suppressNextStopNotice = false;
uint32_t g_bleLowFreeHeap = UINT32_MAX;
uint32_t g_bleLowLargestFreeBlock = UINT32_MAX;
std::atomic<bool> g_lifecycleReevaluationPending{true};
std::atomic<bool> g_reAdmissionPending{true};
std::atomic<bool> g_readerBleReturnPending{false};
std::atomic<bool> g_readerBleReturnRetryBlocked{false};
std::atomic<uint32_t> g_readerBleReturnGeneration{0};
std::atomic<bool> g_webExclusiveActive{false};
std::atomic<bool> g_noDeviceTimeoutStoppedHost{false};
portMUX_TYPE g_noDeviceTimeoutMux = portMUX_INITIALIZER_UNLOCKED;
char g_pendingNotification[64] = {};
bool g_notificationPending = false;
bool g_noDeviceWindowActive = false;
uint32_t g_noDeviceWindowStartMs = 0;

#if NOOIR_BLE_DIAGNOSTICS
constexpr uint8_t kSerialDiagnosticCapacity = 96;
uint8_t g_serialDiagnosticRecords = 0;
char g_activityName[24] = "none";

void serialDiagnostic(const char* format, ...) {
  if (g_serialDiagnosticRecords >= kSerialDiagnosticCapacity) return;
  char line[168] = {};
  va_list args;
  va_start(args, format);
  vsnprintf(line, sizeof(line), format, args);
  va_end(args);
  line[sizeof(line) - 1] = '\0';
  LOG_INF("BLEDIAG", "%s", line);
  ++g_serialDiagnosticRecords;
}

const char* dispositionName(const InputDisposition disposition) {
  switch (disposition) {
    case InputDisposition::Mapped:
      return "mapped";
    case InputDisposition::Captured:
      return "capture";
    case InputDisposition::Pending:
      return "pending";
    case InputDisposition::Debounced:
      return "debounce";
    case InputDisposition::Unmapped:
      return "unmapped";
    default:
      return "decode";
  }
}

const char* phaseName(const freeink::KeyPhase phase) {
  switch (phase) {
    case freeink::KeyPhase::Press:
      return "press";
    case freeink::KeyPhase::Repeat:
      return "repeat";
    case freeink::KeyPhase::Release:
      return "release";
    default:
      return "unknown";
  }
}

void serialStateDiagnostic() {
  serialDiagnostic("state act=%.20s run=%d conn=%d scan=%d link=%s n=%u", g_activityName,
                   BleHid.isRunning() ? 1 : 0, BleHid.isConnected() ? 1 : 0, BleHid.isScanning() ? 1 : 0,
                   BleHid.connectedName(), static_cast<unsigned>(BleHid.deviceCount()));
}

const char* failureStage(const char* reason) {
  if (!reason) return "unknown";
  if (strstr(reason, "Pairing") || strstr(reason, "security")) return "security";
  if (strstr(reason, "HID") || strstr(reason, "report")) return "hid";
  if (strstr(reason, "Not a HID")) return "hid";
  return "link";
}

#if FREEINK_BLE_HID_RAW_DIAGNOSTICS
constexpr uint8_t kRawReportsPerPoll = 4;

char rawSourceName(const uint8_t source) {
  if (source == 1) return 'R';  // HID Report characteristic (0x2A4D)
  if (source == 2) return 'B';  // Boot keyboard input characteristic (0x2A22)
  return '?';
}

void serialRawReportDiagnostics() {
  for (uint8_t i = 0; i < kRawReportsPerPoll; ++i) {
    freeink::RawReportDiagnostic report;
    if (!BleHid.popRawReport(report)) return;

    // Keep each line comfortably below the logger's bounded line buffer. A
    // report longer than 20 bytes is continued with the same sequence number.
    constexpr uint8_t kBytesPerLine = 20;
    const uint8_t firstBytes = report.storedLength < kBytesPerLine ? report.storedLength : kBytesPerLine;
    char hex[ kBytesPerLine * 2 + 1 ] = {};
    for (uint8_t byte = 0; byte < firstBytes; ++byte) {
      snprintf(hex + byte * 2, sizeof(hex) - byte * 2, "%02X", static_cast<unsigned>(report.payload[byte]));
    }
    serialDiagnostic("raw q=%lu t=%lu dt=%u src=%c ty=%u id=%02X len=%u n=%u st=%02X x=%u b=%s",
                     static_cast<unsigned long>(report.sequence), static_cast<unsigned long>(report.uptimeMs),
                     static_cast<unsigned>(report.deltaMs), rawSourceName(report.source),
                     static_cast<unsigned>(report.reportType), static_cast<unsigned>(report.reportId),
                     static_cast<unsigned>(report.length), static_cast<unsigned>(report.storedLength),
                     static_cast<unsigned>(report.state), report.truncated ? 1u : 0u, hex);

    for (uint8_t offset = firstBytes; offset < report.storedLength; offset += kBytesPerLine) {
      const uint8_t bytes = report.storedLength - offset < kBytesPerLine ? report.storedLength - offset : kBytesPerLine;
      memset(hex, 0, sizeof(hex));
      for (uint8_t byte = 0; byte < bytes; ++byte) {
        snprintf(hex + byte * 2, sizeof(hex) - byte * 2, "%02X",
                 static_cast<unsigned>(report.payload[offset + byte]));
      }
      serialDiagnostic("raw+ q=%lu o=%u b=%s", static_cast<unsigned long>(report.sequence),
                       static_cast<unsigned>(offset), hex);
    }
  }
}
#endif
#endif

void sampleHeap() {
  const uint32_t freeHeap = static_cast<uint32_t>(ESP.getFreeHeap());
  const uint32_t largest = static_cast<uint32_t>(ESP.getMaxAllocHeap());
  g_bleLowFreeHeap = std::min(g_bleLowFreeHeap, freeHeap);
  g_bleLowLargestFreeBlock = std::min(g_bleLowLargestFreeBlock, largest);
}

void appendRecord(const DiagnosticType type, const uint8_t kind, const uint8_t value,
                  const uint8_t mappedButton) {
  sampleHeap();
  DiagnosticRecord next;
  next.uptimeMs = millis();
  next.freeHeap = static_cast<uint32_t>(ESP.getFreeHeap());
  next.minFreeHeap = static_cast<uint32_t>(ESP.getMinFreeHeap());
  next.largestFreeBlock = static_cast<uint32_t>(ESP.getMaxAllocHeap());
  next.bleLowFreeHeap = g_bleLowFreeHeap;
  next.bleLowLargestFreeBlock = g_bleLowLargestFreeBlock;
  next.type = type;
  next.keyKind = kind;
  next.keyValue = value;
  next.mappedButton = mappedButton;
  next.running = BleHid.isRunning();
  next.connected = BleHid.isConnected();
  next.connecting = BleHid.isConnecting();
  next.scanning = BleHid.isScanning();
  if (next.connected) strncpy(next.deviceName, BleHid.connectedName(), sizeof(next.deviceName) - 1);
  portENTER_CRITICAL(&g_diagnosticMux);
  g_diagnosticRing[g_diagnosticHead] = next;
  g_diagnosticHead = static_cast<uint8_t>((g_diagnosticHead + 1) % kDiagnosticCapacity);
  if (g_diagnosticCount < kDiagnosticCapacity) ++g_diagnosticCount;
  portEXIT_CRITICAL(&g_diagnosticMux);
}

void queueNotification(const char* text) {
  if (!text || !text[0]) return;
#if NOOIR_BLE_READER_COEXISTENCE
  // A connection popup requests a full ActivityManager render. Do not queue it
  // while Reader is anywhere on the activity stack; keep the connection event
  // in diagnostics and avoid rendering the Reader solely for this message.
  if (activityManager.isReaderActivity() && strncmp(text, "Bluetooth connected:", 20) == 0) {
#if NOOIR_BLE_DIAGNOSTICS
    serialDiagnostic("notification reader_suppressed type=connected");
#endif
    return;
  }
#endif
  strncpy(g_pendingNotification, text, sizeof(g_pendingNotification) - 1);
  g_pendingNotification[sizeof(g_pendingNotification) - 1] = '\0';
  g_notificationPending = true;
}

void resetNoDeviceWindow() {
  g_noDeviceWindowActive = false;
  g_noDeviceWindowStartMs = 0;
}

void suppressNoDeviceRetryUntilLifecycleBoundary() {
  portENTER_CRITICAL(&g_noDeviceTimeoutMux);
  g_noDeviceTimeoutStoppedHost.store(true, std::memory_order_relaxed);
  portEXIT_CRITICAL(&g_noDeviceTimeoutMux);
}

void updateNoDeviceWindow(const bool running, const bool connected) {
  const uint16_t timeoutSeconds = SETTINGS.bluetoothNoDeviceTimeoutSeconds;
  const bool hasConnectionOpportunity = BleHid.isConnecting() || BleHid.isScanning() || BleHid.pairedCount() > 0;
  if (!SETTINGS.bluetoothEnabled || timeoutSeconds == 0 || !running || connected ||
      !hasConnectionOpportunity || !connectionAdmissionAllowed()) {
    resetNoDeviceWindow();
    return;
  }

  const uint32_t now = millis();
  if (!g_noDeviceWindowActive) {
    g_noDeviceWindowActive = true;
    g_noDeviceWindowStartMs = now;
    return;
  }
  if (now - g_noDeviceWindowStartMs < static_cast<uint32_t>(timeoutSeconds) * 1000UL) return;

  // Stop retrying after the configured timeout, but retain the saved
  // preference so BLE can recover after a later lifecycle boundary.
  suppressNoDeviceRetryUntilLifecycleBoundary();
  resetNoDeviceWindow();
  stop();
  requestLifecycleReevaluation();
  LOG_INF("BLEMEM", "stage=no_device_timeout ms=%lu stopped=1 saved=%d",
          static_cast<unsigned long>(millis()), SETTINGS.bluetoothEnabled ? 1 : 0);
  queueNotification(I18N.get(StrId::STR_BT_TURNED_OFF_NO_DEVICE));
}

}  // namespace
#endif

void setActivityContext(const char* name) {
#if FREEINK_CAP_BLE_HID_HOST && NOOIR_BLE_DIAGNOSTICS
  if (name == nullptr || name[0] == '\0') name = "none";
  strncpy(g_activityName, name, sizeof(g_activityName) - 1);
  g_activityName[sizeof(g_activityName) - 1] = '\0';
#else
  (void)name;
#endif
}

void requestLifecycleReevaluation() {
#if FREEINK_CAP_BLE_HID_HOST
  // SETTINGS is a function-local static store. Its accessor may enter the
  // C++ initialization guard and block; never call it while interrupts are
  // masked by this critical section. The saved preference is only a snapshot:
  // a concurrent change is rechecked by the main lifecycle admission path.
  const bool bluetoothEnabled = SETTINGS.bluetoothEnabled != 0;
  portENTER_CRITICAL(&g_noDeviceTimeoutMux);
  if (!lifecycleReevaluationAllowed(bluetoothEnabled,
                                   g_webExclusiveActive.load(std::memory_order_acquire),
                                   g_noDeviceTimeoutStoppedHost.load(std::memory_order_relaxed))) {
    g_reAdmissionPending.store(false, std::memory_order_release);
    g_lifecycleReevaluationPending.store(false, std::memory_order_release);
    portEXIT_CRITICAL(&g_noDeviceTimeoutMux);
    return;
  }
  g_reAdmissionPending.store(true, std::memory_order_release);
  if (g_readerBleReturnPending.load(std::memory_order_acquire) &&
      g_readerBleReturnRetryBlocked.load(std::memory_order_acquire)) {
    portEXIT_CRITICAL(&g_noDeviceTimeoutMux);
    return;
  }
  g_lifecycleReevaluationPending.store(true, std::memory_order_release);
  portEXIT_CRITICAL(&g_noDeviceTimeoutMux);
#endif
}

bool takeLifecycleReevaluation() {
#if FREEINK_CAP_BLE_HID_HOST
  return g_lifecycleReevaluationPending.exchange(false, std::memory_order_acq_rel);
#else
  return false;
#endif
}

bool reAdmissionPending() {
#if FREEINK_CAP_BLE_HID_HOST
  return g_reAdmissionPending.load(std::memory_order_acquire);
#else
  return false;
#endif
}

void clearReAdmissionPending() {
#if FREEINK_CAP_BLE_HID_HOST
  g_reAdmissionPending.store(false, std::memory_order_release);
#endif
}

bool readerBleReturnPending() {
#if FREEINK_CAP_BLE_HID_HOST
  return SETTINGS.bluetoothEnabled && g_readerBleReturnPending.load(std::memory_order_acquire);
#else
  return false;
#endif
}

void clearReaderBleReturnPending() {
#if FREEINK_CAP_BLE_HID_HOST
  g_readerBleReturnPending.store(false, std::memory_order_release);
  g_readerBleReturnRetryBlocked.store(false, std::memory_order_release);
#endif
}

uint32_t readerBleReturnGeneration() {
#if FREEINK_CAP_BLE_HID_HOST
  return g_readerBleReturnGeneration.load(std::memory_order_acquire);
#else
  return 0;
#endif
}

void signalReaderBleReturnLifecycleEvent(const char* reason) {
#if FREEINK_CAP_BLE_HID_HOST
  if (!SETTINGS.bluetoothEnabled || !g_readerBleReturnPending.load(std::memory_order_acquire)) return;
  const uint32_t generation = g_readerBleReturnGeneration.fetch_add(1, std::memory_order_acq_rel) + 1;
  g_readerBleReturnRetryBlocked.store(false, std::memory_order_release);
  g_reAdmissionPending.store(true, std::memory_order_release);
  g_lifecycleReevaluationPending.store(true, std::memory_order_release);
  LOG_INF("BLEMEM", "stage=reader_return_event generation=%u reason=%.28s free=%u largest=%u",
          static_cast<unsigned>(generation), reason ? reason : "lifecycle", static_cast<unsigned>(ESP.getFreeHeap()),
          static_cast<unsigned>(ESP.getMaxAllocHeap()));
#else
  (void)reason;
#endif
}

void blockReaderBleReturnRetry() {
#if FREEINK_CAP_BLE_HID_HOST
  if (g_readerBleReturnPending.load(std::memory_order_acquire)) {
    g_readerBleReturnRetryBlocked.store(true, std::memory_order_release);
    g_lifecycleReevaluationPending.store(false, std::memory_order_release);
  }
#endif
}

void allowReaderBleReturnRetry() {
#if FREEINK_CAP_BLE_HID_HOST
  g_readerBleReturnRetryBlocked.store(false, std::memory_order_release);
#endif
}

void setWebExclusive(const bool active) {
#if FREEINK_CAP_BLE_HID_HOST
  // BLE Off is the BLE-less runtime mode. A Web activity with no live host
  // must not create a BLE-only lifecycle gate or pending-return state.
  if (active && !SETTINGS.bluetoothEnabled && !BleHid.isRunning()) return;
  const bool readerReturnWasPending = g_readerBleReturnPending.load(std::memory_order_acquire);
  g_webExclusiveActive.store(active, std::memory_order_release);
  if (active) {
    g_reAdmissionPending.store(false, std::memory_order_release);
    g_lifecycleReevaluationPending.store(false, std::memory_order_release);
    // Web owns the radio and heap until its exit boundary. Preserve a Reader
    // return intent across that boundary so a Web transition cannot erase the
    // only event-driven re-admission request.
    g_readerBleReturnRetryBlocked.store(false, std::memory_order_release);
  } else if (SETTINGS.bluetoothEnabled) {
    g_reAdmissionPending.store(true, std::memory_order_release);
    g_lifecycleReevaluationPending.store(true, std::memory_order_release);
    if (readerReturnWasPending) signalReaderBleReturnLifecycleEvent("web_exit");
  }
  LOG_INF("BLEMEM", "stage=web_exclusive active=%d reader_pending=%d generation=%u readmission=%d lifecycle=%d free=%u largest=%u",
          active ? 1 : 0, g_readerBleReturnPending.load(std::memory_order_acquire) ? 1 : 0,
          static_cast<unsigned>(g_readerBleReturnGeneration.load(std::memory_order_acquire)),
          g_reAdmissionPending.load(std::memory_order_acquire) ? 1 : 0,
          g_lifecycleReevaluationPending.load(std::memory_order_acquire) ? 1 : 0,
          static_cast<unsigned>(ESP.getFreeHeap()), static_cast<unsigned>(ESP.getMaxAllocHeap()));
#else
  (void)active;
#endif
}

bool webExclusiveActive() {
#if FREEINK_CAP_BLE_HID_HOST
  return g_webExclusiveActive.load(std::memory_order_acquire);
#else
  return false;
#endif
}

 #if FREEINK_CAP_BLE_HID_HOST && NOOIR_BLE_DIAGNOSTICS
void recordDiagnosticEvent(const char* format, ...) {
  if (!format) return;
  va_list args;
  va_start(args, format);
  char line[168] = {};
  vsnprintf(line, sizeof(line), format, args);
  va_end(args);
  serialDiagnostic("event %s", line);
}
#endif

bool connectionAdmissionAllowed() {
#if FREEINK_CAP_BLE_HID_HOST
  if (g_noDeviceTimeoutStoppedHost.load(std::memory_order_acquire) ||
      g_webExclusiveActive.load(std::memory_order_acquire) || !SETTINGS.bluetoothEnabled ||
      !activityManager.bluetoothShouldBeActive() || WiFi.getMode() != WIFI_MODE_NULL)
    return false;

  const bool readerContext = activityManager.isReaderActivity();
  const bool resourceSensitive = activityManager.bluetoothResourceSensitive();
  const bool readerRenderWorkPending =
      readerContext && (RenderLock::peek() || activityManager.hasPendingRenderWork());
  if (resourceSensitive || !readerBleAdmissionRenderWorkSafe(readerContext, readerRenderWorkPending))
    return false;
  // The heap floors admit a cold NimBLE host start, not a connection on a host
  // that already owns its memory. Keep Reader contexts on their existing
  // pressure policy above; an initialized host does not pay the cold-start
  // floor a second time merely because NimBLE itself consumed startup headroom.
  if (BleHid.isRunning()) return true;
  const char* activityName = activityManager.currentActivityName();
  const bool homeContext = strcmp(activityName, "Home") == 0 || strcmp(activityName, "RecentBooks") == 0;
  const bool homeRenderWorkPending = homeContext && (RenderLock::peek() || activityManager.hasPendingRenderWork());
  if (!homeBleColdStartRenderWorkSafe(homeContext, homeRenderWorkPending)) return false;
  // Every cold start still uses the strict global floors except a confirmed
  // idle EPUB return. This margin is unavailable to Reader entry, builds,
  // renders, image/grayscale work, other Reader formats, and Settings.
  const bool quiescentEpubReturn =
      readerContext && activityManager.isCurrentReaderActivity() &&
      strcmp(activityManager.currentActivityName(), "EpubReader") == 0 && readerBleReturnPending() &&
      !resourceSensitive && !RenderLock::peek() && !activityManager.hasPendingRenderWork();
  return readerBleStartMemoryAdmittedForQuiescentEpubReturn(
      static_cast<size_t>(ESP.getFreeHeap()), static_cast<size_t>(ESP.getMaxAllocHeap()), quiescentEpubReturn);
#else
  // Keep the existing BLE-off stub behavior; callers still receive the
  // normal begin/connect failure from the capability-safe host stubs.
  return true;
#endif
}

void setBluetoothEnabled(const bool enabled) {
#if FREEINK_CAP_BLE_HID_HOST
  clearNoDeviceTimeoutSuppression();
  SETTINGS.bluetoothEnabled = enabled ? 1 : 0;
  resetNoDeviceWindow();
  requestLifecycleReevaluation();
  if (!enabled) {
    clearReaderBleReturnPending();
    stop();
    SETTINGS.saveToFile();
    return;
  }
  // Persist intent only. The main lifecycle performs admission, Wi-Fi,
  // activity, and RenderLock checks before any cold initialization.
  SETTINGS.saveToFile();
#else
  (void)enabled;
#endif
}

void toggleBluetooth() {
#if FREEINK_CAP_BLE_HID_HOST
  setBluetoothEnabled(!SETTINGS.bluetoothEnabled);
#endif
}

void clearNoDeviceTimeoutSuppression() {
#if FREEINK_CAP_BLE_HID_HOST
  portENTER_CRITICAL(&g_noDeviceTimeoutMux);
  const bool wasSuppressed = g_noDeviceTimeoutStoppedHost.exchange(false, std::memory_order_relaxed);
  portEXIT_CRITICAL(&g_noDeviceTimeoutMux);
  if (wasSuppressed) LOG_INF("BLEMEM", "stage=no_device_timeout_retry_unblocked boundary=explicit_or_activity");
#endif
}

bool bluetoothEnabled() {
#if FREEINK_CAP_BLE_HID_HOST
  return SETTINGS.bluetoothEnabled != 0;
#else
  return false;
#endif
}

// NimBLE controller init/deinit hang (interrupt WDT) if run at the 10 MHz low-power
// frequency, so force normal CPU speed around both. Centralized here so every caller
// (boot restore, settings toggle, reader toggle, sleep) is covered automatically.
bool ensureStarted() {
#if FREEINK_CAP_BLE_HID_HOST
  // Settings OFF is the BLE-less runtime mode. Do not even emit admission
  // diagnostics or reach the NimBLE begin path when the user has disabled it.
  if (!SETTINGS.bluetoothEnabled) return false;
  BleHid.setConnectionPermissionCallback(&connectionAdmissionAllowed);
  // All start callers, including Bluetooth Settings, pass through this gate.
  // A Reader may remain on the activity stack while a modal settings screen is
  // visible, so the same normal heap policy must apply there and after a true
  // Reader exit as well.
  const bool readerContext = activityManager.isReaderActivity();
  LOG_INF("BLEMEM", "stage=before_ensure_admission activity=%.20s host_running=%d free=%u largest=%u floors=%u/%u",
          activityManager.currentActivityName(), BleHid.isRunning() ? 1 : 0, ESP.getFreeHeap(), ESP.getMaxAllocHeap(),
          static_cast<unsigned>(kReaderBleStartMinFreeHeap),
          static_cast<unsigned>(kReaderBleStartMinLargestBlock));
  if (!connectionAdmissionAllowed()) {
#if NOOIR_BLE_DIAGNOSTICS
    recordDiagnosticEvent("ble_admit result=defer source=ensure reader=%d sensitive=%d lock=%d free=%u max=%u",
                          readerContext ? 1 : 0, activityManager.bluetoothResourceSensitive() ? 1 : 0,
                          RenderLock::peek() ? 1 : 0, ESP.getFreeHeap(), ESP.getMaxAllocHeap());
#endif
    return false;
  }
  LOG_INF("BLEMEM", "stage=ensure_admission result=allow source=ensure activity=%.20s free=%u largest=%u",
          activityManager.currentActivityName(), ESP.getFreeHeap(), ESP.getMaxAllocHeap());
  BleHid.setInputPreset(static_cast<freeink::InputPreset>(SETTINGS.bleControllerPreset));
#endif
  const uint32_t beforeFree = static_cast<uint32_t>(ESP.getFreeHeap());
  const uint32_t beforeLargest = static_cast<uint32_t>(ESP.getMaxAllocHeap());
  const unsigned long startedMs = millis();
  HalPowerManager::Lock powerLock;
#if FREEINK_CAP_BLE_HID_HOST
  LOG_INF("BLEMEM", "stage=before_begin free=%u largest=%u", beforeFree, beforeLargest);
#endif
#if FREEINK_CAP_BLE_HID_HOST && NOOIR_BLE_DIAGNOSTICS && FREEINK_BLE_HID_RAW_DIAGNOSTICS
  BleHid.clearRawReports();
#endif
  const bool started = BleHid.begin(kHostName);
#if FREEINK_CAP_BLE_HID_HOST
  LOG_INF("BLEMEM", "stage=begin_return result=%d free=%u largest=%u", started ? 1 : 0, ESP.getFreeHeap(),
          ESP.getMaxAllocHeap());
#endif
#if FREEINK_CAP_BLE_HID_HOST && NOOIR_BLE_DIAGNOSTICS
  g_serialDiagnosticRecords = 0;
  serialDiagnostic("start act=%.20s ok=%d ms=%lu f0=%lu m0=%lu f1=%lu m1=%lu", g_activityName, started ? 1 : 0,
                   millis() - startedMs, static_cast<unsigned long>(beforeFree),
                   static_cast<unsigned long>(beforeLargest), static_cast<unsigned long>(ESP.getFreeHeap()),
                   static_cast<unsigned long>(ESP.getMaxAllocHeap()));
  if (started) {
    const uint8_t bonds = BleHid.pairedCount();
    serialDiagnostic("bond n=%u", static_cast<unsigned>(bonds));
    for (uint8_t i = 0; i < bonds && i < 4; ++i) {
      const auto& bond = BleHid.paired(i);
      serialDiagnostic("bond i=%u addr=%.17s type=%u", static_cast<unsigned>(i), bond.addr,
                       static_cast<unsigned>(bond.addrType));
    }
  }
#endif
  // The pinned host persists bonds in NVS and already performs bounded
  // reconnects from poll(). Queue the first known bond immediately after a
  // fresh begin so a reader/settings lifecycle transition does not add an
  // unnecessary four-second backoff. The host still owns security, HID
  // discovery, subscription, timeout and retry behavior.
  if (started && BleHid.pairedCount() > 0 && !BleHid.isConnected() && !BleHid.isConnecting()) {
    const auto& bond = BleHid.paired(0);
    const bool queued = BleHid.connect(bond.addr);
#if FREEINK_CAP_BLE_HID_HOST
    LOG_INF("BLEMEM", "stage=connection_request ms=%lu activity=%.20s result=%s free=%u largest=%u",
            static_cast<unsigned long>(millis()), activityManager.currentActivityName(), queued ? "queued" : "rejected",
            ESP.getFreeHeap(), ESP.getMaxAllocHeap());
#endif
#if FREEINK_CAP_BLE_HID_HOST && NOOIR_BLE_DIAGNOSTICS
    recordDiagnosticEvent(queued ? "reconnect_start src=begin addr=%.17s type=%u queued=1"
                                 : "reconnect_join_inflight src=begin addr=%.17s type=%u",
                          bond.addr, static_cast<unsigned>(bond.addrType));
#endif
  }
  return started;
}

// Full teardown (NimBLE deinit), not just a link drop, so the BLE stack's RAM is
// returned to the heap — otherwise memory-hungry work like EPUB inflate can't
// allocate even after the user turns Bluetooth off.
void stopForReaderResourcePressure() {
#if FREEINK_CAP_BLE_HID_HOST
  const bool eligible = SETTINGS.bluetoothEnabled && activityManager.isReaderActivity();
  if (eligible) {
    const bool wasPending = g_readerBleReturnPending.exchange(true, std::memory_order_acq_rel);
    g_readerBleReturnRetryBlocked.store(false, std::memory_order_release);
    if (!wasPending) {
      const uint32_t generation = g_readerBleReturnGeneration.fetch_add(1, std::memory_order_acq_rel) + 1;
      LOG_INF("BLEMEM", "stage=reader_return_armed generation=%u running=%d connected=%d connecting=%d free=%u largest=%u",
              static_cast<unsigned>(generation), BleHid.isRunning() ? 1 : 0, BleHid.isConnected() ? 1 : 0,
              BleHid.isConnecting() ? 1 : 0,
              static_cast<unsigned>(ESP.getFreeHeap()), static_cast<unsigned>(ESP.getMaxAllocHeap()));
    }
  }
  requestLifecycleReevaluation();
#endif
  stop();
}

void stop() {
#if !FREEINK_CAP_BLE_HID_HOST
  return;
#else
  const bool wasRunning = BleHid.isRunning();
  resetNoDeviceWindow();
  // Drop queued reports and decoder/held-key state even when the host has
  // already stopped. Re-admission must begin with a clean input boundary so a
  // stale press cannot leak into Reader, Settings, or a popup after reconnect.
  BleHid.resetInputState();
  // Sleep, Web, and other lifecycle boundaries may call stop defensively.
  // If Settings already has BLE Off and the host is gone, keep the path
  // genuinely inert: no NimBLE end call, power lock, or lifecycle log.
  if (!wasRunning) return;
  g_suppressNextStopNotice = true;
  const uint32_t beforeFree = static_cast<uint32_t>(ESP.getFreeHeap());
  const uint32_t beforeLargest = static_cast<uint32_t>(ESP.getMaxAllocHeap());
  const unsigned long startedMs = millis();
  HalPowerManager::Lock powerLock;
#if FREEINK_CAP_BLE_HID_HOST
  if (wasRunning) {
    LOG_INF("HEAPSHAPE", "stage=ble_stop_before free=%u largest=%u min=%u internal8=%u/%u dma8=%u/%u",
            static_cast<unsigned>(beforeFree), static_cast<unsigned>(beforeLargest),
            static_cast<unsigned>(ESP.getMinFreeHeap()),
            static_cast<unsigned>(heap_caps_get_free_size(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT)),
            static_cast<unsigned>(heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT)),
            static_cast<unsigned>(heap_caps_get_free_size(MALLOC_CAP_DMA | MALLOC_CAP_8BIT)),
            static_cast<unsigned>(heap_caps_get_largest_free_block(MALLOC_CAP_DMA | MALLOC_CAP_8BIT)));
  }
#endif
  BleHid.end();
#if FREEINK_CAP_BLE_HID_HOST
  if (wasRunning) {
    LOG_INF("HEAPSHAPE", "stage=ble_stop_after free=%u largest=%u min=%u internal8=%u/%u dma8=%u/%u",
            static_cast<unsigned>(ESP.getFreeHeap()), static_cast<unsigned>(ESP.getMaxAllocHeap()),
            static_cast<unsigned>(ESP.getMinFreeHeap()),
            static_cast<unsigned>(heap_caps_get_free_size(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT)),
            static_cast<unsigned>(heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT)),
            static_cast<unsigned>(heap_caps_get_free_size(MALLOC_CAP_DMA | MALLOC_CAP_8BIT)),
            static_cast<unsigned>(heap_caps_get_largest_free_block(MALLOC_CAP_DMA | MALLOC_CAP_8BIT)));
  }
#endif
#if FREEINK_CAP_BLE_HID_HOST && NOOIR_BLE_DIAGNOSTICS && FREEINK_BLE_HID_RAW_DIAGNOSTICS
  BleHid.clearRawReports();
#endif
#if FREEINK_CAP_BLE_HID_HOST && NOOIR_BLE_DIAGNOSTICS
  serialDiagnostic("stop act=%.20s ms=%lu f0=%lu m0=%lu f1=%lu m1=%lu", g_activityName, millis() - startedMs,
                   static_cast<unsigned long>(beforeFree), static_cast<unsigned long>(beforeLargest),
                   static_cast<unsigned long>(ESP.getFreeHeap()),
                   static_cast<unsigned long>(ESP.getMaxAllocHeap()));
#endif
#endif
}

void pollLifecycle() {
#if FREEINK_CAP_BLE_HID_HOST
  if (!SETTINGS.bluetoothEnabled) {
    // Keep the disabled state inert: no BLE sampling, retry window, pending
    // admission, stale notification, or lifecycle transition is carried into
    // the next enabled session.
    g_lifecycleKnown = false;
    g_lastRunning = false;
    g_lastConnected = false;
    g_lastConnecting = false;
    g_lastScanning = false;
    g_lastDeviceCount = 0;
    g_suppressNextStopNotice = false;
    g_bleLowFreeHeap = UINT32_MAX;
    g_bleLowLargestFreeBlock = UINT32_MAX;
    g_notificationPending = false;
    resetNoDeviceWindow();
    g_reAdmissionPending.store(false, std::memory_order_release);
    g_lifecycleReevaluationPending.store(false, std::memory_order_release);
    g_readerBleReturnPending.store(false, std::memory_order_release);
    g_readerBleReturnRetryBlocked.store(false, std::memory_order_release);
    return;
  }
  sampleHeap();
#if NOOIR_BLE_DIAGNOSTICS && FREEINK_BLE_HID_RAW_DIAGNOSTICS
  serialRawReportDiagnostics();
#endif
  const bool running = BleHid.isRunning();
  const bool connected = BleHid.isConnected();
  const bool connecting = BleHid.isConnecting();
  const bool scanning = BleHid.isScanning();
  const uint8_t deviceCount = BleHid.deviceCount();
  const bool changed = !g_lifecycleKnown || running != g_lastRunning || connected != g_lastConnected ||
                       connecting != g_lastConnecting || scanning != g_lastScanning;
  char connectFailure[48] = {};
  const bool hasConnectFailure = BleHid.takeConnectFailure(connectFailure, sizeof(connectFailure));
  const bool deviceListChanged = deviceCount != g_lastDeviceCount;

  if (changed) {
    const bool hostStopped = g_lifecycleKnown && g_lastRunning && !running;
    const bool connectStarted = connecting && (!g_lifecycleKnown || !g_lastConnecting);
    const bool unexpectedDisconnect = g_lifecycleKnown && g_lastConnected && !connected && running &&
                                      !g_suppressNextStopNotice;
    appendRecord(DiagnosticType::State, 0xFF, 0, 0xFF);
#if NOOIR_BLE_DIAGNOSTICS
    serialStateDiagnostic();
    if (connectStarted) serialDiagnostic("connect_attempt src=host");
#endif
    if (unexpectedDisconnect) queueNotification("Bluetooth disconnected");
    if (running && connected && !g_lastConnected) {
      recordDiagnosticEvent("connect_success secure=1 hid=1 name=%.28s", BleHid.connectedName());
      recordDiagnosticEvent("ble_stage=connected_idle free=%u max=%u", ESP.getFreeHeap(), ESP.getMaxAllocHeap());
#if FREEINK_CAP_BLE_HID_HOST
      LOG_INF("BLEMEM", "stage=connected_idle free=%u largest=%u", ESP.getFreeHeap(), ESP.getMaxAllocHeap());
#endif
      char message[64];
      snprintf(message, sizeof(message), "Bluetooth connected: %.36s", BleHid.connectedName());
      queueNotification(message);
      g_readerBleReturnPending.store(false, std::memory_order_release);
      g_readerBleReturnRetryBlocked.store(false, std::memory_order_release);
    }
    if (g_lastConnected && !connected && running) recordDiagnosticEvent("disconnect reason=unreported");
    g_suppressNextStopNotice = false;
    g_lifecycleKnown = true;
    g_lastRunning = running;
    g_lastConnected = connected;
    g_lastConnecting = connecting;
    g_lastScanning = scanning;
    if (hostStopped) requestLifecycleReevaluation();
  }
#if NOOIR_BLE_DIAGNOSTICS
  if (deviceListChanged) {
    if (deviceCount > 0) {
      const auto& device = BleHid.device(static_cast<uint8_t>(deviceCount - 1));
      serialDiagnostic("scan n=%u name=%.24s addr=%.17s rssi=%d hid=%d", static_cast<unsigned>(deviceCount),
                       device.name, device.addr, device.rssi, device.hid ? 1 : 0);
    } else {
      serialDiagnostic("scan n=0");
    }
  }
  if (hasConnectFailure) {
    serialDiagnostic("connect_fail stage=%s %.36s", failureStage(connectFailure), connectFailure);
  }
#else
  (void)connectFailure;
#endif
  if (hasConnectFailure) {
    LOG_INF("BLEMEM", "stage=connect_failure ms=%lu activity=%.20s reason=%.36s",
            static_cast<unsigned long>(millis()), activityManager.currentActivityName(), connectFailure);
  }
  g_lastDeviceCount = deviceCount;
  updateNoDeviceWindow(running, connected);
  if (!changed && !hasConnectFailure && !deviceListChanged) return;
#endif
}

bool takeNotification(char* out, size_t outLen) {
#if FREEINK_CAP_BLE_HID_HOST
  if (!out || outLen == 0 || !g_notificationPending) return false;
  strncpy(out, g_pendingNotification, outLen - 1);
  out[outLen - 1] = '\0';
  g_pendingNotification[0] = '\0';
  g_notificationPending = false;
  return true;
#else
  (void)out;
  (void)outLen;
  return false;
#endif
}

void suppressNextDisconnectNotice() {
#if FREEINK_CAP_BLE_HID_HOST
  if (BleHid.isConnected()) g_suppressNextStopNotice = true;
#endif
}

bool encodeKey(const freeink::KeyEvent& ev, uint8_t& kind, uint8_t& value) {
  if (ev.special != freeink::SpecialKey::None) {
    kind = 0;
    value = static_cast<uint8_t>(ev.special);
    return true;
  }
  if (ev.keycode != 0) {
    kind = 1;
    value = ev.keycode;
    return true;
  }
  return false;
}

bool encodeLearnedKey(const freeink::KeyEvent& ev, uint8_t& kind, uint8_t& value) {
  if (mapping::encodeNormalizedSpecialForLearning(ev.specialNormalized,
                                                  static_cast<uint8_t>(ev.special), kind, value))
    return true;
  return mapping::encodeKeyboardUsageForLearning(ev.usagePage, ev.keycode, kind, value);
}

namespace {
const char* specialName(uint8_t value) {
  switch (static_cast<freeink::SpecialKey>(value)) {
    case freeink::SpecialKey::Enter:
      return "Enter";
    case freeink::SpecialKey::Backspace:
      return "Backspace";
    case freeink::SpecialKey::Tab:
      return "Tab";
    case freeink::SpecialKey::Escape:
      return "Escape";
    case freeink::SpecialKey::Delete:
      return "Delete";
    case freeink::SpecialKey::Left:
      return "Left";
    case freeink::SpecialKey::Right:
      return "Right";
    case freeink::SpecialKey::Up:
      return "Up";
    case freeink::SpecialKey::Down:
      return "Down";
    case freeink::SpecialKey::Home:
      return "Home";
    case freeink::SpecialKey::End:
      return "End";
    case freeink::SpecialKey::PageUp:
      return "Page Up";
    case freeink::SpecialKey::PageDown:
      return "Page Down";
    default:
      return nullptr;
  }
}
}  // namespace

void describeKey(uint8_t kind, uint8_t value, char* out, size_t outLen) {
  if (!out || outLen == 0) return;
  if (kind == 0) {
    const char* name = specialName(value);
    if (name) {
      strncpy(out, name, outLen - 1);
      out[outLen - 1] = '\0';
      return;
    }
  }
  if (kind == 2) {
    snprintf(out, outLen, "Keyboard 0x%02X", static_cast<unsigned>(value));
    return;
  }
  // Printable ASCII usage handled as a generic key code; show the raw value.
  snprintf(out, outLen, "Key 0x%02X", static_cast<unsigned>(value));
}

void recordDecodedKey(const freeink::KeyEvent& event, const uint8_t kind, const uint8_t value,
                      const uint8_t mappedButton, const InputDisposition disposition,
                      const bool inactivityReset) {
#if FREEINK_CAP_BLE_HID_HOST
  appendRecord(DiagnosticType::Input, kind, value, mappedButton);
#if NOOIR_BLE_DIAGNOSTICS
  serialDiagnostic("[INPUTDIAG] act=%.20s k=%u v=%02X phase=%s norm=%u:%02X action=%u d=%s reset=%d",
                   g_activityName, static_cast<unsigned>(kind), static_cast<unsigned>(value), phaseName(event.phase),
                   static_cast<unsigned>(kind), static_cast<unsigned>(value), static_cast<unsigned>(mappedButton),
                   dispositionName(disposition), inactivityReset ? 1 : 0);
#endif
#else
  (void)event;
  (void)kind;
  (void)value;
  (void)mappedButton;
  (void)disposition;
  (void)inactivityReset;
#endif
}

uint8_t diagnosticCount() {
#if FREEINK_CAP_BLE_HID_HOST
  portENTER_CRITICAL(&g_diagnosticMux);
  const uint8_t count = g_diagnosticCount;
  portEXIT_CRITICAL(&g_diagnosticMux);
  return count;
#else
  return 0;
#endif
}

bool diagnosticAtNewest(const uint8_t newestIndex, DiagnosticRecord& out) {
#if FREEINK_CAP_BLE_HID_HOST
  portENTER_CRITICAL(&g_diagnosticMux);
  if (newestIndex >= g_diagnosticCount) {
    portEXIT_CRITICAL(&g_diagnosticMux);
    return false;
  }
  const int index = (static_cast<int>(g_diagnosticHead) - 1 - newestIndex + kDiagnosticCapacity) %
                    kDiagnosticCapacity;
  out = g_diagnosticRing[index];
  portEXIT_CRITICAL(&g_diagnosticMux);
  return true;
#else
  (void)newestIndex;
  (void)out;
  return false;
#endif
}

void clearDiagnostics() {
#if FREEINK_CAP_BLE_HID_HOST
  portENTER_CRITICAL(&g_diagnosticMux);
  g_diagnosticHead = 0;
  g_diagnosticCount = 0;
  portEXIT_CRITICAL(&g_diagnosticMux);
  g_bleLowFreeHeap = UINT32_MAX;
  g_bleLowLargestFreeBlock = UINT32_MAX;
#if NOOIR_BLE_DIAGNOSTICS
  g_serialDiagnosticRecords = 0;
#endif
#endif
}

void formatDiagnostic(const DiagnosticRecord& record, char* out, const size_t outLen) {
#if FREEINK_CAP_BLE_HID_HOST
  if (!out || outLen == 0) return;
  if (record.type == DiagnosticType::Input) {
    snprintf(out, outLen, "K %02X:%02X -> %02X h%lu/%lu", record.keyKind, record.keyValue,
             record.mappedButton, static_cast<unsigned long>(record.freeHeap),
             static_cast<unsigned long>(record.largestFreeBlock));
  } else {
    snprintf(out, outLen, "S r%d c%d k%d s%d h%lu/%lu", record.running ? 1 : 0,
             record.connected ? 1 : 0, record.connecting ? 1 : 0, record.scanning ? 1 : 0,
             static_cast<unsigned long>(record.freeHeap),
             static_cast<unsigned long>(record.largestFreeBlock));
  }
#else
  (void)record;
  if (out && outLen) out[0] = '\0';
#endif
}

}  // namespace bleinput
