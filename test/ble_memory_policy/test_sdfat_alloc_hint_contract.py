"""Regression checks for the SdFat recent-free allocation hint patch."""

import importlib.util
from pathlib import Path
import unittest


ROOT = Path(__file__).resolve().parents[2]
PATCH_PATH = ROOT / "scripts/patch_sdfat_alloc_hint.py"
SPEC = importlib.util.spec_from_file_location("patch_sdfat_alloc_hint", PATCH_PATH)
PATCH_MODULE = importlib.util.module_from_spec(SPEC)
assert SPEC and SPEC.loader
SPEC.loader.exec_module(PATCH_MODULE)


class SdFatAllocationHintContract(unittest.TestCase):
    def test_known_freed_cluster_becomes_the_next_search_hint_idempotently(self):
        source = """bool FatPartition::freeChain(Cluster_t cluster) {
    if (cluster < m_allocSearchStart) {
      m_allocSearchStart = cluster - 1;
    }
    cluster = next;
}
"""
        patched = PATCH_MODULE.patch_fat_partition(source)
        self.assertIn("m_allocSearchStart = cluster - 1;", patched)
        self.assertNotIn("if (cluster < m_allocSearchStart)", patched)
        self.assertEqual(patched.count(PATCH_MODULE.PATCH_MARKER), 1)
        self.assertEqual(PATCH_MODULE.patch_fat_partition(patched), patched)

    def test_unknown_sdfat_source_fails_closed(self):
        with self.assertRaisesRegex(RuntimeError, "Expected one SdFat freeChain"):
            PATCH_MODULE.patch_fat_partition("bool FatPartition::freeChain(Cluster_t cluster) {}")

    def test_every_base_firmware_profile_applies_the_patch(self):
        platformio = (ROOT / "platformio.ini").read_text(encoding="utf-8")
        base = platformio.split("[base]", 1)[1].split("\n[", 1)[0]
        self.assertIn("pre:scripts/patch_sdfat_alloc_hint.py", base)

    def test_successful_patch_exports_runtime_build_diagnostic(self):
        script = PATCH_PATH.read_text(encoding="utf-8")
        self.assertIn('env.Append(CPPDEFINES=[("NOOIR_SDFAT_RECENT_FREE_HINT", 1)])', script)
        self.assertLess(script.index("env.Append(CPPDEFINES"), script.index('print("Applied Nooir'))


if __name__ == "__main__":
    unittest.main()
