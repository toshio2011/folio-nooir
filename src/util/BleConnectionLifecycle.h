#pragma once

#include <atomic>
#include <cstdint>

namespace ble_session {

enum class ConnectionPhase : uint8_t {
  Idle = 0,
  Queued,
  GapConnect,
  LinkEstablished,
  GattDiscovery,
  Security,
  HidSetup,
  Ready,
  Disconnecting,
  TeardownPending,
};

enum class GapAttemptOutcome : uint8_t { Pending = 0, FailureDeferred, Connected, Failed };

// A failed NimBLEClient callback can run before the pinned client finishes
// updating its connection fields. Keep failure non-terminal until a deferred
// host-queue event acknowledges that the callback returned.
class GapAttemptCompletion {
 public:
  GapAttemptOutcome outcome() const { return outcome_.load(std::memory_order_acquire); }
  void reset() { outcome_.store(GapAttemptOutcome::Pending, std::memory_order_release); }

  bool publishConnected() { return transition(GapAttemptOutcome::Pending, GapAttemptOutcome::Connected); }
  bool deferFailure() { return transition(GapAttemptOutcome::Pending, GapAttemptOutcome::FailureDeferred); }
  bool publishImmediateFailure() { return transition(GapAttemptOutcome::Pending, GapAttemptOutcome::Failed); }
  bool commitFailureAfterHostCallback() {
    return transition(GapAttemptOutcome::FailureDeferred, GapAttemptOutcome::Failed);
  }

 private:
  std::atomic<GapAttemptOutcome> outcome_{GapAttemptOutcome::Failed};

  bool transition(GapAttemptOutcome from, GapAttemptOutcome to) {
    return outcome_.compare_exchange_strong(from, to, std::memory_order_acq_rel,
                                            std::memory_order_acquire);
  }
};

// A single atomic word linearizes phase changes and stop requests across the
// application, connection worker, and NimBLE callbacks. NimBLE calls stay
// outside this helper and outside FreeRTOS critical sections.
class ConnectionLifecycle {
 public:
  ConnectionPhase phase() const { return decodePhase(state_.load(std::memory_order_acquire)); }
  bool stopRequested() const { return (state_.load(std::memory_order_acquire) & kStopBit) != 0; }
  // Ready keeps the connection claim while the sole worker is parked. It is
  // not an actively executing worker operation, but still blocks reconnect via
  // reconnectAllowed()/claimWorker().
  bool workerActive() const { const ConnectionPhase current = phase(); return current != ConnectionPhase::Idle && current != ConnectionPhase::Ready; }
  bool reconnectAllowed() const { return state_.load(std::memory_order_acquire) == encode(ConnectionPhase::Idle, false); }

  bool claimWorker() { return transition(ConnectionPhase::Idle, ConnectionPhase::Queued, false); }
  bool beginGap() { return transition(ConnectionPhase::Queued, ConnectionPhase::GapConnect, true); }

  // Called by NimBLE's onConnect callback before the synchronous connect()
  // waiter is released. A pending stop is retained while publishing link-up.
  bool markLinkEstablished() { return transition(ConnectionPhase::GapConnect, ConnectionPhase::LinkEstablished, false); }
  bool beginGattDiscovery() { return transition(ConnectionPhase::LinkEstablished, ConnectionPhase::GattDiscovery, true); }
  bool beginSecurity() { return transition(ConnectionPhase::GattDiscovery, ConnectionPhase::Security, true); }
  bool beginHidSetup() { return transition(ConnectionPhase::Security, ConnectionPhase::HidSetup, true); }
  bool markReady() { return transition(ConnectionPhase::HidSetup, ConnectionPhase::Ready, true); }
  bool beginDisconnect() {
    uint32_t observed = state_.load(std::memory_order_acquire);
    for (;;) {
      const ConnectionPhase current = decodePhase(observed);
      const bool connectedPhase = current == ConnectionPhase::Idle || current == ConnectionPhase::LinkEstablished ||
                                  current == ConnectionPhase::GattDiscovery || current == ConnectionPhase::Security ||
                                  current == ConnectionPhase::HidSetup || current == ConnectionPhase::Ready;
      if (!connectedPhase || (observed & (kStopBit | kLinkLossPendingBit)) != 0) return false;
      if (state_.compare_exchange_weak(observed, encode(ConnectionPhase::Disconnecting, false),
                                       std::memory_order_acq_rel, std::memory_order_acquire)) return true;
    }
  }

  // A disconnect callback only publishes link loss. The connection worker
  // claims cleanup after its current synchronous NimBLE operation unwinds.
  // The handled bit makes duplicate callbacks idempotent until the next claim.
  bool requestLinkLoss() {
    uint32_t observed = state_.load(std::memory_order_acquire);
    for (;;) {
      const ConnectionPhase current = decodePhase(observed);
      const bool linkedPhase = current == ConnectionPhase::LinkEstablished || current == ConnectionPhase::GattDiscovery ||
                               current == ConnectionPhase::Security || current == ConnectionPhase::HidSetup ||
                               current == ConnectionPhase::Ready || current == ConnectionPhase::Disconnecting;
      if (!linkedPhase || (observed & (kLinkLossPendingBit | kLinkLossHandledBit)) != 0) return false;
      if (state_.compare_exchange_weak(observed, observed | kLinkLossPendingBit,
                                      std::memory_order_acq_rel, std::memory_order_acquire)) return true;
    }
  }

  bool linkLossPending() const { return (state_.load(std::memory_order_acquire) & kLinkLossPendingBit) != 0; }

  bool claimLinkLossCleanup() {
    uint32_t observed = state_.load(std::memory_order_acquire);
    for (;;) {
      if ((observed & kLinkLossPendingBit) == 0) return false;
      const uint32_t flags = observed & (kStopBit | kLinkLossPendingBit);
      const uint32_t desired = encode(ConnectionPhase::Disconnecting, (flags & kStopBit) != 0) | kLinkLossHandledBit;
      if (state_.compare_exchange_weak(observed, desired,
                                      std::memory_order_acq_rel, std::memory_order_acquire)) return true;
    }
  }

  // Stop is idempotent and is retained until the current owner finishes.
  void requestStop() {
    uint32_t observed = state_.load(std::memory_order_acquire);
    while (decodePhase(observed) != ConnectionPhase::Idle && (observed & kStopBit) == 0) {
      if (state_.compare_exchange_weak(observed, observed | kStopBit,
                                      std::memory_order_acq_rel, std::memory_order_acquire)) {
        return;
      }
    }
  }

  // The worker releases its claim only after its final NimBLE operation. If a
  // stop arrived, remain non-idle until end() confirms host teardown.
  bool finishWorker() {
    uint32_t observed = state_.load(std::memory_order_acquire);
    for (;;) {
      // Do not publish Idle/reconnect eligibility if a callback raced the
      // worker's final check. The callback also leaves a semaphore wake token.
      if ((observed & kLinkLossPendingBit) != 0) return false;
      const bool stopping = (observed & kStopBit) != 0;
      if (!stopping && decodePhase(observed) == ConnectionPhase::Ready) return false;
      // NimBLE disconnect() only requests the GAP teardown; its callback may
      // arrive after the call returns. Retain the connection claim until that
      // callback is consumed (or the worker synthesizes link loss if already
      // disconnected), so a late callback cannot land in Idle.
      if (decodePhase(observed) == ConnectionPhase::Disconnecting &&
          (observed & kLinkLossHandledBit) == 0) return false;
      const uint32_t desired = encode(stopping ? ConnectionPhase::TeardownPending : ConnectionPhase::Idle, stopping);
      if (state_.compare_exchange_weak(observed, desired, std::memory_order_acq_rel, std::memory_order_acquire)) {
        return stopping;
      }
    }
  }

  void finishTeardown() { state_.store(encode(ConnectionPhase::Idle, false), std::memory_order_release); }

  // GAP cancellation is valid only on the connection worker that owns the
  // current connect attempt, after a stop/disconnect request, while NimBLE
  // still reports an active central create-connection procedure. The caller
  // supplies that NimBLE predicate; this helper never performs stack calls.
  bool mayCancelGapFromWorker(bool workerOwnsGapOperation, bool cancellationRequested,
                              bool gapProcedureActive) const {
    return workerOwnsGapOperation && cancellationRequested && gapProcedureActive &&
           phase() == ConnectionPhase::GapConnect;
  }

 private:
  static constexpr uint32_t kStopBit = 0x00000100;
  static constexpr uint32_t kLinkLossPendingBit = 0x00000200;
  static constexpr uint32_t kLinkLossHandledBit = 0x00000400;
  static constexpr uint32_t kPhaseMask = 0x000000FF;
  std::atomic<uint32_t> state_{encode(ConnectionPhase::Idle, false)};

  static constexpr uint32_t encode(ConnectionPhase phase, bool stopping) {
    return static_cast<uint32_t>(static_cast<uint8_t>(phase) | (stopping ? kStopBit : 0));
  }

  static ConnectionPhase decodePhase(uint32_t state) {
    return static_cast<ConnectionPhase>(state & kPhaseMask);
  }

  bool transition(ConnectionPhase from, ConnectionPhase to, bool rejectStop) {
    uint32_t observed = state_.load(std::memory_order_acquire);
    for (;;) {
      if (decodePhase(observed) != from || (observed & kLinkLossPendingBit) != 0 ||
          (rejectStop && (observed & kStopBit) != 0)) return false;
      const uint32_t desired = encode(to, (observed & kStopBit) != 0) | (observed & kLinkLossHandledBit);
      if (state_.compare_exchange_weak(observed, desired, std::memory_order_acq_rel, std::memory_order_acquire)) {
        return true;
      }
    }
  }
};

}  // namespace ble_session
