# Folio Nooir browser emulator — isolated v1.6.6 workstream

This Nooir interface packages the browser/WebAssembly build of [eink-qemu](https://github.com/serialx/eink-qemu), pinned at `d1b24a5fbc1bbae7ea31075460f63cd4c989c1dc`. It runs the real Nooir firmware in a browser. The unchanged official Folio Nooir v1.6.6 image is byte-verified for X3 and original X4: 6,082,160 bytes, SHA-256 `badcd6fb6cb4b5323f92714e893f9c488e887aa8f7b5d8e28cd0e6639aeae108`. X4 Pro and X4 Classic remain disabled.

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

The package is intentionally firmware-free. Stable release metadata is looked up automatically, with original X4 selected by default. The release asset redirect prevents the browser from reading the firmware bytes directly, so the user must download from the official release link and select the local file. The page checks GitHub's published SHA-256 when available. No firmware proxy, third-party emulator upload, or firmware mirror is used. Custom `.bin` files are read in the local browser and handed to the same-origin engine; they are not uploaded to a server. Latest stable discovery is automatic; firmware download and launch are not.

## Verified local behavior

The generated review package was tested locally at `http://127.0.0.1:8181/` with the unchanged shared v1.6.6 image. The X3 profile rendered at 528×792 and reported X3 detection; X4 rendered at 480×800 and reported X4 detection. Both detected the virtual SD and reached Nooir Home. The wrapper's file picker accepted the local firmware and the virtual SD upload control added `test_text_decorations.epub` to the FAT32 card.

X3 and X4 virtual controls were verified in Nooir: Left enters Library, Confirm opens the selected EPUB, Right advances a page, and Back exits Reader. X4 rendered pages 1/3 and 2/3; X3 rendered pages 2/3 and 3/3. Reader exit logged saves of `progress.bin`, `recent.json`, and `book-states.json`. After a full reload of the Nooir wrapper origin, the EPUB remained on the virtual SD and the Reader resumed at page 2/3 (67%). The firmware file itself must be selected again after reload because its browser Blob is not persisted. Use one active emulator tab while editing the virtual SD; the upstream engine warns if another tab has a newer card state.

The serial pane shows device detection, SD detection, Nooir build identity, and `render_ready`. First boot may report missing NVS/settings and X4 controller-probe errors before successful detection. We did not deliberately trigger a crash, and persistent crash-dump behavior has not been demonstrated. Up/Down mapping is available, but its distinct behavior has not been checked with a multi-item shelf. Physical X3/X4 tests remain pending.

## Credits and license review

The visible **About & Credits** page and [`LICENSES/REFERENCE-PROJECTS.md`](../LICENSES/REFERENCE-PROJECTS.md) identify code, architecture, simulator, hardware and toolchain sources and link to CrossPoint Reader, CrossInk, CrossPoint Simulator, Reader Simulator, FreeInk SDK, eink-qemu, Espressif and Emscripten. The Nooir shell is identified as project-side work; reference-only projects are labeled. The package preserves the Nooir license, eink-qemu GPL/LGPL/license files, the Emscripten notice, upstream generated distribution notices, scanned third-party notices from the pinned build inputs, and the MIT isolation-worker notice. The C3 ROM's Espressif Apache license and offline ROM component notices are pinned and hash-checked. The exact component map and binary provenance are in [`LICENSES/EINK-QEMU-WEB-BUNDLE.md`](../LICENSES/EINK-QEMU-WEB-BUNDLE.md) and [`LICENSES/ESP32C3-ROM-NOTICES.md`](../LICENSES/ESP32C3-ROM-NOTICES.md).

The pinned engine includes GPL-2.0 code plus file-level LGPL-2.1 and MIT components. A generated web package includes a corresponding-source archive and the exact source/dependency pins. The unlicensed QEMU ROM blobs are excluded and the C3 ROM is sourced from Espressif's explicitly Apache-licensed ELF release. The complete license-source review and final package audit are recorded in [`docs/web-emulator-release-readiness-2026-11-04.md`](../docs/web-emulator-release-readiness-2026-11-04.md). The fresh Linux-built package and corresponding-source archive must pass the workflow before publication.

## Public-hosting readiness

`.github/workflows/nooir-web-emulator-pages.yml` builds an isolated package, runs the pinned engine's tests and ROM smoke check, audits the package and stores a short-lived review artifact. Pages deployment remains disabled until the package and source archive pass review.

The official release asset is not downloaded automatically because GitHub's release redirect blocks safe browser reads; users download the unchanged v1.6.6 firmware from Nooir's official release and select it locally. This is acceptable for the first beta. The package contains no Nooir firmware. No hardware has been flashed, and no physical device behavior is inferred from emulator success.
