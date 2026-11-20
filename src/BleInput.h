#pragma once

// CrossPoint <-> FreeInk BLE HID host glue.
//
// Thin, capability-safe helpers around freeink::BleKeyboardHost (the `BleHid`
// singleton). When FREEINK_CAP_BLE_HID_HOST is compiled out the SDK links stubs,
// so every call here is still valid and simply no-ops / returns false — callers
// need no #ifdefs.
//
// The (kind, value) pair produced by encodeKey() is the stable identity stored in
// CrossPointSettings::bleKeyMap. Page-turner remotes emit "special" keys
// (PageUp/PageDown/arrows); plain keyboards emit usage codes. We deliberately
// ignore modifiers and the printable char for matching (page turners don't use
// modifiers), keeping the persisted entry a trivial two-byte comparison.

#include <BleKeyboardHost.h>

#include <cstddef>
#include <cstdint>

#ifndef FREEINK_CAP_BLE_HID_HOST
#define FREEINK_CAP_BLE_HID_HOST 0
#endif

#ifndef NOOIR_BLE_DIAGNOSTICS
#define NOOIR_BLE_DIAGNOSTICS 0
#endif

#ifndef FREEINK_BLE_HID_RAW_DIAGNOSTICS
#define FREEINK_BLE_HID_RAW_DIAGNOSTICS 0
#endif

#ifndef NOOIR_BLE_READER_COEXISTENCE
#define NOOIR_BLE_READER_COEXISTENCE 0
#endif

#ifndef NOOIR_EPUB_BLE_IDLE_ADMISSION_DIAGNOSTIC
#define NOOIR_EPUB_BLE_IDLE_ADMISSION_DIAGNOSTIC 0
#endif

#ifndef NOOIR_BLE_IINE_TOUCH_GESTURES
#define NOOIR_BLE_IINE_TOUCH_GESTURES 0
#endif

namespace bleinput {

// Advertised central name shown to peripherals during pairing.
inline constexpr const char* kHostName = "Folio Nooir";

// Start the BLE HID host (idempotent). Returns false if BLE is compiled out or
// NimBLE init failed. Safe to call repeatedly.
bool ensureStarted();

// Single application-owned permission predicate for BLE start, reconnect,
// connect, and scan operations. The generic FreeInk host consults this through
// its callback without depending on Nooir activity or rendering classes.
bool connectionAdmissionAllowed();

// Shared Bluetooth preference transition. Disabling requests teardown (which
// may be completed by the connection worker after an in-flight call unwinds);
// enabling only records intent for the centralized lifecycle gate.
bool setBluetoothEnabled(bool enabled);
void toggleBluetooth();
// Clear a no-device timeout pause after an explicit retry action.
void clearNoDeviceTimeoutSuppression();

// Read the saved Bluetooth preference used by startup, Home, and all readers.
bool bluetoothEnabled();
// Human-readable, boot-scoped host state used by allocation lifecycle logs.
// This distinguishes BLE that was never initialized from a host stopped after use.
const char* runtimeStateName();
// Derived from existing host/Reader lifecycle values; no extra poll or task.
const char* userFacingStatusText();
// True only after the quiescent Reader heap-admission predictor rejects readmission.
bool readerWaitingForMemory();
void setReaderWaitingForMemory(bool waiting);

// Drop the active link (e.g. before deep sleep or when the user disables BT).
void stop();
// True after one stop request has been issued and until pollLifecycle observes
// the host's fully stopped state. Callers must treat this as pending teardown,
// not as proof that NimBLE memory has already been reclaimed.
bool stopPending();

// Stop BLE for allocation-heavy Reader work, remembering an eligible connected
// session so the centralized lifecycle can return it after Reader quiesces.
// When the host is already off because a previous admission was deferred, this
// also preserves the enabled/wanted Reader return intent without requiring a
// live NimBLE connection.
void stopForReaderResourcePressure();

// True only for a BLE-enabled session that was running/connected before a
// Reader resource stop. This state never affects BLE-OFF Reader behavior.
bool readerBleReturnPending();
void clearReaderBleReturnPending();
uint32_t readerBleReturnGeneration();
// Compile-gated sequential EPUB/BLE experiment. The X4 experiment is latched
// automatically at a stable EPUB page; the helpers are inert in normal builds
// and BLE-OFF mode. Serial status/disarm commands remain optional diagnostics.
bool armEpubIdleBleDiagnostic();
void disarmEpubIdleBleDiagnostic(const char* reason);
bool epubIdleBleDiagnosticArmed();
bool epubIdleBleDiagnosticStarting();
bool epubIdleBleDiagnosticFaulted();
// Clear only a connected-idle runtime floor trip after a completed render and
// confirmed worker teardown. Host-start/post-start initialization faults stay
// latched until the diagnostic is explicitly re-armed.
bool clearEpubIdleBleRuntimeFloorFaultAfterRender(bool renderComplete, bool readerQuiescent);
bool epubIdleBleDiagnosticPermitPending();
void grantEpubIdleBleDiagnosticPermit();
bool consumeEpubIdleBleDiagnosticPermit();
// Publish the strongest recent EPUB render demand for predictive BLE
// readmission. Values reset at Reader entry/exit and are read atomically by
// the lifecycle gate; this does not allocate or trigger a retry.
void setReaderBleExpectedRenderDemand(std::size_t peakBytes, std::size_t largestRequestBytes);
void setReaderBlePageAwareRenderDemand(std::size_t freeBytes, std::size_t largestBytes, bool known);
// Mark one genuine Reader memory-lifecycle boundary. This unblocks a failed
// one-shot cleanup attempt without creating a polling retry loop.
void signalReaderBleReturnLifecycleEvent(const char* reason);
void blockReaderBleReturnRetry();
void allowReaderBleReturnRetry();

// Web/Transfer owns the heap for the duration of its session. While active,
// BLE admission and reconnects are suppressed even when the user setting is
// enabled. Clearing the mode restores ordinary lifecycle admission.
void setWebExclusive(bool active);
bool webExclusiveActive();

// Sample the published host state and queue bounded lifecycle telemetry.  This
// never performs BLE work itself and must be called from the main loop.
void pollLifecycle();

// Coalesce a meaningful activity/resource transition into one centralized
// BLE eligibility evaluation. Admission failures wait for the next signal
// rather than polling/retrying every main-loop pass.
void requestLifecycleReevaluation();
bool takeLifecycleReevaluation();
bool reAdmissionPending();
// Keep the user's enabled intent pending across temporary Wi-Fi/activity or
// heap-pressure suppression. A successful cold admission clears it; failed
// admissions leave it set until a later real lifecycle event requests another
// evaluation.
void clearReAdmissionPending();

// Consume one non-blocking lifecycle notice. Intentional teardown is silent.
bool takeNotification(char* out, size_t outLen);

// Mark a direct link drop (manual disconnect/unpair) as intentional.
void suppressNextDisconnectNotice();

// Encode a decoded key event into the stable (kind, value) identity used by the
// settings map. kind: 0 = SpecialKey, 1 = legacy/untyped usage. Returns false when the event
// carries no usable identity (no special key and no usage code).
bool encodeKey(const freeink::KeyEvent& ev, uint8_t& kind, uint8_t& value);
// Safe identity for Learn mode: special keys are already normalized; ordinary
// usages are learnable only when FreeInk identified the HID Keyboard page.
// Unclassified/synthetic report bytes are rejected instead of persisted.
bool encodeLearnedKey(const freeink::KeyEvent& ev, uint8_t& kind, uint8_t& value);
// Drop only partial/queued IINE gestures at a BLE-enabled Reader boundary.
void clearPendingIineGesture();

// Human-readable name for a stored (kind, value) identity, for the mapping UI.
// Writes a null-terminated string into out (e.g. "Page Down", "Key 0x4B").
void describeKey(uint8_t kind, uint8_t value, char* out, size_t outLen);

// The pinned FreeInk host exposes decoded KeyEvent values, but not raw HID
// report bytes or active-link RSSI. Keep diagnostics honest and record only the
// fields available at the Nooir boundary.
enum class DiagnosticType : uint8_t { State = 1, Input = 2 };

struct DiagnosticRecord {
  uint32_t uptimeMs = 0;
  uint32_t freeHeap = 0;
  uint32_t minFreeHeap = 0;
  uint32_t largestFreeBlock = 0;
  uint32_t bleLowFreeHeap = 0;
  uint32_t bleLowLargestFreeBlock = 0;
  DiagnosticType type = DiagnosticType::State;
  uint8_t keyKind = 0xFF;
  uint8_t keyValue = 0;
  uint8_t mappedButton = 0xFF;
  bool running = false;
  bool connected = false;
  bool connecting = false;
  bool scanning = false;
  char deviceName[16] = {};
};

constexpr uint8_t kDiagnosticCapacity = 12;

enum class InputDisposition : uint8_t {
  Unusable = 0,
  Unmapped,
  Captured,
  Pending,
  Debounced,
  Mapped,
};

// Diagnostics-only activity context. The normal build compiles this to a
// no-op, and the diagnostic profile supplies it immediately before BLE input
// is drained so event records identify the active Activity without polling it.
void setActivityContext(const char* name);

// Compile-gated diagnostic event channel used by the BLE settings/mapping
// boundary. The normal build formats nothing and emits no serial output.
#if FREEINK_CAP_BLE_HID_HOST && NOOIR_BLE_DIAGNOSTICS
void recordDiagnosticEvent(const char* format, ...);
#else
inline void recordDiagnosticEvent(const char*, ...) {}
#endif

void recordDecodedKey(const freeink::KeyEvent& event, uint8_t kind, uint8_t value, uint8_t mappedButton,
                      InputDisposition disposition = InputDisposition::Unmapped,
                      bool inactivityReset = false);
uint8_t diagnosticCount();
bool diagnosticAtNewest(uint8_t newestIndex, DiagnosticRecord& out);
void clearDiagnostics();
void formatDiagnostic(const DiagnosticRecord& record, char* out, size_t outLen);

}  // namespace bleinput
