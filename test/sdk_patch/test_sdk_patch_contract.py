import hashlib
import pathlib
import unittest

ROOT = pathlib.Path(__file__).resolve().parents[2]


class FreeInkPatchContract(unittest.TestCase):
    def test_pinned_patch_digest_and_hook(self):
        patch = ROOT / "patches/freeink-sdk/nooir-1.6.7_20261120_04.patch"
        hook = (ROOT / "scripts/apply_nooir_freeink_patch.py").read_text(encoding="utf-8")
        digest = hashlib.sha256(patch.read_bytes()).hexdigest()
        self.assertEqual(digest, "3d2e37ac22cf8727bd1e6aff6431617a3f60b071e972a2d01e7857ec048b191f")
        self.assertIn("958720659ea289ae325e83db20049d0ea844800d", hook)
        self.assertIn("git", hook)

    def test_hook_is_registered_before_compilation_scripts(self):
        config = (ROOT / "platformio.ini").read_text(encoding="utf-8")
        self.assertIn("pre:scripts/apply_nooir_freeink_patch.py", config)
        self.assertLess(config.index("pre:scripts/apply_nooir_freeink_patch.py"),
                        config.index("pre:scripts/patch_sdfat_alloc_hint.py"))