#pragma once

#include <stddef.h>
#include <stdint.h>

namespace heap_lifetime_trace {

// IDs identify the allocation call-site phase set by the diagnostic build.
// They are deliberately numeric in the hook; labels are resolved only when
// diagnostics are printed outside the allocator.
enum class CallerId : uint8_t {
  Unknown = 0,
  Boot = 1,
  HomeActivity = 2,
  HomeRender = 3,
  HomeBeforeBle = 4,
  NimbleInit = 5,
  BleControllerInit = 6,
  BleHostInit = 7,
  BleClientCreate = 8,
  BleHostReady = 9,
  HidConnect = 10,
  HidDiscovery = 11,
  HidSecurity = 12,
  HidReady = 13,
  BleShutdown = 14,
  BleControllerCleanup = 15,
  ReaderEntry = 16,
  XtcReader = 17,
  XtcReaderExit = 18,
  HomeDestroy = 19,
  SleepCleanup = 20,
  TraceControl = 21,
  ReaderExit = 22,
  Count = 23,
};

enum class EventKind : uint8_t { Allocate = 1, Free = 2, Failed = 3, UnmatchedFree = 4 };

struct AllocationEvent {
  uintptr_t address;
  uint32_t size;
  uint32_t timestampUs;
  uint32_t requestedCaps;
  uint16_t taskToken;
  uint16_t callerId;
  uint8_t allocationCaller;
  uint8_t eventCaller;
  EventKind kind;
  uint32_t actualSize;  // Allocator-rounded size when known; zero until measured.
};

struct LiveAllocation {
  uintptr_t address;
  uint32_t size;
  uint32_t allocatedAtUs;
  uint32_t requestedCaps;
  uint32_t actualSize;  // Populated at a safe checkpoint, outside allocator hooks.
  uint16_t taskToken;
  CallerId caller;
  uint8_t state;
};

struct CallerUsage {
  uint32_t allocationCount;
  uint32_t allocationBytes;
  uint32_t freeCount;
  uint32_t freedBytes;
  uint32_t failedCount;
};

struct TraceSummary {
  uint32_t allocations;
  uint32_t frees;
  uint32_t failedAllocations;
  uint32_t unmatchedFrees;
  uint32_t droppedLiveRecords;
  uint32_t droppedEvents;
  uint32_t liveCount;
  uint32_t maxProbeCount;
};

template <size_t LiveCapacity, size_t EventCapacity>
class TraceStore {
 public:
  static_assert(LiveCapacity > 0, "live allocation table must not be empty");
  static_assert(EventCapacity > 0, "allocation event ring must not be empty");

  void reset() {
    for (size_t i = 0; i < LiveCapacity; ++i) live_[i] = {};
    for (size_t i = 0; i < EventCapacity; ++i) events_[i] = {};
    for (size_t i = 0; i < static_cast<size_t>(CallerId::Count); ++i) callers_[i] = {};
    summary_ = {};
    eventHead_ = 0;
    eventCount_ = 0;
  }

  void recordAllocation(const uintptr_t address, const size_t size, const uint32_t requestedCaps,
                       const uint32_t timestampUs, const CallerId caller, const uint16_t taskToken) {
    if (address == 0) return;

    // Count every allocation reported by ESP-IDF, including ones the bounded
    // live table cannot retain. The event ring and drop counter make that loss
    // explicit instead of making allocation totals look artificially low.
    ++summary_.allocations;
    if (validCaller(caller)) {
      CallerUsage& usage = callers_[static_cast<size_t>(caller)];
      ++usage.allocationCount;
      usage.allocationBytes += toSize32(size);
    }

    size_t slot = LiveCapacity;
    bool duplicate = false;
    uint32_t probes = 0;
    findInsertSlot(address, slot, duplicate, probes);
    if (probes > summary_.maxProbeCount) summary_.maxProbeCount = probes;
    if (slot == LiveCapacity) {
      ++summary_.droppedLiveRecords;
      pushEvent({address, toSize32(size), timestampUs, requestedCaps, taskToken,
                 static_cast<uint16_t>(caller), static_cast<uint8_t>(caller),
                 static_cast<uint8_t>(caller), EventKind::Allocate});
      return;
    }

    // Same-address allocations can occur when an allocator implements an
    // in-place realloc without a separate free notification.
    if (duplicate) {
      live_[slot] = {};
      --summary_.liveCount;
      ++summary_.unmatchedFrees;
    }
    live_[slot] = {address, toSize32(size), timestampUs, requestedCaps, 0, taskToken, caller, kActive};
    ++summary_.liveCount;
    pushEvent({address, toSize32(size), timestampUs, requestedCaps, taskToken,
               static_cast<uint16_t>(caller), static_cast<uint8_t>(caller),
               static_cast<uint8_t>(caller), EventKind::Allocate});
  }

  void recordFree(const uintptr_t address, const uint32_t timestampUs, const CallerId caller,
                  const uint16_t taskToken) {
    if (address == 0) return;

    const size_t slot = findActiveSlot(address);
    if (slot == LiveCapacity) {
      ++summary_.unmatchedFrees;
      pushEvent({address, 0, timestampUs, 0, taskToken, static_cast<uint16_t>(caller),
                 static_cast<uint8_t>(CallerId::Unknown), static_cast<uint8_t>(caller),
                 EventKind::UnmatchedFree});
      return;
    }

    const LiveAllocation allocation = live_[slot];
    live_[slot].state = kTombstone;
    --summary_.liveCount;
    ++summary_.frees;
    if (validCaller(caller)) {
      CallerUsage& usage = callers_[static_cast<size_t>(caller)];
      ++usage.freeCount;
      usage.freedBytes += allocation.size;
    }
    pushEvent({address, allocation.size, timestampUs, allocation.requestedCaps, taskToken,
               static_cast<uint16_t>(caller), static_cast<uint8_t>(allocation.caller),
               static_cast<uint8_t>(caller), EventKind::Free, allocation.actualSize});
  }

  bool setActualSize(const uintptr_t address, const size_t actualSize) {
    const size_t slot = findActiveSlot(address);
    if (slot == LiveCapacity) return false;
    const uint32_t actualSize32 = toSize32(actualSize);
    live_[slot].actualSize = actualSize32;
    for (size_t offset = 0; offset < eventCount_; ++offset) {
      const size_t index = (eventHead_ + EventCapacity - 1 - offset) % EventCapacity;
      AllocationEvent& event = events_[index];
      if (event.address != address) continue;
      if (event.kind == EventKind::Allocate) event.actualSize = actualSize32;
      break;
    }
    return true;
  }

  void recordFailure(const size_t size, const uint32_t requestedCaps, const uint32_t timestampUs,
                     const CallerId caller, const uint16_t callerFunctionHash) {
    ++summary_.failedAllocations;
    if (validCaller(caller)) ++callers_[static_cast<size_t>(caller)].failedCount;
    pushEvent({0, toSize32(size), timestampUs, requestedCaps, 0, callerFunctionHash,
               static_cast<uint8_t>(caller), static_cast<uint8_t>(caller), EventKind::Failed});
  }

  TraceSummary summary() const { return summary_; }

  bool liveAt(const size_t ordinal, LiveAllocation& output) const {
    size_t seen = 0;
    for (size_t i = 0; i < LiveCapacity; ++i) {
      if (live_[i].state != kActive) continue;
      if (seen++ == ordinal) {
        output = live_[i];
        return true;
      }
    }
    return false;
  }

  bool eventAt(const size_t ordinal, AllocationEvent& output) const {
    if (ordinal >= eventCount_) return false;
    const size_t oldest = (eventHead_ + EventCapacity - eventCount_) % EventCapacity;
    output = events_[(oldest + ordinal) % EventCapacity];
    return true;
  }

  CallerUsage callerUsage(const CallerId caller) const {
    if (!validCaller(caller)) return {};
    return callers_[static_cast<size_t>(caller)];
  }

  size_t eventCount() const { return eventCount_; }
  size_t storageBytes() const { return sizeof(*this); }

 private:
  static constexpr uint8_t kEmpty = 0;
  static constexpr uint8_t kActive = 1;
  static constexpr uint8_t kTombstone = 2;

  static uint32_t toSize32(const size_t size) {
    return size > UINT32_MAX ? UINT32_MAX : static_cast<uint32_t>(size);
  }

  static bool validCaller(const CallerId caller) {
    const auto value = static_cast<size_t>(caller);
    return value > static_cast<size_t>(CallerId::Unknown) && value < static_cast<size_t>(CallerId::Count);
  }

  size_t hashAddress(const uintptr_t address) const {
    const uint32_t folded = static_cast<uint32_t>((address >> 4) ^ (address >> 17));
    return static_cast<size_t>((folded * UINT32_C(2654435761)) % LiveCapacity);
  }

  void findInsertSlot(const uintptr_t address, size_t& slot, bool& duplicate, uint32_t& probes) const {
    const size_t start = hashAddress(address);
    size_t firstTombstone = LiveCapacity;
    for (size_t n = 0; n < LiveCapacity; ++n) {
      const size_t index = (start + n) % LiveCapacity;
      ++probes;
      if (live_[index].state == kActive && live_[index].address == address) {
        slot = index;
        duplicate = true;
        return;
      }
      if (live_[index].state == kTombstone && firstTombstone == LiveCapacity) firstTombstone = index;
      if (live_[index].state == kEmpty) {
        slot = firstTombstone == LiveCapacity ? index : firstTombstone;
        return;
      }
    }
    slot = firstTombstone;
  }

  size_t findActiveSlot(const uintptr_t address) {
    const size_t start = hashAddress(address);
    for (size_t n = 0; n < LiveCapacity; ++n) {
      const size_t index = (start + n) % LiveCapacity;
      if (live_[index].state == kEmpty) return LiveCapacity;
      if (live_[index].state == kActive && live_[index].address == address) return index;
    }
    return LiveCapacity;
  }

  void pushEvent(const AllocationEvent& event) {
    events_[eventHead_] = event;
    eventHead_ = (eventHead_ + 1) % EventCapacity;
    if (eventCount_ < EventCapacity) {
      ++eventCount_;
    } else {
      ++summary_.droppedEvents;
    }
  }

  LiveAllocation live_[LiveCapacity] = {};
  AllocationEvent events_[EventCapacity] = {};
  CallerUsage callers_[static_cast<size_t>(CallerId::Count)] = {};
  TraceSummary summary_ = {};
  size_t eventHead_ = 0;
  size_t eventCount_ = 0;
};

const char* callerName(CallerId caller);
const char* eventName(EventKind kind);

}  // namespace heap_lifetime_trace
