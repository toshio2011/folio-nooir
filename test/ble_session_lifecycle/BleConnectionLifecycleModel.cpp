#include "src/util/BleConnectionLifecycle.h"

#include <cassert>
#include <iostream>

using ble_session::ConnectionLifecycle;
using ble_session::ConnectionPhase;

int main() {
  unsigned checks = 0;

  // 1. Stop during GAP: only the connection worker may cancel, and only while
  // NimBLE still reports an active central connection procedure. Link-up wins
  // the race without cancellation and stop prevents discovery.
  {
    ConnectionLifecycle life;
    assert(life.claimWorker() && life.beginGap());
    life.requestStop();
    assert(life.mayCancelGapFromWorker(true, true, true));
    assert(!life.mayCancelGapFromWorker(false, true, true));
    assert(!life.mayCancelGapFromWorker(true, true, false));
    assert(life.markLinkEstablished());
    assert(!life.mayCancelGapFromWorker(true, true, false));
    assert(!life.beginGattDiscovery());
    assert(life.finishWorker());
    ++checks;
  }

  // 14. A stop wake racing with link establishment cannot cancel an already
  // completed GAP procedure, start GATT, or release the claim before teardown.
  {
    ConnectionLifecycle life;
    assert(life.claimWorker() && life.beginGap());
    life.requestStop();
    assert(!life.mayCancelGapFromWorker(true, true, false));
    assert(life.markLinkEstablished());
    assert(!life.beginGattDiscovery());
    assert(life.finishWorker() && !life.reconnectAllowed());
    life.finishTeardown();
    assert(life.reconnectAllowed());
    ++checks;
  }

  // 15. Terminal connect failure is not visible to the worker until the
  // deferred NimBLE-host event acknowledges callback return/client-state update.
  {
    ble_session::GapAttemptCompletion attempt;
    attempt.reset();
    assert(attempt.outcome() == ble_session::GapAttemptOutcome::Pending);
    assert(attempt.deferFailure());
    assert(attempt.outcome() == ble_session::GapAttemptOutcome::FailureDeferred);
    assert(attempt.commitFailureAfterHostCallback());
    assert(attempt.outcome() == ble_session::GapAttemptOutcome::Failed);
    assert(!attempt.commitFailureAfterHostCallback());
    ++checks;
  }

  // 2. Stop after link establishment.
  {
    ConnectionLifecycle life;
    assert(life.claimWorker() && life.beginGap() && life.markLinkEstablished());
    life.requestStop();
    assert(!life.beginGattDiscovery() && life.finishWorker());
    ++checks;
  }

  // 3. Stop during GATT discovery blocks security/setup transitions.
  {
    ConnectionLifecycle life;
    assert(life.claimWorker() && life.beginGap() && life.markLinkEstablished());
    assert(life.beginGattDiscovery());
    life.requestStop();
    assert(!life.beginSecurity() && life.finishWorker());
    ++checks;
  }

  // 4. A failed/expired GAP attempt releases ownership for one retry.
  {
    ConnectionLifecycle life;
    assert(life.claimWorker() && life.beginGap());
    assert(!life.finishWorker() && life.reconnectAllowed());
    assert(life.claimWorker());
    ++checks;
  }

  // 5. A phase claim blocks another connection or scan owner while active.
  {
    ConnectionLifecycle life;
    assert(life.claimWorker() && life.workerActive());
    assert(!life.claimWorker() && !life.reconnectAllowed());
    ++checks;
  }

  // 6. Reader exit requests stop; the outstanding worker retains ownership
  // until it reaches teardown-pending and teardown is explicitly completed.
  {
    ConnectionLifecycle life;
    assert(life.claimWorker() && life.beginGap());
    life.requestStop();
    assert(life.finishWorker());
    assert(life.phase() == ConnectionPhase::TeardownPending);
    assert(!life.claimWorker());
    life.finishTeardown();
    ++checks;
  }

  // 7. Repeated stop/disconnect attempts cannot clear a pending stop.
  {
    ConnectionLifecycle life;
    assert(life.claimWorker() && life.beginGap());
    life.requestStop();
    life.requestStop();
    assert(life.stopRequested() && !life.beginDisconnect());
    assert(life.finishWorker());
    ++checks;
  }

  // 8. Reconnect stays blocked during deferred teardown and becomes available
  // only after cleanup resets the phase word.
  {
    ConnectionLifecycle life;
    assert(life.claimWorker() && life.beginGap());
    life.requestStop();
    assert(life.finishWorker() && !life.reconnectAllowed());
    life.finishTeardown();
    assert(life.reconnectAllowed() && life.claimWorker());
    ++checks;
  }

  // 9. Sleep/wake follows the same stop -> teardown -> fresh claim sequence.
  {
    ConnectionLifecycle life;
    assert(life.claimWorker() && life.beginGap());
    life.requestStop();
    assert(life.finishWorker());
    life.finishTeardown();
    assert(life.claimWorker());
    ++checks;
  }

  // 10. A post-link loss during GATT blocks the next phase and is claimed once.
  {
    ConnectionLifecycle life;
    assert(life.claimWorker() && life.beginGap() && life.markLinkEstablished() && life.beginGattDiscovery());
    assert(life.requestLinkLoss() && life.linkLossPending());
    assert(!life.beginSecurity() && !life.finishWorker() && !life.reconnectAllowed());
    unsigned cleanupCount = 0;
    if (life.claimLinkLossCleanup()) ++cleanupCount;
    if (life.claimLinkLossCleanup()) ++cleanupCount;
    assert(cleanupCount == 1 && !life.linkLossPending());
    assert(!life.finishWorker() && life.reconnectAllowed());
    ++checks;
  }

  // 11. A Ready link keeps its claim while parked and blocks a new connection
  // until the same worker consumes and finishes link-loss cleanup.
  {
    ConnectionLifecycle life;
    assert(life.claimWorker() && life.beginGap() && life.markLinkEstablished() && life.beginGattDiscovery());
    assert(life.beginSecurity() && life.beginHidSetup() && life.markReady());
    assert(!life.finishWorker() && life.phase() == ConnectionPhase::Ready);
    assert(!life.workerActive() && !life.reconnectAllowed());
    assert(life.requestLinkLoss() && !life.requestLinkLoss());
    assert(life.claimLinkLossCleanup() && !life.claimLinkLossCleanup());
    assert(!life.finishWorker() && life.reconnectAllowed());
    ++checks;
  }

  // 12. disconnect() may return before onDisconnect runs; Disconnecting stays
  // owned until the delayed callback is consumed by worker cleanup.
  {
    ConnectionLifecycle life;
    assert(life.claimWorker() && life.beginGap() && life.markLinkEstablished() && life.beginGattDiscovery());
    assert(life.beginSecurity() && life.beginHidSetup() && life.markReady() && life.beginDisconnect());
    assert(!life.finishWorker() && life.phase() == ConnectionPhase::Disconnecting);
    assert(!life.reconnectAllowed());
    assert(life.requestLinkLoss() && life.claimLinkLossCleanup());
    assert(!life.claimLinkLossCleanup());
    assert(!life.finishWorker() && life.reconnectAllowed());
    ++checks;
  }

  // 13. A stop arriving while disconnect() is in flight cannot retire the
  // connection claim until the delayed callback is consumed by the worker.
  {
    ConnectionLifecycle life;
    assert(life.claimWorker() && life.beginGap() && life.markLinkEstablished() && life.beginGattDiscovery());
    assert(life.beginSecurity() && life.beginHidSetup() && life.markReady() && life.beginDisconnect());
    life.requestStop();
    assert(!life.finishWorker() && life.phase() == ConnectionPhase::Disconnecting);
    assert(!life.reconnectAllowed());
    assert(life.requestLinkLoss() && life.claimLinkLossCleanup());
    assert(life.finishWorker() && life.phase() == ConnectionPhase::TeardownPending);
    assert(!life.reconnectAllowed());
    life.finishTeardown();
    assert(life.reconnectAllowed());
    ++checks;
  }

  std::cout << "PASS modeled BLE lifecycle scenarios=" << checks << '\n';
  return 0;
}
