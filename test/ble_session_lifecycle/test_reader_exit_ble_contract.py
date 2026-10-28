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
    def test_active_ble_is_stopped_once_before_reader_exit_cleanup(self):
        body = function_body(MANAGER, "void ActivityManager::exitActivity(")
        guarded_stop = body.index("if (readerActivity && BleHid.isRunning())")
        reader_exit = body.index("currentActivity->onExit();")
        self.assertLess(guarded_stop, reader_exit)
        stop_section = body[guarded_stop:reader_exit]
        self.assertIn("bleinput::clearReaderBleReturnPending();", stop_section)
        self.assertIn("bleinput::stop();", stop_section)
        self.assertIn("stage=reader_exit_ble_stopped", stop_section)
        self.assertNotIn("stopForReaderResourcePressure", stop_section)


if __name__ == "__main__":
    unittest.main()
