"""Source and packaging safety contracts for the Nooir browser emulator."""

import importlib.util
import json
from pathlib import Path
import tarfile
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

    def test_x4_readiness_accepts_its_observed_generic_refresh_label(self):
        self.assertEqual(
            len(smoke_runner.MILESTONES["x4"].findall("Wait complete: refresh")),
            1,
        )
        self.assertEqual(
            len(smoke_runner.MILESTONES["x3"].findall("Wait complete: refresh")),
            0,
        )

    def test_failed_smoke_diagnostics_remain_runner_local(self):
        self.assertNotIn("Upload bounded emulator smoke diagnostics", web_workflow)
        self.assertNotIn("nooir-emulator-smoke-diagnostics", web_workflow)
        self.assertIn("Detailed emulator output remains on the temporary runner.", web_workflow)
        self.assertIn("grep -E '^(x3|x4): FAIL|^FAIL:'", web_workflow)

    def test_failed_firmware_smoke_reports_safe_readiness_state_only(self):
        error = smoke_runner.smoke_failure(
            "x3",
            "guest/runtime error detected",
            milestones=1,
            memory_marker_seen=True,
            screenshot_requested=False,
            capture_exists=False,
        )

        self.assertIn("guest/runtime error detected", str(error))
        self.assertIn("startup milestones=1/3", str(error))
        self.assertIn("memory marker=yes", str(error))
        self.assertNotIn("Guru Meditation", str(error))
        self.assertLess(len(str(error)), 300)

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

    def test_upstream_fallback_passes_only_the_selected_device(self):
        self.assertIn('id="upstream-fallback"', HTML)
        self.assertIn('href="https://serialx.github.io/eink-qemu/?device=x4"', HTML)
        self.assertIn('rel="noopener noreferrer"', HTML)
        self.assertIn("does not pass a firmware file or firmware URL", HTML)
        self.assertIn('target.searchParams.set("device", selectedDevice())', APP)
        fallback = APP.split("function updateUpstreamFallback()", 1)[1].split("function engineBase()", 1)[0]
        self.assertNotIn("flash", fallback.lower())

    def test_smoke_runner_checks_x4_even_if_x3_fails(self):
        import contextlib
        import io
        from unittest.mock import patch

        engine = Path("mock-engine")
        firmware = Path("mock-firmware.bin")
        error_output = io.StringIO()
        with patch.object(Path, "resolve", autospec=True, side_effect=lambda path, *args, **kwargs: path), \
                patch.object(smoke_runner, "validate_firmware"), \
                patch.object(smoke_runner, "boot_one", side_effect=[RuntimeError("X3 ROM panic"), None]) as boot_one, \
                patch("sys.argv", ["smoke_nooir_shared_firmware.py", "--engine-source", str(engine), "--firmware", str(firmware)]), \
                contextlib.redirect_stderr(error_output):
            result = smoke_runner.main()

        self.assertEqual(result, 1)
        self.assertEqual([call.args[2] for call in boot_one.call_args_list], ["x3", "x4"])
        self.assertIn("x3: FAIL; X3 ROM panic", error_output.getvalue())

    def test_custom_firmware_is_local_and_same_origin(self):
        self.assertIn("URL.createObjectURL(file)", APP)
        self.assertIn("target.origin !== location.origin", APP)
        self.assertIn("not uploaded", APP)
        self.assertIn('accept=".bin,application/octet-stream"', HTML)

    def test_local_screenshot_fallback_captures_engine_canvas_and_offers_download_and_tab(self):
        self.assertIn('id="capture-screen"', HTML)
        self.assertIn('id="screen-capture-dialog"', HTML)
        self.assertIn('id="download-screen-image"', HTML)
        self.assertIn('id="open-screen-image"', HTML)
        self.assertIn('frame.contentDocument?.querySelector("#canvas")', APP)
        self.assertIn('canvas.toBlob(blob =>', APP)
        self.assertIn('URL.createObjectURL(blob)', APP)
        self.assertIn("screenCaptureDialog.close()", APP)
        self.assertIn('"screenshot.css"', packager)
        self.assertTrue((WEB / "screenshot.css").is_file())

    def test_browser_smoke_is_explicitly_local_and_checks_reload(self):
        smoke = (ROOT / "scripts" / "nooir_browser_smoke.mjs").read_text(encoding="utf-8")
        self.assertIn("DOM.setFileInputFiles", smoke)
        self.assertIn("Input.dispatchMouseEvent", smoke)
        self.assertIn("Page.reload", smoke)
        self.assertIn("sdRestoredAfterReload", smoke)
        self.assertIn("progressRestoredAfterReload", smoke)
        self.assertIn("uploaded", smoke.lower())

    def test_ci_does_not_enable_pages_deployment_or_include_reader_firmware(self):
        self.assertIn("if: ${{ false }}", WORKFLOW)
        self.assertIn("No reader firmware binaries were copied.", (ROOT / "scripts" / "package_nooir_web_emulator.py").read_text(encoding="utf-8"))

    def test_public_support_images_are_hash_locked_and_unlicensed_roms_are_rejected(self):
        self.assertEqual(len(package_audit.ALLOWED_SUPPORT_BINS), 4)
        self.assertIn("3336ae473a063be8dae6790bd45fee4506f2cf273aa36f3dfd9e0495ecb665cc", rom_prep)
        self.assertIn("--experimental-licensed-elf-startup", rom_prep)
        self.assertIn("3a69ac75662367dc99fbd5b8ddf265515b6288e79e51e4c4168c597aced7b79d", rom_prep)
        self.assertIn("61d28bfe60e74152e262f11758fe4c0fb5eb794e9ae3b80a62f0631cae7b221b", rom_prep)
        self.assertIn('"redistribution_review_complete": True', rom_prep)
        self.assertIn("pyelftools==0.33", (ROOT / "scripts" / "requirements.txt").read_text(encoding="utf-8"))
        self.assertIn("caa463d3cbef2430a5a35847c1d9f2f152403b17a802050927ff60c8da54fe46", rom_prep)
        self.assertIn("--esp32c3-rom", packager)
        self.assertIn("--emsdk-root", packager)
        self.assertIn("CORRESPONDING-SOURCE.md", packager)
        self.assertIn("copy_esp32c3_rom_notices", packager)
        self.assertIn("ESP32C3-ROM-NEWLIB-COPYING.NEWLIB", packager)
        self.assertIn("ESP32C3-ROM-ESP-IDF-COPYRIGHT.rst", packager)
        self.assertIn("--experimental-licensed-elf-startup", WORKFLOW)
        self.assertIn("pyelftools==0.33", WORKFLOW)
        self.assertIn("nooir-rom-venv/bin/python", WORKFLOW)
        self.assertIn("Smoke the audited package in Chrome on X3 and X4", WORKFLOW)
        self.assertIn("NOOIR_INPUT_PROBE_KEY=Left", WORKFLOW)
        self.assertNotIn("Upload bounded emulator smoke diagnostics", WORKFLOW)
        self.assertNotIn("nooir-emulator-smoke-diagnostics", WORKFLOW)
        rom_identity = (393216, "3a69ac75662367dc99fbd5b8ddf265515b6288e79e51e4c4168c597aced7b79d")
        self.assertEqual((package_builder.ESP32C3_ROM_BYTES, package_builder.ESP32C3_ROM_SHA256), rom_identity)
        self.assertEqual(package_audit.SUPPORT_BIN_HASHES["engine/esp32c3-rom.bin"], rom_identity)

    def test_reviewed_rom_manifest_is_required_and_matches_the_source_archive(self):
        manifest_path = "licenses/esp32c3-rom/esp32c3-rom.bin.manifest.json"
        self.assertIn(manifest_path, package_audit.REQUIRED)
        self.assertIn("def verify_rom_manifest", packager)
        self.assertIn("redistribution_review_complete", packager)
        self.assertIn("nooir-corresponding-source/emulator-support/esp32c3-rom.bin.manifest.json", packager)
        audit_source = (ROOT / "scripts" / "check_nooir_web_package.py").read_text(encoding="utf-8")
        self.assertIn("corresponding-source ROM manifest differs from the packaged manifest", audit_source)
        self.assertIn("nooir-corresponding-source/eink-qemu/docs/devel/build-system.rst", audit_source)
        self.assertIn("nooir-corresponding-source/eink-qemu/configure", audit_source)

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
        with tempfile.TemporaryDirectory(prefix="nooir-web-package-audit-") as temporary:
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

    def test_screenshot_stylesheet_is_a_required_package_file(self):
        self.assertIn("screenshot.css", package_audit.REQUIRED)
        self.assertIn("screenshot.css", package_audit.PACKAGE_ROOT_FILES)

    def test_corresponding_source_omits_private_key_test_fixtures(self):
        with tempfile.TemporaryDirectory(prefix="nooir-source-filter-") as temporary:
            source = Path(temporary) / "eink-qemu"
            (source / "tests" / "keys").mkdir(parents=True)
            (source / "tests" / "unit").mkdir(parents=True)
            (source / "src").mkdir()
            (source / "tests" / "keys" / "id_rsa").write_text("fixture", encoding="utf-8")
            (source / "tests" / "unit" / "crypto-tls-x509-helpers.c").write_text(
                "test key fixture", encoding="utf-8"
            )
            (source / "src" / "main.c").write_text("source", encoding="utf-8")
            archive_path = Path(temporary) / "source.tar.gz"
            with tarfile.open(archive_path, "w:gz") as archive:
                package_builder.add_source_tree(
                    archive, source, "nooir-corresponding-source/eink-qemu"
                )
            with tarfile.open(archive_path, "r:gz") as archive:
                names = set(archive.getnames())
            self.assertNotIn(
                "nooir-corresponding-source/eink-qemu/tests/keys/id_rsa", names
            )
            self.assertNotIn(
                "nooir-corresponding-source/eink-qemu/tests/unit/crypto-tls-x509-helpers.c",
                names,
            )
            self.assertIn("nooir-corresponding-source/eink-qemu/src/main.c", names)


if __name__ == "__main__":
    unittest.main()
