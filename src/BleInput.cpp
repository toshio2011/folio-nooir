#include "BleInput.h"

#include <HalPowerManager.h>
#include <I18n.h>
#include <Logging.h>
#if FREEINK_CAP_BLE_HID_HOST
#include <WiFi.h>
#include <esp_err.h>
#include <esp_heap_caps.h>
#endif

#include <cstdio>
#include <cstdarg>
#include <cstring>
#include <algorithm>
#include <atomic>

#include "util/BleKeyMapping.h"
#include "util/IineTouchGesture.h"
#include "util/HeapLifetimeDiagnostics.h"

#if FREEINK_CAP_BLE_HID_HOST
#include "CrossPointSettings.h"
#include "activities/Activity.h"  // completes Activity for ActivityManager's unique_ptr members
#include "activities/RenderLock.h"
#include "util/BleMemoryPolicy.h"
#include "util/BleNoDeviceTimeoutPolicy.h"
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
std::atomic<bool> g_stopRequestPending{false};
std::atomic<uint32_t> g_stopRequestStartedMs{0};
uint32_t g_bleLowFreeHeap = UINT32_MAX;
uint32_t g_bleLowLargestFreeBlock = UINT32_MAX;
std::atomic<bool> g_lifecycleReevaluationPending{true};
std::atomic<bool> g_reAdmissionPending{true};
std::atomic<bool> g_readerBleReturnPending{false};
// One-bit record of the existing quiescent Reader heap-admission decision.
// This is reason output for the UI, not a second BLE state machine.
std::atomic<bool> g_readerWaitingForMemory{false};
std::atomic<bool> g_readerBleReturnRetryBlocked{false};
std::atomic<uint32_t> g_readerBleReturnGeneration{0};
std::atomic<size_t> g_readerBleExpectedRenderPeakBytes{0};
std::atomic<size_t> g_readerBleLargestRenderRequestBytes{0};
std::atomic<size_t> g_readerBlePageAwareRenderFreeBytes{0};
std::atomic<size_t> g_readerBlePageAwareRenderLargestBytes{0};
std::atomic<bool> g_readerBlePageAwareRenderKnown{false};
std::atomic<bool> g_webExclusiveActive{false};
std::atomic<bool> g_noDeviceTimeoutStoppedHost{false};
portMUX_TYPE g_noDeviceTimeoutMux = portMUX_INITIALIZER_UNLOCKED;
char g_pendingNotification[64] = {};
bool g_notificationPending = false;
ble_session::NoDeviceTimeoutWindow g_noDeviceWindow;
std::atomic<bool> g_bleEverStarted{false};
#if NOOIR_EPUB_BLE_IDLE_ADMISSION_DIAGNOSTIC
std::atomic<bool> g_epubIdleBleDiagnosticArmed{false};
// Startup/host-ready failures remain hard fail-closed faults for this armed
// diagnostic session. A connected-idle runtime floor trip is separate: it may
// be cleared only after a completed render and confirmed full teardown.
std::atomic<bool> g_epubIdleBleDiagnosticFault{false};
std::atomic<bool> g_epubIdleBleRuntimeFloorFault{false};
std::atomic<bool> g_epubIdleBleDiagnosticPermit{false};
std::atomic<bool> g_epubIdleBleDiagnosticStarting{false};
std::atomic<bool> g_epubIdleBleDiagnosticOwnsReturn{false};
std::atomic<uint32_t> g_epubIdleBleDiagnosticStartMs{0};
std::atomic<uint32_t> g_epubIdleBleDiagnosticStartFree{0};
std::atomic<uint32_t> g_epubIdleBleDiagnosticStartLargest{0};
std::atomic<uint32_t> g_epubIdleBleDiagnosticStartMinFree{0};
std::atomic<uint32_t> g_epubIdleBleDiagnosticStopStartFree{0};
std::atomic<uint32_t> g_epubIdleBleDiagnosticStopStartLargest{0};
std::atomic<uint32_t> g_epubIdleBleDiagnosticLastStopAckMs{0};
#endif

void logBleHeapShape(const char* stage) {
  LOG_INF("HEAPSHAPE",
          "stage=%s ble_runtime=%s ble_enabled=%u ble_ever_started=%u free=%u largest=%u min=%u "
          "internal8=%u/%u dma8=%u/%u",
          stage ? stage : "unknown", runtimeStateName(), SETTINGS.bluetoothEnabled ? 1U : 0U,
          g_bleEverStarted.load(std::memory_order_acquire) ? 1U : 0U,
          static_cast<unsigned>(ESP.getFreeHeap()), static_cast<unsigned>(ESP.getMaxAllocHeap()),
          static_cast<unsigned>(ESP.getMinFreeHeap()),
          static_cast<unsigned>(heap_caps_get_free_size(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT)),
          static_cast<unsigned>(heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT)),
          static_cast<unsigned>(heap_caps_get_free_size(MALLOC_CAP_DMA | MALLOC_CAP_8BIT)),
          static_cast<unsigned>(heap_caps_get_largest_free_block(MALLOC_CAP_DMA | MALLOC_CAP_8BIT)));
}

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
    serialDiagnostic("raw q=%lu t=%lu dt=%u src=%c ch=%04X ref=%u ty=%u id=%02X len=%u n=%u st=%02X exp=%u why=%u x=%u b=%s",
                     static_cast<unsigned long>(report.sequence), static_cast<unsigned long>(report.uptimeMs),
                     static_cast<unsigned>(report.deltaMs), rawSourceName(report.source),
                     static_cast<unsigned>(report.characteristicUuid), report.hasReportReference ? 1U : 0U,
                     static_cast<unsigned>(report.reportType), static_cast<unsigned>(report.reportId),
                     static_cast<unsigned>(report.length), static_cast<unsigned>(report.storedLength),
                     static_cast<unsigned>(report.state), static_cast<unsigned>(report.expectedLength),
                     static_cast<unsigned>(report.reason), report.truncated ? 1u : 0u, hex);

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
  g_noDeviceWindow.reset();
}

void suppressNoDeviceRetryUntilLifecycleBoundary() {
  portENTER_CRITICAL(&g_noDeviceTimeoutMux);
  g_noDeviceTimeoutStoppedHost.store(true, std::memory_order_relaxed);
  portEXIT_CRITICAL(&g_noDeviceTimeoutMux);
}

void updateNoDeviceWindow(const bool running, const bool connected) {
  const uint16_t timeoutSeconds = SETTINGS.bluetoothNoDeviceTimeoutSeconds;
  const auto state = g_noDeviceWindow.update(SETTINGS.bluetoothEnabled != 0, timeoutSeconds, running, connected,
                                              millis());
  if (state != ble_session::NoDeviceTimeoutWindowState::Expired) return;

  // The lifecycle poll may have sampled `connected` just before the host
  // completed a connection. Recheck at the point of shutdown so the no-device
  // timeout can never tear down a newly established HID link.
  if (BleHid.isConnected()) {
    resetNoDeviceWindow();
    return;
  }

  // This setting promises to turn Bluetooth off when no device connects.
  // Retain the existing lifecycle pause until a deliberate user re-enable or
  // activity boundary, and persist OFF so wake cannot silently restart BLE.
  suppressNoDeviceRetryUntilLifecycleBoundary();
  const bool settingsSaved = setBluetoothEnabled(false);
  LOG_INF("BLEMEM", "stage=no_device_timeout ms=%lu shutdown_requested=1 saved=%u",
          static_cast<unsigned long>(millis()), settingsSaved ? 1u : 0u);
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
  // A new lifecycle event invalidates the last admission reason until the
  // current predictor pass produces a fresh result.
  g_readerWaitingForMemory.store(false, std::memory_order_release);
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
  g_readerWaitingForMemory.store(false, std::memory_order_release);
#endif
}

bool armEpubIdleBleDiagnostic() {
#if FREEINK_CAP_BLE_HID_HOST && NOOIR_EPUB_BLE_IDLE_ADMISSION_DIAGNOSTIC
  if (!SETTINGS.bluetoothEnabled || WiFi.getMode() != WIFI_MODE_NULL || BleHid.isRunning() || stopPending() ||
      g_webExclusiveActive.load(std::memory_order_acquire) ||
      !activityManager.isCurrentReaderActivity() ||
      std::strcmp(activityManager.currentActivityName(), "EpubReader") != 0 ||
      activityManager.bluetoothResourceSensitive() || RenderLock::peek() ||
      activityManager.hasPendingRenderWork() ||
      !activityManager.bluetoothShouldBeActive()) {
    LOG_INF("EPUBBLE", "stage=diag_arm result=reject enabled=%u wifi=%u running=%u stop_pending=%u activity=%.20s",
            SETTINGS.bluetoothEnabled ? 1U : 0U, WiFi.getMode() != WIFI_MODE_NULL ? 1U : 0U,
            BleHid.isRunning() ? 1U : 0U, stopPending() ? 1U : 0U, activityManager.currentActivityName());
    return false;
  }
  bool expected = false;
  if (!g_epubIdleBleDiagnosticArmed.compare_exchange_strong(expected, true, std::memory_order_acq_rel)) return false;
  g_epubIdleBleDiagnosticFault.store(false, std::memory_order_release);
  g_epubIdleBleRuntimeFloorFault.store(false, std::memory_order_release);
  g_epubIdleBleDiagnosticPermit.store(false, std::memory_order_release);
  g_epubIdleBleDiagnosticStartMs.store(0, std::memory_order_release);
  g_epubIdleBleDiagnosticLastStopAckMs.store(0, std::memory_order_release);
  const bool wasPending = g_readerBleReturnPending.exchange(true, std::memory_order_acq_rel);
  g_epubIdleBleDiagnosticOwnsReturn.store(!wasPending, std::memory_order_release);
  signalReaderBleReturnLifecycleEvent("idle_diag_arm");
  LOG_INF("EPUBBLE", "stage=sequential mode=automatic active=1 generation=%u free=%u largest=%u strict=108544/59392 idle=%u/%u",
          static_cast<unsigned>(g_readerBleReturnGeneration.load(std::memory_order_acquire)),
          static_cast<unsigned>(ESP.getFreeHeap()), static_cast<unsigned>(ESP.getMaxAllocHeap()),
          static_cast<unsigned>(kReaderBleEpubIdleOnlyRequiredFree),
          static_cast<unsigned>(kReaderBleEpubIdleOnlyRequiredLargest));
  return true;
#else
  return false;
#endif
}

void disarmEpubIdleBleDiagnostic(const char* reason) {
#if FREEINK_CAP_BLE_HID_HOST && NOOIR_EPUB_BLE_IDLE_ADMISSION_DIAGNOSTIC
  const bool wasArmed = g_epubIdleBleDiagnosticArmed.exchange(false, std::memory_order_acq_rel);
  g_epubIdleBleDiagnosticPermit.store(false, std::memory_order_release);
  g_epubIdleBleDiagnosticStarting.store(false, std::memory_order_release);
  g_epubIdleBleDiagnosticStartMs.store(0, std::memory_order_release);
  const bool ownsReturn = g_epubIdleBleDiagnosticOwnsReturn.exchange(false, std::memory_order_acq_rel);
  if (ownsReturn && !BleHid.isRunning() && !stopPending()) {
    clearReaderBleReturnPending();
    clearReAdmissionPending();
    (void)takeLifecycleReevaluation();
  }
  if (wasArmed) {
    LOG_INF("EPUBBLE", "stage=diag_arm result=disarmed reason=%.24s free=%u largest=%u",
            reason ? reason : "unspecified", static_cast<unsigned>(ESP.getFreeHeap()),
            static_cast<unsigned>(ESP.getMaxAllocHeap()));
  }
#else
  (void)reason;
#endif
}

bool epubIdleBleDiagnosticArmed() {
#if FREEINK_CAP_BLE_HID_HOST && NOOIR_EPUB_BLE_IDLE_ADMISSION_DIAGNOSTIC
  // Keep the diagnostic selected after a fault so admission cannot fall
  // through to the normal simultaneous-render predictor. The fault is a
  // separate latch and requires explicit disarm/re-arm to clear.
  return g_epubIdleBleDiagnosticArmed.load(std::memory_order_acquire);
#else
  return false;
#endif
}

bool epubIdleBleDiagnosticStarting() {
#if FREEINK_CAP_BLE_HID_HOST && NOOIR_EPUB_BLE_IDLE_ADMISSION_DIAGNOSTIC
  return g_epubIdleBleDiagnosticStarting.load(std::memory_order_acquire);
#else
  return false;
#endif
}

bool epubIdleBleDiagnosticFaulted() {
#if FREEINK_CAP_BLE_HID_HOST && NOOIR_EPUB_BLE_IDLE_ADMISSION_DIAGNOSTIC
  return g_epubIdleBleDiagnosticFault.load(std::memory_order_acquire) ||
         g_epubIdleBleRuntimeFloorFault.load(std::memory_order_acquire);
#else
  return false;
#endif
}

bool clearEpubIdleBleRuntimeFloorFaultAfterRender(const bool renderComplete, const bool readerQuiescent) {
#if FREEINK_CAP_BLE_HID_HOST && NOOIR_EPUB_BLE_IDLE_ADMISSION_DIAGNOSTIC
  const bool canClear = readerBleEpubRuntimeFloorFaultCanClear(
      renderComplete, readerQuiescent, SETTINGS.bluetoothEnabled,
      g_epubIdleBleDiagnosticArmed.load(std::memory_order_acquire),
      g_epubIdleBleRuntimeFloorFault.load(std::memory_order_acquire),
      BleHid.isRunning(), stopPending(), g_readerBleReturnPending.load(std::memory_order_acquire));
  if (!canClear) return false;
  bool expectedFault = true;
  if (!g_epubIdleBleRuntimeFloorFault.compare_exchange_strong(expectedFault, false,
                                                               std::memory_order_acq_rel)) return false;
  g_epubIdleBleDiagnosticPermit.store(false, std::memory_order_release);
  LOG_INF("EPUBBLE", "stage=runtime_floor_recovered after=render_complete teardown=confirmed free=%u largest=%u",
          static_cast<unsigned>(ESP.getFreeHeap()), static_cast<unsigned>(ESP.getMaxAllocHeap()));
  return true;
#else
  (void)renderComplete;
  (void)readerQuiescent;
  return false;
#endif
}

bool epubIdleBleDiagnosticPermitPending() {
#if FREEINK_CAP_BLE_HID_HOST && NOOIR_EPUB_BLE_IDLE_ADMISSION_DIAGNOSTIC
  return g_epubIdleBleDiagnosticPermit.load(std::memory_order_acquire);
#else
  return false;
#endif
}

void grantEpubIdleBleDiagnosticPermit() {
#if FREEINK_CAP_BLE_HID_HOST && NOOIR_EPUB_BLE_IDLE_ADMISSION_DIAGNOSTIC
  if (epubIdleBleDiagnosticArmed() && SETTINGS.bluetoothEnabled && !BleHid.isRunning() && !stopPending()) {
    g_epubIdleBleDiagnosticPermit.store(true, std::memory_order_release);
    LOG_INF("EPUBBLE", "stage=idle_admission_permit grant=1 free=%u largest=%u required=%u/%u",
            static_cast<unsigned>(ESP.getFreeHeap()), static_cast<unsigned>(ESP.getMaxAllocHeap()),
            static_cast<unsigned>(kReaderBleEpubIdleOnlyRequiredFree),
            static_cast<unsigned>(kReaderBleEpubIdleOnlyRequiredLargest));
  }
#endif
}

bool consumeEpubIdleBleDiagnosticPermit() {
#if FREEINK_CAP_BLE_HID_HOST && NOOIR_EPUB_BLE_IDLE_ADMISSION_DIAGNOSTIC
  return g_epubIdleBleDiagnosticPermit.exchange(false, std::memory_order_acq_rel);
#else
  return false;
#endif
}

bool readerWaitingForMemory() {
#if FREEINK_CAP_BLE_HID_HOST
  return g_readerWaitingForMemory.load(std::memory_order_acquire) && readerBleReturnPending();
#else
  return false;
#endif
}

void setReaderWaitingForMemory(const bool waiting) {
#if FREEINK_CAP_BLE_HID_HOST
  g_readerWaitingForMemory.store(waiting, std::memory_order_release);
#else
  (void)waiting;
#endif
}

uint32_t readerBleReturnGeneration() {
#if FREEINK_CAP_BLE_HID_HOST
  return g_readerBleReturnGeneration.load(std::memory_order_acquire);
#else
  return 0;
#endif
}

void setReaderBleExpectedRenderDemand(const size_t peakBytes, const size_t largestRequestBytes) {
#if FREEINK_CAP_BLE_HID_HOST && NOOIR_BLE_READER_COEXISTENCE
  g_readerBleExpectedRenderPeakBytes.store(peakBytes, std::memory_order_release);
  g_readerBleLargestRenderRequestBytes.store(largestRequestBytes, std::memory_order_release);
#else
  (void)peakBytes;
  (void)largestRequestBytes;
#endif
}

void setReaderBlePageAwareRenderDemand(const size_t freeBytes, const size_t largestBytes, const bool known) {
#if FREEINK_CAP_BLE_HID_HOST && NOOIR_BLE_READER_COEXISTENCE
  g_readerBlePageAwareRenderKnown.store(false, std::memory_order_release);
  g_readerBlePageAwareRenderFreeBytes.store(freeBytes, std::memory_order_relaxed);
  g_readerBlePageAwareRenderLargestBytes.store(largestBytes, std::memory_order_relaxed);
  g_readerBlePageAwareRenderKnown.store(known, std::memory_order_release);
#else
  (void)freeBytes;
  (void)largestBytes;
  (void)known;
#endif
}

void signalReaderBleReturnLifecycleEvent(const char* reason) {
#if FREEINK_CAP_BLE_HID_HOST
  if (!SETTINGS.bluetoothEnabled || !g_readerBleReturnPending.load(std::memory_order_acquire)) return;
  g_readerWaitingForMemory.store(false, std::memory_order_release);
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
#if NOOIR_EPUB_BLE_IDLE_ADMISSION_DIAGNOSTIC
  if (readerContext && activityManager.isCurrentReaderActivity() &&
      std::strcmp(activityManager.currentActivityName(), "EpubReader") == 0 &&
      g_epubIdleBleDiagnosticArmed.load(std::memory_order_acquire) &&
      epubIdleBleDiagnosticFaulted())
    return false;
  // Do not let a generic cold-start signal race ahead of the automatic
  // sequential-session latch and fall through to the universal admission
  // floor. The stable-page arm publishes return intent and its own event.
  if (readerContext && activityManager.isCurrentReaderActivity() &&
      std::strcmp(activityManager.currentActivityName(), "EpubReader") == 0 &&
      !g_epubIdleBleDiagnosticArmed.load(std::memory_order_acquire))
    return false;
#endif
  // The heap floors admit a cold NimBLE host start, not a connection on a host
  // that already owns its memory. Keep Reader contexts on their existing
  // pressure policy above; an initialized host does not pay the cold-start
  // floor a second time merely because NimBLE itself consumed startup headroom.
  if (BleHid.isRunning()) return true;
  const char* activityName = activityManager.currentActivityName();
  const bool homeContext = strcmp(activityName, "Home") == 0 || strcmp(activityName, "RecentBooks") == 0;
  const bool homeRenderWorkPending = homeContext && (RenderLock::peek() || activityManager.hasPendingRenderWork());
  if (!homeBleColdStartRenderWorkSafe(homeContext, homeRenderWorkPending)) return false;
  // XTC/XTCH stream one page from SD and do not need EPUB's future section,
  // font, or image-render reserve. Keep the same contiguous-block floor and
  // use the separate, narrowly scoped experimental free-heap budget.
  if (strcmp(activityName, "XtcReader") == 0) {
    const size_t freeHeap = static_cast<size_t>(ESP.getFreeHeap());
    const size_t largestBlock = static_cast<size_t>(ESP.getMaxAllocHeap());
    const bool admitted = readerBleXtcStartMemoryAdmitted(freeHeap, largestBlock);
    LOG_INF("BLEMEM", "stage=format_admission format=XTC_XTCH free=%u largest=%u required=%u/%u admit=%u",
            static_cast<unsigned>(freeHeap), static_cast<unsigned>(largestBlock),
            static_cast<unsigned>(kXtcBleStartMinFreeHeap),
            static_cast<unsigned>(kReaderBleStartMinLargestBlock), admitted ? 1U : 0U);
    return admitted;
  }
  // Every cold start still uses the strict global floors except a confirmed
  // idle EPUB return. This margin is unavailable to Reader entry, builds,
  // renders, image/grayscale work, other Reader formats, and Settings.
  const bool quiescentEpubReturn =
      readerContext && activityManager.isCurrentReaderActivity() &&
      strcmp(activityManager.currentActivityName(), "EpubReader") == 0 && readerBleReturnPending() &&
      !resourceSensitive && !RenderLock::peek() && !activityManager.hasPendingRenderWork();
#if NOOIR_EPUB_BLE_IDLE_ADMISSION_DIAGNOSTIC
  if (quiescentEpubReturn && g_epubIdleBleDiagnosticArmed.load(std::memory_order_acquire)) {
    const bool permit = g_epubIdleBleDiagnosticPermit.load(std::memory_order_acquire);
    const bool faulted = epubIdleBleDiagnosticFaulted();
    const bool admitted = !faulted && readerBleEpubIdleOnlyAdmissionAdmitted(
        static_cast<size_t>(ESP.getFreeHeap()), static_cast<size_t>(ESP.getMaxAllocHeap()), permit, true);
    LOG_INF("EPUBBLE", "stage=idle_admission mode=sequential permit=%u fault=%u free=%u largest=%u required=%u/%u admit=%u",
            permit ? 1U : 0U, faulted ? 1U : 0U, static_cast<unsigned>(ESP.getFreeHeap()),
            static_cast<unsigned>(ESP.getMaxAllocHeap()),
            static_cast<unsigned>(kReaderBleEpubIdleOnlyRequiredFree),
            static_cast<unsigned>(kReaderBleEpubIdleOnlyRequiredLargest), admitted ? 1U : 0U);
    return admitted;
  }
#endif
#if NOOIR_EPUB_BLE_PAGE_AWARE_EXPERIMENT
  if (quiescentEpubReturn && g_readerBlePageAwareRenderKnown.load(std::memory_order_acquire)) {
    return readerBleEpubPageAwareReadmissionAdmitted(
        static_cast<size_t>(ESP.getFreeHeap()), static_cast<size_t>(ESP.getMaxAllocHeap()), true, true,
        g_readerBlePageAwareRenderFreeBytes.load(std::memory_order_acquire),
        g_readerBlePageAwareRenderLargestBytes.load(std::memory_order_acquire));
  }
#endif
  return readerBleQuiescentReadmissionAdmitted(
      static_cast<size_t>(ESP.getFreeHeap()), static_cast<size_t>(ESP.getMaxAllocHeap()), quiescentEpubReturn,
      g_readerBleExpectedRenderPeakBytes.load(std::memory_order_acquire),
      g_readerBleLargestRenderRequestBytes.load(std::memory_order_acquire));
#else
  // Keep the existing BLE-off stub behavior; callers still receive the
  // normal begin/connect failure from the capability-safe host stubs.
  return true;
#endif
}

bool setBluetoothEnabled(const bool enabled) {
#if FREEINK_CAP_BLE_HID_HOST
  if (enabled) clearNoDeviceTimeoutSuppression();
  SETTINGS.bluetoothEnabled = enabled ? 1 : 0;
  if (!enabled) disarmEpubIdleBleDiagnostic("bluetooth_off");
  resetNoDeviceWindow();
  requestLifecycleReevaluation();
  if (!enabled) {
    clearReaderBleReturnPending();
    stop();
  }
  // Persist the user's setting immediately. When a connection worker is still
  // unwinding, stop() queues safe teardown and returns without blocking UI.
  return SETTINGS.saveToFile();
#else
  (void)enabled;
  return false;
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

const char* runtimeStateName() {
#if FREEINK_CAP_BLE_HID_HOST
  return bleRuntimeStateName(SETTINGS.bluetoothEnabled != 0, BleHid.isRunning(),
                             g_bleEverStarted.load(std::memory_order_acquire));
#else
  return "runtime_off";
#endif
}

const char* userFacingStatusText() {
#if FREEINK_CAP_BLE_HID_HOST
  if (!SETTINGS.bluetoothEnabled) return I18N.get(StrId::STR_STATE_OFF);
  if (BleHid.isConnected()) return BleHid.connectedName();
  if (BleHid.isConnecting()) return I18N.get(StrId::STR_CONNECTING);
  if (webExclusiveActive()) return I18N.get(StrId::STR_BT_PAUSED);
  if (readerWaitingForMemory()) return I18N.get(StrId::STR_BT_WAITING_FOR_MEMORY);
  if (readerBleReturnPending()) return I18N.get(StrId::STR_BT_SUSPENDED_FOR_READER);
  if (reAdmissionPending()) return I18N.get(StrId::STR_BT_RECONNECT_PENDING);
  if (!BleHid.isRunning()) return I18N.get(StrId::STR_BT_PAUSED);
  return I18N.get(StrId::STR_BT_NOT_CONNECTED);
#else
  return I18N.get(StrId::STR_STATE_OFF);
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
  const bool xtcStreamingAdmission = std::strcmp(activityManager.currentActivityName(), "XtcReader") == 0;
  const size_t coldMinFree = xtcStreamingAdmission ? kXtcBleStartMinFreeHeap : kReaderBleStartMinFreeHeap;
  LOG_INF("BLEMEM", "stage=before_ensure_admission activity=%.20s rule=%s host_running=%d runtime=%s enabled=%u "
                     "ever_started=%u free=%u largest=%u floors=%u/%u",
          activityManager.currentActivityName(), xtcStreamingAdmission ? "xtc_stream" : "default",
          BleHid.isRunning() ? 1 : 0, runtimeStateName(),
          SETTINGS.bluetoothEnabled ? 1U : 0U,
          g_bleEverStarted.load(std::memory_order_acquire) ? 1U : 0U, ESP.getFreeHeap(), ESP.getMaxAllocHeap(),
          static_cast<unsigned>(coldMinFree),
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
#if NOOIR_HEAP_LIFETIME_TRACE
  const char* activityForTrace = activityManager.currentActivityName();
  const bool homeForTrace = std::strcmp(activityForTrace, "Home") == 0 ||
                            std::strcmp(activityForTrace, "RecentBooks") == 0;
  if (homeForTrace) {
    heap_lifetime_trace::setContext(heap_lifetime_trace::CallerId::HomeBeforeBle);
    heap_lifetime_trace::checkpoint("home_before_ble_startup");
  }
#endif
#endif
  const uint32_t beforeFree = static_cast<uint32_t>(ESP.getFreeHeap());
  const uint32_t beforeLargest = static_cast<uint32_t>(ESP.getMaxAllocHeap());
  const unsigned long startedMs = millis();
  HalPowerManager::Lock powerLock;
#if FREEINK_CAP_BLE_HID_HOST
  heap_lifetime_trace::setContext(heap_lifetime_trace::CallerId::NimbleInit);
  logBleHeapShape("ble_begin_before");
  LOG_INF("BLEMEM", "stage=before_begin runtime=%s enabled=%u ever_started=%u free=%u largest=%u",
          runtimeStateName(), SETTINGS.bluetoothEnabled ? 1U : 0U,
          g_bleEverStarted.load(std::memory_order_acquire) ? 1U : 0U, beforeFree, beforeLargest);
#endif
#if FREEINK_CAP_BLE_HID_HOST && NOOIR_BLE_DIAGNOSTICS && FREEINK_BLE_HID_RAW_DIAGNOSTICS
  BleHid.clearRawReports();
#endif
#if FREEINK_CAP_BLE_HID_HOST && NOOIR_EPUB_BLE_IDLE_ADMISSION_DIAGNOSTIC
  const bool idleDiagnosticStart = epubIdleBleDiagnosticArmed() &&
                                   !BleHid.isRunning() && consumeEpubIdleBleDiagnosticPermit();
  const bool localHeapMinWindowStarted = idleDiagnosticStart &&
      heap_caps_monitor_local_minimum_free_size_start() == ESP_OK;
  if (idleDiagnosticStart) {
    g_epubIdleBleDiagnosticStarting.store(true, std::memory_order_release);
    g_epubIdleBleDiagnosticStartMs.store(millis(), std::memory_order_release);
    g_epubIdleBleDiagnosticStartFree.store(beforeFree, std::memory_order_release);
    g_epubIdleBleDiagnosticStartLargest.store(beforeLargest, std::memory_order_release);
    LOG_INF("EPUBBLE", "stage=idle_start_begin free=%u largest=%u required=%u/%u",
            static_cast<unsigned>(beforeFree), static_cast<unsigned>(beforeLargest),
            static_cast<unsigned>(kReaderBleEpubIdleOnlyRequiredFree),
            static_cast<unsigned>(kReaderBleEpubIdleOnlyRequiredLargest));
  }
#else
  constexpr bool idleDiagnosticStart = false;
#endif
  const bool started = BleHid.begin(kHostName);
#if FREEINK_CAP_BLE_HID_HOST
  if (started) g_bleEverStarted.store(true, std::memory_order_release);
  heap_lifetime_trace::setContext(heap_lifetime_trace::CallerId::HomeActivity);
  heap_lifetime_trace::checkpoint("ble_startup_return");
  logBleHeapShape("ble_begin_after");
  LOG_INF("BLEMEM", "stage=begin_return result=%d runtime=%s enabled=%u ever_started=%u free=%u largest=%u",
          started ? 1 : 0, runtimeStateName(), SETTINGS.bluetoothEnabled ? 1U : 0U,
          g_bleEverStarted.load(std::memory_order_acquire) ? 1U : 0U,
          ESP.getFreeHeap(), ESP.getMaxAllocHeap());
#endif
#if FREEINK_CAP_BLE_HID_HOST && NOOIR_EPUB_BLE_IDLE_ADMISSION_DIAGNOSTIC
  if (idleDiagnosticStart) {
    const size_t minimumFreeDuringStart = localHeapMinWindowStarted
        ? heap_caps_get_minimum_free_size(MALLOC_CAP_8BIT)
        : 0;
    const bool localHeapMinWindowStopped = localHeapMinWindowStarted &&
        heap_caps_monitor_local_minimum_free_size_stop() == ESP_OK;
    g_epubIdleBleDiagnosticStartMinFree.store(static_cast<uint32_t>(minimumFreeDuringStart),
                                               std::memory_order_release);
    const size_t afterFree = ESP.getFreeHeap();
    const size_t afterLargest = ESP.getMaxAllocHeap();
    LOG_INF("EPUBBLE", "stage=idle_start_host_ready elapsed_ms=%u free=%u largest=%u delta_free=%d delta_largest=%d "
                        "min_free_during_start=%u min_window=%u",
            static_cast<unsigned>(millis() - g_epubIdleBleDiagnosticStartMs.load(std::memory_order_acquire)),
            static_cast<unsigned>(afterFree), static_cast<unsigned>(afterLargest),
            static_cast<int32_t>(afterFree) - static_cast<int32_t>(beforeFree),
            static_cast<int32_t>(afterLargest) - static_cast<int32_t>(beforeLargest),
            static_cast<unsigned>(minimumFreeDuringStart), localHeapMinWindowStopped ? 1U : 0U);
    const bool belowIdleFloor = !readerBleEpubIdleOnlyPostStartSafe(afterFree, afterLargest);
    if (!started || belowIdleFloor) {
      g_epubIdleBleDiagnosticFault.store(true, std::memory_order_release);
      g_epubIdleBleDiagnosticPermit.store(false, std::memory_order_release);
      if (started) stopForReaderResourcePressure();
      LOG_ERR("EPUBBLE", "stage=idle_start_fault started=%u reason=%s free=%u largest=%u floor=%u/%u",
              started ? 1U : 0U, started ? "post_start_floor" : "host_start_failed",
              static_cast<unsigned>(afterFree), static_cast<unsigned>(afterLargest),
              static_cast<unsigned>(kReaderBleEpubPageAwareRecoveryFreeFloor),
              static_cast<unsigned>(kReaderBleEpubPageAwareRecoveryLargestFloor));
      g_epubIdleBleDiagnosticStarting.store(false, std::memory_order_release);
      return false;
    }
    g_epubIdleBleDiagnosticStarting.store(false, std::memory_order_release);
  }
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
  // BLE OFF is also the no-survival-policy mode. Reader entry must not arm a
  // BLE return, touch the HID input state, or run the BLE lifecycle stop path.
  if (!SETTINGS.bluetoothEnabled) return;
  g_readerWaitingForMemory.store(false, std::memory_order_release);
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
  if (g_stopRequestPending.load(std::memory_order_acquire)) return;
  const bool wasRunning = BleHid.isRunning();
  if (!wasRunning) {
    resetNoDeviceWindow();
    BleHid.resetInputState();
    return;
  }
  bool expected = false;
  if (!g_stopRequestPending.compare_exchange_strong(expected, true, std::memory_order_acq_rel)) return;
  g_stopRequestStartedMs.store(millis(), std::memory_order_release);
  resetNoDeviceWindow();
  // Drop queued reports and decoder/held-key state even when the host has
  // started stopping. Re-admission must begin with a clean input boundary so a
  // stale press cannot leak into Reader, Settings, or a popup after reconnect.
  BleHid.resetInputState();
  // Sleep, Web, and other lifecycle boundaries may call stop defensively.
  // If Settings already has BLE Off and the host is gone, keep the path
  // genuinely inert: no NimBLE end call, power lock, or lifecycle log.
  g_suppressNextStopNotice = true;
  const uint32_t beforeFree = static_cast<uint32_t>(ESP.getFreeHeap());
  const uint32_t beforeLargest = static_cast<uint32_t>(ESP.getMaxAllocHeap());
  const unsigned long startedMs = millis();
  HalPowerManager::Lock powerLock;
  logBleHeapShape("ble_stop_request_before");
#if NOOIR_EPUB_BLE_IDLE_ADMISSION_DIAGNOSTIC
  if (g_epubIdleBleDiagnosticArmed.load(std::memory_order_acquire)) {
    g_epubIdleBleDiagnosticStopStartFree.store(beforeFree, std::memory_order_release);
    g_epubIdleBleDiagnosticStopStartLargest.store(beforeLargest, std::memory_order_release);
    LOG_INF("EPUBBLE", "stage=stop_begin free=%u largest=%u",
            static_cast<unsigned>(beforeFree), static_cast<unsigned>(beforeLargest));
  }
#endif
  heap_lifetime_trace::setContext(heap_lifetime_trace::CallerId::BleShutdown);
  heap_lifetime_trace::checkpoint("ble_shutdown_begin");
  BleHid.end();
  heap_lifetime_trace::checkpoint("ble_shutdown_request_return");
  const char* activityAfterStop = activityManager.currentActivityName();
  if (std::strcmp(activityAfterStop, "Home") == 0 || std::strcmp(activityAfterStop, "RecentBooks") == 0) {
    heap_lifetime_trace::setContext(heap_lifetime_trace::CallerId::HomeActivity);
  } else if (activityManager.isReaderActivity()) {
    heap_lifetime_trace::setContext(heap_lifetime_trace::CallerId::ReaderEntry);
  }
  logBleHeapShape("ble_stop_request_return");
#if FREEINK_CAP_BLE_HID_HOST && NOOIR_BLE_DIAGNOSTICS && FREEINK_BLE_HID_RAW_DIAGNOSTICS
  BleHid.clearRawReports();
#endif
#if FREEINK_CAP_BLE_HID_HOST && NOOIR_BLE_DIAGNOSTICS
  serialDiagnostic("stop_request act=%.20s call_ms=%lu f0=%lu m0=%lu f1=%lu m1=%lu running=%u pending=1",
                   g_activityName, millis() - startedMs,
                   static_cast<unsigned long>(beforeFree), static_cast<unsigned long>(beforeLargest),
                   static_cast<unsigned long>(ESP.getFreeHeap()),
                   static_cast<unsigned long>(ESP.getMaxAllocHeap()), BleHid.isRunning() ? 1U : 0U);
#endif
#endif
}

bool stopPending() {
#if FREEINK_CAP_BLE_HID_HOST
  return g_stopRequestPending.load(std::memory_order_acquire);
#else
  return false;
#endif
}

void pollLifecycle() {
#if FREEINK_CAP_BLE_HID_HOST
  // isRunning() remains true until the worker is parked and removed and NimBLE
  // has been confirmed deinitialized. This is the acknowledgement edge for
  // every asynchronous stop request.
  if (g_stopRequestPending.load(std::memory_order_acquire) && !BleHid.isRunning()) {
    if (g_stopRequestPending.exchange(false, std::memory_order_acq_rel)) {
      const uint32_t elapsedMs = millis() - g_stopRequestStartedMs.exchange(0, std::memory_order_acq_rel);
#if NOOIR_EPUB_BLE_IDLE_ADMISSION_DIAGNOSTIC
      g_epubIdleBleDiagnosticLastStopAckMs.store(millis(), std::memory_order_release);
      if (g_epubIdleBleDiagnosticArmed.load(std::memory_order_acquire)) {
        const uint32_t freeAfter = static_cast<uint32_t>(ESP.getFreeHeap());
        const uint32_t largestAfter = static_cast<uint32_t>(ESP.getMaxAllocHeap());
        const uint32_t freeBefore = g_epubIdleBleDiagnosticStopStartFree.load(std::memory_order_acquire);
        const uint32_t largestBefore = g_epubIdleBleDiagnosticStopStartLargest.load(std::memory_order_acquire);
        LOG_INF("EPUBBLE", "stage=stop_ack elapsed_ms=%u free=%u largest=%u recovered=%u/%u",
                static_cast<unsigned>(elapsedMs), static_cast<unsigned>(freeAfter),
                static_cast<unsigned>(largestAfter),
                static_cast<unsigned>(freeAfter > freeBefore ? freeAfter - freeBefore : 0U),
                static_cast<unsigned>(largestAfter > largestBefore ? largestAfter - largestBefore : 0U));
      }
#endif
      LOG_INF("BLELC", "stage=stop_complete elapsed_ms=%u free=%u largest=%u owner=main_poll",
              static_cast<unsigned>(elapsedMs), static_cast<unsigned>(ESP.getFreeHeap()),
              static_cast<unsigned>(ESP.getMaxAllocHeap()));
#if NOOIR_BLE_DIAGNOSTICS
      serialDiagnostic("stop_complete elapsed_ms=%lu free=%lu largest=%lu owner=main_poll",
                       static_cast<unsigned long>(elapsedMs), static_cast<unsigned long>(ESP.getFreeHeap()),
                       static_cast<unsigned long>(ESP.getMaxAllocHeap()));
#endif
    }
  }
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
    g_readerWaitingForMemory.store(false, std::memory_order_release);
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
#if NOOIR_EPUB_BLE_IDLE_ADMISSION_DIAGNOSTIC
  if (g_epubIdleBleDiagnosticArmed.load(std::memory_order_acquire) && running &&
      !epubIdleBleDiagnosticFaulted()) {
    const size_t freeHeap = ESP.getFreeHeap();
    const size_t largest = ESP.getMaxAllocHeap();
    if (!readerBleEpubIdleOnlyPostStartSafe(freeHeap, largest)) {
      bool expectedFault = false;
      if (g_epubIdleBleRuntimeFloorFault.compare_exchange_strong(expectedFault, true,
                                                                  std::memory_order_acq_rel)) {
        g_epubIdleBleDiagnosticPermit.store(false, std::memory_order_release);
        LOG_ERR("EPUBBLE", "stage=runtime_floor_fault free=%u largest=%u floor=%u/%u action=stop",
                static_cast<unsigned>(freeHeap), static_cast<unsigned>(largest),
                static_cast<unsigned>(kReaderBleEpubPageAwareRecoveryFreeFloor),
                static_cast<unsigned>(kReaderBleEpubPageAwareRecoveryLargestFloor));
        stopForReaderResourcePressure();
      }
    }
  }
#endif
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
#if NOOIR_EPUB_BLE_IDLE_ADMISSION_DIAGNOSTIC
      if (g_epubIdleBleDiagnosticArmed.load(std::memory_order_acquire)) {
        const uint32_t startMs = g_epubIdleBleDiagnosticStartMs.load(std::memory_order_acquire);
        const uint32_t stopAckMs = g_epubIdleBleDiagnosticLastStopAckMs.load(std::memory_order_acquire);
        LOG_INF("EPUBBLE", "stage=hid_ready elapsed_from_start_ms=%u elapsed_from_stop_ack_ms=%u "
                            "start_free=%u start_largest=%u min_free_during_start=%u free=%u largest=%u",
                startMs ? static_cast<unsigned>(millis() - startMs) : 0U,
                stopAckMs ? static_cast<unsigned>(millis() - stopAckMs) : 0U,
                static_cast<unsigned>(g_epubIdleBleDiagnosticStartFree.load(std::memory_order_acquire)),
                static_cast<unsigned>(g_epubIdleBleDiagnosticStartLargest.load(std::memory_order_acquire)),
                static_cast<unsigned>(g_epubIdleBleDiagnosticStartMinFree.load(std::memory_order_acquire)),
                static_cast<unsigned>(ESP.getFreeHeap()), static_cast<unsigned>(ESP.getMaxAllocHeap()));
      }
#endif
      char message[64];
      snprintf(message, sizeof(message), "Bluetooth connected: %.36s", BleHid.connectedName());
      queueNotification(message);
      g_readerBleReturnPending.store(false, std::memory_order_release);
      g_readerBleReturnRetryBlocked.store(false, std::memory_order_release);
      g_readerWaitingForMemory.store(false, std::memory_order_release);
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
  // Generic descriptor tokens only participate in their full learned
  // signature; never let them alias a legacy keycode binding.
  if (ev.learnedIdentityValid) return false;
  if (ev.usagePage == 0x0D && mapping::validIdentity(mapping::kIineGestureIdentityKind, ev.keycode)) {
    kind = mapping::kIineGestureIdentityKind;
    value = ev.keycode;
    return true;
  }
  if (ev.usagePage == 0x09 &&
      mapping::validIdentity(mapping::kIineMouseButtonIdentityKind, ev.keycode)) {
    kind = mapping::kIineMouseButtonIdentityKind;
    value = ev.keycode;
    return true;
  }
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
  if (ev.learnedIdentityValid && mapping::validIdentity(ev.learnedIdentityKind, ev.learnedIdentityValue)) {
    kind = ev.learnedIdentityKind;
    value = ev.learnedIdentityValue;
    return true;
  }
  if (mapping::encodeNormalizedSpecialForLearning(ev.specialNormalized,
                                                  static_cast<uint8_t>(ev.special), kind, value))
    return true;
  return mapping::encodeHidUsageForLearning(ev.usagePage, ev.keycode, kind, value);
}

void clearPendingIineGesture() {
#if FREEINK_CAP_BLE_HID_HOST && NOOIR_BLE_IINE_TOUCH_GESTURES
  if (SETTINGS.bluetoothEnabled) BleHid.clearPendingGestureInput();
#endif
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
  if (kind == mapping::kIineGestureIdentityKind) {
    const char* name = nullptr;
    switch (value) {
      case mapping::kIineGestureTap: name = "Tap"; break;
      case mapping::kIineGestureSwipeLeft: name = "Swipe Left"; break;
      case mapping::kIineGestureSwipeRight: name = "Swipe Right"; break;
      case mapping::kIineGestureSwipeUp: name = "Swipe Up"; break;
      case mapping::kIineGestureSwipeDown: name = "Swipe Down"; break;
      default: break;
    }
    if (value == mapping::kIineGestureSwipeLeft || value == mapping::kIineGestureSwipeRight ||
        value == mapping::kIineGestureSwipeUp || value == mapping::kIineGestureSwipeDown) {
      const auto physical = bleinput::iine_touch::physicalDirectionForGesture(
          static_cast<bleinput::iine_touch::Gesture>(value));
      const char* synthetic = value == mapping::kIineGestureSwipeLeft ? "left" :
                              value == mapping::kIineGestureSwipeRight ? "right" :
                              value == mapping::kIineGestureSwipeUp ? "up" : "down";
      snprintf(out, outLen, "IINE %s (swipe %s)",
               bleinput::iine_touch::physicalDirectionName(physical), synthetic);
    }
    else if (name) snprintf(out, outLen, "IINE %s", name);
    else snprintf(out, outLen, "IINE gesture 0x%02X", static_cast<unsigned>(value));
    return;
  }
  if (kind == mapping::kIineMouseButtonIdentityKind && value >= 1) {
    snprintf(out, outLen, "HID Button %u", static_cast<unsigned>(value));
    return;
  }
  if (kind == 2) {
    snprintf(out, outLen, "Keyboard 0x%02X", static_cast<unsigned>(value));
    return;
  }
  if (kind == 3) {
    snprintf(out, outLen, "Consumer 0x%02X", static_cast<unsigned>(value));
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
