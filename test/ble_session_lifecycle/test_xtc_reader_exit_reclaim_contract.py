"""Keep XTC exit progress-safe and release rebuildable Reader memory in order."""

from pathlib import Path
import unittest


ROOT = Path(__file__).resolve().parents[2]
SOURCE = (ROOT / "src/activities/reader/XtcReaderActivity.cpp").read_text(encoding="utf-8")


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


class XtcReaderExitReclaimContract(unittest.TestCase):
    def test_position_and_stats_are_saved_before_parser_release(self):
        body = function_body(SOURCE, "void XtcReaderActivity::onExit()")
        self.assertLess(body.index("saveProgress();"), body.index("xtc.reset();"))
        self.assertLess(body.index("READING_STATS.recordSession"), body.index("xtc.reset();"))

    def test_rebuildable_reader_caches_release_after_format_owner(self):
        body = function_body(SOURCE, "void XtcReaderActivity::onExit()")
        ordered = (
            "xtc.reset();",
            "releaseSdFontCaches()",
            "ImageBlock::releaseRenderCache()",
            "renderer.releaseGrayscaleStripScratch()",
            "sdFontSystem.releaseReaderCaches()",
            "sdFontSystem.releaseReaderFontOwner(renderer)",
            "releaseReaderPressureState()",
        )
        offsets = [body.index(item) for item in ordered]
        self.assertEqual(offsets, sorted(offsets))

    def test_heap_checkpoints_bracket_each_cleanup_group(self):
        body = function_body(SOURCE, "void XtcReaderActivity::onExit()")
        stages = (
            "xtc_exit_before_reset",
            "xtc_exit_after_reset",
            "xtc_exit_disposable_font",
            "xtc_exit_render_cache",
            "xtc_exit_sd_font_state",
            "xtc_exit_pressure_state",
        )
        for stage in stages:
            self.assertIn(f'NOOIR_READER_MEMORY_CHECKPOINT("{stage}"', body)

    def test_exit_cleanup_does_not_change_ble_policy_or_persist_again(self):
        body = function_body(SOURCE, "void XtcReaderActivity::onExit()")
        self.assertNotIn("BleHid", body)
        self.assertNotIn("startBluetooth", body)
        self.assertNotIn("stopBluetooth", body)
        self.assertEqual(body.count("saveProgress();"), 1)


if __name__ == "__main__":
    unittest.main()
