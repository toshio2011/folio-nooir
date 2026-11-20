import unittest
from pathlib import Path


ROOT = Path(__file__).resolve().parents[2]
PATCH_SCRIPT = (ROOT / "scripts/patch_jpegdec.py").read_text(encoding="utf-8")


class JpegdecPatchEnvironmentContract(unittest.TestCase):
    def test_only_the_active_platformio_environment_cache_is_patched(self):
        self.assertIn('env_dir = env.get("PIOENV")', PATCH_SCRIPT)
        self.assertIn('jpeg_dir = os.path.join(libdeps_dir, env_dir, "JPEGDEC")', PATCH_SCRIPT)
        self.assertNotIn('for env_dir in os.listdir(libdeps_dir)', PATCH_SCRIPT)

    def test_patch_failures_identify_the_active_dependency_directory(self):
        self.assertIn("does not apply cleanly in %s", PATCH_SCRIPT)


if __name__ == "__main__":
    unittest.main()
