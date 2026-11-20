from pathlib import Path
import re
import unittest

ROOT = Path(__file__).resolve().parents[2]
BLE = (ROOT / "src/BleInput.cpp").read_text(encoding="utf-8")
MAIN = (ROOT / "src/main.cpp").read_text(encoding="utf-8")
READER_MENU = (ROOT / "src/activities/reader/EpubReaderMenuActivity.cpp").read_text(encoding="utf-8")
SETTINGS = (ROOT / "src/activities/settings/BluetoothSettingsActivity.cpp").read_text(encoding="utf-8")
KEYS = (ROOT / "lib/I18n/I18nKeys.h").read_text(encoding="utf-8")
TRANSLATIONS = ROOT / "lib/I18n/translations"


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
    raise AssertionError(f"Unclosed function body: {signature}")


class BleStatusFeedbackContract(unittest.TestCase):
    def test_status_uses_shared_existing_lifecycle_state_with_specific_precedence(self):
        body = function_body(BLE, "const char* userFacingStatusText()")
        ordered = (
            "!SETTINGS.bluetoothEnabled",
            "BleHid.isConnected()",
            "BleHid.isConnecting()",
            "webExclusiveActive()",
            "readerWaitingForMemory()",
            "readerBleReturnPending()",
            "reAdmissionPending()",
            "!BleHid.isRunning()",
        )
        positions = [body.index(token) for token in ordered]
        self.assertEqual(positions, sorted(positions))
        for key in (
            "STR_BT_WAITING_FOR_MEMORY",
            "STR_BT_SUSPENDED_FOR_READER",
            "STR_BT_RECONNECT_PENDING",
        ):
            self.assertIn(f"StrId::{key}", body)
            self.assertIn(key, KEYS)

    def test_all_31_locales_have_nonempty_status_translations(self):
        files = sorted(TRANSLATIONS.glob("*.yaml"))
        self.assertEqual(len(files), 31)
        expected = (
            "STR_BT_WAITING_FOR_MEMORY",
            "STR_BT_SUSPENDED_FOR_READER",
            "STR_BT_RECONNECT_PENDING",
        )
        for path in files:
            text = path.read_text(encoding="utf-8")
            for key in expected:
                matches = re.findall(rf"(?m)^{key}:\s*\"([^\"]+)\"\s*$", text)
                self.assertEqual(len(matches), 1, f"{path.name}: {key}")
                self.assertTrue(matches[0].strip(), f"{path.name}: empty {key}")

    def test_memory_wait_is_only_set_for_quiescent_reader_heap_rejection(self):
        self.assertIn("const bool quiescentReaderHeapDefer", MAIN)
        predicate = MAIN[MAIN.index("const bool quiescentReaderHeapDefer") :]
        predicate = predicate[: predicate.index(";") + 1]
        for token in (
            "activityManager.isCurrentReaderActivity()",
            "readerBleReturnPending()",
            "!resourceSensitive",
            "!RenderLock::peek()",
            "!activityManager.hasPendingRenderWork()",
            "WiFi.getMode() == WIFI_MODE_NULL",
        ):
            self.assertIn(token, predicate)

    def test_memory_wait_latch_clears_with_pending_and_host_transitions(self):
        clear = "g_readerWaitingForMemory.store(false, std::memory_order_release)"
        for signature in (
            "void clearReaderBleReturnPending()",
            "void requestLifecycleReevaluation()",
            "void signalReaderBleReturnLifecycleEvent(const char* reason)",
            "void stopForReaderResourcePressure()",
        ):
            self.assertIn(clear, function_body(BLE, signature), signature)
        poll = function_body(BLE, "void pollLifecycle()")
        disabled = poll[: poll.index("sampleHeap();")]
        connected = poll[poll.index("if (running && connected && !g_lastConnected)") :]
        self.assertIn(clear, disabled)
        self.assertIn(clear, connected)
        lifecycle = function_body(MAIN, "void updateBluetoothLifecycle()")
        self.assertGreaterEqual(lifecycle.count("bleinput::setReaderWaitingForMemory(false)"), 5)
        self.assertIn("bleinput::setReaderWaitingForMemory(quiescentReaderHeapDefer)", lifecycle)
        # Every early state transition before admission has a clear so an old
        # heap rejection cannot survive a newer lifecycle decision.
        for marker in ("if (bleinput::webExclusiveActive())", "if (!wanted)", "if (blocked)"):
            block = lifecycle[lifecycle.index(marker) :]
            block = block[: block.index("\n  }") + 4]
            self.assertIn("bleinput::setReaderWaitingForMemory(false)", block, marker)
        self.assertIn(
            "bleinput::setReaderWaitingForMemory(false);\n    activityManager.notifyBluetoothActivationResult(true);",
            lifecycle,
        )
        admitted = lifecycle[lifecycle.index('LOG_INF("BLEMEM", "stage=cold_admission result=allow') :]
        self.assertIn("bleinput::setReaderWaitingForMemory(false)", admitted)

    def test_existing_settings_and_reader_menu_share_status_without_new_refresh_path(self):
        self.assertIn("bleinput::userFacingStatusText()", SETTINGS)
        self.assertIn("bleinput::userFacingStatusText()", READER_MENU)
        render = READER_MENU[READER_MENU.index("void EpubReaderMenuActivity::render") :]
        self.assertNotIn("postBluetoothNotification", render)
        self.assertNotIn("requestUpdate", render)
        self.assertNotIn("delay(", render)


if __name__ == "__main__":
    unittest.main()
