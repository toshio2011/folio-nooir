# X4 Pro / X4 Classic hardware-foundation evidence review — 2026-11-27

**Status:** isolated feasibility branch only. Experimental binaries are not release candidates and are not safe for physical flashing. No source application files, original X4/X3 firmware, Web Emulator implementation, or shared toolchain were changed. No physical device was connected or flashed.

## Provenance and approach decision

- D: worktree: `D:\fatiha\side\_worktrees\nooir-hardware-foundation-feasibility-20261127`
- Branch: `codex/nooir-crosspoint-hardware-foundation-feasibility-20261127`
- Parent HEAD: `02d8080d7e6c66dc687071438df5994df9c8f8dc`
- The only pre-document source delta is the `freeink-sdk-s3` gitlink, from `995d9bd523b9c45b18710bca17a3158ce724a7f4` to `de19f154ef0963879592f3baf6da68606122797c` (27 commits ahead, 0 behind; 61 files changed, +1,828/-236 in the submodule diff). No Nooir application file changed.
- The selected experiment is **Approach B**: retain Nooir 1.6.7 and its separate device profiles, and trial the newer FreeInk hardware layer. It preserved the Nooir UI and compiled for both profiles; Pro application, Reader, SDMMC mount, and progress persistence were exercised in QEMU. This is provisional: the Reader timing comparison does not show a performance win, and neither profile has physical validation.
- **Approach A** (keep the earlier SDK) remains the least-change fallback, but misses the candidate X4 display, SDMMC, MSC lifecycle, and PSRAM/font-cache fixes.
- **Approach C** (move the whole Nooir app to CrossPoint) was not selected: it is a much larger application/UI change with no evidence of better Nooir feature parity or equivalent regression safety.
- The candidate SDK checkpoint is associated with CrossPoint 2.1.0rc, a pre-release, not the stable reference. The stable comparison build used CrossPoint Reader 2.0.0 at commit `42d58daaabcf469a2e5e1ba871714fb9dcf51d29`, SDK `5e94fe7480f019c89fbf691aef87523eb1b46704`. See the [official CrossPoint releases](https://github.com/crosspoint-reader/crosspoint-reader/releases) and [FreeInk SDK repository](https://github.com/Free-Ink/freeink-sdk).

The stable CrossPoint 2.0.0 reference binaries are also preserved under `D:\fatiha\side\_builds\nooir-hardware-foundation-20261127\reference-artifacts`: Pro `firmware.bin` 5,376,544 bytes SHA-256 `f51232b77fddc1759f0cede37e929bf410d6ba13ac500563a3cd3f320b14b444`, `firmware.elf` 99,979,176 bytes SHA-256 `4cbb4fc52a14a1f2e6d8bb2f4d9458d6daa7e6e08cebd996d71c5b26df049a45`; Classic `firmware.bin` 5,364,080 bytes SHA-256 `f5c9a6c4dcbb43727d23087eb4d3894c5e87b6efba105fdaaf6a993444c8e58c`, `firmware.elf` 99,674,592 bytes SHA-256 `9d8c7fa04b980a8da8db118286a5a17f8c0aad6ca58b5ce0939a4c3420892838`.
- For these two Nooir builds only, `scripts/patch_nooir_usb_msc_watchdog.py` applied a temporary vendor-source watchdog adjustment. The script SHA-256 is `DDED2AF75D035421A638950FB631F117B9D601810DD0019417E1F45E83FBA5DA`; it changed the SDK file from SHA `cff2d3036ab3e1e1296b87a084b9d972b04b89d7b8a6f0d6781f8c77caefa82a` to `392689071D5C5B797F91C7A4CAF584F4279153476D983808DD09DC58C023C293` during compilation. The tracked SDK file was restored to the clean candidate revision afterwards. The builds therefore prove this local build recipe, not an unmodified upstream build.

## Preserved firmware and build evidence

The original run3 artifacts and the new candidate artifacts were read and SHA-256 verified. None was overwritten.

| Profile | BIN bytes / SHA-256 | Matching ELF bytes / SHA-256 | Build result |
|---|---|---|---|
| Preserved run3 X4 Pro | 5,765,536 / `03dfc9e157df4d18adb864d87e2bc5a05df34c76e5e77a2304e166f156ba6002` | 93,834,740 / `3364471f3d65a2a745221a7a31dcae865f1751ed7f166c0c3af849434ce7bb8d` | Previously successful, unchanged |
| Preserved run3 X4 Classic | 5,755,696 / `9fdee5dbd9c5fe9d3bd355627cd74773e71f3ab564d56b2aa39894e7df86ff6b` | 93,490,396 / `3bc85fdf23f04f9df6d047a42b1509b477f3ec5b4a46aea94b74ddcde12354a7` | Previously successful, unchanged |
| Candidate SDK de19 X4 Pro | 5,781,792 / `2e5d13ee5347db71325ace83c8171047af54b02095344a65056a04e3266b2d43` | 93,943,240 / `683a6e62105158b60ae8011d04c1ea11329845a24ae8cd655d5e9d40080bfddc` | Successful |
| Candidate SDK de19 X4 Classic | 5,772,336 / `479816c14b1a1d8654972e1b0c809011e2d6dd69fd134e8a03739367a9d55d33` | 93,599,836 / `890e7f32ae8378db538122e3e7cb8c22b8333a7966aeb8047ad73ca76ca2ebae` | Successful |

All four images target ESP32-S3, board `esp32-s3-devkitc1-n16r8`, 16 MiB DIO flash at 80 MHz, 8 MiB OPI PSRAM, 1-bit SDMMC, and the existing 6.25 MiB app slot. Pro enables touch and frontlight and disables physical navigation buttons; Classic disables touch/frontlight and enables physical buttons. BLE is disabled and `NOOIR_DISABLE_FIRMWARE_FLASHING=1` remains set. Candidate app identities are `1.6.7-x4p-exp-no-flash` and `1.6.7-x4c-exp-no-flash`.

- Successful logs: `D:\fatiha\side\_builds\nooir-hardware-foundation-20261127\logs\nooir-de19-x4pro-build-attempt02-20261127.log` and `...\nooir-de19-x4classic-build-20261127.log`.
- Per-build manifests and files: `D:\fatiha\side\_builds\nooir-hardware-foundation-20261127\reference-artifacts\nooir-de19-x4pro\build-provenance.json` and `...\nooir-de19-x4classic\build-provenance.json`.
- Build warnings were inherited/non-fatal: wolfSSL and `FILE_READ/WRITE` macro redefinitions, deprecated `NetworkClient::flush()`, explicit `Rect` constructor warnings, an unrelated legacy PlatformIO `nooir_version` option, and Windows Long Path support disabled. No global Windows or PlatformIO settings were changed.
- Matching bootloader and partition files are also preserved with the build records. The Pro QEMU-only full flash has SHA-256 `a3ee6293c3cbd524fd0f462aa71413628c59aaec687e5b0cca25bc9e48239725`. It is **NOT FOR PHYSICAL FLASHING**.

## Actual visual review

The reviewed evidence root is `D:\fatiha\side\_builds\nooir-hardware-foundation-20261127\evidence`. The requested run3/run4/run5 directories contain build logs, QEMU frames/screenshots, JSONL/serial logs and metrics. No video recording was present; run5 contains USB host-harness logs, not a device recording.

### Nooir X4 Pro screens that are present

- Existing app Recent/Library/Finished captures: `visual-pro-existing-bin-20261127\run-main-shelves-corrected\shots\main-recent.png`, `main-library.png`, and `main-finished.png`.
- Candidate app capture: `qemu\nooir-de19-x4pro-boot-20261127\shots\home.png` (the Recent shelf), `library.png`, `reader-first.png`, `next1.png` through `next8.png`, `reader-exit.png`, and `reader-reopened.png`.
- Same-surface before/after comparisons are preserved under `visual-comparison-20261127`: Recent Home `nooir-pro-home-before-after.png` (SHA-256 `bd07b0661269ec0b5e5dd9bfa4849daccbc6f710e38085843a84c9`); EPUB Reader page 3/10 `nooir-pro-reader-page3-before-after.png` (SHA-256 `72a4270d5137ee14676511ec63c631db2671b86def3bc25bdc081c6b10d63b08`). The old and candidate Reader screenshots show identical page wrapping and footer at the same saved page. The hardware-layer trial did not polish the UI.
- The fixture has two synthetic books and no embedded covers. The cover boxes therefore display clipped title placeholders, including “X4 Classic Si...” and “X4 Class...”; the first item also says no synopsis is available. This is fixture evidence, not a judgment of production cover assets. Item borders, progress badges, title block and footer are aligned consistently, but the short titles visibly clip and the two-item fixture leaves most of the shelf blank. It cannot assess pagination or a large library.
- The Library screenshot shows a Books folder selection without a cover or synopsis. Finished is an empty-state screenshot. Neither can establish real-library spacing, shelf density, or thumbnail quality.
- The candidate Reader screenshot has high-contrast large serif text and an unobstructed page/progress footer. Justified lines have conspicuously wide inter-word gaps in this repeated synthetic paragraph. The candidate `reader-first.png` is actually saved position 2/10, not page 1. `reader-reopened.png` shows 10/10 and 100% after exit/reopen, confirming persistence in this QEMU card. This is not representative production EPUB typography or a cover-quality check.
- The Classic SDL2 screenshots are `diagnostics\eink-qemu-0.9.2\classic-simulator-multipage-home.bmp`, `classic-simulator-multipage-reader-page0.bmp`, and `...reader-page1.bmp`. They show host-rendered Home/Reader and the host button navigation path; they do not show the S3 firmware.
- CrossPoint 2.0.0 Pro reference captures include `evidence\qemu\crosspoint-2.0.0-pro-boot-baseline-run03-20261127\shots\home.png`, Reader `...\crosspoint-2.0.0-pro-reader-session-20261127\shots\reader-first.png`, and Settings Display/Controls and USB screens. CrossPoint Home is a different menu/card design and its Reader uses a different font/layout fixture; these are reference screens, not side-by-side claims of equivalent Nooir screens.

### Requested visuals that remain unconfirmed

No actual Nooir captures were found for Folio Main versus Lite, Featured Book, Spine layouts, a populated Finished shelf, XTC, XTCH, CBZ, Nooir Settings, Nooir USB Drive, Pro touch/gesture settings, or Pro frontlight/warmth controls. No screenshot proves production covers, swipe feedback, Classic physical-button hints on the S3 build, or how these screens compare with the original retail C3 unit. The harness did not test a Classic S3 image. These are explicitly unconfirmed, not passes.

## Performance review and comparison

All times below come from preserved raw serial/QEMU logs, JSONL events and metrics files under `evidence\qemu`; values are QEMU guest-time measurements, not physical device wall-clock measurements. P95 is nearest-rank. For n=8 it equals the maximum, so it is a sample bound, not a stable tail estimate.

| Measure | Samples | Median | p95 / worst | Interpretation |
|---|---:|---:|---:|---|
| Existing Nooir Pro page-render total | 3: 2132, 1310, 1323 ms | 1323 ms | 2132 / 2132 ms | Matches the reported 1,323 ms median; only 3 samples |
| Candidate Nooir Pro page-render total, all pages | 10: 2131, 1312, 1321, 1312, 1323, 1311, 1323, 1311, 1293, 2102 ms | 1316.5 ms | 2131 / 2131 ms | First/reopen work raises the tail; median change is not material |
| Candidate Nooir Pro page-turn render total | 8: 1312, 1321, 1312, 1323, 1311, 1323, 1311, 1293 ms | 1312 ms | 1323 / 1323 ms | No meaningful improvement from the older 1,316.5 ms two-turn median |
| Candidate Nooir Pro input-to-settled screenshot | 8: 1603, 1391, 1407, 1380, 1386, 1475, 1403, 1487 ms | 1405 ms | 1603 / 1603 ms | Includes input/panel/emulator settling, not just renderer |
| Stable CrossPoint 2.0.0 Pro input-to-settled screenshot | 8: 1349, 1275, 1278, 1274, 1274, 1282, 1273, 1287 ms | 1276.5 ms | 1349 / 1349 ms | Same QEMU machine/panel family and pacing; candidate Nooir median is 128.5 ms (~10.1%) slower in this small sample |

Raw evidence used for these measurements:

- Existing Nooir run4: `D:\p\run4\qemu\pro-run3-reader-reopen-fast\log.txt`, `metrics.json`, `run.jsonl`, and heap snapshots.
- Candidate Nooir: `D:\fatiha\side\_builds\nooir-hardware-foundation-20261127\evidence\qemu\nooir-de19-x4pro-boot-20261127\log.txt`, `metrics.json`, `run.jsonl`, `report.html`, and `heaps\`.
- Stable CrossPoint: `D:\fatiha\side\_builds\nooir-hardware-foundation-20261127\evidence\qemu\crosspoint-2.0.0-pro-reader-session-20261127\log.txt`, `metrics.json`, `run.jsonl`, and `heaps\`.
- Run5 USB host harness: `D:\p\run5\logs\usb-drive-activity-host-20261126-final.log`, SHA-256 `4e065e8ea0eb08c44c8b9e571a2e8ca201a51bde095cfc93881fbbb8e41effd0`.

Both applications ran on eink-qemu 0.9.2 `x4pro,panel=uc8279-68`, with the same copied synthetic SD-card image and scripted input/settle pacing. The Nooir run used the 50,000 ms virtual Power-hold workaround; CrossPoint did not need it. The fixture pagination differs (Nooir reports 10 pages; CrossPoint 9), and app drawing differs, so the comparison is useful but not a controlled identical-page benchmark. CrossPoint's release build does not emit Nooir's internal `Page render` spans. **The data does not show Nooir faster than CrossPoint.** It does show the candidate SDK pin retained approximately the old Nooir render time while changing the hardware library.

Raw Nooir render breakdown explains the 1,323 ms case:

- First render: prewarm 9 ms; BW render 45 ms; display 1,498 ms; BW store 2 ms; gray-plane preparation 85 + 98 ms; gray display 338 ms; BW restore 56 ms; total 2,131 ms.
- Warm page turns: BW render 39–45 ms; display 673–676 ms; gray-plane work about 174–191 ms; gray display 342–343 ms; storage/restore about 60 ms; total 1,293–1,323 ms. QEMU logs show a 557 ms gray pre-refresh wait and about 228–229 ms gray display wait.
- The initial render's 1,498 ms display phase dominates the first-page outlier. On steady turns, renderer BW work is only 39–45 ms; panel/waveform sequencing is the largest visible term. These are emulator timing phases, not a measurement of a retail panel.
- The run logs SDMMC mount and a lightweight metadata-cache load but do not time SD reads, EPUB decompression, glyph rasterization, image decode, or host-side QEMU work separately. Do not attribute the remaining ~1.3 s to SD or decoding without new instrumentation.
- Candidate EPUB open is a single trace and includes library/input/display work; the older comparable run recorded 5,535 guest ms from tap to Reader screenshot. Reader exit logged 168,368 µs. Neither is enough for a latency distribution. The 50-second power hold makes screenshot time unsuitable as cold boot time. No warm-boot benchmark exists.
- No timed workload covers book-selection movement, shelf pagination, partial refreshes by action, thumbnail decode, 400+ books, production covers, XTC/XTCH/CBZ, or USB initialization/teardown. The fixture produced missing thumbnail-cache lookups, but none was timed.
- Candidate Pro QEMU snapshots retained about 7.69 MiB free PSRAM; the custom internal-heap log reached about 229 KiB free under Reader pressure with a largest block around 188 KiB. No corrupt heap block was reported. CrossPoint uses different heap instrumentation and feature allocations, so its free-heap figures are not an apples-to-apples win. Neither run captured task stack high-water marks.
- Existing run5 USB host tests are C++ stubs for activity sequencing only. CrossPoint's QEMU USB screen reached a TinyUSB installation failure; eink-qemu does not provide host MSC enumeration/transfer. Neither is an S3 USB performance test.

## Firmware-in-QEMU and Classic host evidence boundaries

Candidate Pro QEMU log evidence records X4 Pro identity, display initialization, PSRAM allocator activity, SDMMC mount, Home and Library render, EPUB Reader, eight page turns, Reader exit, and saved-progress reopen. The virtual-power workaround is preserved. The missing emulated IMU and first-boot settings defaults are logged as nonfatal. Deep sleep/wake, retail power sequencing, physical touch/swipe, LEDs, panel, SD electrical behavior and recovery remain untested.

Classic produced a successful S3 BIN/ELF pair, but no Classic firmware QEMU machine is available. The previous SDL2 run passed host app navigation, EPUB open, page turn, exit and persistence. Ubuntu WSL was present with SDL2 2.30.0 and g++ 13.3.0; this review did not repeat that complete simulator replay because application sources were unchanged and the preserved run remains host-only. This does not execute or certify the Classic BIN.

## Safety and restart gates

Both candidate builds retain OTA/update suppression and `NOOIR_DISABLE_FIRMWARE_FLASHING=1` for SD firmware-flasher writes. This disables normal update/recovery routes; it does not provide a backup or restore path. The full 16 MiB Pro flash image is composed for QEMU only. No firmware was flashed, and no image here is ready for physical flashing. A board-matched backup and independently validated recovery route remain prerequisites before any separately authorized hardware write.

Preserve the run3 original BIN/ELF pairs, candidate BIN/ELF pairs, QEMU-only image, raw logs, metrics, screenshots and build manifests. Do not rebuild merely to reconcile hashes. Resume with one of: a concrete browser integration question, a reproducible performance target with controlled workloads, or physical hardware plus a proven recovery route.

## Full UX and performance validation continuation — 2026-11-27

This addendum supersedes earlier-in-this-document statements that XTC, XTCH, CBZ, or the 400-file library fixture had not been exercised. It records the additional evidence gathered after that review. No application source was changed and neither ESP32-S3 firmware image was rebuilt. The Pro QEMU runs used the exact preserved candidate image and matching ELF; all 16 MiB merged-flash images remain **QEMU ONLY — NOT FOR PHYSICAL FLASHING**.

### Preserved fixtures and identities

- New evidence is under `D:\fatiha\side\_builds\nooir-hardware-foundation-20261127\evidence\full-ux-validation-20261127`. The self-authored fixture set has 400 bulk EPUBs, seven special EPUBs, one XTC, one XTCH, one CBZ, 410 total book files and 22,174,577 bytes. No personal books or third-party artwork were included. `fixtures\fixture-manifest.json` SHA-256: `3c3ba5339f80c6ad5058cd570659cf8cef4fdd1af1ebd32b89a0513304be9f44`; the validator report `fixture-validation.json` SHA-256: `541278268bdb939604041d4fc340c627a038ca219cb507e59589446c97d710c5`. The report records 406 valid EPUBs, one deliberately malformed EPUB, and valid XTC/XTCH/CBZ headers and containers.
- Candidate Pro application BIN: `D:\fatiha\side\_builds\nooir-hardware-foundation-20261127\reference-artifacts\nooir-de19-x4pro\folio-nooir-v1.6.7-x4pro-sdk-de19-EXPERIMENTAL.bin`, 5,781,792 bytes, SHA-256 `2e5d13ee5347db71325ace83c8171047af54b02095344a65056a04e3266b2d43`; matching ELF SHA-256 `683a6e62105158b60ae8011d04c1ea11329845a24ae8cd655d5e9d40080bfddc`.
- Candidate Classic application BIN: `D:\fatiha\side\_builds\nooir-hardware-foundation-20261127\reference-artifacts\nooir-de19-x4classic\folio-nooir-v1.6.7-x4classic-sdk-de19-EXPERIMENTAL.bin`, 5,772,336 bytes, SHA-256 `479816c14b1a1d8654972e1b0c809011e2d6dd69fd134e8a03739367a9d55d33`; matching ELF SHA-256 `890e7f32ae8378db538122e3e7cb8c22b8333a7966aeb8047ad73ca76ca2ebae`.
- The candidate Pro merged QEMU flash is 16,777,216 bytes, SHA-256 `a3ee6293c3cbd524fd0f462aa71413628c59aaec687e5b0cca25bc9e48239725`. Do not flash it. The preserved original run3 BIN/ELF pairs and run4 QEMU image were rechecked; they remain unchanged. Exact checksum records are in `evidence-index.json`.

### Pro firmware-in-QEMU results

The runner was eink-qemu 0.9.2, machine `x4pro,panel=uc8279-68`, the exact candidate Pro image and ELF, and the documented 50,000 ms virtual Power-hold workaround. It used the generated SD media and D:-hosted temp/cache paths. QEMU reached the Nooir app, logged `Hardware detect: X4 Pro`, `Display initialized`, and `SDMMC card mounted`; the virtual PSRAM allocator supplied multi-megabyte buffers. This is firmware execution in an emulator, not a physical-board result.

| Fixture / run | Result | Evidence and scope |
|---|---|---|
| 400-book `Library400` folder | Passed for opening and displaying the file list | `candidate-large-library-attempt03`; 400 EPUBs were staged and filenames appeared. At that screen internal free heap was about 204,728 bytes, largest block 161,644 bytes, PSRAM about 7.88 MiB free, with no heap corruption. This is a visible file-list check, not a 400-book cover shelf, pagination test, or latency distribution. |
| Generated EPUB | Passed open, cover/body render, next page, Reader exit, and progress write | `candidate-realistic-epub-confirmed-attempt02`; the first render took 5,133 ms, the next body page 2,109 ms. The Reader-exit profile path took 76,977 µs including about 75,403 µs of saves. One metadata retrieval took 875 ms. The Home shelf CPU render was 89.9 ms initially and 140.8 ms on return with the cover loaded. |
| XTC 1-bit | Passed open, page 1 and 2 render, exit to Home | `candidate-xtc-reader`; logs verify the 8-page XTC header. A page-1 heap snapshot showed 7,933,532 bytes PSRAM free, 234,856 bytes internal heap free and largest block 190,964 bytes; no corrupt heap block. |
| XTCH 2-bit | Passed open, page 1 and 2 render, exit to Home | `candidate-xtch-reader-retry01`; logs verify 8 pages and 2-bit depth. This proves basic XTCH dispatch/render in this QEMU build only. |
| CBZ | Passed page 1, cached page 2, and Reader exit | `candidate-cbz-reader-retry01`; logs show the page-1 foreground path at 2,016 ms plus 1,495 ms panel refresh, then a cache-backed page-2 path at 145 ms plus 600 ms refresh. Look-ahead pages took about 1.96–1.98 s each and wrote 86,404-byte persistent page caches. The page-1 screenshot visibly includes the `Next ready … cached …` prefetch progress strip; this is firmware UI status, not a QEMU title bar. Assess whether that progress treatment is suitably quiet for normal reading before calling CBZ presentation polished. |

Each successful format run logged two `[ERR]` entries for the emulator’s absent IMU and first-boot default settings, plus expected first-run cache misses. The QEMU scripts ended `ok=true`, showed no panic/assert/crash, and logged zero warnings. The XTC/XTCH test art is deliberately simple generated line art, suitable for checking the two decoders but not comic-image quality. Initial XTCH/CBZ runner attempts used a wrong artifact path and stopped before QEMU boot; the failed setup logs are retained separately, and the corrected runs above passed.

### What the images show

- The actual run4 Pro captures at `D:\p\run4\qemu\pro-run3-reader-reopen-fast\shots` show the Recent shelf, Library folder, selected book, EPUB Reader, forward/back page, Reader exit and return to Library. The separate `pro-run3-progress-reopen-after-cold-boot\shots` shows Home and Reader after a new emulator process. The run3 directory itself has BIN/ELF and manifests, but no screenshots. Run5 contains four short USB host-harness logs and no device video.
- The new generated cover is visible in `candidate-realistic-epub-confirmed-attempt02\shots\recent-after-reader.png`; title/author/progress blocks align and remain readable, while fine cover art is heavily dithered at thumbnail size. The Reader’s large serif text is readable, but the repeated, intentionally justified fixture has conspicuously wide word gaps. A separate existing-image comparison shows the old and candidate Pro Reader pixels identical for the same saved page; that typography is inherited Nooir behavior, not a regression from the SDK trial.
- XTC and XTCH screenshots prove full-screen page output but use the simple line-art fixture. CBZ’s first-page capture retains the prefetch status strip while look-ahead is active; the second-page image shows cached content without that strip.
- The existing Classic SDL2 captures `classic-simulator-multipage-home.bmp`, `...library.bmp`, `...reader-page0.bmp`, `...reader-page1.bmp`, `...finished.bmp`, and `...after-exit.bmp` show the bottom Menu/Open/Library/Finished button row and EPUB page movement without any touch event. They are host-rendered simulator output; they do not execute the Classic S3 BIN or exercise GPIO.
- No verified Nooir screenshots were found for Folio Lite, Featured Book, Spine layouts, a populated Finished shelf, Nooir Settings, Nooir USB Drive, or Pro lighting/warmth controls. Earlier QEMU Home swipe attempts did not change the screen and timed out; that failed route does not prove the features themselves fail. Scripted QEMU taps used for navigation are not physical GT911/Home-key validation.

### Matched performance results

The corrected comparison used the same generated typography EPUB, X4 Pro QEMU machine/panel, 2050 MiB/32 KiB-cluster card, non-turbo timing, and 20 scripted warm page turns. Values are guest time. p95 uses nearest-rank. Full arrays and raw `run.jsonl`/serial/metrics checksums are in `matched-performance-summary.json` and `evidence-index.json`.

| 20 turns | Median press-to-settled | p95 | Worst | Render spans |
|---|---:|---:|---:|---|
| Preserved original run3 Nooir Pro | 1,420 ms | 2,260 ms | 2,303 ms | 20 warm renders: median 1,276 ms, p95 1,285 ms, worst 2,103 ms |
| Candidate SDK de19 Nooir Pro | 1,415 ms | 2,249 ms | 2,283 ms | 20 warm renders: median 1,276 ms, p95 1,285 ms, worst 2,103 ms |
| Stable CrossPoint 2.0.0 Pro | 1,403 ms | 1,534 ms | 2,331 ms | Release emits no matching internal `Page render` span; only press-to-settled is compared |

The old and candidate Nooir cover renders were 5,152 and 5,157 ms; the first body page was 2,111 and 2,110 ms. Their warm render phase medians are nearly identical: BW layout/render 25 ms (p95/worst 29 ms); main display phase 674.5 ms median, 677 ms p95, 1,501 ms worst; gray LSB preparation 76 ms median, gray MSB 87.5 ms, gray display 342 ms median (343 ms p95/worst), and BW restore 58.5 ms. The long display/waveform phase dominates these QEMU spans; a 1,501 ms display outlier accounts for a warm-render worst of 2,103 ms. The candidate does not show a meaningful speedup over the original app. CrossPoint’s median settled interval is close; its p95 is lower in this small sample but its worst is 2,331 ms. This remains an emulator comparison, not physical panel latency.

A single Reader memory sample after the matched warm sequence showed 7,928,936 bytes free PSRAM (low-water about 7,777,952), 230,260 bytes internal free and 179,208 bytes largest internal block, with no heap corruption. A single RecentBooks log recorded `render_stack_high_water_bytes=3712`; this is one task watermark observation, not a task-stack profile. CBZ has the single-run phase detail above. The data still does not separate EPUB SD reads, zip/decode, image decode, glyph work, display-driver time and QEMU host overhead. No comparable timing exists for cold/warm boot, selection movement, pagination, partial refresh by action, thumbnail distributions, USB initialization/teardown, or 400-item scroll performance.

### Classic current-toolchain attempt

The previously successful Classic SDL2 Home/Library/EPUB/page-turn/exit/progress replay and screenshots remain valid as host-app evidence. A fresh compile attempt against this isolated source used Ubuntu WSL, SDL2 2.30.0, g++ 13.3.0 and a D:-only PlatformIO/cache/temp setup. It ran 18m42s and failed at `src/activities/network/NetworkModeSelectionActivity.cpp:12:33`: the simulator target does not define `FREEINK_CAP_USB_MSC`. The build also warned about possible `snprintf` truncation in ClockWeatherActivity and a possibly null `AP_PASSWORD` passed to `strlen`. No application/config change was made to mask these diagnostics; the complete failed log is `D:\fatiha\side\_builds\nooir-hardware-foundation-20261127\evidence\full-ux-validation-20261127\logs\classic-simulator-build-attempt02.log`. No current-source Classic simulator executable was produced and no Classic S3 BIN was run.

### Storage, limitations and next decision

Persistent outputs, run logs, screenshots, generated fixtures, WSL simulator cache and temporary storage used for this continuation are under D:. One earlier initial QEMU run recorded an automatically generated flash file under `C:\Users\fatiha\AppData\Local\Temp`; it was not used as a project workspace, and later runs explicitly redirected QEMU temp to D:. Do not recreate or rely on the old C: workspace. The initial XTCH/CBZ path errors are preserved and clearly marked setup-only.

No source optimization or firmware rebuild is justified by the matched results: the app’s page output is pixel-identical and its timing tracks the previous Nooir build. The concrete polish issue to revisit is the persistent CBZ prefetch-progress strip; typography and cover judgements need a representative, rights-cleared commercial-quality fixture before UI changes. To resume, prioritize (1) a reproducible Classic simulator capability definition in the host-only build config, without changing the Classic hardware profile; (2) capture Nooir Lite/Spine/Settings/USB/Pro light screens with a reachable control route; (3) instrument 400-book selection/pagination/thumbnails and separate storage/decode/display costs; and (4) physical Pro and Classic validation only after a board-matched backup and independently tested recovery route. No physical firmware was flashed, no release was made, and no S3 app source or BIN was changed in this continuation.
