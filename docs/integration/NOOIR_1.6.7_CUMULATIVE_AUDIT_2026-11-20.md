# Folio Nooir 1.6.7 cumulative integration audit — 20 November 2026

## Scope and lineage

This audit compares the integrated source with official 1.6.6 (`b6d428a3187afc25d434fcb475e9ab436099c1ba`), the public 1.6.7 refinement branch at `1345eca01c597a4f648c0402f5d97798f68a1302`, and the protected isolated 20261120_04 candidate. The refinement branch's changes after 1.6.6 are documentation-only; the firmware implementation in this branch comes from the cumulative _04 source overlay, not by merging unrelated experimental branches.

The local root checkout on D: was dirty and remains untouched. This work uses the isolated branch `codex/folio-nooir-1.6.7-integrated-20261120`. Classic/Pro, S3/Web Emulator and MihonNooir trees are excluded. The status-bar patch, rejected cache probe and unrelated feature experiments are also excluded.

The source overlay manifest is `20261120_04_SOURCE_OVERLAY_MANIFEST.json` in this directory. It records candidate and prior-file SHA-256 values for every overlaid source file. The _04 firmware-input manifest reports 3,591 source inputs and 1,445 external dependency files, SHA-256 `F54D7888F913291DD8E74B0221B012C881FFECF655E0D842E589A81ADFCC15C6`; it was generated from an isolated, uncommitted source snapshot, so it is content provenance rather than Git-commit provenance.

FreeInk is pinned to reachable public commit `958720659ea289ae325e83db20049d0ea844800d`. Four semantic Nooir deltas to that SDK are carried as `patches/freeink-sdk/nooir-1.6.7_20261120_04.patch` and applied by `scripts/apply_nooir_freeink_patch.py`; the other 381 captured SDK files match that commit after line-ending normalization. The patch is based on the FreeInk MIT-licensed source and retains its attribution and license.

## Feature inventory

| Area | Integrated source status | Evidence / remaining limits |
|---|---|---|
| Reader formats and EPUB styling | Present: EPUB, XTC, XTCH and CBZ paths remain; EPUB CSS, fonts, images, RTL and existing rendering options are retained. | Source overlay from _04 and existing Reader contracts; complex-book and long-duration X4 acceptance remains incomplete. |
| Force Paragraph Indents | Present; existing setting and rendering path retained. | Source-level setting/data path present. Dedicated visual regression across all formats remains needed. |
| SPI-array and byte fallback | Present; shared SPI-array transfer path plus SdFat allocation hints and byte-mode fallback remain selectable. | Original X4 _04 used the SPI-array profile. Physical X3 acceptance and stack-margin checks remain open; X3 default is unchanged. |
| Adaptive BLE and generic HID | Present: generic descriptor/report-aware input learning, IINE support, exactly-once event handling, mappings, saved profiles, Device Info diagnostics and truthful BLE status UI. | Mapping contracts 22/22; BLE usability contracts 5/5; owner-reported X4 _04 observations. HID compatibility across untested devices remains open. |
| Automatic EPUB BLE recovery | Present in _04 source: automatic stable-page admission, command retention, confirmed shutdown before risky rendering, bounded post-render recovery and automatic readmission. | Owner reports physical repeated turn/reconnect success. Full post-fix raw COM3 capture is not present here; the saved `physical_com3_20261120_full.txt` is the earlier pre-fix evidence and must not be attributed to _04. The 20-turn single-session goal remains unverified. |
| XTC / XTCH | XTC continuous BLE path is retained; XTCH path and streaming remain. | Owner reports two mapped XTC actions on _04. XTCH physical validation remains outstanding. |
| Reader persistence and shelf state | Present: progress/state stores, validated atomic persistence, snapshot policy, authoritative recent-book completion/progress refresh and reset/reread semantics. | Shelf progress contract checks 5/5; persistence/recovery and full physical navigation tests remain. |
| Memory and lifecycle improvements | Present: existing adaptive tiers, staged cleanup, phase-safe worker-owned teardown, confirmed shutdown acknowledgement, page-demand-aware BLE suspension and BLE-OFF early bypass. Thresholds and NimBLE pools are retained. | Lifecycle contracts 97/97 with pinned NimBLE path; separate no-device watchdog and partial NimBLE initialization rollback remain unresolved. No source test proves physical callback safety. |
| Statistics and Achievements | Present: 30 achievements using existing counters, with a lightweight monochrome collection view. | Achievement tests 9/9; translation/layout validation is not complete on every device/theme. |
| Main/Lite and Settings | Both themes and existing Settings behavior are retained; BLE profile editing/switching and Device Info are integrated. | BLE usability tests pass; X3 layout and complete Main/Lite visual acceptance remain. |
| Web UI and Library | Existing Web, transfer and Library flows remain; bounded response writers and existing post-operation BLE recovery hooks are included. | Web source contracts included; post-Web fragmented-heap recovery and full physical Library/Web lifecycle acceptance remain. |
| Clock and Weather | Existing Wi-Fi-session synchronization and manual sync policy are retained. | No new source redesign. Same-session manual NTP retry is documented as a future improvement, not a release blocker. |
| Random sleep image pool | Existing shuffled-pool implementation remains unchanged in this integration. | Not part of _04's focused physical evidence; native pool test was not executed because CMake is unavailable in this environment. |

## Tests already run against this integration worktree

- SDK patch contract: 2/2 pass.
- BLE session/lifecycle contracts: 97/97 pass when pointed at the pinned NimBLE source.
- HID mapping: 22/22 pass.
- BLE usability/status: 5/5 pass.
- Achievement thresholds: 9/9 pass.
- RecentBooks progress policy: 5/5 pass.
- BLE memory-policy discovery: 40 tests passed; one module could not import because `lib/hal/NooirSdSpiDiagnostics.h` is absent from both the baseline and candidate snapshot.
- Native GoogleTest/CMake fixtures were not run because CMake is not installed. These results are source/host checks, not device acceptance.

A full integrated production build and final release-profile build are separate gates. Do not treat the _04 build as proof that a later integrated commit or final release image compiles identically.

## Release blockers and limits

1. No-device Interrupt-WDT remains an unresolved release safety issue.
2. Partial NimBLE initialization failure rollback remains unproven; do not fault-inject it on hardware.
3. The owner-reported _04 physical milestone is not accompanied by a raw post-fix COM3 file that identifies the tested image hash. The available saved raw log is pre-fix evidence. Preserve both facts without claiming cryptographic image/log binding.
4. The 20 consecutive EPUB turns in one session, backward turns, XTCH, BLE-OFF latency, demanding EPUBs, X3, full Main/Lite layouts, sleep/wake and long-duration behavior still require final-image validation.
5. The _04 binary's `b846e155` inherited label is not a Git commit identifier.
6. No public release, tag, flash or merge to `main` is authorized by this integration task.

## Excluded work

No status-bar implementation, rejected EPUB cache probe, experimental threshold reduction, pool tuning, Classic/Pro port, S3/Web Emulator port or MihonNooir source was integrated. Existing EB4D, _02, _03 and _04 artifacts remain separate and unmodified.
