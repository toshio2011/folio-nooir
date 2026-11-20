#include <gtest/gtest.h>

#include <array>

#include "src/BleInput.h"
#include "src/util/BleMemoryPolicy.h"
#include "src/util/BleBondOrder.h"
#include "src/util/BleNoDeviceTimeoutPolicy.h"
#include "src/util/BleConnectionLifecycle.h"
#include "src/util/BluetoothSettingsNavigationPolicy.h"

TEST(BleSessionLifecycle, SavedOnPreferenceAllowsNormalReconnectAfterWake) {
  EXPECT_TRUE(bleinput::lifecycleReevaluationAllowed(true, false, false));
}

TEST(BleSessionLifecycle, ExplicitOffAndWebExclusiveStillBlockReconnect) {
  EXPECT_FALSE(bleinput::lifecycleReevaluationAllowed(false, false, false));
  EXPECT_FALSE(bleinput::lifecycleReevaluationAllowed(true, true, false));
}

TEST(BleSessionLifecycle, NoDeviceTimeoutCoversTheWholeDisconnectedRunningSession) {
  EXPECT_TRUE(ble_session::noDeviceTimeoutWindowShouldRun(true, 60, true, false));
  EXPECT_FALSE(ble_session::noDeviceTimeoutWindowShouldRun(false, 60, true, false));
  EXPECT_FALSE(ble_session::noDeviceTimeoutWindowShouldRun(true, 0, true, false));
  EXPECT_FALSE(ble_session::noDeviceTimeoutWindowShouldRun(true, 60, false, false));
  EXPECT_FALSE(ble_session::noDeviceTimeoutWindowShouldRun(true, 60, true, true));
}

TEST(BleSessionLifecycle, NoDeviceTimeoutUsesWrapSafeElapsedTimeAndDoesNotFireEarly) {
  EXPECT_FALSE(ble_session::noDeviceTimeoutExpired(29'999, 0, 30));
  EXPECT_TRUE(ble_session::noDeviceTimeoutExpired(30'000, 0, 30));
  EXPECT_TRUE(ble_session::noDeviceTimeoutExpired(980, UINT32_MAX - 19, 1));
  EXPECT_TRUE(ble_session::noDeviceTimeoutExpired(5'000, UINT32_MAX - 4'999, 5));
}

TEST(BleSessionLifecycle, ConnectionResetsNoDeviceTimeoutAndDisconnectStartsFreshWindow) {
  ble_session::NoDeviceTimeoutWindow window;
  EXPECT_EQ(window.update(true, 30, true, false, 1'000), ble_session::NoDeviceTimeoutWindowState::Started);
  EXPECT_EQ(window.update(true, 30, true, false, 29'000), ble_session::NoDeviceTimeoutWindowState::Waiting);
  EXPECT_EQ(window.update(true, 30, true, true, 29'500), ble_session::NoDeviceTimeoutWindowState::Inactive);
  EXPECT_FALSE(window.active());

  EXPECT_EQ(window.update(true, 30, true, false, 90'000), ble_session::NoDeviceTimeoutWindowState::Started);
  EXPECT_EQ(window.startMs(), 90'000u);
  EXPECT_EQ(window.update(true, 30, true, false, 119'999), ble_session::NoDeviceTimeoutWindowState::Waiting);
  EXPECT_EQ(window.update(true, 30, true, false, 120'000), ble_session::NoDeviceTimeoutWindowState::Expired);
}

TEST(BleSessionLifecycle, SettingsTransitionsDoNotPauseOrRestartNoDeviceTimeout) {
  ble_session::NoDeviceTimeoutWindow window;
  EXPECT_EQ(window.update(true, 60, true, false, 10'000), ble_session::NoDeviceTimeoutWindowState::Started);
  // The host and saved preference are unchanged while the UI moves through
  // Settings, so the same no-device session continues to consume its timeout.
  EXPECT_EQ(window.update(true, 60, true, false, 55'000), ble_session::NoDeviceTimeoutWindowState::Waiting);
  EXPECT_EQ(window.startMs(), 10'000u);
  EXPECT_EQ(window.update(true, 60, true, false, 70'000), ble_session::NoDeviceTimeoutWindowState::Expired);
}

TEST(BleSessionLifecycle, IntentionalReaderBleStopResetsTimeoutBeforeSafeReadmission) {
  ble_session::NoDeviceTimeoutWindow window;
  EXPECT_EQ(window.update(true, 60, true, false, 10'000), ble_session::NoDeviceTimeoutWindowState::Started);
  EXPECT_EQ(window.update(true, 60, true, false, 55'000), ble_session::NoDeviceTimeoutWindowState::Waiting);
  EXPECT_EQ(window.update(true, 60, false, false, 55'001), ble_session::NoDeviceTimeoutWindowState::Inactive);
  EXPECT_FALSE(window.active());

  // Reader survival stop/cooldown can last longer than the configured timer.
  // A later host readmission must start a new full no-device window.
  EXPECT_EQ(window.update(true, 60, true, false, 180'000), ble_session::NoDeviceTimeoutWindowState::Started);
  EXPECT_EQ(window.startMs(), 180'000u);
  EXPECT_EQ(window.update(true, 60, true, false, 239'999), ble_session::NoDeviceTimeoutWindowState::Waiting);
  EXPECT_EQ(window.update(true, 60, true, false, 240'000), ble_session::NoDeviceTimeoutWindowState::Expired);
}

TEST(BleSessionLifecycle, SettingsOffAndDisabledTimeoutClearTheNoDeviceWindow) {
  ble_session::NoDeviceTimeoutWindow window;
  EXPECT_EQ(window.update(true, 60, true, false, 4'000), ble_session::NoDeviceTimeoutWindowState::Started);
  EXPECT_EQ(window.update(false, 60, false, false, 20'000), ble_session::NoDeviceTimeoutWindowState::Inactive);
  EXPECT_FALSE(window.active());

  EXPECT_EQ(window.update(true, 0, true, false, 30'000), ble_session::NoDeviceTimeoutWindowState::Inactive);
  EXPECT_FALSE(window.active());
  EXPECT_EQ(window.update(true, 60, true, false, 40'000), ble_session::NoDeviceTimeoutWindowState::Started);
}

// These are deterministic state-machine tests for the synchronized phase helper.
// They do not execute FreeRTOS or NimBLE and are not substitutes for concurrency
// stress or physical X4 validation.
TEST(BleConnectionLifecycle, StopDuringGapRemainsCancellableOnlyForOwningWorker) {
  ble_session::ConnectionLifecycle life;
  ASSERT_TRUE(life.claimWorker());
  ASSERT_TRUE(life.beginGap());
  life.requestStop();
  EXPECT_TRUE(life.mayCancelGapFromWorker(true, true, true));
  EXPECT_FALSE(life.mayCancelGapFromWorker(false, true, true));
  EXPECT_FALSE(life.mayCancelGapFromWorker(true, true, false));
  ASSERT_TRUE(life.markLinkEstablished());
  EXPECT_FALSE(life.mayCancelGapFromWorker(true, true, true));
  EXPECT_FALSE(life.beginGattDiscovery());
  EXPECT_TRUE(life.finishWorker());
  EXPECT_FALSE(life.reconnectAllowed());
  life.finishTeardown();
  EXPECT_TRUE(life.reconnectAllowed());
}

TEST(BleConnectionLifecycle, LinkEstablishmentWinningCancelRaceIsNotCancelledOrDiscoveredAfterStop) {
  ble_session::ConnectionLifecycle life;
  ASSERT_TRUE(life.claimWorker());
  ASSERT_TRUE(life.beginGap());
  life.requestStop();

  // The NimBLE GAP procedure has already ended as the link-up callback races
  // the worker's stop wake. The worker must not issue cancelConnect or GATT.
  EXPECT_FALSE(life.mayCancelGapFromWorker(true, true, false));
  ASSERT_TRUE(life.markLinkEstablished());
  EXPECT_FALSE(life.mayCancelGapFromWorker(true, true, false));
  EXPECT_FALSE(life.beginGattDiscovery());
  EXPECT_TRUE(life.finishWorker());
  EXPECT_FALSE(life.reconnectAllowed());
  life.finishTeardown();
  EXPECT_TRUE(life.reconnectAllowed());
}

TEST(BleConnectionLifecycle, FailedCallbackRemainsPendingUntilDeferredHostQueueAcknowledgement) {
  ble_session::GapAttemptCompletion attempt;
  attempt.reset();
  EXPECT_EQ(attempt.outcome(), ble_session::GapAttemptOutcome::Pending);

  ASSERT_TRUE(attempt.deferFailure());
  EXPECT_EQ(attempt.outcome(), ble_session::GapAttemptOutcome::FailureDeferred);
  // The worker may queue its static event, but it cannot retry or tear down
  // until the event runs after NimBLEClient::handleGapEvent has returned.
  EXPECT_FALSE(attempt.publishImmediateFailure());
  ASSERT_TRUE(attempt.commitFailureAfterHostCallback());
  EXPECT_EQ(attempt.outcome(), ble_session::GapAttemptOutcome::Failed);
  EXPECT_FALSE(attempt.commitFailureAfterHostCallback());
}

TEST(BleConnectionLifecycle, StopAfterLinkPreventsGattDiscovery) {
  ble_session::ConnectionLifecycle life;
  ASSERT_TRUE(life.claimWorker());
  ASSERT_TRUE(life.beginGap());
  ASSERT_TRUE(life.markLinkEstablished());
  life.requestStop();
  EXPECT_EQ(life.phase(), ble_session::ConnectionPhase::LinkEstablished);
  EXPECT_FALSE(life.beginGattDiscovery());
  EXPECT_TRUE(life.finishWorker());
}

TEST(BleConnectionLifecycle, StopDuringGattDiscoveryPreventsNextPhase) {
  ble_session::ConnectionLifecycle life;
  ASSERT_TRUE(life.claimWorker());
  ASSERT_TRUE(life.beginGap());
  ASSERT_TRUE(life.markLinkEstablished());
  ASSERT_TRUE(life.beginGattDiscovery());
  life.requestStop();
  EXPECT_EQ(life.phase(), ble_session::ConnectionPhase::GattDiscovery);
  EXPECT_FALSE(life.beginSecurity());
  EXPECT_TRUE(life.finishWorker());
}

TEST(BleConnectionLifecycle, FailedGapAttemptCanRetryAfterWorkerReleasesClaim) {
  ble_session::ConnectionLifecycle life;
  ASSERT_TRUE(life.claimWorker());
  ASSERT_TRUE(life.beginGap());
  EXPECT_FALSE(life.finishWorker());
  EXPECT_EQ(life.phase(), ble_session::ConnectionPhase::Idle);
  EXPECT_TRUE(life.reconnectAllowed());
  EXPECT_TRUE(life.claimWorker());
}

TEST(BleConnectionLifecycle, RepeatedStopAndDisconnectCannotReleasePendingShutdown) {
  ble_session::ConnectionLifecycle life;
  ASSERT_TRUE(life.claimWorker());
  ASSERT_TRUE(life.beginGap());
  life.requestStop();
  life.requestStop();
  EXPECT_TRUE(life.stopRequested());
  EXPECT_FALSE(life.beginDisconnect());
  EXPECT_TRUE(life.finishWorker());
  EXPECT_EQ(life.phase(), ble_session::ConnectionPhase::TeardownPending);
  EXPECT_FALSE(life.reconnectAllowed());
}

TEST(BleConnectionLifecycle, DisconnectAfterGATTIsOwnedByWorker) {
  ble_session::ConnectionLifecycle life;
  ASSERT_TRUE(life.claimWorker());
  ASSERT_TRUE(life.beginGap());
  ASSERT_TRUE(life.markLinkEstablished());
  ASSERT_TRUE(life.beginGattDiscovery());
  ASSERT_TRUE(life.beginDisconnect());
  EXPECT_EQ(life.phase(), ble_session::ConnectionPhase::Disconnecting);
  EXPECT_FALSE(life.claimWorker());
  EXPECT_FALSE(life.finishWorker());
  EXPECT_FALSE(life.reconnectAllowed());
  ASSERT_TRUE(life.requestLinkLoss());
  ASSERT_TRUE(life.claimLinkLossCleanup());
  EXPECT_FALSE(life.finishWorker());
  EXPECT_TRUE(life.reconnectAllowed());
}

TEST(BleConnectionLifecycle, ShutdownBlocksReconnectUntilTeardownAndSupportsWake) {
  ble_session::ConnectionLifecycle life;
  ASSERT_TRUE(life.claimWorker());
  ASSERT_TRUE(life.beginGap());
  life.requestStop();
  EXPECT_TRUE(life.finishWorker());
  EXPECT_FALSE(life.claimWorker());
  EXPECT_FALSE(life.reconnectAllowed());
  life.finishTeardown();
  EXPECT_TRUE(life.claimWorker());
}

TEST(BleConnectionLifecycle, LinkLossDuringGattBlocksNextStepAndIsConsumedOnce) {
  ble_session::ConnectionLifecycle life;
  ASSERT_TRUE(life.claimWorker());
  ASSERT_TRUE(life.beginGap());
  ASSERT_TRUE(life.markLinkEstablished());
  ASSERT_TRUE(life.beginGattDiscovery());
  ASSERT_TRUE(life.requestLinkLoss());
  EXPECT_TRUE(life.linkLossPending());
  EXPECT_FALSE(life.beginSecurity());  // no next GATT/security operation after observed loss
  EXPECT_FALSE(life.finishWorker());   // the atomic loss bit keeps reconnect blocked
  EXPECT_FALSE(life.reconnectAllowed());

  int cleanupCount = 0;
  if (life.claimLinkLossCleanup()) ++cleanupCount;
  if (life.claimLinkLossCleanup()) ++cleanupCount;
  EXPECT_EQ(cleanupCount, 1);
  EXPECT_FALSE(life.linkLossPending());
  EXPECT_EQ(life.phase(), ble_session::ConnectionPhase::Disconnecting);
  EXPECT_FALSE(life.reconnectAllowed());
  EXPECT_FALSE(life.finishWorker());
  EXPECT_EQ(life.phase(), ble_session::ConnectionPhase::Idle);
  EXPECT_TRUE(life.reconnectAllowed());
}

TEST(BleConnectionLifecycle, ReadyLinkLossWakesParkedWorkerAndBlocksReconnectUntilCleanup) {
  ble_session::ConnectionLifecycle life;
  ASSERT_TRUE(life.claimWorker());
  ASSERT_TRUE(life.beginGap());
  ASSERT_TRUE(life.markLinkEstablished());
  ASSERT_TRUE(life.beginGattDiscovery());
  ASSERT_TRUE(life.beginSecurity());
  ASSERT_TRUE(life.beginHidSetup());
  ASSERT_TRUE(life.markReady());
  EXPECT_FALSE(life.finishWorker());  // preserve the Ready connection claim while parked
  EXPECT_EQ(life.phase(), ble_session::ConnectionPhase::Ready);
  EXPECT_FALSE(life.workerActive());
  EXPECT_FALSE(life.reconnectAllowed());

  ASSERT_TRUE(life.requestLinkLoss());
  EXPECT_FALSE(life.requestLinkLoss());  // duplicate callback coalesces
  EXPECT_FALSE(life.reconnectAllowed());
  EXPECT_TRUE(life.claimLinkLossCleanup());
  EXPECT_FALSE(life.claimLinkLossCleanup());
  EXPECT_FALSE(life.finishWorker());
  EXPECT_TRUE(life.reconnectAllowed());
}

TEST(BleConnectionLifecycle, DelayedDisconnectCallbackRetainsDisconnectingClaim) {
  ble_session::ConnectionLifecycle life;
  ASSERT_TRUE(life.claimWorker());
  ASSERT_TRUE(life.beginGap());
  ASSERT_TRUE(life.markLinkEstablished());
  ASSERT_TRUE(life.beginGattDiscovery());
  ASSERT_TRUE(life.beginSecurity());
  ASSERT_TRUE(life.beginHidSetup());
  ASSERT_TRUE(life.markReady());
  ASSERT_TRUE(life.beginDisconnect());
  EXPECT_FALSE(life.finishWorker());  // disconnect() returned; callback has not arrived
  EXPECT_EQ(life.phase(), ble_session::ConnectionPhase::Disconnecting);
  EXPECT_FALSE(life.reconnectAllowed());

  ASSERT_TRUE(life.requestLinkLoss());  // delayed onDisconnect callback
  ASSERT_TRUE(life.claimLinkLossCleanup());
  EXPECT_FALSE(life.claimLinkLossCleanup());
  EXPECT_FALSE(life.finishWorker());
  EXPECT_TRUE(life.reconnectAllowed());
}

TEST(BleConnectionLifecycle, StopDuringDelayedDisconnectWaitsForLinkLossCleanup) {
  ble_session::ConnectionLifecycle life;
  ASSERT_TRUE(life.claimWorker());
  ASSERT_TRUE(life.beginGap());
  ASSERT_TRUE(life.markLinkEstablished());
  ASSERT_TRUE(life.beginGattDiscovery());
  ASSERT_TRUE(life.beginSecurity());
  ASSERT_TRUE(life.beginHidSetup());
  ASSERT_TRUE(life.markReady());
  ASSERT_TRUE(life.beginDisconnect());
  life.requestStop();
  EXPECT_FALSE(life.finishWorker());  // stop cannot retire ownership before delayed callback
  EXPECT_EQ(life.phase(), ble_session::ConnectionPhase::Disconnecting);
  EXPECT_FALSE(life.reconnectAllowed());

  ASSERT_TRUE(life.requestLinkLoss());
  ASSERT_TRUE(life.claimLinkLossCleanup());
  EXPECT_TRUE(life.finishWorker());
  EXPECT_EQ(life.phase(), ble_session::ConnectionPhase::TeardownPending);
  EXPECT_FALSE(life.reconnectAllowed());
  life.finishTeardown();
  EXPECT_TRUE(life.reconnectAllowed());
}

namespace {
enum class SettingsAction { ToggleBluetooth, Timeout, Scan, Disconnect, Paired };
struct SettingsRow {
  SettingsAction action;
};
}  // namespace

TEST(BluetoothSettingsNavigation, ConnectedRowInsertionPreservesSelectedAction) {
  const std::array disconnected = {SettingsRow{SettingsAction::ToggleBluetooth},
                                   SettingsRow{SettingsAction::Timeout}, SettingsRow{SettingsAction::Scan},
                                   SettingsRow{SettingsAction::Paired}};
  const std::array connected = {SettingsRow{SettingsAction::ToggleBluetooth},
                                SettingsRow{SettingsAction::Timeout}, SettingsRow{SettingsAction::Scan},
                                SettingsRow{SettingsAction::Disconnect}, SettingsRow{SettingsAction::Paired}};
  EXPECT_EQ(bluetooth_settings::selectionIndexForAction(connected, SettingsAction::Paired,
                                                        SettingsAction::ToggleBluetooth), 4);
  EXPECT_EQ(bluetooth_settings::selectionIndexForAction(disconnected, SettingsAction::Paired,
                                                        SettingsAction::ToggleBluetooth), 3);
  EXPECT_EQ(bluetooth_settings::selectionIndexForAction(disconnected, SettingsAction::Disconnect,
                                                        SettingsAction::ToggleBluetooth), 0);
}

TEST(BluetoothSettingsNavigation, ScanningKeepsTrailingOffSelectedAsDevicesArrive) {
  EXPECT_EQ(bluetooth_settings::selectionAfterDataCountChange(0, 0, 1), 1);
  EXPECT_EQ(bluetooth_settings::selectionAfterDataCountChange(1, 1, 4), 4);
  EXPECT_EQ(bluetooth_settings::selectionAfterDataCountChange(2, 4, 6), 2);
  EXPECT_EQ(bluetooth_settings::selectionAfterDataCountChange(5, 4, 3), 3);
  EXPECT_EQ(bluetooth_settings::selectionAfterDataCountChange(7, 7, 0), 0);  // OFF after scan results release
  EXPECT_EQ(bluetooth_settings::selectionAfterDataCountChange(3, 3, 5), 5);  // paired-list OFF follows growth
  EXPECT_EQ(bluetooth_settings::selectionAfterDataCountChange(5, 5, 2), 2);  // paired-list OFF follows shrinkage
}

TEST(BluetoothSettingsNavigation, RetryTransitionPreservesMovedOffOrBackAction) {
  EXPECT_EQ(bluetooth_settings::failureSelectionAfterPreparation(0, true), 0);   // Retry keeps row 0
  EXPECT_EQ(bluetooth_settings::failureSelectionAfterPreparation(1, true), 1);   // OFF keeps row 1
  EXPECT_EQ(bluetooth_settings::failureSelectionAfterPreparation(2, true), 2);   // Back keeps row 2
  EXPECT_EQ(bluetooth_settings::failureSelectionAfterPreparation(0, false), 0);  // OFF without Retry
  EXPECT_EQ(bluetooth_settings::failureSelectionAfterPreparation(1, false), 0);  // OFF without Retry
  EXPECT_EQ(bluetooth_settings::failureSelectionAfterPreparation(2, false), 1);  // Back without Retry
}

TEST(BluetoothSettingsNavigation, AutomaticBluetoothOffRequiresImmediateMenuRebuild) {
  EXPECT_TRUE(bluetooth_settings::preferenceOffTransitionRequiresMenuRebuild(true, false));
  EXPECT_FALSE(bluetooth_settings::preferenceOffTransitionRequiresMenuRebuild(false, false));
  EXPECT_FALSE(bluetooth_settings::preferenceOffTransitionRequiresMenuRebuild(true, true));
}

TEST(BleBondOrder, SuccessfulConnectionPromotesOnlyThatBondWithoutDroppingOthers) {
  struct Bond { int id; } bonds[] = {{1}, {2}, {3}, {4}};
  ASSERT_TRUE(ble_bonds::promoteToFront(bonds, 4, 2));
  EXPECT_EQ(bonds[0].id, 3);
  EXPECT_EQ(bonds[1].id, 1);
  EXPECT_EQ(bonds[2].id, 2);
  EXPECT_EQ(bonds[3].id, 4);
  EXPECT_FALSE(ble_bonds::promoteToFront(bonds, 4, 0));
  EXPECT_FALSE(ble_bonds::promoteToFront(bonds, 4, 5));
}
