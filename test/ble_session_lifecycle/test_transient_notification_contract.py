"""Keep global toasts behind modal UI and restore their framebuffer underlay."""

from pathlib import Path
import unittest


ROOT = Path(__file__).resolve().parents[2]
MANAGER = (ROOT / "src/activities/ActivityManager.cpp").read_text(encoding="utf-8")
HOME = (ROOT / "src/activities/home/HomeActivity.h").read_text(encoding="utf-8")
RECENT = (ROOT / "src/activities/home/RecentBooksActivity.h").read_text(encoding="utf-8")
THEME = (ROOT / "src/components/themes/BaseTheme.cpp").read_text(encoding="utf-8")


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


class TransientNotificationContract(unittest.TestCase):
    def test_home_menu_sequences_gate_pending_and_visible_toasts(self):
        body = function_body(MANAGER, "void ActivityManager::renderTaskLoop()")
        self.assertIn("currentActivity->hasModalOverlay()", body)
        self.assertIn("TransientNotificationPolicy::canPaint(false, modalOverlayActive)", body)
        self.assertIn("TransientNotificationPolicy::canPaint(isReaderActivity(), modalOverlayActive)", body)
        self.assertIn("hasModalOverlay() const override", HOME)
        self.assertIn("bool hasModalOverlay() const override", RECENT)

    def test_toast_underlay_is_restored_before_activity_redraw_and_refreshed_if_idle(self):
        body = function_body(MANAGER, "void ActivityManager::renderTaskLoop()")
        self.assertLess(body.index("copyBufferToRegion("), body.index("currentActivity->render("))
        self.assertIn("stage=backdrop_refresh", body)
        self.assertIn("getDisplayUpdateSequence() == displayUpdateSequenceBeforeRender", body)
        self.assertIn("kMaxTransientNotificationBackingBytes = 8 * 1024", body)

    def test_modal_close_uses_fast_refresh_and_dismissal_keeps_input_forwarding(self):
        render = function_body(MANAGER, "void ActivityManager::renderTaskLoop()")
        self.assertIn("modalOverlayClosed", render)
        self.assertIn("stage=modal_close_refresh", render)
        self.assertIn("stage=modal_close_refresh mode=fast", render)
        self.assertNotIn("requestNextRefreshMode(HalDisplay::FULL_REFRESH)", render)
        loop = function_body(MANAGER, "void ActivityManager::loop()")
        self.assertLess(loop.index("if (dismissed) requestUpdate();"), loop.index("currentActivity->loop();"))

    def test_notification_bounds_measure_without_heap_allocations(self):
        body = function_body(THEME, "Rect BaseTheme::popupPaintBounds(")
        self.assertIn("char line[kNotificationLineCapacity]", body)
        self.assertNotIn("std::vector", body)
        self.assertNotIn("std::string", body)


if __name__ == "__main__":
    unittest.main()
