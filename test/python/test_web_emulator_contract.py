"""Source and packaging safety contracts for the Nooir browser emulator."""

import importlib.util
import hashlib
import json
from pathlib import Path
import struct
import tarfile
import tempfile
import unittest
import zlib


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
browser_diagnostics = load_module(
    "nooir_browser_smoke_diagnostics", ROOT / "scripts" / "prepare_nooir_browser_smoke_diagnostics.py"
)
rom_prep = (ROOT / "scripts" / "prepare_esp32c3_rom.py").read_text(encoding="utf-8")
packager = (ROOT / "scripts" / "package_nooir_web_emulator.py").read_text(encoding="utf-8")
web_workflow = (ROOT / ".github" / "workflows" / "nooir-web-emulator-pages.yml").read_text(encoding="utf-8")
softfloat_prep = load_module(
    "nooir_softfloat_source_prep", ROOT / "scripts" / "prepare_nooir_softfloat_source.py"
)
acceptance_evidence = load_module(
    "nooir_browser_acceptance_evidence", ROOT / "scripts" / "prepare_nooir_browser_acceptance_evidence.py"
)


class WebEmulatorContractTests(unittest.TestCase):
    def test_packaged_attribution_link_resolves_to_copied_crosspoint_license(self):
        with tempfile.TemporaryDirectory(prefix="nooir-attribution-link-", dir=ROOT) as temporary:
            root = Path(temporary)
            packaged = root / "package"
            license_doc = packaged / "licenses" / "NOOIR-REFERENCE-PROJECTS.md"
            package_builder.write_packaged_reference_inventory(
                ROOT / "LICENSES" / "REFERENCE-PROJECTS.md", license_doc
            )
            (packaged / "licenses" / "folio-nooir").mkdir(parents=True)
            (packaged / "licenses" / "folio-nooir" / "LICENSE").write_text("MIT", encoding="utf-8")
            (packaged / "licenses" / "CORRESPONDING-SOURCE.md").write_text("source", encoding="utf-8")
            (packaged / "licenses" / "ESP32C3-ROM-NOTICES.md").write_text("rom", encoding="utf-8")
            errors = package_audit.audit_local_attribution_links(packaged)
            self.assertEqual(errors, [])
            self.assertIn("CrossPoint MIT License", license_doc.read_text(encoding="utf-8"))
            self.assertNotIn("`../LICENSE`", license_doc.read_text(encoding="utf-8"))

    def test_attribution_link_audit_rejects_missing_or_escaping_files(self):
        with tempfile.TemporaryDirectory(prefix="nooir-attribution-negative-", dir=ROOT) as temporary:
            package = Path(temporary) / "package"
            licenses = package / "licenses"
            licenses.mkdir(parents=True)
            (licenses / "NOOIR-REFERENCE-PROJECTS.md").write_text(
                "[missing](missing-LICENSE) [escape](../../outside.txt)\n", encoding="utf-8"
            )
            errors = package_audit.audit_local_attribution_links(package)
            self.assertTrue(any("does not resolve" in error for error in errors))
            self.assertTrue(any("escapes package" in error for error in errors))

    def test_packaged_readme_and_rom_notice_links_resolve_to_included_sources(self):
        with tempfile.TemporaryDirectory(prefix="nooir-package-doc-links-", dir=ROOT) as temporary:
            package = Path(temporary) / "package"
            licenses = package / "licenses"
            rom_assets = licenses / "esp32c3-rom"
            rom_assets.mkdir(parents=True)
            package_builder.write_packaged_markdown(
                WEB / "README.md",
                package / "README.md",
                {
                    "](../LICENSES/REFERENCE-PROJECTS.md)": "](licenses/NOOIR-REFERENCE-PROJECTS.md)",
                    "](../LICENSES/EINK-QEMU-WEB-BUNDLE.md)": "](licenses/EINK-QEMU-WEB-BUNDLE.md)",
                    "](../LICENSES/ESP32C3-ROM-NOTICES.md)": "](licenses/ESP32C3-ROM-NOTICES.md)",
                    "](../docs/web-emulator-release-readiness-2026-11-04.md)": "](licenses/EINK-QEMU-WEB-BUNDLE.md)",
                },
            )
            package_builder.write_packaged_markdown(
                ROOT / "LICENSES" / "ESP32C3-ROM-NOTICES.md",
                licenses / "ESP32C3-ROM-NOTICES.md",
                {
                    "](../scripts/prepare_esp32c3_rom.py)": "](esp32c3-rom/prepare_esp32c3_rom.py)",
                },
            )
            for relative in (
                "licenses/NOOIR-REFERENCE-PROJECTS.md",
                "licenses/EINK-QEMU-WEB-BUNDLE.md",
                "licenses/CORRESPONDING-SOURCE.md",
                "licenses/esp32c3-rom/esp-rom-elfs-README.md",
                "licenses/esp32c3-rom/LICENSE",
                "licenses/esp32c3-rom/prepare_esp32c3_rom.py",
            ):
                target = package / relative
                target.parent.mkdir(parents=True, exist_ok=True)
                target.write_text("local target", encoding="utf-8")
            (package / "licenses/esp32c3-rom/esp-rom-elfs-README.md").write_text(
                "See [the license](LICENSE).\n", encoding="utf-8"
            )
            self.assertEqual(package_audit.audit_local_attribution_links(package), [])
            bad_source = Path(temporary) / "bad-upstream-README.md"
            bad_source.write_text("[bad](../../../outside.md)\n", encoding="utf-8")
            with self.assertRaises(SystemExit):
                package_builder.write_packaged_upstream_readme(
                    bad_source, package / "licenses" / "eink-qemu" / "bad-README.md"
                )

    def test_packaged_engine_readme_links_resolve_or_point_to_the_pinned_source(self):
        with tempfile.TemporaryDirectory(prefix="nooir-upstream-readme-links-", dir=ROOT) as temporary:
            root = Path(temporary)
            source = root / "upstream-README.md"
            package = root / "package"
            destination = package / "licenses" / "eink-qemu" / "README.md"
            source.write_text(
                "[profiles](eink-qemu/profiles/README.md) "
                "[guide](EINK-QEMU.md#webassembly-build) "
                "[releases](../../releases) [actions](../../actions) "
                "[COPYING](COPYING) [LICENSE](LICENSE) "
                "![desktop](eink-qemu/screenshots/desktop-light.png)\n",
                encoding="utf-8",
            )
            package_builder.write_packaged_upstream_readme(source, destination)
            for name in ("COPYING", "LICENSE"):
                (destination.parent / name).write_text("license text", encoding="utf-8")
            packaged = destination.read_text(encoding="utf-8")
            self.assertIn(
                "https://github.com/serialx/eink-qemu/blob/d1b24a5fbc1bbae7ea31075460f63cd4c989c1dc/eink-qemu/profiles/README.md",
                packaged,
            )
            self.assertIn(
                "https://raw.githubusercontent.com/serialx/eink-qemu/d1b24a5fbc1bbae7ea31075460f63cd4c989c1dc/eink-qemu/screenshots/desktop-light.png",
                packaged,
            )
            self.assertIn("Nooir package note", packaged)
            self.assertEqual(package_audit.audit_local_attribution_links(package), [])
            bad_source = Path(temporary) / "bad-upstream-README.md"
            bad_source.write_text("[bad](../../../outside.md)\n", encoding="utf-8")
            with self.assertRaises(SystemExit):
                package_builder.write_packaged_upstream_readme(
                    bad_source, package / "licenses" / "eink-qemu" / "bad-README.md"
                )
    def test_success_evidence_is_curated_and_excludes_firmware_and_sd_inputs(self):
        def png_chunk(name, data):
            kind = name.encode("ascii")
            return struct.pack(">I", len(data)) + kind + data + struct.pack(">I", zlib.crc32(kind + data))

        ihdr = struct.pack(">IIBBBBB", 1, 1, 8, 6, 0, 0, 0)
        png = b"\x89PNG\r\n\x1a\n" + png_chunk("IHDR", ihdr)
        png += png_chunk("IDAT", zlib.compress(bytes((0, 20, 40, 60, 255))))
        png += png_chunk("IEND", b"")
        digest = hashlib.sha256(png).hexdigest()
        with tempfile.TemporaryDirectory(prefix="nooir-acceptance-evidence-", dir=ROOT) as temporary:
            root = Path(temporary)
            input_root = root / "input"
            output = root / "public-evidence"
            fields = acceptance_evidence.REQUIRED_SCREEN_FIELDS
            for device in ("x3", "x4"):
                source = input_root / device
                source.mkdir(parents=True)
                screenshots = {}
                for field in fields:
                    stem = acceptance_evidence.SCREEN_FIELDS[field]
                    page_name = f"{device}-{stem}.png"
                    panel_name = f"{device}-{stem}-panel.png"
                    (source / page_name).write_bytes(png)
                    (source / panel_name).write_bytes(png)
                    screenshots[field] = {
                        "pageSha256": digest, "pageWidth": 1, "pageHeight": 1,
                        "panelFile": panel_name, "panelSha256": digest,
                        "panelWidth": 1, "panelHeight": 1,
                    }
                download_name = f"folio-nooir-{device}-screen.png"
                (source / download_name).write_bytes(png)
                (source / "firmware.bin").write_bytes(b"must not copy")
                (source / f"nooir-test-{device}.img").write_bytes(b"must not copy")
                (source / "serial.log").write_text(
                    "app=1.6.6+b961c1f7\nrender_ready\n[private C:\\Users\\reader\\profile] ghp_secret-token\n",
                    encoding="utf-8",
                )
                result = {
                    "device": device, "result": "PASS", "firmwareSha256": "secret-firmware-id",
                    "firmware": "C:\\private\\firmware.bin", "sd": "nooir-test.img",
                    "acceptance": {key: True for key in acceptance_evidence.REQUIRED_ACCEPTANCE},
                    "screenshots": screenshots,
                    "screenshotExport": {"downloadedFile": {
                        "name": download_name, "bytes": len(png), "width": 1, "height": 1, "sha256": digest,
                    }},
                }
                (source / "result.json").write_text(json.dumps(result), encoding="utf-8")

            acceptance_evidence.build_acceptance_evidence(input_root, output)
            files = [path.relative_to(output).as_posix() for path in output.rglob("*") if path.is_file()]
            self.assertFalse(any(name.endswith((".bin", ".img")) for name in files))
            for device in ("x3", "x4"):
                summary = (output / device / "result.json").read_text(encoding="utf-8")
                serial = (output / device / "serial-summary.txt").read_text(encoding="utf-8")
                self.assertNotIn("firmwareSha256", summary)
                self.assertNotIn("C:\\\\private", summary)
                self.assertNotIn("ghp_secret-token", serial)
                self.assertNotIn("Users", serial)
                self.assertIn("render_ready", serial)

    def test_success_evidence_refuses_incomplete_acceptance_results(self):
        with tempfile.TemporaryDirectory(prefix="nooir-acceptance-negative-", dir=ROOT) as temporary:
            root = Path(temporary)
            input_root = root / "input"
            input_root.mkdir()
            for device in ("x3", "x4"):
                source = input_root / device
                source.mkdir()
                result = {
                    "device": device, "result": "PASS",
                    "acceptance": {key: True for key in acceptance_evidence.REQUIRED_ACCEPTANCE},
                }
                if device == "x4":
                    result["acceptance"]["epubOpen"] = False
                (source / "result.json").write_text(json.dumps(result), encoding="utf-8")
            with self.assertRaises(SystemExit):
                acceptance_evidence.build_acceptance_evidence(input_root, root / "output")

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

    def test_browser_smoke_profiles_run_independently_and_upload_only_failure_diagnostics(self):
        self.assertIn("id: browser_smoke", web_workflow)
        self.assertIn("for device in x3 x4; do", web_workflow)
        self.assertIn("|| smoke_status=$?", web_workflow)
        self.assertIn("scripts/prepare_nooir_browser_smoke_diagnostics.py", web_workflow)
        self.assertIn("browser_smoke_failed=1", web_workflow)
        self.assertIn("Upload X3/X4 browser smoke diagnostics (failure only)", web_workflow)
        self.assertIn("steps.browser_smoke.outcome == 'failure'", web_workflow)
        startup_diagnostics = web_workflow.split('if [ "$cdp_ready" -ne 1 ]; then', 1)[1].split(
            'echo "Chrome CDP /json/list became ready."', 1
        )[0]
        self.assertIn("--user-data-dir=[redacted browser profile]", startup_diagnostics)
        self.assertIn("[redacted local path]", startup_diagnostics)
        self.assertIn(r"(?:bin|rom|elf)", startup_diagnostics)
        self.assertIn("path: build/nooir-browser-smoke-diagnostics/", web_workflow)
        upload = web_workflow.split("name: nooir-browser-smoke-diagnostics", 1)[1].split(
            "      - name: Upload Chrome startup diagnostics", 1
        )[0]
        self.assertNotIn("firmware", upload.lower())
        self.assertNotIn("rom", upload.lower())
        self.assertNotIn("profile", upload.lower())
        self.assertLess(
            web_workflow.index("Package Nooir shell and audit licenses"),
            web_workflow.index("Smoke the audited package in Chrome on X3 and X4"),
        )
        self.assertNotIn("NOOIR_INPUT_PROBE_KEY=Left", web_workflow)
        self.assertIn("scripts/prepare_nooir_browser_acceptance_evidence.py", web_workflow)
        self.assertIn("Upload successful X3/X4 browser acceptance evidence (separate artifact)", web_workflow)
        self.assertLess(
            web_workflow.index("Prepare successful X3/X4 acceptance evidence outside the package"),
            web_workflow.index("Upload successful X3/X4 browser acceptance evidence (separate artifact)"),
        )
        acceptance_upload = web_workflow.split("name: nooir-browser-acceptance-evidence", 1)[1].split(
            "      - name: Upload X3/X4 browser smoke diagnostics", 1
        )[0]
        self.assertNotIn("firmware", acceptance_upload.lower())
        self.assertNotIn("nooir-test-", acceptance_upload.lower())
        self.assertNotIn("rom", acceptance_upload.lower())

    def test_screenshot_export_is_exact_file_not_only_preview_or_click(self):
        self.assertIn("waitForScreenshotDownload", (ROOT / "scripts" / "nooir_browser_smoke.mjs").read_text(encoding="utf-8"))
        self.assertIn("createImageBitmap(blob)", (ROOT / "scripts" / "nooir_browser_smoke.mjs").read_text(encoding="utf-8"))
        self.assertIn("Downloaded PNG differs from the screenshot preview bytes", (ROOT / "scripts" / "verify_nooir_screenshot_download.mjs").read_text(encoding="utf-8"))

    def test_firmware_ci_is_skipped_only_for_known_web_only_changes(self):
        ci = (ROOT / ".github" / "workflows" / "ci.yml").read_text(encoding="utf-8")
        ignored = ci.split("    paths-ignore:\n", 1)[1].split("\njobs:", 1)[0]
        patterns = [line.strip()[3:-1] for line in ignored.splitlines() if line.strip().startswith("- '")]
        current_pr3_paths = (
            ".github/workflows/ci.yml",
            ".github/workflows/nooir-web-emulator-pages.yml",
            "web-emulator/README.md", "web-emulator/app.js", "web-emulator/dependencies.json",
            "web-emulator/index.html", "web-emulator/screenshot.css", "web-emulator/style.css",
            "scripts/check_nooir_web_package.py", "scripts/nooir_browser_smoke.mjs",
            "scripts/package_nooir_web_emulator.py", "scripts/prepare_esp32c3_rom.py",
            "scripts/prepare_nooir_softfloat_source.py", "scripts/prepare_nooir_browser_smoke_diagnostics.py",
            "scripts/prepare_nooir_browser_acceptance_evidence.py", "scripts/verify_nooir_screenshot_download.mjs",
            "scripts/select_cdp_page_target.mjs", "scripts/serve_nooir_web_emulator.py",
            "scripts/smoke_nooir_shared_firmware.py", "scripts/requirements.txt",
            "test/node/cdp-target-selection.test.mjs", "test/node/sd-asset-location.test.mjs",
            "test/node/screenshot-download.test.mjs",
            "test/python/test_web_emulator_contract.py",
            "LICENSES/REFERENCE-PROJECTS.md",
            "LICENSES/CORRESPONDING-SOURCE.md", "LICENSES/EINK-QEMU-WEB-BUNDLE.md",
            "LICENSES/ESP32C3-ROM-APACHE-2.0.txt", "LICENSES/ESP32C3-ROM-BSD-3-CLAUSE.txt",
            "LICENSES/ESP32C3-ROM-ELF-README.md", "LICENSES/ESP32C3-ROM-ESP-IDF-COPYRIGHT.rst",
            "LICENSES/ESP32C3-ROM-NEWLIB-COPYING.NEWLIB", "LICENSES/ESP32C3-ROM-NOTICES.md",
            "LICENSES/SPDX-CC0-1.0.txt", "LICENSES/eink-qemu-components/dtc/GPL",
            "LICENSES/eink-qemu-components/dtc/BSD-2-Clause", "LICENSES/eink-qemu-components/dtc/README.license",
            "LICENSES/eink-qemu-components/keycodemapdb/LICENSE.BSD",
            "LICENSES/eink-qemu-components/keycodemapdb/LICENSE.GPL2",
            "ROADMAP.md", "README.md", "HANDOFF.md", "docs/web-emulator-pr-draft.md",
            "docs/web-emulator-readiness.md", "docs/web-emulator-release-readiness-2026-11-04.md",
        )
        def excluded(source_path):
            return any(source_path == pattern or (pattern.endswith("/**") and source_path.startswith(pattern[:-2])) for pattern in patterns)

        for source_path in current_pr3_paths:
            self.assertTrue(excluded(source_path), f"PR #3 web-only path would still trigger firmware CI: {source_path}")
        firmware_paths = (".gitmodules", "freeink-sdk", "platformio.ini", "src/Battery.cpp", "test/CMakeLists.txt")
        for source_path in firmware_paths:
            self.assertFalse(excluded(source_path), f"firmware-impacting path must run firmware CI: {source_path}")
        self.assertIn("Any firmware source", ci)

    def test_browser_smoke_failure_diagnostics_are_redacted_and_bounded(self):
        import tempfile

        with tempfile.TemporaryDirectory(dir=ROOT) as tmp:
            tmp_path = Path(tmp)
            source = tmp_path / "smoke.log"
            output = tmp_path / "diagnostics"
            lines = [f"ordinary log line {index} " + ("x" * 1500) for index in range(120)]
            lines.extend([
                "AssertionError: first Nooir Home assertion failed at C:\\Users\\reader\\AppData\\Local\\Chrome\\User Data\\Profile 1",
                "launch --user-data-dir=/tmp/nooir-private-profile",
                "firmware C:\\runner\\folio-nooir-v1.6.6-firmware.bin",
                "ROM asset /tmp/esp32s3_rev0_rom.elf",
                "Bearer abcdefghijklmnopqrstuvwxyz0123456789",
                "ghp_abcdefghijklmnopqrstuvwxyz0123456789",
                "webSocketDebuggerUrl=ws://127.0.0.1:9224/devtools/browser/private-id",
                "-----BEGIN PRIVATE KEY-----",
                "PRIVATE_KEY_MATERIAL_MUST_NOT_ESCAPE",
                "-----END PRIVATE KEY-----",
            ])
            source.write_text("\n".join(lines) + "\n", encoding="utf-8")
            browser_diagnostics.record("x3", 1, source, output)

            summary = (output / "summary.txt").read_text(encoding="utf-8")
            diagnostic_log = (output / "x3.log").read_text(encoding="utf-8")
            self.assertIn("x3: FAIL", summary)
            self.assertIn("AssertionError: first Nooir Home assertion failed", summary)
            self.assertIn("first Nooir Home assertion failed", diagnostic_log)
            for secret in (
                "reader\\AppData",
                "Profile 1",
                "nooir-private-profile",
                "firmware.bin",
                "esp32s3_rev0_rom.elf",
                "Bearer abcdef",
                "ghp_abcdefghijklmnopqrstuvwxyz",
                "private-id",
                "PRIVATE_KEY_MATERIAL_MUST_NOT_ESCAPE",
            ):
                self.assertNotIn(secret, summary + diagnostic_log)
            self.assertLessEqual(len(diagnostic_log.splitlines()), browser_diagnostics.MAX_LOG_LINES)
            self.assertLessEqual(
                len(diagnostic_log.encode("utf-8")),
                browser_diagnostics.MAX_LOG_LINES * browser_diagnostics.MAX_LINE_CHARS + 100,
            )
            self.assertEqual(
                {path.name for path in output.iterdir()},
                {"summary.txt", "x3.log"},
            )

            browser_diagnostics.record("x4", 0, source, output)
            self.assertIn("x4: PASS", (output / "summary.txt").read_text(encoding="utf-8"))
            self.assertFalse((output / "x4.log").exists())

    def test_first_error_summary_redacts_inline_private_key_block(self):
        import tempfile

        with tempfile.TemporaryDirectory(dir=ROOT) as tmp:
            tmp_path = Path(tmp)
            source = tmp_path / "smoke.log"
            output = tmp_path / "diagnostics"
            source.write_text(
                "AssertionError: boot failed -----BEGIN PRIVATE KEY----- "
                "PRIVATE_KEY_MATERIAL_MUST_NOT_ESCAPE -----END PRIVATE KEY-----\n",
                encoding="utf-8",
            )
            browser_diagnostics.record("x4", 1, source, output)
            summary = (output / "summary.txt").read_text(encoding="utf-8")
            diagnostic_log = (output / "x4.log").read_text(encoding="utf-8")

            self.assertIn("x4: FAIL", summary)
            self.assertIn("[redacted private-key block]", summary)
            self.assertNotIn("PRIVATE_KEY_MATERIAL_MUST_NOT_ESCAPE", summary + diagnostic_log)

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
        self.assertIn("performance.timeOrigin !==", smoke)
        self.assertIn("sdRestoredAfterReload", smoke)
        self.assertIn("progressRestoredAfterReload", smoke)
        self.assertIn("uploaded", smoke.lower())

    def test_review_package_upload_survives_browser_failure_only_after_audit_passes(self):
        self.assertIn("id: package_audit", WORKFLOW)
        self.assertIn(
            "if: always() && steps.package_audit.outcome == 'success'",
            WORKFLOW,
        )
        self.assertLess(
            WORKFLOW.index("Package Nooir shell and audit licenses"),
            WORKFLOW.index("Smoke the audited package in Chrome on X3 and X4"),
        )
        review = WORKFLOW.split("name: nooir-web-emulator-review", 1)[1].split(
            "  # Deployment stays impossible", 1
        )[0]
        self.assertIn("path: build/nooir-web-emulator", review)
        self.assertIn("retention-days: 7", review)
        self.assertIn("shutil.copytree(engine_dist, engine_output, ignore=ignore_firmware", packager)
        self.assertIn('lower.endswith((".xota", ".bin", ".elf", ".pem"))', packager)

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
        self.assertNotIn("NOOIR_INPUT_PROBE_KEY=Left", WORKFLOW)
        self.assertIn("Upload X3/X4 browser smoke diagnostics (failure only)", WORKFLOW)
        self.assertIn("nooir-browser-smoke-diagnostics", WORKFLOW)
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
            # Match Git's text-file clean conversion on Windows checkouts.
            data = (ROOT / "LICENSES" / filename).read_bytes().replace(b"\r\n", b"\n")
            actual = package_builder.git_blob_sha1(data)
            self.assertEqual(actual, blob_sha)
            self.assertEqual(package_audit.git_blob_sha1(data), blob_sha)
        self.assertIn("value=\"x4pro\" disabled", HTML)
        self.assertIn("value=\"x4classic\" disabled", HTML)

    def test_pinned_qemu_dependency_notices_match_and_are_copied_exactly(self):
        expected = {
            "keycodemapdb/LICENSE.BSD": "ec1a29d34d6e419411c75523408aca72f705345c",
            "keycodemapdb/LICENSE.GPL2": "d511905c1647a1e311e8b20d5930a37a9c2531cd",
            "dtc/BSD-2-Clause": "da366e2ce50be9b4ec250cad773677bee7fc1d9d",
            "dtc/GPL": "d159169d1050894d3ea3b98e1c965c4058208fe1",
            "dtc/README.license": "102b004d02290bddd7e537d5d45a0ba8f724d85b",
        }
        for relative, blob_sha in expected.items():
            data = (ROOT / "LICENSES" / "eink-qemu-components" / relative).read_bytes()
            self.assertEqual(package_builder.git_blob_sha1(data), blob_sha)
            self.assertEqual(package_audit.git_blob_sha1(data), blob_sha)
        with tempfile.TemporaryDirectory(prefix="nooir-pinned-license-") as temporary:
            output = Path(temporary)
            copied = package_builder.copy_pinned_component_license_texts(ROOT, output)
            self.assertEqual(copied, len(expected))
            for relative in expected:
                target = output / "licenses/eink-qemu-components/build-subprojects" / relative
                self.assertEqual(target.read_bytes(), (ROOT / "LICENSES/eink-qemu-components" / relative).read_bytes())

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
            fixture_paths = (
                "tests/keys/id_rsa",
                "tests/keys/id_ed25519",
                "tests/keys/vagrant",
                "tests/unit/crypto-tls-x509-helpers.c",
                "tests/unit/test-crypto-der.c",
                "tests/unit/test-crypto-akcipher.c",
                "tests/bench/test_akcipher_keys.c.inc",
                "tests/bench/benchmark-crypto-akcipher.c",
            )
            retained_paths = (
                "src/main.c",
                "docs/devel/build-system.rst",
                "eink-qemu/wasm/build.sh",
                "eink-qemu/wasm/build-deps.sh",
                "eink-qemu/wasm/package-web.sh",
                "subprojects/dtc.wrap",
                "subprojects/keycodemapdb.wrap",
                "subprojects/berkeley-softfloat-3.wrap",
                "tests/keys/README",
                "tests/keys/id_rsa.pub",
                "tests/unit/test-crypto-cipher.c",
                "tests/unit/vendor/tests/keys/id_rsa",
            )
            for relative in fixture_paths + retained_paths:
                fixture = source / relative
                fixture.parent.mkdir(parents=True, exist_ok=True)
                fixture.write_text("test source fixture", encoding="utf-8")
            archive_path = Path(temporary) / "source.tar.gz"
            with tarfile.open(archive_path, "w:gz") as archive:
                package_builder.add_source_tree(
                    archive, source, "nooir-corresponding-source/eink-qemu"
                )
            with tarfile.open(archive_path, "r:gz") as archive:
                names = set(archive.getnames())
            for relative in fixture_paths:
                self.assertNotIn(
                    f"nooir-corresponding-source/eink-qemu/{relative}", names
                )
            for relative in retained_paths:
                self.assertIn(
                    f"nooir-corresponding-source/eink-qemu/{relative}", names
                )

    def test_strict_audit_rejects_all_eight_private_key_archive_paths(self):
        prohibited = {
            "nooir-corresponding-source/eink-qemu/tests/keys/id_rsa",
            "nooir-corresponding-source/eink-qemu/tests/keys/id_ed25519",
            "nooir-corresponding-source/eink-qemu/tests/keys/vagrant",
            "nooir-corresponding-source/eink-qemu/tests/unit/crypto-tls-x509-helpers.c",
            "nooir-corresponding-source/eink-qemu/tests/unit/test-crypto-der.c",
            "nooir-corresponding-source/eink-qemu/tests/unit/test-crypto-akcipher.c",
            "nooir-corresponding-source/eink-qemu/tests/bench/test_akcipher_keys.c.inc",
            "nooir-corresponding-source/eink-qemu/tests/bench/benchmark-crypto-akcipher.c",
        }
        with tempfile.TemporaryDirectory(prefix="nooir-audit-prohibited-archive-") as temporary:
            archive_path = Path(temporary) / "source.tar.gz"
            with tarfile.open(archive_path, "w:gz") as archive:
                for name in sorted(prohibited):
                    archive.addfile(tarfile.TarInfo(name))
            with tarfile.open(archive_path, "r:gz") as archive:
                names = {member.name for member in archive.getmembers()}
            self.assertEqual(
                set(package_audit.find_prohibited_source_archive_paths(names)), prohibited
            )

    def test_strict_audit_allows_runtime_and_build_sources(self):
        retained = {
            "nooir-corresponding-source/eink-qemu/configure",
            "nooir-corresponding-source/eink-qemu/docs/devel/build-system.rst",
            "nooir-corresponding-source/eink-qemu/eink-qemu/wasm/build.sh",
            "nooir-corresponding-source/eink-qemu/eink-qemu/wasm/build-deps.sh",
            "nooir-corresponding-source/eink-qemu/hw/display/epaper.c",
            "nooir-corresponding-source/eink-qemu/tests/keys/id_rsa.pub",
            "nooir-corresponding-source/eink-qemu/tests/unit/test-crypto-cipher.c",
        }
        with tempfile.TemporaryDirectory(prefix="nooir-audit-allowed-archive-") as temporary:
            archive_path = Path(temporary) / "source.tar.gz"
            with tarfile.open(archive_path, "w:gz") as archive:
                for name in sorted(retained):
                    archive.addfile(tarfile.TarInfo(name))
            with tarfile.open(archive_path, "r:gz") as archive:
                names = {member.name for member in archive.getmembers()}
            self.assertEqual(package_audit.find_prohibited_source_archive_paths(names), [])


    def test_corresponding_source_excludes_generated_node_binary_shims(self):
        with tempfile.TemporaryDirectory(prefix="nooir-node-shims-") as temporary:
            source = Path(temporary) / "frontend"
            generated = source / "package" / "node_modules" / ".bin"
            generated.mkdir(parents=True)
            (generated / "vite").write_text("generated shim", encoding="utf-8")
            package_source = source / "package" / "node_modules" / "vite" / "index.js"
            package_source.parent.mkdir(parents=True)
            package_source.write_text("source", encoding="utf-8")
            archive_path = Path(temporary) / "source.tar.gz"
            with tarfile.open(archive_path, "w:gz") as archive:
                package_builder.add_source_tree(
                    archive, source, "frontend-package-sources", include_node_modules=True
                )
            with tarfile.open(archive_path, "r:gz") as archive:
                names = set(archive.getnames())
            self.assertNotIn("frontend-package-sources/package/node_modules/.bin/vite", names)
            self.assertIn("frontend-package-sources/package/node_modules/vite/index.js", names)

    def test_softfloat_preparation_requires_the_exact_qemu_wrap_pin(self):
        with tempfile.TemporaryDirectory(prefix="nooir-softfloat-wrap-") as temporary:
            engine = Path(temporary) / "engine"
            wrap = engine / "subprojects" / "berkeley-softfloat-3.wrap"
            wrap.parent.mkdir(parents=True)
            wrap.write_text(
                "[wrap-git]\n"
                "url = https://gitlab.com/qemu-project/berkeley-softfloat-3.git\n"
                "revision = b64af41c3276f97f0e181920400ee056b9c88037\n",
                encoding="utf-8",
            )
            self.assertEqual(
                softfloat_prep.read_pinned_wrap(engine),
                (softfloat_prep.EXPECTED_URL, softfloat_prep.EXPECTED_REVISION),
            )
            wrap.write_text(
                "[wrap-git]\nurl = https://gitlab.com/qemu-project/berkeley-softfloat-3.git\n"
                "revision = guessed-revision\n",
                encoding="utf-8",
            )
            with self.assertRaises(SystemExit):
                softfloat_prep.read_pinned_wrap(engine)

    def test_softfloat_preparation_is_in_the_release_workflow_and_source_map(self):
        self.assertIn("scripts/prepare_nooir_softfloat_source.py", web_workflow)
        self.assertEqual(
            softfloat_prep.EXPECTED_URL,
            "https://gitlab.com/qemu-project/berkeley-softfloat-3.git",
        )
        self.assertIn("--depth=1", (ROOT / "scripts" / "prepare_nooir_softfloat_source.py").read_text(encoding="utf-8"))
        self.assertIn(softfloat_prep.EXPECTED_REVISION, packager)
        self.assertIn(softfloat_prep.EXPECTED_REVISION, (ROOT / "LICENSES" / "CORRESPONDING-SOURCE.md").read_text(encoding="utf-8"))
        audit_source = (ROOT / "scripts" / "check_nooir_web_package.py").read_text(encoding="utf-8")
        self.assertIn("PINNED_SOFTFLOAT", audit_source)
        self.assertIn("NOOIR-PROVENANCE.txt", audit_source)

    def test_component_license_scan_follows_source_links_without_leaving_checkout(self):
        from unittest.mock import patch

        with tempfile.TemporaryDirectory(prefix="nooir-license-symlink-") as temporary:
            source = Path(temporary) / "checkout"
            target = source / "actual-dtc-source"
            target.mkdir(parents=True)
            for index in range(15):
                (target / f"LICENSE.{index:02d}").write_text(
                    "license notice", encoding="utf-8"
                )
            outside = Path(temporary) / "outside-checkout"
            outside.mkdir()
            (outside / "LICENSE.private").write_text("must not copy", encoding="utf-8")
            linked_root = source / "build-subprojects"
            try:
                linked_root.symlink_to(target, target_is_directory=True)
                (target / "escape").symlink_to(outside, target_is_directory=True)
            except OSError as exc:
                self.skipTest(f"directory symlinks unavailable in this environment: {exc}")
            output = Path(temporary) / "package"
            with patch.object(
                package_builder,
                "LICENSE_AUDIT_ROOTS",
                (("symlinked-dtc", "build-subprojects"),),
            ):
                copied = package_builder.copy_component_license_texts(source, output)
            self.assertEqual(copied, 15)
            self.assertTrue(
                (output / "licenses/eink-qemu-components/symlinked-dtc/LICENSE.00").is_file()
            )
            self.assertFalse(
                (output / "licenses/eink-qemu-components/symlinked-dtc/escape/LICENSE.private").exists()
            )


if __name__ == "__main__":
    unittest.main()
