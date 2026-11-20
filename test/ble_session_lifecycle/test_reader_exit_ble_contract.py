"""Keep Reader teardown from saving and freeing state under an active BLE host."""

from pathlib import Path
import unittest


ROOT = Path(__file__).resolve().parents[2]
MANAGER = (ROOT / "src/activities/ActivityManager.cpp").read_text(encoding="utf-8")


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


class ReaderExitBleContract(unittest.TestCase):
    def test_resource_owner_exit_refuses_to_destroy_state_before_stop_ack(self):
        body = function_body(MANAGER, "bool ActivityManager::exitActivity(")
        guard = body.index("currentActivity->isReaderActivity() || currentActivity->bluetoothResourceSensitive()")
        reader_exit = body.index("currentActivity->onExit();")
        self.assertLess(guard, reader_exit)
        self.assertIn("return false;", body[guard:reader_exit])
        self.assertNotIn("bleinput::stop();", body[guard:reader_exit])
        self.assertIn("BleHid.isRunning() || bleinput::stopPending()", body[guard:reader_exit])
        self.assertNotIn("stage=reader_exit_ble_stopped", body)

    def test_reader_transition_waits_for_completion_ack_before_destroying_activity(self):
        loop = function_body(MANAGER, "void ActivityManager::loop()")
        gate = loop.index("bleShutdownRequiredForPendingTransition() && (BleHid.isRunning() || bleinput::stopPending())")
        self.assertLess(gate, loop.index("currentActivity->loop();"))
        self.assertIn("if (BleHid.isRunning() && !bleinput::stopPending())", loop[gate:])
        self.assertIn("bleinput::stop();", loop[gate:])


if __name__ == "__main__":
    unittest.main()
