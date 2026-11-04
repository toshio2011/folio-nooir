# Draft pull request (local review copy)

**Current status:** no PR exists yet. This file is a review draft until the
isolated branch is pushed and the build workflow supplies a package artifact.
The latest release-gate report is
[`web-emulator-release-readiness-2026-11-04.md`](web-emulator-release-readiness-2026-11-04.md).

**Proposed title:** Add a firmware-free Folio Nooir browser emulator for X3/X4

**Target:** a web-emulator-only review branch based on official Folio Nooir v1.6.6 commit `b6d428a3187afc25d434fcb475e9ab436099c1ba`. The local branch is `codex/nooir-web-emulator-beta`. It will target a dedicated v1.6.6 review base so it cannot flow into ongoing v1.6.7 development.

## Description

This change prepares a Nooir-branded browser emulator around the pinned eink-qemu WebAssembly engine (`d1b24a5fbc1bbae7ea31075460f63cd4c989c1dc`). Original X4 is selected initially; X3 is selectable. X4 Pro and X4 Classic remain disabled until emulator compatibility is verified.

The page discovers the latest stable Nooir release and links to the official firmware download. The browser cannot read the redirected release asset bytes; the user must save the `.bin` and select it locally. The shared v1.6.6 X3/X4 pairing is accepted only when release tag, size, and digest match. Firmware is not included in the source or generated package and is never sent to a remote server.

The packager carries the pinned engine, Nooir shell, licenses, attribution inventory, generated notices, and root-scope isolation worker required for a future GitHub Pages deployment. A package audit rejects reader firmware and allows only the exact upstream emulator-support binaries. A build-only Actions workflow is prepared; its Pages deployment job is hard-disabled.

## Local validation recorded

- The packaged Nooir wrapper was served on loopback and booted the unchanged official v1.6.6 image in X3 and original X4 profiles.
- X3 rendered at 528×792 and X4 at 480×800, with matching hardware-detection and `render_ready` serial messages.
- Virtual Left, Confirm, Right, and Back controls were traced into the guest and used to navigate Library, open the test EPUB, turn pages, and exit Reader on both profiles.
- The Nooir virtual FAT32 SD accepted `test_text_decorations.epub`. `progress.bin`, `recent.json`, and `book-states.json` saves were observed. After a full reload of the packaged wrapper origin, the EPUB remained present and Reader resumed at page 2/3 (67%).
- Browser release metadata lookup succeeded, but the redirected asset-byte fetch failed with a browser CORS error. Stable firmware still needs manual download and local selection.
- The pinned Node suite passed 115 tests locally; two POSIX host-tool-dependent test files require the Ubuntu tools installed by CI. The web-emulator Python contract suite passed 8/8 after moving its temporary fixture under the ignored build directory. Prior v1.6.6 workstream results report 148 Python tests and native CTest 585/585.
- ROM preparation generated the hash-verified Espressif C3 ROM. The previous local browser boot used the earlier engine ROM; the newly licensed ROM has not yet been smoke-tested in QEMU.
- A synthetic package fixture correctly failed because it lacked the resolved Meson subproject checkout. It is not evidence that the final package audit passes. The fresh Linux build and artifact inspection remain required.

## Outstanding before public review or deployment

- Push the isolated review branch, open a draft PR against the dedicated v1.6.6 base, then inspect the complete Linux build, X3/X4 licensed-ROM smoke, corresponding-source archive, and package audit.
- Inspect the workflow artifact for exact support-binary hashes and absence of reader firmware, secrets, and unrelated files.
- Keep Pages deployment disabled unless those checks pass. Manual local firmware selection is accepted for the first beta.
- Keep Pro/Classic disabled until their machine and firmware combinations are independently verified.

No reader firmware is bundled. No physical device was flashed. No merge or deployment has occurred.
