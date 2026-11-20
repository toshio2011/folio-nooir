"""Keep the isolated allocation-lifetime tracer bounded and behavior-gated."""

from pathlib import Path
import unittest


ROOT = Path(__file__).resolve().parents[2]
TRACE_CPP = (ROOT / "src/util/HeapLifetimeDiagnostics.cpp").read_text(encoding="utf-8")
TRACE_STORE = (ROOT / "src/util/HeapLifetimeTraceStore.h").read_text(encoding="utf-8")
PLATFORMIO = (ROOT / "platformio.ini").read_text(encoding="utf-8")
BLE_INPUT = (ROOT / "src/BleInput.cpp").read_text(encoding="utf-8")
BLE_HOST = (ROOT / "freeink-sdk/libs/network/BleKeyboardHost/src/BleKeyboardHost.cpp").read_text(
    encoding="utf-8"
)
ACTIVITY_MANAGER = (ROOT / "src/activities/ActivityManager.cpp").read_text(encoding="utf-8")
MAIN = (ROOT / "src/main.cpp").read_text(encoding="utf-8")


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


class HeapLifetimeTraceContract(unittest.TestCase):
    def test_trace_is_a_unique_spi_array_ble_diagnostic_profile(self):
        profile = PLATFORMIO.split("[env:gh_release_spiarray_ble_heaptrace_20261105_01]", 1)[1].split(
            "\n[", 1
        )[0]
        self.assertIn("extends = env:gh_release_spiarray_ble_resourcediag_02", profile)
        self.assertIn("USE_SPI_ARRAY_TRANSFER=1", PLATFORMIO)
        self.assertIn("-DNOOIR_HEAP_LIFETIME_TRACE=1", profile)
        self.assertIn("CONFIG_HEAP_USE_HOOKS=y", profile)
        self.assertIn("--wrap=esp_bt_controller_init", profile)
        self.assertIn("--wrap=esp_bt_controller_deinit", profile)
        self.assertNotIn("build_dir", profile)
        self.assertIn("gh_release_spiarray_ble_heaptrace_20261105_01", PLATFORMIO)

    def test_hook_store_has_fixed_capacity_and_explicit_overflow_accounting(self):
        self.assertIn("using Store = TraceStore<160, 48>;", TRACE_CPP)
        self.assertIn("sizeof(Store) <= 5900", TRACE_CPP)
        self.assertIn("g_store.storageBytes()", TRACE_CPP)
        self.assertIn("++summary_.droppedLiveRecords", TRACE_STORE)
        self.assertIn("++summary_.droppedEvents", TRACE_STORE)
        self.assertIn("++summary_.allocations", TRACE_STORE)
        self.assertIn("++summary_.unmatchedFrees", TRACE_STORE)

    def test_allocator_rounded_sizes_are_measured_only_at_safe_checkpoints(self):
        self.assertIn("heap_caps_get_allocated_size", TRACE_CPP)
        self.assertIn("vTaskSuspendAll()", TRACE_CPP)
        self.assertIn("xTaskResumeAll()", TRACE_CPP)
        self.assertIn("snapshot.actualTrusted", TRACE_CPP)
        self.assertIn("g_reentrantEvents == 0", TRACE_CPP)
        self.assertIn("coverageComplete", TRACE_CPP)
        self.assertIn("droppedLiveRecords == 0", TRACE_CPP)
        self.assertIn("unmatchedFrees == 0", TRACE_CPP)
        self.assertIn("actual_bytes=%u", TRACE_CPP)
        self.assertIn("Any logger allocations are real allocations too", TRACE_CPP)
        alloc = function_body(TRACE_CPP, 'extern "C" void esp_heap_trace_alloc_hook')
        free = function_body(TRACE_CPP, 'extern "C" void esp_heap_trace_free_hook')
        self.assertNotIn("heap_caps_get_allocated_size", alloc)
        self.assertNotIn("heap_caps_get_allocated_size", free)

    def test_allocator_hooks_do_not_log_allocate_or_block(self):
        alloc = function_body(TRACE_CPP, 'extern "C" void esp_heap_trace_alloc_hook')
        free = function_body(TRACE_CPP, 'extern "C" void esp_heap_trace_free_hook')
        for body in (alloc, free):
            for forbidden in ("LOG_", "Serial", "malloc(", "calloc(", "realloc(", "free(", "new ", "Storage.", "portENTER_CRITICAL"):
                self.assertNotIn(forbidden, body)
        self.assertIn("esp_timer_get_time()", TRACE_CPP)
        self.assertNotIn("esp_cpu_get_cycle_count", TRACE_CPP)
        self.assertNotIn("rdcycle", TRACE_CPP.lower())

    def test_trace_uses_allocator_hooks_once_for_cpp_and_c_allocation_paths(self):
        self.assertIn("esp_heap_trace_alloc_hook", TRACE_CPP)
        self.assertIn("esp_heap_trace_free_hook", TRACE_CPP)
        self.assertIn("heap_caps_register_failed_alloc_callback", TRACE_CPP)
        self.assertNotIn("__wrap_malloc", TRACE_CPP)
        self.assertNotIn("__wrap__Zn", TRACE_CPP)

    def test_checkpoints_cover_home_ble_hid_shutdown_activity_and_sleep(self):
        for stage in (
            "home_before_ble_startup",
            "ble_startup_return",
            "ble_shutdown_begin",
            "ble_shutdown_request_return",
        ):
            self.assertIn(stage, BLE_INPUT)
        for stage in ("host_ready", "hid_input_ready", "host_end_pre", "host_end_post"):
            self.assertIn(stage, BLE_HOST)
        for stage in ("home_destroy_begin", "home_destroy_complete", "home_activity_enter_complete"):
            self.assertIn(stage, ACTIVITY_MANAGER)
        self.assertIn("sleep_cleanup_complete", (ROOT / "src/activities/boot_sleep/SleepActivity.cpp").read_text(
            encoding="utf-8"
        ))
        self.assertIn('cmd == "HEAPTRACE_DUMP"', MAIN)

    def test_ble_disabled_still_returns_before_nimble_start(self):
        body = function_body(BLE_INPUT, "bool ensureStarted()")
        disabled = body.index("if (!SETTINGS.bluetoothEnabled) return false;")
        begin = body.index("BleHid.begin(kHostName)")
        self.assertLess(disabled, begin)
        self.assertIn("if (!SETTINGS.bluetoothEnabled) return;", function_body(BLE_INPUT, "void stopForReaderResourcePressure()"))


if __name__ == "__main__":
    unittest.main()
