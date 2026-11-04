"""Source and packaging safety contracts for the Nooir browser emulator."""

import importlib.util
import json
from pathlib import Path
import tempfile
import unittest


ROOT = Path(__file__).resolve().parents[2]
WEB = ROOT / "web-emulator"
APP = (WEB / "app.js").read_text(encoding="utf-8")
HTML = (WEB / "index.html").read_text(encoding="utf-8")
WORKFLOW = (ROOT / ".github" / "workflows" / "nooir-web-emulator-pages.yml").read_text(encoding="utf-8")


def load_module(name: str, path: Path):
    spec = importlib.util.spec_from_file_location(name, path)
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


package_audit = load_module(
    "nooir_web_package_audit", ROOT / "scripts" / "check_nooir_web_package.py"
)
package_builder = load_module(
    "nooir_web_package_builder", ROOT / "scripts" / "package_nooir_web_emulator.py"
)
smoke_runner = load_module(
    "nooir_shared_firmware_smoke", ROOT / "scripts" / "smoke_nooir_shared_firmware.py"
)
rom_prep = (ROOT / "scripts" / "prepare_esp32c3_rom.py").read_text(encoding="utf-8")
packager = (ROOT / "scripts" / "package_nooir_web_emulator.py").read_text(encoding="utf-8")
web_workflow = (ROOT / ".github" / "workflows" / "nooir-web-emulator-pages.yml").read_text(encoding="utf-8")


class WebEmulatorContractTests(unittest.TestCase):
    def test_guest_runtime_failure_is_detected_and_drained(self):
        self.assertTrue(smoke_runner.is_guest_runtime_error("Guru Meditation Error: test"))
        self.assertTrue(smoke_runner.is_guest_runtime_error("RuntimeError: test"))
        self.assertTrue(smoke_runner.is_guest_runtime_error("Aborted()"))
        self.assertFalse(smoke_runner.is_guest_runtime_error("normal boot output"))
        self.assertEqual(smoke_runner.GUEST_FAILURE_DRAIN_SECONDS, 2.0)

    def test_failed_smoke_uploads_only_a_bounded_text_diagnostic(self):
        self.assertIn("tail -c 16384", web_workflow)
        self.assertIn("name: nooir-emulator-smoke-diagnostics", web_workflow)
        self.assertIn("smoke-failure-tail.txt", web_workflow)
        self.assertIn("if-no-files-found: ignore", web_workflow)

    def test_failed_firmware_smoke_includes_a_bounded_log_tail(self):
        with tempfile.TemporaryDirectory() as temp_name:
            log = Path(temp_name) / "x3.log"
            log.write_bytes(b"EARLY-LOG-MARKER\n" + b"x" * 20_000 + b"Guru Meditation Error: test\n")

            error = smoke_runner.smoke_failure("x3", "guest/runtime error", log)

        self.assertIn("guest/runtime error", str(error))
        self.assertIn("Guru Meditation Error: test", str(error))
        self.assertNotIn("EARLY-LOG-MARKER", str(error))
        self.assertLess(len(str(error)), 17_000)

    def test_release_lookup_is_official_read_only_metadata_and_asset_link(self):
        self.assertIn("https://api.github.com/repos/toshio2011/folio-nooir/releases/latest", APP)
        self.assertIn('credentials: "omit"', APP)
        self.assertIn("RELEASE_DOWNLOAD_PREFIX", APP)
        self.assertNotIn("DEV_FIRMWARE", APP)
        self.assertNotIn("enginePort", APP)
        self.assertNotIn("devEngine", APP)

    def test_only_verified_shared_v166_pair_is_automatically_recognized(self):
        self.assertIn('models = shared166 ? ["x3", "x4"] : [selectedDevice()]', APP)
        self.assertIn('"verifiedNooirDevices": ["x3", "x4"]', (WEB / "dependencies.json").read_text(encoding="utf-8"))
        self.assertIn('<option value="x4" selected>', HTML)
        self.assertIn('<option value="x3">', HTML)
        self.assertIn('value="x4pro" disabled', HTML)
        self.assertIn('value="x4classic" disabled', HTML)

    def test_custom_firmware_is_local_and_same_origin(self):
        self.assertIn("URL.createObjectURL(file)", APP)
        self.assertIn("target.origin !== location.origin", APP)
        self.assertIn("not uploaded", APP)
        self.assertIn('accept=".bin,application/octet-stream"', HTML)

    def test_ci_does_not_enable_pages_deployment_or_include_reader_firmware(self):
        self.assertIn("if: ${{ false }}", WORKFLOW)
        self.assertIn("No reader firmware binaries were copied.", (ROOT / "scripts" / "package_nooir_web_emulator.py").read_text(encoding="utf-8"))

    def test_public_support_images_are_hash_locked_and_unlicensed_roms_are_rejected(self):
        self.assertEqual(len(package_audit.ALLOWED_SUPPORT_BINS), 4)
        self.assertIn("3336ae473a063be8dae6790bd45fee4506f2cf273aa36f3dfd9e0495ecb665cc", rom_prep)
        self.assertIn("caa463d3cbef2430a5a35847c1d9f2f152403b17a802050927ff60c8da54fe46", rom_prep)
        self.assertIn("--esp32c3-rom", packager)
        self.assertIn("--emsdk-root", packager)
        self.assertIn("CORRESPONDING-SOURCE.md", packager)
        self.assertIn("copy_esp32c3_rom_notices", packager)
        self.assertIn("ESP32C3-ROM-NEWLIB-COPYING.NEWLIB", packager)
        self.assertIn("ESP32C3-ROM-ESP-IDF-COPYRIGHT.rst", packager)

    def test_visible_credits_link_corresponding_sources_and_rom_origins(self):
        self.assertIn("sources/eink-qemu-d1b24a5f-corresponding-source.tar.gz", HTML)
        self.assertIn("github.com/espressif/esp-rom-elfs", HTML)
        self.assertIn("github.com/emscripten-core/emscripten", HTML)
        self.assertIn("esp32c3-rom/esp-idf-COPYRIGHT.rst", HTML)

    def test_vendor_rom_notice_files_match_pinned_git_objects(self):
        expected = {
            "ESP32C3-ROM-ELF-README.md": "0df304ef2d7c2ba4009c3e9931d98a951321dec2",
            "ESP32C3-ROM-APACHE-2.0.txt": "261eeb9e9f8b2b4b0d119366dda99c6fd7d35c64",
            "ESP32C3-ROM-ESP-IDF-COPYRIGHT.rst": "d6f42432fec445e5f05971147a8a1de6d2cbf93e",
            "ESP32C3-ROM-NEWLIB-COPYING.NEWLIB": "d056d18f74c923772ee53fffc131cded0ef26c78",
            "ESP32C3-ROM-BSD-3-CLAUSE.txt": "086d3992cb3a8f7000127bda96d6cbe673c8e788",
            "SPDX-CC0-1.0.txt": "0e259d42c996742e9e3cba14c677129b2c1b6311",
        }
        for filename, blob_sha in expected.items():
            data = (ROOT / "LICENSES" / filename).read_bytes()
            actual = package_builder.git_blob_sha1(data)
            self.assertEqual(actual, blob_sha)
            self.assertEqual(package_audit.git_blob_sha1(data), blob_sha)
        self.assertIn("value=\"x4pro\" disabled", HTML)
        self.assertIn("value=\"x4classic\" disabled", HTML)

    def test_package_audit_rejects_unreviewed_binary_and_checks_support_hashes(self):
        temporary_root = ROOT / "build" / "web-emulator-test-tmp"
        temporary_root.mkdir(parents=True, exist_ok=True)
        with tempfile.TemporaryDirectory(dir=temporary_root) as temporary:
            package = Path(temporary)
            for relative in package_audit.REQUIRED:
                target = package / relative
                target.parent.mkdir(parents=True, exist_ok=True)
                target.write_text("", encoding="utf-8")
            (package / "dependencies.json").write_text(json.dumps({
                "engineRevision": package_audit.PINNED_ENGINE,
                "verifiedNooirDevices": ["x3", "x4"],
                "firmwareDistribution": "excluded; the user selects a local file",
            }), encoding="utf-8")
            (package / "engine/boot").mkdir(parents=True, exist_ok=True)
            (package / "engine/boot/nvs-crosspoint.bin").write_bytes(b"support")
            (package / "engine/boot/nooir-v1.6.6.bin").write_bytes(b"reader firmware")

            from unittest.mock import patch
            import io
            import contextlib

            captured = io.StringIO()
            with patch("sys.argv", ["check_nooir_web_package.py", str(package)]), \
                 contextlib.redirect_stdout(captured):
                result = package_audit.main()
            self.assertEqual(result, 1)
            self.assertIn("engine/boot/nooir-v1.6.6.bin", captured.getvalue())
            self.assertNotIn("binary is not on the reviewed emulator-support allowlist: engine/boot/nvs-crosspoint.bin", captured.getvalue())
            self.assertIn("support binary provenance hash mismatch: engine/boot/nvs-crosspoint.bin", captured.getvalue())


if __name__ == "__main__":
    unittest.main()
