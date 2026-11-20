#include "HeapLifetimeDiagnostics.h"

#if NOOIR_HEAP_LIFETIME_TRACE

#include <Arduino.h>
#include <Logging.h>
#include <esp_bt.h>
#include <esp_heap_caps.h>
#include <esp_memory_utils.h>
#include <esp_timer.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>

#include <stdint.h>

namespace heap_lifetime_trace {
namespace {

using Store = TraceStore<160, 48>;
static_assert(sizeof(Store) <= 5900, "heap lifetime trace storage exceeded its documented budget");

Store g_store;
volatile bool g_recording = false;
volatile bool g_insideHook = false;
bool g_startedOnce = false;
volatile uint8_t g_context = static_cast<uint8_t>(CallerId::Boot);
uint32_t g_hookCount = 0;
uint32_t g_reentrantEvents = 0;
uint32_t g_hookSampleCount = 0;
uint32_t g_hookSampleTotalUs = 0;
uint32_t g_hookSampleMaxUs = 0;
uint32_t g_registerFailureHookResult = 0;

uint32_t timestampUs() { return static_cast<uint32_t>(esp_timer_get_time()); }

uint32_t hashFunctionName(const char* functionName) {
  uint32_t hash = UINT32_C(2166136261);
  if (!functionName) return hash;
  for (size_t i = 0; i < 40 && functionName[i] != '\0'; ++i) {
    hash ^= static_cast<uint8_t>(functionName[i]);
    hash *= UINT32_C(16777619);
  }
  return hash;
}

void recordFailedAllocation(const size_t size, const uint32_t caps, const char* functionName) {
  if (!g_recording) return;
  if (g_insideHook) {
    ++g_reentrantEvents;
    return;
  }
  g_insideHook = true;
  const CallerId caller = static_cast<CallerId>(g_context);
  g_store.recordFailure(size, caps, timestampUs(), caller,
                        static_cast<uint16_t>(hashFunctionName(functionName)));
  ++g_hookCount;
  g_insideHook = false;
}

bool recordingWasActive() {
  const bool wasActive = g_recording;
  g_recording = false;
  return wasActive;
}

struct CheckpointSnapshot {
  TraceSummary summary{};
  uint32_t freeBytes = 0;
  uint32_t largestBlock = 0;
  uint32_t phaseLiveCount = 0;
  uint32_t phaseLiveBytes = 0;
  uint32_t actualBytes = 0;
  uint32_t actualCount = 0;
  uint32_t phaseActualBytes = 0;
  uint32_t phaseActualCount = 0;
  uint32_t sampleUs = 0;
  bool actualTrusted = false;
  bool coverageComplete = false;
};

CheckpointSnapshot captureCheckpointSnapshot(const CallerId phase, const bool persistActualSizes,
                                              const bool resumeRecording) {
  CheckpointSnapshot snapshot{};
  const int64_t beginUs = esp_timer_get_time();
  vTaskSuspendAll();
  snapshot.summary = g_store.summary();
  // Reentrant drops can make a tracked address stale, so skip pointer queries
  // in that case. Capacity/unmatched counters reduce completeness, but do not
  // invalidate the live addresses that were successfully tracked.
  snapshot.actualTrusted = g_reentrantEvents == 0;
  snapshot.coverageComplete = snapshot.summary.droppedLiveRecords == 0 &&
                              snapshot.summary.unmatchedFrees == 0 && g_reentrantEvents == 0;
  snapshot.freeBytes = static_cast<uint32_t>(ESP.getFreeHeap());
  snapshot.largestBlock = static_cast<uint32_t>(ESP.getMaxAllocHeap());
  LiveAllocation allocation{};
  for (size_t i = 0; i < snapshot.summary.liveCount; ++i) {
    if (!g_store.liveAt(i, allocation)) break;
    if (allocation.caller == phase) {
      ++snapshot.phaseLiveCount;
      snapshot.phaseLiveBytes += allocation.size;
    }
    if (snapshot.actualTrusted) {
      const size_t actualSize = heap_caps_get_allocated_size(reinterpret_cast<void*>(allocation.address));
      snapshot.actualBytes += static_cast<uint32_t>(actualSize);
      ++snapshot.actualCount;
      if (allocation.caller == phase) {
        snapshot.phaseActualBytes += static_cast<uint32_t>(actualSize);
        ++snapshot.phaseActualCount;
      }
      if (persistActualSizes) g_store.setActualSize(allocation.address, actualSize);
    }
  }
  snapshot.sampleUs = static_cast<uint32_t>(esp_timer_get_time() - beginUs);
  // Re-arm before resuming other tasks, so frees after this point stay in the trace.
  g_recording = resumeRecording;
  static_cast<void>(xTaskResumeAll());
  return snapshot;
}

const char* memoryRegion(const void* address) {
  if (!address) return "none";
  if (esp_ptr_internal(address)) return "internal";
  if (esp_ptr_external_ram(address)) return "external";
  return "other";
}

}  // namespace

const char* callerName(const CallerId caller) {
  switch (caller) {
    case CallerId::Boot:
      return "boot";
    case CallerId::HomeActivity:
      return "home_activity";
    case CallerId::HomeRender:
      return "home_render";
    case CallerId::HomeBeforeBle:
      return "home_before_ble";
    case CallerId::NimbleInit:
      return "nimble_init";
    case CallerId::BleControllerInit:
      return "ble_controller_init";
    case CallerId::BleHostInit:
      return "ble_host_init";
    case CallerId::BleClientCreate:
      return "ble_client_create";
    case CallerId::BleHostReady:
      return "ble_host_ready";
    case CallerId::HidConnect:
      return "hid_connect";
    case CallerId::HidDiscovery:
      return "hid_discovery";
    case CallerId::HidSecurity:
      return "hid_security";
    case CallerId::HidReady:
      return "hid_ready";
    case CallerId::BleShutdown:
      return "ble_shutdown";
    case CallerId::BleControllerCleanup:
      return "ble_controller_cleanup";
    case CallerId::ReaderEntry:
      return "reader_entry";
    case CallerId::XtcReader:
      return "xtc_reader";
    case CallerId::XtcReaderExit:
      return "xtc_reader_exit";
    case CallerId::HomeDestroy:
      return "home_destroy";
    case CallerId::SleepCleanup:
      return "sleep_cleanup";
    case CallerId::TraceControl:
      return "trace_control";
    case CallerId::ReaderExit:
      return "reader_exit";
    default:
      return "unknown";
  }
}

const char* eventName(const EventKind kind) {
  switch (kind) {
    case EventKind::Allocate:
      return "alloc";
    case EventKind::Free:
      return "free";
    case EventKind::Failed:
      return "failed";
    case EventKind::UnmatchedFree:
      return "unmatched_free";
    default:
      return "unknown";
  }
}

void start() {
  if (g_startedOnce) return;
  g_startedOnce = true;
  g_recording = false;
  g_store.reset();
  g_hookCount = 0;
  g_reentrantEvents = 0;
  g_hookSampleCount = 0;
  g_hookSampleTotalUs = 0;
  g_hookSampleMaxUs = 0;
  g_context = static_cast<uint8_t>(CallerId::Boot);
  const esp_err_t registerResult =
      heap_caps_register_failed_alloc_callback(recordFailedAllocation);
  g_registerFailureHookResult = static_cast<uint32_t>(registerResult);
  g_recording = true;
  checkpoint("trace_started");
}

void setContext(const CallerId caller) { g_context = static_cast<uint8_t>(caller); }

void checkpoint(const char* label) {
  // Never wait for an allocator hook. On the single-core target this also
  // avoids reading the fixed store midway through a preempted hook update.
  if (g_insideHook) return;
  const bool wasActive = recordingWasActive();
  const CallerId owner = static_cast<CallerId>(g_context);
  const CheckpointSnapshot snapshot = captureCheckpointSnapshot(owner, true, wasActive);
  // Any logger allocations are real allocations too. Tag them separately
  // instead of pausing tracing or silently losing unrelated task activity.
  g_context = static_cast<uint8_t>(CallerId::TraceControl);
  LOG_INF("HEAPTRACE", "checkpoint=%.22s phase=%s free=%u largest=%u live=%u phase_live=%u "
                       "phase_live_requested=%u actual_status=%s coverage=%s actual_count=%u actual_bytes=%u "
                       "phase_actual_count=%u phase_actual_bytes=%u sample_us=%u allocs=%u frees=%u "
                       "dropped_live=%u dropped_events=%u",
          label ? label : "unknown", callerName(owner), static_cast<unsigned>(snapshot.freeBytes),
          static_cast<unsigned>(snapshot.largestBlock), static_cast<unsigned>(snapshot.summary.liveCount),
          static_cast<unsigned>(snapshot.phaseLiveCount), static_cast<unsigned>(snapshot.phaseLiveBytes),
          snapshot.actualTrusted ? "rounded" : "untrusted",
          snapshot.coverageComplete ? "complete" : "partial", static_cast<unsigned>(snapshot.actualCount),
          static_cast<unsigned>(snapshot.actualBytes), static_cast<unsigned>(snapshot.phaseActualCount),
          static_cast<unsigned>(snapshot.phaseActualBytes), static_cast<unsigned>(snapshot.sampleUs),
          static_cast<unsigned>(snapshot.summary.allocations), static_cast<unsigned>(snapshot.summary.frees),
          static_cast<unsigned>(snapshot.summary.droppedLiveRecords),
          static_cast<unsigned>(snapshot.summary.droppedEvents));
  g_context = static_cast<uint8_t>(owner);
}

void stopAndDump() {
  if (g_insideHook) return;
  g_recording = false;
  // This is a terminal trace snapshot. Scheduler suspension keeps all live
  // pointers valid while ESP-IDF reports allocator-rounded block sizes.
  const CheckpointSnapshot snapshot = captureCheckpointSnapshot(CallerId::TraceControl, true, false);
  const TraceSummary summary = snapshot.summary;
  LOG_INF("HEAPTRACE", "dump storage=%u free=%u largest=%u hooks=%u allocs=%u frees=%u failed=%u "
                       "unmatched_free=%u live=%u dropped_live=%u dropped_events=%u recursive=%u "
                       "max_probe=%u hook_sample_count=%u hook_sample_total_us=%u hook_sample_max_us=%u "
                       "failed_hook_register=%u actual_status=%s coverage=%s actual_count=%u actual_bytes=%u sample_us=%u "
                       "timebase=esp_timer_get_time_us_low32",
          static_cast<unsigned>(g_store.storageBytes()), static_cast<unsigned>(snapshot.freeBytes),
          static_cast<unsigned>(snapshot.largestBlock), static_cast<unsigned>(g_hookCount),
          static_cast<unsigned>(summary.allocations), static_cast<unsigned>(summary.frees),
          static_cast<unsigned>(summary.failedAllocations), static_cast<unsigned>(summary.unmatchedFrees),
          static_cast<unsigned>(summary.liveCount), static_cast<unsigned>(summary.droppedLiveRecords),
          static_cast<unsigned>(summary.droppedEvents), static_cast<unsigned>(g_reentrantEvents),
          static_cast<unsigned>(summary.maxProbeCount), static_cast<unsigned>(g_hookSampleCount),
          static_cast<unsigned>(g_hookSampleTotalUs), static_cast<unsigned>(g_hookSampleMaxUs),
          static_cast<unsigned>(g_registerFailureHookResult), snapshot.actualTrusted ? "rounded" : "untrusted",
          snapshot.coverageComplete ? "complete" : "partial",
          static_cast<unsigned>(snapshot.actualCount), static_cast<unsigned>(snapshot.actualBytes),
          static_cast<unsigned>(snapshot.sampleUs));

  for (uint8_t raw = 1; raw < static_cast<uint8_t>(CallerId::Count); ++raw) {
    const CallerId caller = static_cast<CallerId>(raw);
    const CallerUsage usage = g_store.callerUsage(caller);
    if (!usage.allocationCount && !usage.freeCount && !usage.failedCount) continue;
    LOG_INF("HEAPTRACE", "owner=%s id=%u alloc_count=%u alloc_bytes=%u free_count=%u freed_bytes=%u failed=%u",
            callerName(caller), static_cast<unsigned>(raw), static_cast<unsigned>(usage.allocationCount),
            static_cast<unsigned>(usage.allocationBytes), static_cast<unsigned>(usage.freeCount),
            static_cast<unsigned>(usage.freedBytes), static_cast<unsigned>(usage.failedCount));
  }

  LiveAllocation allocation{};
  for (size_t i = 0; i < summary.liveCount; ++i) {
    if (!g_store.liveAt(i, allocation)) break;
    const uint32_t ageUs = timestampUs() - allocation.allocatedAtUs;
    LOG_INF("HEAPTRACE", "live address=%p requested_bytes=%u actual_bytes=%u caps_requested=0x%08lx caller=%u/%s "
                         "allocated_us=%lu age_us=%lu region=%s",
            reinterpret_cast<void*>(allocation.address), static_cast<unsigned>(allocation.size),
            static_cast<unsigned>(allocation.actualSize), static_cast<unsigned long>(allocation.requestedCaps),
            static_cast<unsigned>(allocation.caller),
            callerName(allocation.caller), static_cast<unsigned long>(allocation.allocatedAtUs),
            static_cast<unsigned long>(ageUs), memoryRegion(reinterpret_cast<void*>(allocation.address)));
  }

  AllocationEvent event{};
  for (size_t i = 0; i < g_store.eventCount(); ++i) {
    if (!g_store.eventAt(i, event)) break;
    LOG_INF("HEAPTRACE", "event=%s address=%p requested_bytes=%u actual_bytes=%u caps_requested=0x%08lx alloc_caller=%u/%s "
                         "event_caller=%u/%s caller_id=0x%04x task_token=0x%04x time_us=%lu",
            eventName(event.kind), reinterpret_cast<void*>(event.address), static_cast<unsigned>(event.size),
            static_cast<unsigned>(event.actualSize),
            static_cast<unsigned long>(event.requestedCaps), static_cast<unsigned>(event.allocationCaller),
            callerName(static_cast<CallerId>(event.allocationCaller)), static_cast<unsigned>(event.eventCaller),
            callerName(static_cast<CallerId>(event.eventCaller)), static_cast<unsigned>(event.callerId),
            static_cast<unsigned>(event.taskToken), static_cast<unsigned long>(event.timestampUs));
  }
}

}  // namespace heap_lifetime_trace

extern "C" void esp_heap_trace_alloc_hook(void* pointer, const size_t size,
                                                        const uint32_t caps) {
  using namespace heap_lifetime_trace;
  if (!g_recording) return;
  if (g_insideHook) {
    ++g_reentrantEvents;
    return;
  }
  g_insideHook = true;
  const int64_t startUs = esp_timer_get_time();
  const CallerId caller = static_cast<CallerId>(g_context);
  const uint16_t taskToken = 0;  // Caller context is explicit; no task API is called in the hook.
  g_store.recordAllocation(reinterpret_cast<uintptr_t>(pointer), size, caps,
                            static_cast<uint32_t>(startUs), caller, taskToken);
  ++g_hookCount;
  if ((g_hookCount & 63U) == 0) {
    const uint32_t elapsedUs = static_cast<uint32_t>(esp_timer_get_time() - startUs);
    ++g_hookSampleCount;
    g_hookSampleTotalUs += elapsedUs;
    if (elapsedUs > g_hookSampleMaxUs) g_hookSampleMaxUs = elapsedUs;
  }
  g_insideHook = false;
}

extern "C" void esp_heap_trace_free_hook(void* pointer) {
  using namespace heap_lifetime_trace;
  if (!g_recording || !pointer) return;
  if (g_insideHook) {
    ++g_reentrantEvents;
    return;
  }
  g_insideHook = true;
  const int64_t startUs = esp_timer_get_time();
  g_store.recordFree(reinterpret_cast<uintptr_t>(pointer), static_cast<uint32_t>(startUs),
                     static_cast<CallerId>(g_context), 0);
  ++g_hookCount;
  if ((g_hookCount & 63U) == 0) {
    const uint32_t elapsedUs = static_cast<uint32_t>(esp_timer_get_time() - startUs);
    ++g_hookSampleCount;
    g_hookSampleTotalUs += elapsedUs;
    if (elapsedUs > g_hookSampleMaxUs) g_hookSampleMaxUs = elapsedUs;
  }
  g_insideHook = false;
}

extern "C" esp_err_t __real_esp_bt_controller_init(esp_bt_controller_config_t* config);
extern "C" esp_err_t __real_esp_bt_controller_deinit(void);

extern "C" esp_err_t __wrap_esp_bt_controller_init(esp_bt_controller_config_t* config) {
  heap_lifetime_trace::setContext(heap_lifetime_trace::CallerId::BleControllerInit);
  const esp_err_t result = __real_esp_bt_controller_init(config);
  heap_lifetime_trace::setContext(heap_lifetime_trace::CallerId::BleHostInit);
  return result;
}

extern "C" esp_err_t __wrap_esp_bt_controller_deinit(void) {
  heap_lifetime_trace::setContext(heap_lifetime_trace::CallerId::BleControllerCleanup);
  return __real_esp_bt_controller_deinit();
}

#else

namespace heap_lifetime_trace {
const char* callerName(const CallerId) { return "disabled"; }
const char* eventName(const EventKind) { return "disabled"; }
void start() {}
void setContext(const CallerId) {}
void checkpoint(const char*) {}
void stopAndDump() {}
}  // namespace heap_lifetime_trace

#endif
