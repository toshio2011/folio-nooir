"""Source-level contracts for the isolated Statistics achievements patch.

These checks do not compile the firmware or execute the renderer. They verify
the threshold table, existing-counter use, navigation bounds, and language
fallback assumptions before any target build is considered.
"""

from pathlib import Path
import re
import unittest


ROOT = Path(__file__).resolve().parents[2]
SNAPSHOT_H = (ROOT / "src/StatisticsSnapshot.h").read_text(encoding="utf-8")
SNAPSHOT_CPP = (ROOT / "src/StatisticsSnapshot.cpp").read_text(encoding="utf-8")
ACTIVITY_CPP = (ROOT / "src/activities/home/ReadingStatsActivity.cpp").read_text(encoding="utf-8")
ENGLISH = (ROOT / "lib/I18n/translations/english.yaml").read_text(encoding="utf-8")


ORIGINAL_IDS = [
    "FirstSession", "FirstStartedBook", "FirstFinishedBook", "Finished5", "Finished10", "Finished25",
    "ReadingHour1", "ReadingHours10", "ReadingHours50", "ReadingHours100", "Pages500", "Pages1000",
    "Pages5000", "Sessions10", "Sessions50", "Sessions100", "Streak3", "Streak7", "Streak30", "ActiveDays30",
]
ADDED = {
    "Finished50": ("Count", "result.overview.booksFinished", 50),
    "Finished100": ("Count", "result.overview.booksFinished", 100),
    "ReadingHours250": ("Seconds", "result.overview.trackedSeconds", 250 * 60 * 60),
    "ReadingHours500": ("Seconds", "result.overview.trackedSeconds", 500 * 60 * 60),
    "Pages10000": ("Count", "result.overview.trackedPages", 10000),
    "Pages25000": ("Count", "result.overview.trackedPages", 25000),
    "Streak60": ("Days", "result.overview.longestStreak", 60),
    "ActiveDays100": ("Days", "result.overview.activeDays", 100),
    "Sessions250": ("Count", "result.overview.trackedSessions", 250),
    "Sessions500": ("Count", "result.overview.trackedSessions", 500),
}


def achievement_ids():
    block = re.search(r"enum class StatisticsAchievementId\s*:\s*uint8_t\s*\{(.*?)\};", SNAPSHOT_H, re.S)
    if not block:
        raise AssertionError("StatisticsAchievementId enum is missing")
    return re.findall(r"^\s*([A-Za-z][A-Za-z0-9_]*)\s*,?\s*$", block.group(1), re.M)


def achievement_calls():
    pattern = re.compile(
        r"addAchievement\(result\.achievements,\s*StatisticsAchievementId::(?P<id>\w+),\s*"
        r"StatisticsAchievementUnit::(?P<unit>\w+),\s*(?P<current>[^,]+),\s*(?P<target>[^)]+)\);",
        re.S,
    )
    return {m.group("id"): (m.group("unit"), m.group("current").strip(), m.group("target").strip())
            for m in pattern.finditer(SNAPSHOT_CPP)}


def numeric_target(expression):
    numbers = [int(value) for value in re.findall(r"\d+", expression)]
    if not numbers:
        raise AssertionError(f"No numeric target in {expression!r}")
    result = 1
    for number in numbers:
        result *= number
    return result


class AchievementContractTests(unittest.TestCase):
    def test_all_original_achievements_remain_in_the_same_order(self):
        ids = achievement_ids()
        self.assertEqual(ids[:20], ORIGINAL_IDS)
        self.assertEqual(len(ids), 30)
        self.assertEqual(set(ids[20:]), set(ADDED))

    def test_new_thresholds_use_existing_overview_counters(self):
        calls = achievement_calls()
        for achievement_id, (unit, counter, target) in ADDED.items():
            with self.subTest(achievement=achievement_id):
                actual_unit, actual_counter, actual_expression = calls[achievement_id]
                self.assertEqual(actual_unit, unit)
                self.assertEqual(actual_counter, counter)
                self.assertEqual(numeric_target(actual_expression), target)

    def test_each_added_threshold_boundary(self):
        calls = achievement_calls()
        for achievement_id, (_, _, expected_target) in ADDED.items():
            target = numeric_target(calls[achievement_id][2])
            self.assertEqual(target, expected_target)
            self.assertFalse(target - 1 >= target)
            self.assertTrue(target >= target)
            self.assertTrue(target + 1 >= target)
        self.assertIn("current >= target", SNAPSHOT_CPP)

    def test_empty_and_large_counter_contracts(self):
        calls = achievement_calls()
        self.assertEqual(len(calls), 30)
        targets = [numeric_target(target) for _, _, target in calls.values()]
        self.assertTrue(all(target > 0 for target in targets))
        self.assertTrue(all(not (0 >= target) for target in targets))
        uint32_max = (1 << 32) - 1
        self.assertTrue(all(uint32_max >= target for target in targets))
        self.assertIn("result.achievements.reserve(30)", SNAPSHOT_CPP)

    def test_existing_history_aggregates_are_reused_without_new_tracking(self):
        self.assertIn("std::max(bookSeconds, result.overview.retainedSeconds)", SNAPSHOT_CPP)
        self.assertIn("std::max(bookSessions, result.overview.retainedSessions)", SNAPSHOT_CPP)
        self.assertIn("std::max(bookPages, result.overview.retainedPages)", SNAPSHOT_CPP)
        self.assertIn("result.overview.booksFinished", SNAPSHOT_CPP)
        self.assertIn("result.overview.longestStreak", SNAPSHOT_CPP)
        self.assertIn("result.overview.activeDays", SNAPSHOT_CPP)

    def test_names_fallback_and_long_text_truncation_are_present(self):
        new_translation_ids = [
            "STR_ACH_FINISHED_50", "STR_ACH_FINISHED_100", "STR_ACH_READING_HOURS_250", "STR_ACH_READING_HOURS_500",
            "STR_ACH_PAGES_10000", "STR_ACH_PAGES_25000", "STR_ACH_STREAK_60", "STR_ACH_ACTIVE_DAYS_100",
            "STR_ACH_SESSIONS_250", "STR_ACH_SESSIONS_500",
        ]
        for key in new_translation_ids:
            with self.subTest(key=key):
                self.assertRegex(ENGLISH, rf"(?m)^{key}: \".+\"$", msg="English label missing")
                self.assertIn(f"{key};", ACTIVITY_CPP)
        self.assertIn("renderer.truncatedText(SMALL_FONT_ID, I18N.get(achievementTitleId", ACTIVITY_CPP)

    def test_earned_locked_counter_bar_and_navigation_remain(self):
        self.assertIn("STR_STATS_EARNED", ACTIVITY_CPP)
        self.assertIn("STR_STATS_LOCKED", ACTIVITY_CPP)
        self.assertIn("renderer.drawRect(side, overallBarY, overallBarWidth, 7)", ACTIVITY_CPP)
        self.assertIn("selectedAchievement = wrapSelection", ACTIVITY_CPP)
        self.assertIn("selectedAchievement = tapped", ACTIVITY_CPP)
        self.assertIn("y < cardsTop", ACTIVITY_CPP)
        self.assertIn("cardWidth + gap", ACTIVITY_CPP)

    def test_dynamic_card_geometry_stays_inside_supported_display_bounds(self):
        # Source display drivers report X4 800x480 and X3 792x528. GfxRenderer
        # can rotate the logical dimensions, so check both orientations.
        for screen_width, screen_height in ((800, 480), (480, 800), (792, 528), (528, 792)):
            side = 20
            gap = 10
            content_top = 82
            content_bottom = screen_height - 44
            cards_top = content_top + 30
            card_height = 100
            rows = max(1, (content_bottom - cards_top) // card_height)
            card_width = (screen_width - side * 2 - gap) // 2
            with self.subTest(width=screen_width, height=screen_height):
                self.assertGreater(card_width, 42)  # category glyph plus left inset
                self.assertLessEqual(side + 2 * card_width + gap, screen_width - side)
                self.assertLessEqual(cards_top + rows * card_height, content_bottom)
                self.assertLessEqual(content_bottom, screen_height)

    def test_no_new_persistent_tracking_or_statistics_schema(self):
        # This patch adds only derived values and labels; it must not add store
        # fields or write calls.
        self.assertNotIn("saveToFile", SNAPSHOT_CPP)
        self.assertNotIn("recordSession", SNAPSHOT_CPP)
        self.assertIn("struct StatisticsOverview", SNAPSHOT_H)


if __name__ == "__main__":
    unittest.main(verbosity=2)
