# Folio Nooir browser emulator — preliminary readiness snapshot

> Historical snapshot. Its original blocker list and package counts predate
> the licensed-ROM/source-package remediation. Use the current
> [2026-11-04 release-gate report](web-emulator-release-readiness-2026-11-04.md)
> as the source of truth before publication.

**Status: not ready for public release.** The packaged local site now boots the unchanged official v1.6.6 image as X3 and original X4, routes the virtual controls into Nooir, and preserves the virtual SD and reading position across a full page reload. A clean web-only local branch and build-only Pages workflow are prepared. Public release is blocked by the unfinished license/corresponding-source review, the static-host firmware download limitation, and the fact that the GitHub workflow has not yet been run and reviewed.

This work remains based on Folio Nooir v1.6.6 commit `b6d428a3187afc25d434fcb475e9ab436099c1ba` on the web-only local branch `codex/nooir-web-emulator-beta`. The separate Pro/Classic port remains in its own worktree. No firmware release asset has been modified. Nothing has been pushed, merged, deployed, or flashed. v1.6.7 was not touched.

## Device compatibility matrix

| Device | Firmware/build | Emulator screen and detection | Virtual controls | SD and reading | BLE / power / physical |
|---|---|---|---|---|---|
| XTEINK X3 | Official shared v1.6.6 image, loaded only for local testing; 6,082,160 bytes, SHA-256 `badcd6fb6cb4b5323f92714e893f9c488e887aa8f7b5d8e28cd0e6639aeae108` | Dedicated X3 profile; 528×792; serial reports `Hardware detect: X3`, SD detected, and `render_ready` | Left enters Library; Confirm opens the EPUB; Right advances to page 3/3; Back exits and saves reader state | FAT32 virtual SD retained the EPUB; X3 geometry-specific EPUB cache rebuilt; progress was restored at page 2/3 | BLE and sleep/power behavior not validated; physical X3 not tested |
| Original X4 | Same verified shared image and digest | Dedicated X4 profile; 480×800; serial reports `Hardware detect: X4`, SD detected, and `render_ready` | Left enters Library; Confirm opens; Right advances to page 2/3; Back exits and saves reader state | EPUB opened from virtual SD; after a full wrapper-page reload, the same card remained and Reader resumed at page 2/3 (67%) | BLE and sleep/power behavior not validated; physical X4 not tested |
| X4 Pro | No Nooir firmware paired for this emulator | Upstream machine exists, but Nooir pairing is not established | Disabled in Nooir UI | Not tested | Not tested |
| X4 Classic | No machine model in the pinned engine | Unsupported | Disabled in Nooir UI | Not tested | Not tested |

No new firmware binaries were built for this emulator milestone. The reader image was a temporary local test input and is excluded from the source and generated package. X3/X4 compatibility results describe the emulator only; no physical device was flashed or validated.

## Packaged browser validation

The generated review package was `build/nooir-web-emulator-review-20261103-r5` (131 files; 110,021,554 bytes) from eink-qemu commit `d1b24a5fbc1bbae7ea31075460f63cd4c989c1dc`. It contains no reader firmware. The local package audit allows only seven named upstream emulator-support `.bin` files and rejects Nooir `.bin`, `.xota`, and `.elf` firmware assets.

Using a loopback-only preview at `http://127.0.0.1:8181/`, the wrapper selected the official local v1.6.6 image by its published digest, booted X4, displayed the 480×800 framebuffer, and showed live serial output. The same browser profile then selected the X3 machine and reported the X3 display at 528×792. X4 and X3 both opened `test/epubs/test_text_decorations.epub` from the virtual FAT32 SD. The X4 reader displayed pages 1/3 and 2/3; X3 displayed pages 2/3 and 3/3. Back returned to the library and serial logs recorded `progress.bin`, `recent.json`, and `book-states.json` saves.

After a full reload of the packaged Nooir origin, the firmware had to be selected again because the browser keeps the chosen file as an in-memory Blob. The EPUB and saved progress remained in the browser's virtual SD. Reopening the book restored page 2/3 (67%). This verifies persistence through the packaged wrapper's origin, not just through eink-qemu's direct origin.

The virtual input trace confirmed that the on-screen controls send the expected ArrowLeft, Enter, ArrowRight, and Escape key events into the guest. Up/Down events are present in the pinned engine, but their distinct Nooir behavior has not been tested with a multi-item shelf.

## Stable firmware selection

At validation time, GitHub's `releases/latest` metadata endpoint returned stable v1.6.6 in the browser. The API metadata request succeeded; fetching the release asset bytes through the returned asset API URL failed in Chrome with `TypeError: Failed to fetch` at the cross-origin asset redirect. GitHub documents CORS support for REST API requests, but that did not make this redirected binary readable by the browser: [GitHub CORS documentation](https://docs.github.com/en/rest/using-the-rest-api/using-cors-and-jsonp-to-make-cross-origin-requests).

The current safe flow detects the latest stable release, defaults the hardware selector to original X4, links to the official GitHub asset, and asks the user to save and select the `.bin` locally. The page checks the published SHA-256 where available and never sends the file to a server. Stable-version discovery is automatic; firmware download and loading are not. A static GitHub Pages site cannot proxy the release asset. Automatic loading would require an approved same-origin firmware copy or a backend proxy; neither is part of this source package.

## Runtime and diagnostics

The serial pane shows boot output, Nooir build identity, device detection, SD detection, and render readiness. First boot can log missing NVS/settings; X4 also reports unsuccessful I2C probes and absent RTC/IMU. Those messages were followed by correct X3/X4 detection and a ready screen. We did not deliberately crash the firmware, and we did not verify a persistent crash-dump facility. The browser console can report engine/JavaScript failures, but that is separate from a firmware crash dump.

The local preview helper serves `.mjs` as `text/javascript` and adds the COOP/COEP/CORP headers needed by the isolated WebAssembly package. The GitHub Pages workflow builds and audits a review package; its `deploy-pages` job is hard-disabled. It has not run on GitHub.

## License and public release gate

The visible About & Credits page links CrossPoint Reader, CrossInk, CrossPoint Simulator, Reader Simulator, FreeInk SDK, and eink-qemu. The package includes the Nooir license, eink-qemu `COPYING`, `COPYING.LIB`, and `LICENSE`, upstream origins, generated engine notices, the isolation-worker MIT notice, and 70 scanned third-party notice/license texts. `LICENSES/EINK-QEMU-WEB-BUNDLE.md` now lists exact hashes for the seven emulator-support binaries.

The corresponding-source and license review remains incomplete. The generated WASM/runtime output has not been mapped to every linked source/license at file level, the complete dependency source set and modification set has not been verified as a distribution bundle, and the exact license status of the Espressif ROM binaries in this bundle still needs confirmation. Espressif states that ESP32 ROM code is proprietary and not open source, while separately licensing its published ROM ELF files under Apache-2.0; the package review must establish the provenance and terms for the exact QEMU ROM artifacts: [Espressif ROM ELF repository](https://github.com/espressif/esp-rom-elfs/). A reviewer/maintainer must sign off before publication.

Before a public beta, finish the license and source review, resolve the stable-binary hosting decision, run and inspect the build-only workflow on GitHub, and review the exact package contents. The original release firmware remains outside the source package. Do not enable deployment until those gates pass.
