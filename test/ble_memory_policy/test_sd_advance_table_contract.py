import unittest
from pathlib import Path


ROOT = Path(__file__).resolve().parents[2]
SD_FONT_CPP = (ROOT / "lib/EpdFont/SdCardFont.cpp").read_text(encoding="utf-8")
POLICY_H = (ROOT / "lib/EpdFont/LowestCodepoints.h").read_text(encoding="utf-8")
POLICY_TEST = (ROOT / "test/sd_advance_table/SdAdvanceTablePolicyTest.cpp").read_text(encoding="utf-8")


class SdAdvanceTableContract(unittest.TestCase):
    def test_workspace_is_bounded_by_cache_limit_plus_layout_glyphs(self):
        self.assertIn("SCRATCH_CAPACITY = ADVANCE_CACHE_LIMIT + 2", SD_FONT_CPP)
        self.assertIn("std::min<uint32_t>(inputByteUpperBound + 2, SCRATCH_CAPACITY)", SD_FONT_CPP)
        self.assertIn("if (!hasUnfilledStyle) return 0", SD_FONT_CPP)
        self.assertNotIn("MAX_UNIQUE_CODEPOINTS = 4096", SD_FONT_CPP)

    def test_candidates_are_filtered_and_bounded_per_style(self):
        self.assertIn("for (uint8_t si = 0; si < MAX_STYLES; ++si)", SD_FONT_CPP)
        self.assertIn("advanceTableLookup(si, cp, nullptr)", SD_FONT_CPP)
        self.assertIn("replacementIdx < 0 && findGlobalGlyphIndex(style, cp) < 0", SD_FONT_CPP)
        self.assertIn("SdFontAdvancePolicy::retainLowestUnique", SD_FONT_CPP)
        self.assertIn("kScratchCapacity = kAdvanceLimit + 2", POLICY_TEST)

    def test_missing_glyph_diagnostic_uses_same_bounded_workspace(self):
        self.assertIn("collectMissing", SD_FONT_CPP)
        self.assertIn("totalMissed += static_cast<int>(cpCount)", SD_FONT_CPP)
        self.assertIn("same bounded scratch", SD_FONT_CPP)

    def test_workspace_failure_retry_memoization_remains_intact(self):
        self.assertIn("advanceWorkspaceFailedBytes_", SD_FONT_CPP)
        self.assertIn("advanceWorkspaceFailureLargest_", SD_FONT_CPP)
        self.assertIn("advanceWorkspaceFailureGeneration_", SD_FONT_CPP)
        self.assertIn("advanceWorkspaceSuppressionLogged_", SD_FONT_CPP)


if __name__ == "__main__":
    unittest.main()
