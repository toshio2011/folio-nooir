"""Keep the cold BLE retry bounded to quiescent Home memory pressure."""

from pathlib import Path
import unittest


ROOT = Path(__file__).resolve().parents[2]
MAIN = (ROOT / "src/main.cpp").read_text(encoding="utf-8")
ACTIVITY_MANAGER = (ROOT / "src/activities/ActivityManager.cpp").read_text(encoding="utf-8")


def function_body(source, signature):
    start = source.index(signature)
    open_brace = source.index("{", start)
    depth = 1
    for index in range(open_brace + 1, len(source)):
        if source[index] == "{":
            depth += 1
        elif source[index] == "}":
            depth -= 1
            if depth == 0:
                return source[open_brace + 1:index]
    raise AssertionError(f"unterminated function: {signature}")


class HomeBleReclaimContract(unittest.TestCase):
    def test_home_reclaim_uses_existing_floors_and_disposable_cache_path(self):
        self.assertIn('strcmp(activityName, "Home") == 0', MAIN)
        self.assertIn('strcmp(activityName, "RecentBooks") == 0', MAIN)
        self.assertIn("bleinput::homeBleStartReclaimAllowed", MAIN)
        self.assertIn('"ble_home_cold_start"', MAIN)
        self.assertIn("memorypressure::ReclaimLevel::Reconstructible", MAIN)

    def test_retry_waits_until_render_is_idle_and_heap_is_rechecked(self):
        self.assertIn("RenderLock::peek() || activityManager.hasPendingRenderWork()", MAIN)
        self.assertIn("if (reclaim.memoryImproved())", MAIN)
        self.assertIn("admitted = bleinput::connectionAdmissionAllowed();", MAIN)
        self.assertIn("bleinput::requestLifecycleReevaluation();", MAIN)

    def test_no_gain_home_reclaim_is_attempted_once_per_home_session(self):
        lifecycle = function_body(MAIN, "void updateBluetoothLifecycle()")
        self.assertIn("static bool homeBleReclaimAttempted = false;", lifecycle)
        self.assertIn("if (!homeActivity) homeBleReclaimAttempted = false;", lifecycle)
        self.assertIn("if (!wanted) {\n    homeBleReclaimAttempted = false;", lifecycle)
        self.assertIn("homeBleReclaimAttempted = true;\n      const auto reclaim", lifecycle)
        self.assertIn("static_cast<size_t>(ESP.getMaxAllocHeap()), homeBleReclaimAttempted)", lifecycle)

    def test_render_notification_wakes_task_after_releasing_application_spinlock(self):
        body = function_body(ACTIVITY_MANAGER, "void ActivityManager::queueRenderNotification()")
        self.assertLess(body.index("taskEXIT_CRITICAL(&activityManagerSpinlock)"), body.index("xTaskNotify("))
        critical = body.split("taskENTER_CRITICAL(&activityManagerSpinlock)", 1)[1].split(
            "taskEXIT_CRITICAL(&activityManagerSpinlock)", 1
        )[0]
        self.assertNotIn("xTaskNotify(", critical)

    def test_render_mutex_owner_query_is_outside_application_spinlock(self):
        body = function_body(ACTIVITY_MANAGER, "void ActivityManager::requestUpdateAndWait()")
        self.assertLess(body.index("xSemaphoreGetMutexHolder("), body.index("taskENTER_CRITICAL(&activityManagerSpinlock)"))
        critical = body.split("taskENTER_CRITICAL(&activityManagerSpinlock)", 1)[1].split(
            "taskEXIT_CRITICAL(&activityManagerSpinlock)", 1
        )[0]
        self.assertNotIn("xSemaphoreGetMutexHolder(", critical)


if __name__ == "__main__":
    unittest.main()
