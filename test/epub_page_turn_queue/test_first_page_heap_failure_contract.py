import unittest
from pathlib import Path


ROOT = Path(__file__).resolve().parents[2]
READER_CPP = (ROOT / "src/activities/reader/EpubReaderActivity.cpp").read_text(encoding="utf-8")
READER_H = (ROOT / "src/activities/reader/EpubReaderActivity.h").read_text(encoding="utf-8")


class FirstPageHeapFailureContract(unittest.TestCase):
    def test_suspended_zero_page_build_is_terminal_and_recoverable(self):
        self.assertIn(
            "reader_build_policy::shouldShowFirstPageHeapError(section->pageCount, buildHeapPaused)",
            READER_CPP,
        )
        start = READER_CPP.index("if (buildSuspendedAwaitingDemand) {\n    if (reader_build_policy::shouldShowFirstPageHeapError")
        end = READER_CPP.index("if (section->isPartial()", start)
        terminal_path = READER_CPP[start:end]
        self.assertIn('"first_page_blocked_by_heap spine=', terminal_path)
        self.assertIn("restartHeapSuspendedEmptyBuild.store(false", terminal_path)
        self.assertIn("showBuildError();", terminal_path)
        self.assertIn("return;", terminal_path)

    def test_heap_recovery_suspends_parser_before_error_path(self):
        self.assertIn("section->suspendBuild();", READER_CPP)
        self.assertIn("buildSuspendedAwaitingDemand = true;", READER_CPP)
        self.assertIn("buildHeapPaused = !readerWindow;", READER_CPP)

    def test_existing_foreground_build_safety_floors_remain(self):
        self.assertIn("BACKGROUND_BUILD_MIN_FREE_HEAP = 32 * 1024", READER_H)
        self.assertIn("BACKGROUND_BUILD_MIN_MAX_ALLOC = 16 * 1024", READER_H)


if __name__ == "__main__":
    unittest.main()
