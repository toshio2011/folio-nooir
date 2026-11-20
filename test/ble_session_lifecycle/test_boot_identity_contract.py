"""Keep diagnostic serial captures attributable to their exact firmware source."""

from pathlib import Path
import unittest


ROOT = Path(__file__).resolve().parents[2]
MAIN = (ROOT / "src/main.cpp").read_text(encoding="utf-8")
VERSION_SCRIPT = (ROOT / "scripts/git_branch.py").read_text(encoding="utf-8")


class BootIdentityContract(unittest.TestCase):
    def test_ble_diagnostic_build_logs_full_source_and_descriptor_at_boot(self):
        self.assertIn("defined(FREEINK_BLE_HID_LIFECYCLE_DIAGNOSTICS)", MAIN)
        self.assertIn('LOG_INF("BUILD", "source_sha=%s app=%s", NOOIR_BUILD_SHA, NOOIR_APP_VERSION);', MAIN)

    def test_startup_record_identifies_runtime_storage_and_ble_policy(self):
        self.assertIn("#include <SdFat.h>", MAIN)
        self.assertIn('const char* sdTransferMode = "array";', MAIN)
        self.assertIn('const char* sdTransferMode = "byte";', MAIN)
        self.assertIn('LOG_INF("BUILD", "device=%s sd_spi=%s %luHz pins=%d/%d/%d/%d fat_hint=%u ble_policy=%u"', MAIN)
        self.assertIn("NOOIR_SDFAT_RECENT_FREE_HINT", MAIN)
        self.assertIn("kReaderBleReadmissionPolicyVersion", MAIN)

    def test_build_script_embeds_exact_commit_and_short_descriptor(self):
        self.assertIn("['rev-parse', 'HEAD']", VERSION_SCRIPT)
        self.assertIn("('NOOIR_BUILD_SHA'", VERSION_SCRIPT)
        self.assertIn("('NOOIR_APP_VERSION'", VERSION_SCRIPT)


if __name__ == "__main__":
    unittest.main()
