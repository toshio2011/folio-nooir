"""Keep BLE startup/teardown heap traces explicit about runtime history."""

from pathlib import Path
import unittest


ROOT = Path(__file__).resolve().parents[2]
BLE = (ROOT / "src/BleInput.cpp").read_text(encoding="utf-8")
ACTIVITY = (ROOT / "src/activities/ActivityManager.cpp").read_text(encoding="utf-8")
MAIN = (ROOT / "src/main.cpp").read_text(encoding="utf-8")
POLICY = (ROOT / "src/util/BleMemoryPolicy.h").read_text(encoding="utf-8")
READER_SHELL = (ROOT / "src/activities/reader/ReaderActivity.cpp").read_text(encoding="utf-8")
XTC_READER = (ROOT / "src/activities/reader/XtcReaderActivity.cpp").read_text(encoding="utf-8")


def function_body(source: str, signature: str) -> str:
    start = source.index(signature)
    open_brace = source.index("{", start)
    depth = 0
    for index in range(open_brace, len(source)):
        if source[index] == "{":
            depth += 1
        elif source[index] == "}":
            depth -= 1
            if depth == 0:
                return source[open_brace + 1 : index]
    raise AssertionError(f"unclosed function body: {signature}")


class BleRuntimeShapeContract(unittest.TestCase):
    def test_runtime_state_tracks_successful_boot_scoped_start_history(self):
        self.assertIn("std::atomic<bool> g_bleEverStarted{false}", BLE)
        self.assertIn("if (started) g_bleEverStarted.store(true", BLE)
        self.assertIn("bleRuntimeStateName(SETTINGS.bluetoothEnabled != 0, BleHid.isRunning()", BLE)
        for state in ("runtime_off", "disabled_after_use", "enabled_not_started", "stopped_after_use", "running"):
            self.assertIn(f'"{state}"', POLICY)

    def test_start_and_stop_log_caps_and_runtime_state(self):
        for stage in ("ble_begin_before", "ble_begin_after"):
            self.assertIn(f'logBleHeapShape("{stage}")', BLE)
        for stage in ("ble_stop_request_before", "ble_stop_request_return"):
            self.assertIn(f'logBleHeapShape("{stage}")', BLE)
        self.assertNotIn('logBleHeapShape("ble_stop_after")', BLE)
        self.assertIn("MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT", BLE)
        self.assertIn("MALLOC_CAP_DMA | MALLOC_CAP_8BIT", BLE)
        self.assertIn("ble_runtime=%s ble_enabled=%u ble_ever_started=%u", BLE)

    def test_reader_transitions_and_cold_admission_include_runtime_state(self):
        self.assertIn("bleinput::runtimeStateName()", ACTIVITY)
        self.assertIn("bleinput::runtimeStateName()", MAIN)
        self.assertNotIn("stage=reader_exit_ble_stopped runtime=%s enabled=%u", ACTIVITY)
        self.assertIn('stage=stop_complete elapsed_ms=%u free=%u largest=%u owner=main_poll', BLE)
        self.assertIn("if (bleinput::stopPending()) return;", MAIN)

    def test_memory_admission_floors_are_unchanged(self):
        self.assertIn("kReaderBleStartMinFreeHeap = 96 * 1024", POLICY)
        self.assertIn("kReaderBleLogicalMinLargestBlock = 36 * 1024", POLICY)

    def test_ble_off_skips_host_admission_and_reader_survival_stop(self):
        ensure = function_body(BLE, "bool ensureStarted()")
        off_gate = ensure.index("if (!SETTINGS.bluetoothEnabled) return false;")
        self.assertLess(off_gate, ensure.index("BleHid.setConnectionPermissionCallback"))
        self.assertLess(off_gate, ensure.index("BleHid.begin(kHostName)"))

        survival_stop = function_body(BLE, "void stopForReaderResourcePressure()")
        off_gate = survival_stop.index("if (!SETTINGS.bluetoothEnabled) return;")
        for operation in ("requestLifecycleReevaluation();", "stop();"):
            self.assertLess(off_gate, survival_stop.index(operation))
        self.assertIn("const bool eligible = SETTINGS.bluetoothEnabled && activityManager.isReaderActivity();",
                      survival_stop)
        self.assertIn("g_readerBleReturnPending.exchange(true", survival_stop)
        self.assertLess(survival_stop.index("requestLifecycleReevaluation();"), survival_stop.index("stop();"))

        reader_enter = function_body(READER_SHELL, "void ReaderActivity::onEnter()")
        enabled_branch = reader_enter.index("if (bleinput::bluetoothEnabled())")
        self.assertLess(enabled_branch, reader_enter.index("bleinput::stopForReaderResourcePressure();"))
        self.assertLess(enabled_branch, reader_enter.index('bleinput::signalReaderBleReturnLifecycleEvent("reader_enter")'))
        self.assertIn("showOpeningBookFeedback(renderer, initialBookPath);", reader_enter)

    def test_ble_off_skips_admission_reconnect_and_ble_only_home_reclaim(self):
        lifecycle = function_body(MAIN, "void updateBluetoothLifecycle()")
        wanted = lifecycle.index("!deepSleepInProgress && bleinput::bluetoothEnabled()")
        not_wanted = lifecycle.index("if (!wanted)", wanted)
        self.assertLess(wanted, not_wanted)
        self.assertLess(not_wanted, lifecycle.index("if (!bleinput::reAdmissionPending())"))
        self.assertLess(not_wanted, lifecycle.index("const bool reclaimAllowed"))

        reclaim_gate = function_body(POLICY, "constexpr bool homeBleStartReclaimAllowed(")
        self.assertIn("bluetoothEnabled", reclaim_gate)

    def test_ble_off_preserves_xct_reader_caches_at_exit(self):
        on_exit = function_body(XTC_READER, "void XtcReaderActivity::onExit()")
        self.assertIn("xtc.reset();", on_exit)
        reset = on_exit.index("xtc.reset();")
        ble_reclaim = on_exit.index("if (bleinput::bluetoothEnabled())")
        self.assertLess(reset, ble_reclaim)

        release_calls = (
            "releaseSdFontCaches()",
            "ImageBlock::releaseRenderCache()",
            "renderer.releaseGrayscaleStripScratch()",
            "sdFontSystem.releaseReaderCaches()",
            "sdFontSystem.releaseReaderFontOwner(renderer)",
            "releaseReaderPressureState()",
        )
        block_open = on_exit.index("{", ble_reclaim)
        depth = 0
        block_close = None
        for index in range(block_open, len(on_exit)):
            if on_exit[index] == "{":
                depth += 1
            elif on_exit[index] == "}":
                depth -= 1
                if depth == 0:
                    block_close = index
                    break
        self.assertIsNotNone(block_close)
        ble_reclaim_body = on_exit[block_open + 1 : block_close]
        for call in release_calls:
            self.assertIn(call, ble_reclaim_body)
        self.assertLess(reset, ble_reclaim)


if __name__ == "__main__":
    unittest.main()
