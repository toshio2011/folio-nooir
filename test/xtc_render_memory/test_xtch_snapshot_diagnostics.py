"""Keep XTCH snapshot heap traces cheap and release-boundary aware."""

from pathlib import Path
import unittest


ROOT = Path(__file__).resolve().parents[2]
SOURCE = (ROOT / "src/activities/reader/XtcReaderActivity.cpp").read_text(encoding="utf-8")


class XtchSnapshotDiagnosticsContract(unittest.TestCase):
    def test_snapshot_heap_deltas_are_compile_gated_and_name_the_owner(self):
        self.assertIn("defined(FREEINK_BLE_HID_LIFECYCLE_DIAGNOSTICS)", SOURCE)
        self.assertIn("XTCH bw_snapshot phase=store", SOURCE)
        self.assertIn("XTCH bw_snapshot phase=release", SOURCE)
        self.assertIn("owner=xtch_bw_snapshot", SOURCE)
        self.assertIn("free=%u->%u", SOURCE)
        self.assertIn("largest=%u->%u", SOURCE)

    def test_every_stored_snapshot_release_uses_the_instrumented_boundary(self):
        self.assertEqual(SOURCE.count("renderer.restoreBwBuffer();"), 1)
        self.assertIn('restoreBwSnapshot("lsb_stream_error");', SOURCE)
        self.assertIn('restoreBwSnapshot("msb_stream_error");', SOURCE)
        self.assertIn('restoreBwSnapshot("render_complete");', SOURCE)


if __name__ == "__main__":
    unittest.main()
