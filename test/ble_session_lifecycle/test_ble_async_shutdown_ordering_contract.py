"""Contracts for asynchronous BLE stop acknowledgement and safe callers."""

from pathlib import Path
import unittest


ROOT = Path(__file__).resolve().parents[2]
BLE_INPUT = (ROOT / "src/BleInput.cpp").read_text(encoding="utf-8")
BLE_HEADER = (ROOT / "src/BleInput.h").read_text(encoding="utf-8")
MANAGER = (ROOT / "src/activities/ActivityManager.cpp").read_text(encoding="utf-8")
MANAGER_HEADER = (ROOT / "src/activities/ActivityManager.h").read_text(encoding="utf-8")
MAIN = (ROOT / "src/main.cpp").read_text(encoding="utf-8")
HOST = (ROOT / "freeink-sdk/libs/network/BleKeyboardHost/src/BleKeyboardHost.cpp").read_text(encoding="utf-8")


def function_body(source: str, signature: str) -> str:
    start = source.index(signature)
    opening = source.index("{", start)
    depth = 0
    for index in range(opening, len(source)):
        if source[index] == "{":
            depth += 1
        elif source[index] == "}":
            depth -= 1
            if depth == 0:
                return source[opening + 1 : index]
    raise AssertionError(f"unclosed function: {signature}")


class BleAsyncShutdownOrderingContract(unittest.TestCase):
    def test_gap_navigation_stop_is_worker_canceled_without_timeout_wait(self):
        gap_wait = function_body(HOST, "ble_session::GapAttemptOutcome waitForGapAttemptResult()")
        self.assertIn("connectionOperationAborted()", gap_wait)
        self.assertIn("mayCancelGapFromWorker(true, cancelRequested", gap_wait)
        self.assertIn("ble_gap_conn_active() != 0", gap_wait)
        self.assertIn("g_client->cancelConnect()", gap_wait)
        self.assertIn("xSemaphoreTake(g_connWakeSemaphore, portMAX_DELAY)", gap_wait)
        self.assertNotIn("vTaskDelay", gap_wait)

        end = function_body(HOST, "void BleKeyboardHost::end()")
        worker_gate = end.index("if (workerOrScanActive || scanOperationInProgress())")
        worker_return = end.index("return;", worker_gate)
        self.assertIn("notifyConnectionWorker();", end[worker_gate:worker_return])
        self.assertLess(worker_return, end.index("closeAllGapConnections(800)"))
        self.assertNotIn("ulTaskNotifyTake", end)

        manager_loop = function_body(MANAGER, "void ActivityManager::loop()")
        transition_gate = manager_loop.index(
            "bleShutdownRequiredForPendingTransition() && (BleHid.isRunning() || bleinput::stopPending())")
        first_transition_return = manager_loop.index("return;", transition_gate)
        self.assertNotIn("delay(", manager_loop[transition_gate:first_transition_return])
        self.assertNotIn("ulTaskNotifyTake", manager_loop[transition_gate:first_transition_return])

    def test_stop_request_is_one_shot_and_ack_waits_for_host_stopped(self):
        stop = function_body(BLE_INPUT, "void stop()")
        self.assertIn("g_stopRequestPending.load(std::memory_order_acquire)", stop)
        self.assertIn("compare_exchange_strong", stop)
        self.assertIn("BleHid.end();", stop)
        self.assertIn("stopPending()", BLE_HEADER)

        poll = function_body(BLE_INPUT, "void pollLifecycle()")
        self.assertIn("g_stopRequestPending.load(std::memory_order_acquire) && !BleHid.isRunning()", poll)
        self.assertIn("g_stopRequestPending.exchange(false", poll)
        self.assertNotIn("g_stopRequestPending.exchange(false", stop)

    def test_host_publishes_stopped_only_after_worker_and_nimble_cleanup(self):
        complete = function_body(HOST, "void BleKeyboardHost::completeStoppedState()")
        worker_ready = complete.index("!g_workerTeardownReady.load")
        worker_delete = complete.index("vTaskDelete(finishedWorker)")
        nimble_check = complete.index("if (NimBLEDevice::isInitialized())", worker_delete)
        stopped_publish = complete.index("begun_ = false;")
        self.assertLess(worker_ready, worker_delete)
        self.assertLess(worker_delete, nimble_check)
        self.assertLess(nimble_check, stopped_publish)
        self.assertIn("g_connTaskNotifyRefs != 0", complete)

    def test_activity_transition_waits_for_completion_ack_before_input_or_exit(self):
        loop = function_body(MANAGER, "void ActivityManager::loop()")
        gate = loop.index("bleShutdownRequiredForPendingTransition() && (BleHid.isRunning() || bleinput::stopPending())")
        self.assertLess(gate, loop.index("currentActivity->loop();"))
        self.assertIn("return;", loop[gate:])
        self.assertIn("bool bleShutdownRequiredForPendingTransition() const;", MANAGER_HEADER)
        self.assertIn("if (!exitActivity(lock))", loop)

    def test_sleep_resumes_only_after_main_loop_sees_stopped_host(self):
        enter_sleep = function_body(MAIN, "void enterDeepSleep(bool fromTimeout = false)")
        self.assertIn("sleepBleTeardownPending = true;", enter_sleep)
        self.assertIn("bleinput::stop();", enter_sleep)
        self.assertIn("return;", enter_sleep)
        self.assertIn("ble_quiesced", enter_sleep)
        self.assertIn("async_waited=%u", enter_sleep)

        loop = function_body(MAIN, "void loop()")
        wait = loop.index("if (sleepBleTeardownPending)")
        host_check = loop.index("if (BleHid.isRunning())", wait)
        sleep_resume = loop.index("enterDeepSleep(sleepPendingFromTimeout);", host_check)
        self.assertLess(host_check, sleep_resume)
        self.assertIn("bleinput::pollLifecycle();", loop[:wait])

    def test_sleep_and_transition_wait_do_not_repeat_stop_requests(self):
        lifecycle = function_body(MAIN, "void updateBluetoothLifecycle()")
        sleep_guard = lifecycle.index("if (sleepBleTeardownPending) return;")
        transition_gate = lifecycle.index("bleShutdownRequiredForPendingTransition() || bleinput::stopPending()")
        self.assertLess(sleep_guard, transition_gate)
        self.assertIn("if (BleHid.isRunning() && !bleinput::stopPending())", lifecycle[transition_gate:])


if __name__ == "__main__":
    unittest.main()
