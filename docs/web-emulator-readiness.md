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

## Current readiness addendum — 2026-11-17

**Status: not ready to publish; locally prepared for reviewed hosted validation.** This addendum is newer than the historical snapshot above and preserves its earlier evidence.

### Current checkpoint

- PR #3 is open and draft at remote/local base `33e3c5e1a03f6239f050409e7a41b29d4098d011`. Local-only changes are on `codex/nooir-web-emulator-beta-followup-20261117`; no commit/push/CI/deploy occurred.
- Private/manual X3 and original X4 Reader workflows remain the reported functional baseline. The green hosted run 44017191071 built/audited the package and completed a Chrome step, but its workflow injected `NOOIR_INPUT_PROBE_KEY=Left` and stopped after Home plus one input. It does not prove hosted EPUB navigation, screenshot download, or persistence acceptance.
- Existing review artifact ID 14299759687 is 111,518,504 bytes, SHA-256 `960cd2d0cbc74be0f3e8f530e0ae041b24d832351093c5c085b98fff4bceaf39`. It has no reader firmware or test SD image; its `.img` is the blank runtime template. It is an older artifact with the broken CrossPoint MIT link and is not a candidate for publication.

### Work prepared locally

The browser smoke now verifies a completed device-named PNG exists, is a structurally valid PNG, and byte-matches the screenshot preview using SHA-256 and dimensions. Four focused Node tests pass. The packaging step rewrites the CrossPoint attribution to the included `licenses/folio-nooir/LICENSE`; the new strict package check verifies local links in shipped Markdown/HTML and rejects missing or escaping links. Contract tests cover valid and invalid targets.

Successful acceptance evidence is prepared as a separate success-only output containing allowlisted screenshots, dimensions/hashes, structured acceptance booleans and a bounded serial-marker summary. It omits firmware/ROM/SD bytes, SD identifiers/hashes, raw logs, local paths and user profile data. The helper's positive/negative contracts pass. No real-browser evidence was generated in this local continuation, and no old loose screenshots/log/results were found in the scoped preservation folders.

The browser/UI review confirms X4 default, X3/X4 enabled, Pro/Classic disabled, latest-stable metadata with manual official download/local selection because GitHub's binary redirect is not reliably readable cross-origin, browser-memory firmware handling/no upload, browser-profile SD/settings persistence, visible errors/fallbacks, and experimental limitations. The interface should continue to disclose slow startup, occasional missed presses and unverified browsers.

### Validation and blockers

- Node screenshot tests: 4/4 pass.
- Python Web Emulator contracts: 36 pass, 1 skipped.
- Node syntax and Python bytecode compilation pass.
- Tracked diff whitespace check passes with CR-at-EOL recognized; LF test worktree was used because Windows Git `core.autocrlf=true` otherwise changes pinned LF license bytes during reads.
- Local full workflow YAML parser unavailable; structural contracts pass, but hosted workflow checks remain necessary.
- No complete local QEMU/WASM package build or new strict audit was possible: the D: working checkout lacks pinned engine source, generated engine distribution, prepared ROM input, and Emscripten SDK. The prior hosted source archive and audit passed for the old package only. The corrected local package must be rebuilt and strictly audited in hosted validation.
- CI run 44017191186 failed during recursive checkout for the unrelated unavailable FreeInk gitlink `7bd6aebe9b40198d7dddf455400cee9225430726`; no tests/build ran. A local web-only path filter proposal does not change the pin and keeps firmware/SDK paths on normal firmware CI. Confirm branch-protection behavior for skipped checks before any merge.

### Deployment decision and gates

Keep Pages hard-disabled and PR #3 draft. Before publication, a reviewed commit/push must run the full X3 and X4 browser workflows separately, validate actual screenshot downloads and Reader/persistence flows, upload the separate filtered evidence artifact, rebuild the corrected distributable package, pass strict license/corresponding-source/link/exclusion audits, and undergo manual artifact inspection. After all gates, request a separate explicit approval to enable Pages/deploy. Roll back by redeploying the last approved commit or taking the site offline; keep its exact commit and package hash for that purpose.

X4 Pro and X4 Classic remain disabled and do not block the X3/X4 beta.
### Package-link audit refinement — 2026-11-17

The previous review ZIP is unchanged and not publishable: its CrossPoint MIT link is broken, and its copied upstream eink-qemu README points to documentation absent from the site package. The package builder now rewrites only generated documentation copies: CrossPoint points to the included Nooir MIT text; upstream README links point to the pinned eink-qemu commit, while packaged COPYING/LICENSE links remain internal. The corresponding-source archive remains the unmodified upstream source.

A read-only link-only audit of the preserved artifact layout after the proposed transformations checked 32 local links across 15 Markdown/HTML documents, with zero missing/escaping targets. This confirms the link mapping against the archived file layout; the package was not regenerated and the complete strict package audit remains pending. A new contract also rejects README links that escape the pinned source tree.
## 2026-11-17 final integrity check
Current branch/HEAD are recorded in HANDOFF.md. The final `git status`/`git diff` check failed with a work-tree diagnostic despite valid branch/HEAD resolution; no Git metadata was changed. R2 manifest verified 14 files with zero hash/size mismatches, but fresh stage inventory is unverified and must be resolved before commit. The private-key pattern match is synthetic redaction-test text with no key payload. Full package audit and actual browser screenshot-download acceptance remain pending.
## 2026-11-18 pre-push gate
Git metadata is intact; the earlier status error was the D: access context. Authorized status reports the exact R3 set (11 modified tracked, 3 untracked, none staged). A CRLF-only local run fails the pinned notice byte-hash check because `core.autocrlf=true`; the verified LF checkout matches the reviewed code and passes 37 Python contracts (one skipped), 4 PNG helper tests, and JS syntax checks. Full package build/audit and full browser acceptance remain hosted gates because the pinned build inputs are absent locally. PR #3 is open/draft at `33e3c5e1a03f6239f050409e7a41b29d4098d011`; Pages remains disabled.

## 2026-11-18 hosted acceptance result

**Readiness: NOT READY for public beta deployment.** PR #3 remains open/draft; current remote commit is `106da25a10b1898d682906c5a1d617684cf50cba`. Pages is still disabled.

Hosted run [44223336526](https://github.com/toshio2011/folio-nooir/actions/runs/44223336526) passed the pinned engine, licensed ROM, official v1.6.6 input checksum, native X3/X4 startup, corresponding-source archive, and strict package/license audit. PR formatting [44223336516](https://github.com/toshio2011/folio-nooir/actions/runs/44223336516) passed. The Chrome step failed independently on X3 and X4 at the first Confirm after Library: the expected screen change was at least 2%, but was not observed. Each profile had reached Home, screenshot file validation/download and Library. EPUB, page-turn, Reader exit, browser reload, and progress restoration checks did not execute.

Confirmed smoke setup gap: workflow creates a FAT32 image and empty `/books` but places no EPUB there. The empty-folder visual transition is the leading cause of the exact assertion, but available diagnostics do not include QOM key state or final screen-difference values, so whether input delivery contributed remains unproven. The local candidate adds the already-tracked `test/epubs/test_text_decorations.epub` at the root of each ephemeral test image. The Library step now proceeds directly to the EPUB, where the guest `activity=EpubReader` marker is the navigation assertion rather than an empty-folder pixel threshold. A redacted allowlist-based action summary is included for future failures. Neither change is pushed.

The review artifact is ID `14392711035`, 111,516,044 bytes, SHA-256 `6f7c39f81bcf8fcdc766ab87c0a40b2e3667a318a8b48b6c7514ddd3e8e92cf2`; extracted size 208,703,027 bytes. Its 99,552,083-byte pinned corresponding-source archive has SHA-256 `DC022ED29F8A2844F42AD0C8A45E8A5417686F9B72B80E927AC3FC8C57AAF5EC`. Strict audit passed both hosted Linux and the local LF-only checkout. It contains no reader firmware, EPUB, or temporary test SD; only hash-approved C3 emulator ROM/boot/NVS/partition support binaries and the blank runtime SD template. Authentic license notices, attribution targets and source inventory passed the audit.

The first local strict-audit call from the Windows CRLF checkout failed only because its checked-out ROM preparation script line endings differed. The package and LF source hashes match (`DDE01D5ED9AB4509625C791240DF51A5A555FE68A83127C7583171B7B7D133E1`), and the LF audit passed. Do not alter license files to address this conversion.

Local checks for the follow-up candidate: Python contracts 39 passed/1 skipped, Node screenshot tests 4/4, JS/Python syntax checks pass, whitespace check passes. Actual local FAT creation/Chrome smoke was not run; required local build inputs are absent.

**Next gate:** inspect local candidate diff and R5 manifest, then request approval before another push/hosted run. Both X3 and X4 must complete screenshot, Home, Library, the EPUB, forward/back page turns, Reader exit, SD restoration and progress restoration. Manually review the package produced by that same successful run. Refresh PR description before public review. Keep X4 Pro/Classic disabled; they do not block this beta. A separate explicit approval is required to enable Pages/deploy. Roll back an approved deployment by restoring the last approved Pages artifact/commit or taking the site offline and verifying the public URL.


## 2026-11-18 R5 follow-up — pre-push validation

**Status: approved for one six-file commit/push and build-only CI; not deployment-ready.** Remote PR #3 was verified open/draft at `106da25a10b1898d682906c5a1d617684cf50cba`. The smoke workflow puts the existing tracked test EPUB at the root of each temporary FAT32 card after packaging, so opening the single Library item must reach the guest `activity=EpubReader` marker. This removes the previous empty-folder pixel-only gate. Page-turn checks use guest spine/page state plus rendered output; screenshot checks require a real PNG file matching preview bytes.

Local validation: Node tests 18/18; Python contracts 39 passed/1 expected symlink skip from the LF checkout, including pinned-notice hash validation, temporary-only fixture checks, and failure redaction. The CRLF checkout reports one keycodemapdb blob mismatch solely from line-ending conversion; the full LF suite passes and no notice was edited. Syntax checks pass; a YAML parser is unavailable locally, so hosted Actions parsing is required. Package generation/license audit must be repeated by the hosted workflow. Full local browser smoke is unavailable.

The distributable path remains `build/nooir-web-emulator`; fixture images are created afterward and stored only at `build/nooir-browser-smoke/engine/nooir-test-*.img`. Firmware remains user-supplied/ephemeral, the EPUB is excluded from packaging, X3 and X4 run independently, and success evidence is uploaded separately from the package. PR stays draft; Pages and Pro/Classic remain disabled.
