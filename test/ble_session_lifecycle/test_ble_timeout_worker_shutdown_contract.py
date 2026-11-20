"""Regression contracts for prompt BLE timeout shutdown during worker connects."""

from pathlib import Path
import os
import unittest

ROOT = Path(__file__).resolve().parents[2]
HOST = (ROOT / "freeink-sdk/libs/network/BleKeyboardHost/src/BleKeyboardHost.cpp").read_text(encoding="utf-8")
LIFECYCLE = (ROOT / "src/util/BleConnectionLifecycle.h").read_text(encoding="utf-8")
PINNED_NIMBLE = Path(os.environ.get("NOOIR_PINNED_NIMBLE_ROOT", ""))


def function_body(source: str, signature: str) -> str:
    start = source.index(signature)
    opening = source.index("{", start)
    depth = 0
    for index in range(opening, len(source)):
        if source[index] == "{": depth += 1
        elif source[index] == "}":
            depth -= 1
            if depth == 0: return source[opening + 1:index]
    raise AssertionError(f"unclosed function: {signature}")


class BleTimeoutWorkerShutdownContract(unittest.TestCase):
    def test_stop_defers_without_cross_task_gap_cancellation(self):
        end = function_body(HOST, "void BleKeyboardHost::end()")
        self.assertIn("g_connectionLifecycle.requestStop();", end)
        self.assertNotIn("cancelConnect", end)
        delegated = end.index("if (!calledFromConnWorker && workerExists)")
        scanner = end.index("NimBLEDevice::getScan()", delegated)
        self.assertIn("notifyConnectionWorker();", end[delegated:scanner])
        self.assertIn("return;", end[delegated:scanner])
        self.assertNotIn("while (g_connecting", end)

    def test_gatt_discovery_and_disconnect_stay_with_worker(self):
        connect = function_body(HOST, "void doConnect(")
        self.assertIn("g_connectionLifecycle.beginGap()", connect)
        self.assertIn("g_connectionLifecycle.beginGattDiscovery()", connect)
        self.assertIn("g_connectionLifecycle.beginSecurity()", connect)
        self.assertIn("g_connectionLifecycle.beginHidSetup()", connect)
        worker = function_body(HOST, "void connTaskFn(void*)")
        self.assertIn("doConnect(g_targetAddr, g_targetType);", worker)
        self.assertIn("performOwnedClientDisconnect();", worker)
        self.assertNotIn("g_client->disconnect();", worker)
        self.assertIn("finishWorkerTeardown();", worker)

    def test_post_link_failure_disconnects_claim_phase_before_gap_request(self):
        connect = function_body(HOST, "void doConnect(")
        self.assertNotIn("g_client->disconnect();", connect)
        self.assertEqual(connect.count("disconnectClientFromWorker();"), 3)
        request = function_body(HOST, "bool disconnectClientFromWorker()")
        self.assertLess(request.index("beginDisconnect()"), request.index("performOwnedClientDisconnect();"))
        perform = function_body(HOST, "void performOwnedClientDisconnect()")
        self.assertIn("g_client->disconnect();", perform)
        self.assertIn("requestLinkLoss();", perform)
        self.assertIn("consumeLinkLossOnWorker();", perform)

    def test_scanner_calls_are_gated_while_worker_or_teardown_is_active(self):
        start = function_body(HOST, "void BleKeyboardHost::startScan(uint32_t ms)")
        stop = function_body(HOST, "void BleKeyboardHost::stopScan()")
        for body in (start, stop):
            self.assertIn("g_connectionLifecycle.workerActive()", body)
            self.assertIn("g_teardownPending.load", body)
        self.assertNotIn("cancelConnect", start + stop)

    def test_worker_completion_is_required_before_begin_can_restart_host(self):
        begin = function_body(HOST, "bool BleKeyboardHost::begin(")
        self.assertIn("g_connecting.load", begin)
        self.assertIn("g_teardownPending.load", begin)
        self.assertIn("g_connectionLifecycle.workerActive()", begin)
        self.assertLess(begin.index("g_teardownPending.load"), begin.index("if (begun_) return true"))

    def test_shutdown_waits_for_parked_worker_removal_and_stack_deinit(self):
        end = function_body(HOST, "void BleKeyboardHost::end()")
        complete = function_body(HOST, "void BleKeyboardHost::completeStoppedState()")
        poll = function_body(HOST, "void BleKeyboardHost::poll()")
        self.assertIn("g_workerTeardownReady.store(true", end)
        self.assertIn("xSemaphoreTake(g_connWakeSemaphore, portMAX_DELAY)", end)
        self.assertIn("g_connTaskNotifyRefs != 0", complete)
        self.assertLess(complete.index("vTaskDelete(finishedWorker)"), complete.index("begun_ = false;"))
        self.assertLess(complete.index("begun_ = false;"), complete.index("g_teardownPending.store(false"))
        self.assertIn("completeStoppedState();", poll)
        header = (ROOT / "freeink-sdk/libs/network/BleKeyboardHost/include/BleKeyboardHost.h").read_text(encoding="utf-8")
        self.assertIn("bool isRunning() const { return begun_; }", header)
        self.assertIn("g_teardownPending.load", function_body(HOST, "bool BleKeyboardHost::connect("))

    def test_repeated_stop_is_idempotent_without_repeated_owner_wake(self):
        end = function_body(HOST, "void BleKeyboardHost::end()")
        repeated = end.index("if (teardownAlreadyPending && !firstStopRequest && !calledFromConnWorker)")
        opening = end.index("{", repeated)
        depth = 0
        close = opening
        for close in range(opening, len(end)):
            if end[close] == "{": depth += 1
            elif end[close] == "}":
                depth -= 1
                if depth == 0: break
        self.assertNotIn("notifyConnectionWorker();", end[opening + 1:close])
        self.assertIn("if (firstStopRequest)", end)
        self.assertIn("if (workerExists && !calledFromConnWorker) notifyConnectionWorker();", end)
        self.assertIn("return;", end[opening + 1:close])
        self.assertNotIn("cancelConnect", end)

    def test_phase_model_is_synchronized_and_stop_is_idempotent(self):
        self.assertIn("ConnectionPhase::GapConnect", LIFECYCLE)
        self.assertIn("ConnectionPhase::GattDiscovery", LIFECYCLE)
        self.assertIn("workerOwnsGapOperation && cancellationRequested && gapProcedureActive", LIFECYCLE)
        self.assertIn("void requestStop()", LIFECYCLE)
        self.assertIn("compare_exchange_weak", LIFECYCLE)

    def test_gap_connect_is_async_and_only_worker_cancels_live_central_procedure(self):
        connect = function_body(HOST, "ble_session::GapAttemptOutcome startGapAttempt(")
        self.assertIn("/*asyncConnect=*/true", connect)
        self.assertIn("waitForGapAttemptResult()", connect)
        self.assertIn("/*exchangeMTU=*/true", connect)

        wait = function_body(HOST, "ble_session::GapAttemptOutcome waitForGapAttemptResult()")
        self.assertIn("g_connectionLifecycle.mayCancelGapFromWorker(true, cancelRequested", wait)
        self.assertIn("ble_gap_conn_active() != 0", wait)
        self.assertIn("g_client->cancelConnect()", wait)
        self.assertIn("xSemaphoreTake(g_connWakeSemaphore, portMAX_DELAY)", wait)
        end = function_body(HOST, "void BleKeyboardHost::end()")
        self.assertNotIn("cancelConnect", end)
        scan = function_body(HOST, "void BleKeyboardHost::startScan(uint32_t ms)")
        self.assertNotIn("cancelConnect", scan)

    def test_gap_terminal_callbacks_wake_worker_and_early_disconnect_finishes_gap_wait(self):
        connected = function_body(HOST, "void onConnect(NimBLEClient*) override")
        failed = function_body(HOST, "void onConnectFail(NimBLEClient*, int reason) override")
        disconnected = function_body(HOST, "void onDisconnect(NimBLEClient*, int reason) override")
        self.assertIn("g_gapAttemptCompletion.publishConnected();", connected)
        self.assertIn("g_gapAttemptCompletion.deferFailure();", failed)
        self.assertIn("notifyConnectionWorker();", connected)
        self.assertIn("notifyConnectionWorker();", failed)
        self.assertIn("ConnectionPhase::GapConnect", disconnected)
        self.assertIn("g_gapAttemptCompletion.deferFailure();", disconnected)
        self.assertIn("requestLinkLoss()", disconnected)

    def test_terminal_failure_is_committed_only_on_serialized_host_queue(self):
        failed = function_body(HOST, "void onConnectFail(NimBLEClient*, int reason) override")
        disconnected = function_body(HOST, "void onDisconnect(NimBLEClient*, int reason) override")
        deferred = function_body(HOST, "void deferredGapFailureCommitEvent(struct ble_npl_event*)")
        wait = function_body(HOST, "ble_session::GapAttemptOutcome waitForGapAttemptResult()")
        for callback in (failed, disconnected):
            self.assertNotIn("publishImmediateFailure", callback)
            self.assertNotIn("commitFailureAfterHostCallback", callback)
            self.assertNotIn("ble_npl_eventq_put", callback)
        self.assertIn("commitFailureAfterHostCallback();", deferred)
        self.assertNotIn("g_client", deferred)
        self.assertIn("nimble_port_get_dflt_eventq()", wait)
        self.assertIn("ble_npl_eventq_put", wait)
        self.assertIn("g_gapFailureCommitEventQueued = true;", wait)
        self.assertLess(wait.index("ble_npl_eventq_put"), wait.index("xSemaphoreTake(g_connWakeSemaphore, portMAX_DELAY)"))

        # Run with NOOIR_PINNED_NIMBLE_ROOT set to the exact dependency path to
        # assert the vendor callback/state-update order used by this handoff.
        if PINNED_NIMBLE:
            client = (PINNED_NIMBLE / "NimBLEClient.cpp").read_text(encoding="utf-8")
            disconnect = function_body(client, "case BLE_GAP_EVENT_DISCONNECT:")
            cb = disconnect.index("m_pClientCallbacks->onConnectFail(pClient, rc);")
            status = disconnect.index("pClient->m_connStatus = DISCONNECTED;", cb)
            self.assertLess(cb, status)

            port = (PINNED_NIMBLE / "nimble/porting/nimble/src/nimble_port.c").read_text(encoding="utf-8")
            run = function_body(port, "void\nnimble_port_run(void)")
            self.assertLess(run.index("ble_npl_eventq_get("), run.index("ble_npl_event_run(ev);"))
            getter = function_body(port, "struct ble_npl_eventq *\nnimble_port_get_dflt_eventq(void)")
            self.assertIn("return &g_eventq_dflt;", getter)

            npl = (PINNED_NIMBLE / "nimble/porting/npl/freertos/src/npl_os_freertos.c").read_text(encoding="utf-8")
            put = function_body(npl, "npl_freertos_eventq_put(struct ble_npl_eventq *evq")
            self.assertIn("xQueueSendToBack(evq->q, &ev, portMAX_DELAY)", put)
        else:
            self.skipTest("Set NOOIR_PINNED_NIMBLE_ROOT to run vendor callback-order contract")

    def test_gap_timeout_remains_pinned_nimble_owned_and_gatt_starts_only_after_callback(self):
        self.assertIn("g_client->setConnectTimeout(kConnectTimeoutMs);", HOST)
        connect = function_body(HOST, "void doConnect(")
        self.assertIn("startGapAttempt(addr)", connect)
        self.assertLess(connect.index("startGapAttempt(addr)"), connect.index("beginGattDiscovery()"))
        self.assertIn("if (connectionOperationAborted() || !g_connectionLifecycle.beginGattDiscovery()) return;", connect)


    def test_disconnect_callback_only_publishes_link_loss_for_worker(self):
        callback = function_body(HOST, "void onDisconnect(NimBLEClient*, int reason) override")
        self.assertIn("g_connectionLifecycle.requestLinkLoss()", callback)
        self.assertIn("notifyConnectionWorker();", callback)
        self.assertNotIn("onLinkDown", callback)
        self.assertNotIn("resetInputState", callback)
        self.assertNotIn("clearHidReportBindings", callback)

        link_down = function_body(HOST, "void BleKeyboardHost::onLinkDown()")
        self.assertIn("clearHidReportBindings();", link_down)
        self.assertIn("connected_ = false;", link_down)
        self.assertIn("connecting_ = false;", link_down)
        self.assertIn("resetInputState();", link_down)
        # GAP callback reports link state; the connection worker/end owner handles teardown.
        for forbidden in ("self().end()", "NimBLEDevice::deinit", "closeAllGapConnections", "vTaskDelete"):
            self.assertNotIn(forbidden, link_down)

    def test_all_worker_wakes_are_isolated_from_nimble_task_notifications(self):
        wake = function_body(HOST, "void notifyConnectionWorker()")
        self.assertIn("xSemaphoreGive(wake)", wake)
        self.assertNotIn("xTaskNotifyGive", HOST)
        self.assertNotIn("ulTaskNotifyTake", HOST)
        self.assertIn("xSemaphoreCreateBinaryStatic", HOST)
        self.assertGreaterEqual(HOST.count("xSemaphoreTake(g_connWakeSemaphore, portMAX_DELAY)"), 3)
        self.assertIn("g_connTaskNotifyRefs != 0", function_body(HOST, "void BleKeyboardHost::completeStoppedState()"))
        self.assertNotIn("vSemaphoreDelete", HOST)  # static semaphore outlives host cycles/callbacks

    def test_active_gatt_checks_abort_and_worker_consumes_link_loss_once(self):
        setup = function_body(HOST, "bool setupHid(NimBLEClient* client)")
        connect = function_body(HOST, "void doConnect(")
        worker = function_body(HOST, "void connTaskFn(void*)")
        self.assertGreaterEqual(setup.count("connectionOperationAborted()"), 12)
        self.assertGreaterEqual(connect.count("connectionOperationAborted()"), 10)
        self.assertIn("consumeLinkLossOnWorker();", worker)
        consume = function_body(HOST, "bool consumeLinkLossOnWorker()")
        self.assertIn("claimLinkLossCleanup()", consume)
        self.assertIn("self().onLinkDown();", consume)

    def test_poll_routes_both_missing_and_unclaimed_links_to_worker(self):
        poll = function_body(HOST, "void BleKeyboardHost::poll()")
        self.assertIn("g_connectionLifecycle.requestLinkLoss()", poll)
        self.assertIn("disconnect();", poll)
        self.assertNotIn("g_client->disconnect();", poll)

    def test_ready_link_remains_owned_until_link_loss_cleanup(self):
        self.assertIn("current != ConnectionPhase::Idle && current != ConnectionPhase::Ready", LIFECYCLE)
        self.assertIn("decodePhase(observed) == ConnectionPhase::Ready", LIFECYCLE)
        self.assertIn("bool requestLinkLoss()", LIFECYCLE)
        self.assertIn("bool claimLinkLossCleanup()", LIFECYCLE)

    def test_disconnect_phase_is_retained_for_delayed_on_disconnect_callback(self):
        finish = function_body(LIFECYCLE, "bool finishWorker()")
        self.assertIn("decodePhase(observed) == ConnectionPhase::Disconnecting", finish)
        self.assertIn("return false;", finish)
        disconnect_callback = function_body(HOST, "void onDisconnect(NimBLEClient*, int reason) override")
        self.assertIn("requestLinkLoss()", disconnect_callback)
        worker = function_body(HOST, "void connTaskFn(void*)")
        self.assertIn("consumeLinkLossOnWorker();", worker)
        perform = function_body(HOST, "void performOwnedClientDisconnect()")
        self.assertIn("if (!g_client || !g_client->isConnected())", perform)

    def test_gap_disconnect_is_confirmed_before_client_release_and_host_deinit(self):
        end = function_body(HOST, "void BleKeyboardHost::end()")
        disconnect = end.index("g_client->disconnect();")
        stale_link_terminate = end.index("ble_gap_terminate(handle, BLE_ERR_REM_USER_CONN_TERM);")
        gap_wait = end.index("if (!closeAllGapConnections(800))")
        settle = end.index("vTaskDelay(pdMS_TO_TICKS(150));")
        delete_client = end.index("deleteDisconnectedClients();")
        clear_pointer = end.index("g_client = nullptr;")
        deinit = end.index("NimBLEDevice::deinit(true);")

        self.assertLess(disconnect, gap_wait)
        self.assertLess(stale_link_terminate, gap_wait)
        self.assertLess(gap_wait, settle)
        self.assertLess(settle, delete_client)
        self.assertLess(delete_client, clear_pointer)
        self.assertLess(clear_pointer, deinit)
        self.assertIn("return;", end[gap_wait:settle])

    def test_begin_cannot_delete_worker_before_pending_shutdown_gate(self):
        begin = function_body(HOST, "bool BleKeyboardHost::begin(")
        pending_gate = begin.index("g_teardownPending.load")
        stale_recovery = begin.index("if (NimBLEDevice::isInitialized())")
        self.assertLess(pending_gate, stale_recovery)
        self.assertIn("return false;", begin[pending_gate:stale_recovery])
        retire = function_body(HOST, "bool retireUnreferencedConnectionWorker()")
        self.assertIn("g_connTaskNotifyRefs != 0", retire)
        self.assertLess(retire.index("g_connTask = nullptr;"), retire.index("vTaskDelete(staleWorker);"))

if __name__ == "__main__":
    unittest.main()
