"""Keep useful per-owner heap checkpoints around Settings exit."""

from pathlib import Path
import unittest


ROOT = Path(__file__).resolve().parents[2]
SOURCE = (ROOT / "src/activities/settings/SettingsActivity.cpp").read_text(encoding="utf-8")


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
    raise AssertionError(f"unclosed function body: {signature}")


class SettingsExitHeapCheckpointContract(unittest.TestCase):
    def test_checkpoints_bracket_activity_vector_and_theme_boundaries(self):
        exit_body = function_body(SOURCE, "void SettingsActivity::onExit()")
        checkpoints = (
            'logSettingsDiagnostic("onExit_begin")',
            'logSettingsExitHeapCheckpoint("onExit_after_activity"',
            'currentGroupSettings.clear()',
            'logSettingsExitHeapCheckpoint("onExit_after_group_clear"',
            'currentGroupSettings.shrink_to_fit()',
            'logSettingsExitHeapCheckpoint("onExit_after_group_shrink"',
            'logSettingsExitHeapCheckpoint("onExit_before_theme_reload"',
            'UITheme::getInstance().reload()',
            'logSettingsExitHeapCheckpoint("onExit_after_theme_reload"',
            'logSettingsDiagnostic("onExit_end")',
        )
        positions = [exit_body.index(item) for item in checkpoints]
        self.assertEqual(positions, sorted(positions))

    def test_checkpoint_reports_allocation_shape_without_touching_runtime_state(self):
        checkpoint = function_body(SOURCE, "void logSettingsExitHeapCheckpoint(")
        for field in ("entries=%u", "capacity=%u", "capacity_bytes=%u", "free=%u", "largest=%u"):
            self.assertIn(field, checkpoint)
        self.assertIn("capacity * sizeof(SettingInfo)", checkpoint)
        self.assertNotIn("SETTINGS.", checkpoint)
        self.assertNotIn("shrink_to_fit", checkpoint)


if __name__ == "__main__":
    unittest.main()
