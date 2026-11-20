"""Keep SD-font interval sharing bounded, validated, and ownership-safe."""

from pathlib import Path
import unittest


ROOT = Path(__file__).resolve().parents[2]
SOURCE = (ROOT / "lib/EpdFont/SdCardFont.cpp").read_text(encoding="utf-8")
REGISTRY = (ROOT / "lib/EpdFont/SharedIntervalTableRegistry.h").read_text(encoding="utf-8")


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


class SdFontIntervalSharingContract(unittest.TestCase):
    def test_compare_occurs_during_validating_read_before_duplicate_allocation(self):
        load = function_body(SOURCE, "bool SdCardFont::load(")
        self.assertIn("sharedIntervals.candidates(s.header.intervalCount)", load)
        self.assertIn("for (uint32_t j = 0; j < s.header.intervalCount; ++j)", load)
        self.assertIn("sharedIntervals.filterCandidatesAt(shareCandidates, j", load)
        self.assertIn("const auto& prior = static_cast<const PerStyle::BmpInterval16*>", load)
        self.assertIn("const auto& prior = static_cast<const EpdUnicodeInterval*>", load)
        self.assertLess(load.index("sharedIntervals.filterCandidatesAt"), load.index("new (std::nothrow)"))

    def test_private_fallback_and_last_reference_release_are_preserved(self):
        release = function_body(SOURCE, "void SdCardFont::releaseIntervals(")
        self.assertIn("sharedIntervals.release(table)", release)
        self.assertIn("delete[] s.fullIntervals", release)
        self.assertIn("delete[] s.bmpIntervals", release)
        self.assertIn("sharedIntervals.add(table, s.header.intervalCount", SOURCE)
        self.assertIn("private in that case", REGISTRY)
        self.assertIn("if (entry.references > 1)", REGISTRY)

    def test_registry_capacity_is_fixed_and_metrics_deduplicate_local_aliases(self):
        self.assertIn("constexpr size_t MAX_SHARED_INTERVALS = 16", SOURCE)
        self.assertIn("alreadyCounted", function_body(SOURCE, "uint32_t SdCardFont::retainedCoverageBytes() const"))
        self.assertIn("shared_refs=%u", function_body(SOURCE, "void SdCardFont::logCoverageOwners() const"))


if __name__ == "__main__":
    unittest.main()
