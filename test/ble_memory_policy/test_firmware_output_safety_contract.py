"""Ensure PlatformIO artifacts are copied only to new, unique paths."""

from pathlib import Path
import unittest


ROOT = Path(__file__).resolve().parents[2]


class FirmwareOutputSafetyContract(unittest.TestCase):
    def test_copy_hook_refuses_existing_configured_or_default_artifacts(self):
        source = (ROOT / "scripts/copy_firmware.py").read_text(encoding="utf-8")
        self.assertIn("configured_output = os.environ.get(\"NOOIR_FIRMWARE_OUTPUT\")", source)
        self.assertIn("if destination.exists():", source)
        self.assertIn("Refusing to overwrite existing firmware artifact", source)
        self.assertLess(source.index("if destination.exists():"), source.index("shutil.copy2("))


if __name__ == "__main__":
    unittest.main()
