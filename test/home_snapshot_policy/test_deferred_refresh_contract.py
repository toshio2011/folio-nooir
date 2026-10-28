"""Keep periodic Home ghost cleanup off active frames and on safe sleep boundaries."""

from pathlib import Path
import unittest


ROOT = Path(__file__).resolve().parents[2]
HOME = (ROOT / "src/activities/home/RecentBooksActivity.cpp").read_text(encoding="utf-8")
SLEEP = (ROOT / "src/activities/boot_sleep/SleepActivity.cpp").read_text(encoding="utf-8")
POLICY = (ROOT / "src/util/HomeSnapshotPolicy.h").read_text(encoding="utf-8")


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


class DeferredRefreshContract(unittest.TestCase):
    def test_home_selection_uses_fast_refresh_and_only_latches_cleanup_debt(self):
        body = function_body(HOME, "void displayHomeFrame(")
        self.assertIn("chooseRefresh(orientationChanged)", body)
        self.assertIn("FAST_REFRESH", body)
        self.assertIn("mode=deferred cause=periodic_ghost_cleanup", body)
        self.assertNotIn("PeriodicGhostCleanup", body)

    def test_safe_sleep_cleanup_reuses_the_completed_graphic_frame(self):
        cleanup = function_body(SLEEP, "bool runDeferredHomeGhostCleanup(")
        frame = function_body(SLEEP, "void displaySleepFrame(")
        self.assertIn("chooseMaintenanceRefresh(refreshState, safeBoundary)", cleanup)
        self.assertIn('displayBuffer(HalDisplay::FULL_REFRESH, "periodic_ghost_cleanup_sleep_boundary")', cleanup)
        self.assertNotRegex(cleanup, r"\b(clearScreen|drawBitmap|drawText|fillRect|invertScreen)\s*\(")
        self.assertIn("if (!runDeferredHomeGhostCleanup(renderer, safeBoundary)) renderer.displayBuffer(mode);", frame)
        self.assertNotRegex(frame, r"\b(clearScreen|drawBitmap|drawText|fillRect|invertScreen)\s*\(")

    def test_sleep_previews_do_not_consume_pending_home_cleanup(self):
        self.assertIn("displaySleepFrame(renderer, HalDisplay::HALF_REFRESH, !preview)", SLEEP)
        self.assertIn("refreshOverlayBase(renderer, !preview)", SLEEP)
        self.assertIn("if (safeBoundary && state.periodicCleanupPending)", POLICY)


if __name__ == "__main__":
    unittest.main()
