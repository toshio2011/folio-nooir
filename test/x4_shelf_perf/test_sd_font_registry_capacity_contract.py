"""Keep SD-font registry capacity proportional to discovered families."""

from pathlib import Path
import unittest


ROOT = Path(__file__).resolve().parents[2]
SOURCE = (ROOT / "lib/EpdFont/SdCardFontRegistry.cpp").read_text(encoding="utf-8")


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


class SdFontRegistryCapacityContract(unittest.TestCase):
    def test_discovery_does_not_reserve_the_maximum_family_count(self):
        body = function_body(SOURCE, "bool SdCardFontRegistry::discover()")
        self.assertIn("families_.clear()", body)
        self.assertNotIn("families_.reserve(MAX_SD_FAMILIES)", body)
        self.assertIn("scanRoot(FONTS_DIR_HIDDEN, families_)", body)
        self.assertIn("scanRoot(FONTS_DIR_VISIBLE, families_)", body)
        self.assertIn("families_.resize(MAX_SD_FAMILIES)", body)


if __name__ == "__main__":
    unittest.main()
