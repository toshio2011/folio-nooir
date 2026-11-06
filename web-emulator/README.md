# Folio Nooir browser emulator — isolated v1.6.6 workstream

This Nooir interface packages the browser/WebAssembly build of [eink-qemu](https://github.com/serialx/eink-qemu), pinned at `d1b24a5fbc1bbae7ea31075460f63cd4c989c1dc`. It runs the real Nooir firmware in a browser. The unchanged official Folio Nooir v1.6.6 image is byte-verified for X3 and original X4: 6,082,160 bytes, SHA-256 `badcd6fb6cb4b5323f92714e893f9c488e887aa8f7b5d8e28cd0e6639aeae108`. X4 Pro and X4 Classic remain disabled.

## Experimental beta status and limitations

The preliminary manual workflow is available for X3 and original X4: select a local firmware `.bin`, use the virtual SD card and buttons, open an EPUB, and save a screen capture. The firmware file is read in the browser and is not uploaded. Startup can be slow, and some virtual button presses may be missed; the wrapper holds taps for at least 150 ms, but a press may still need to be repeated. Manual reading checks were done in Chrome only. Firefox, Safari, and in-app browsers are unverified. X4 Pro and X4 Classic are unsupported.

This is not a release-ready public package. Automated browser runs are intermittent, the current local experiment uses an ELF-derived ROM image that the package audit does not yet allow, and a complete corresponding-source archive has not passed the package audit. Do not deploy the site or redistribute a test package until those gates pass.

## Build and preview

Build the pinned eink-qemu revision using its [WebAssembly build guide](https://github.com/serialx/eink-qemu/blob/d1b24a5fbc1bbae7ea31075460f63cd4c989c1dc/EINK-QEMU.md#webassembly-build), then create a clean package:

```sh
python scripts/package_nooir_web_emulator.py \
  --engine-source /path/to/eink-qemu \
  --engine-dist /path/to/eink-qemu/build-wasm-32limit/web-dist \
  --esp32c3-rom build/esp32c3-rom.bin \
  --emsdk-root /path/to/emsdk \
  --output build/nooir-web-emulator
python scripts/check_nooir_web_package.py build/nooir-web-emulator
python scripts/serve_nooir_web_emulator.py --directory build/nooir-web-emulator
```

The source checkout must be at the exact pin. Before packaging, run `scripts/prepare_esp32c3_rom.py` to obtain and verify the Espressif Apache-2.0 ROM ELF, then convert it to the engine's flat C3 image. The packager refuses to replace a populated output directory. It copies only the hash-allowlisted C3 ROM, C3 bootloader, CrossPoint NVS seed and partition table; it excludes the unlicensed upstream ROM blobs, S3/Picco support images, reader `.bin` and `.xota` firmware. The audit checks exact hashes, source and license files, and that no reader firmware or key files are present. It also builds a co-hosted corresponding-source archive from the pinned engine, resolved build sources, and locked frontend dependency sources. The local server sets the COOP/COEP headers needed for WebAssembly threads and SharedArrayBuffer, and serves `.mjs` files as JavaScript. Preview the packaged site on the one origin it serves; a split-port iframe is blocked by the engine's cross-origin isolation policy.

**Current gate (2026-11-06):** historical local Chrome manual checks passed for original X3 and X4 with the unchanged, checksum-verified v1.6.6 firmware: Home/Library, FAT32 SD, EPUB open, page turns and Reader exit; X4 progress survived wrapper reload, and X3 read saved progress from the intentionally shared SD. Current automation is intermittent and is not a reproducible full pass. GitHub Actions built the pinned engine and prepared the licensed ROM, but the firmware smoke failed before package creation; the corresponding-source archive and package audit were skipped. The local debug package fails its audit and is not for redistribution. The official firmware must match SHA-256 `badcd6fb6cb4b5323f92714e893f9c488e887aa8f7b5d8e28cd0e6639aeae108`. The diagnostic external link opens upstream eink-qemu with only the selected model; it never passes a firmware file or URL.

The package is intentionally firmware-free. Stable release metadata is looked up automatically, with original X4 selected by default. The release asset redirect prevents the browser from reading the firmware bytes directly, so the user must download from the official release link and select the local file. The page checks GitHub's published SHA-256 when available. No firmware proxy, third-party emulator upload, or firmware mirror is used. Custom `.bin` files are read in the local browser and handed to the same-origin engine; they are not uploaded to a server. Latest stable discovery is automatic; firmware download and launch are not.

## Local browser regression test

`scripts/nooir_browser_smoke.mjs` drives Chrome through its built-in DevTools protocol and exercises one device per isolated browser profile. It loads a local FAT32 test image before selecting the user's local firmware file, then checks Nooir readiness, the screen capture/download flow, Library and Reader navigation, page forward/back, Reader exit, and SD restoration after a full wrapper reload. The test deliberately waits for each display update and only retries after confirming that no delayed action occurred; it does not queue presses while the panel is busy. Use a separate copy of the test SD image and Chrome profile for X3 and X4.

The script has no npm dependencies. Start `scripts/serve_nooir_web_emulator.py` on loopback. For a browser test, copy the chosen SD image into the local review package's `engine/` directory (never the source package), then open the preview in a dedicated Chrome process with `--remote-debugging-port=9224 --user-data-dir=<new-profile>` and run:

```sh
node scripts/nooir_browser_smoke.mjs \
  http://127.0.0.1:8177/ x4 /path/to/official-v1.6.6/firmware.bin \
  /path/to/a-copy-of/nooir-test-sd.img /path/to/new-evidence-x4
```

Set `NOOIR_SD_ASSET` to the copied image's filename under `engine/` if it differs from `nooir-test-x4.img`; the default is `nooir-test-x3.img` for X3. The script verifies that this served asset has the same SHA-256 as the supplied SD image. Run again with `x3`, a separate SD image copy, a fresh Chrome profile and a new evidence directory. `result.json`, `serial.log`, screenshots and any failure detail remain in the evidence directory. The test will not overwrite an existing evidence directory. It reports exact restored-page image equality as the progress check; a visually correct but non-identical page is reported as a failure for manual review.

The Nooir wrapper's **Save screenshot** control captures the actual nested e-paper canvas in this same-origin browser, previews the PNG, and offers both a normal download link and an open-in-new-tab fallback. If the browser refuses canvas capture, the dialog reports the failure and leaves the browser's built-in capture tool available.

## Earlier local validation (before licensed-ROM replacement)

The generated review package was tested locally at `http://127.0.0.1:8181/` with the unchanged shared v1.6.6 image and eink-qemu's original C3 ROM. The X3 profile rendered at 528×792 and reported X3 detection; X4 rendered at 480×800 and reported X4 detection. Both detected the virtual SD and reached Nooir Home. The wrapper's file picker accepted the local firmware and the virtual SD upload control added `test_text_decorations.epub` to the FAT32 card. This historical result does not validate the Apache-licensed ROM candidate now used by the build-only smoke.

X3 and X4 virtual controls were verified in Nooir: Left enters Library, Confirm opens the selected EPUB, Right advances a page, and Back exits Reader. X4 rendered pages 1/3 and 2/3; X3 rendered pages 2/3 and 3/3. Reader exit logged saves of `progress.bin`, `recent.json`, and `book-states.json`. After a full reload of the Nooir wrapper origin, the EPUB remained on the virtual SD and the Reader resumed at page 2/3 (67%). The firmware file itself must be selected again after reload because its browser Blob is not persisted. Use one active emulator tab while editing the virtual SD; the upstream engine warns if another tab has a newer card state.

The serial pane shows device detection, SD detection, Nooir build identity, and `render_ready`. First boot may report missing NVS/settings and X4 controller-probe errors before successful detection. We did not deliberately trigger a crash, and persistent crash-dump behavior has not been demonstrated. Up/Down mapping is available, but its distinct behavior has not been checked with a multi-item shelf. Physical X3/X4 tests remain pending.

## Credits and license review

The visible **About & Credits** page and [`LICENSES/REFERENCE-PROJECTS.md`](../LICENSES/REFERENCE-PROJECTS.md) identify code, architecture, simulator, hardware and toolchain sources and link to CrossPoint Reader, CrossInk, CrossPoint Simulator, Reader Simulator, FreeInk SDK, eink-qemu, Espressif and Emscripten. The Nooir shell is identified as project-side work; reference-only projects are labeled. The package preserves the Nooir license, eink-qemu GPL/LGPL/license files, the Emscripten notice, upstream generated distribution notices, scanned third-party notices from the pinned build inputs, and the MIT isolation-worker notice. The C3 ROM's Espressif Apache license and offline ROM component notices are pinned and hash-checked. The exact component map and binary provenance are in [`LICENSES/EINK-QEMU-WEB-BUNDLE.md`](../LICENSES/EINK-QEMU-WEB-BUNDLE.md) and [`LICENSES/ESP32C3-ROM-NOTICES.md`](../LICENSES/ESP32C3-ROM-NOTICES.md).

The pinned engine includes GPL-2.0 code plus file-level LGPL-2.1 and MIT components. A generated web package includes a corresponding-source archive and the exact source/dependency pins. The unlicensed QEMU ROM blobs are excluded and the C3 ROM is sourced from Espressif's explicitly Apache-licensed ELF release. The complete license-source review and final package audit are recorded in [`docs/web-emulator-release-readiness-2026-11-04.md`](../docs/web-emulator-release-readiness-2026-11-04.md). The fresh Linux-built package and corresponding-source archive must pass the workflow before publication.

## Public-hosting readiness

`.github/workflows/nooir-web-emulator-pages.yml` builds an isolated package, runs the pinned engine's tests and ROM smoke check, audits the package and stores a short-lived review artifact. Pages deployment remains disabled until the package and source archive pass review.

The official release asset is not downloaded automatically because GitHub's release redirect blocks safe browser reads; users download the unchanged v1.6.6 firmware from Nooir's official release and select it locally. This is acceptable for the first beta. The package contains no Nooir firmware. No hardware has been flashed, and no physical device behavior is inferred from emulator success.
