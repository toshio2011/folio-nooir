# Folio Nooir 1.6.7 — development continuation record

**Checkpoint:** 2026-11-24
**Scope:** Original XTEINK X4, ESP32-C3. This record summarizes prior decisions and evidence; it does not add or validate firmware behavior.

## Authoritative source and published release

The original-X4 development source is maintained on D: on branch `codex/folio-nooir-1.6.7-integrated-20261120`. The published `1.6.7` tag points to integration commit `5696a555f9f741e8b80c7360d26a74129750610d`; the official image was built from source commit `ad0f269d13aed74d535c619588e1b3bde4f4ecbc`.

The clean-label official validation image is recorded in [`NOOIR_1.6.7_BUILD_RECORD_2026-11-20.md`](../integration/NOOIR_1.6.7_BUILD_RECORD_2026-11-20.md): BIN SHA-256 `FA8F2DD28250887BE7BFF177D20195584D5ADFDEA4D1EF9A51DD8904DF78981D`; matching ELF SHA-256 `4AE61B86EBDA603E03A63B6B08B48A4955ACCA179C7474AAB41518D9D4EAACFA`. It is an original-X4 ESP32-C3, 16 MB, SPI-array-enabled build. Its compile and link passed, but this exact official image has not been physically validated. Do not attribute experimental `_04` hardware results to it. The newer remote release documentation and README were retained unchanged during this documentation sync.

## BLE development lineage

| Candidate | Change and evidence | Preserved image identity |
|---|---|---|
| `20261119_02` | Automatic EPUB idle admission and sequential disconnect/render/reconnect path. XTC connected in physical testing. EPUB admission could fail when the available largest block was 12 bytes below its then-used requirement; that was a policy-composition issue, not evidence to keep lowering thresholds. | BIN `E4FAD1BD9398F8A57C4045B4729F4D0F6BFC916FE2A0A2132D3B3073D691F99E`; ELF `3BFB88E95F6B3B3D4ACE6A26F1173244C37651D6E9B0F708E4A731957F00A442`. |
| `20261119_03` | Separated idle admission from post-start/render requirements. The November 20 COM3 evidence showed three Yiser-J6 EPUB sessions, three forward commands, and worker teardown of about 190–221 ms. Rendering still crossed the 32 KiB runtime floor (observed 30,692, 27,240 and 27,936 B), and post-render admission did not run; this motivated proactive suspension and event-scoped recovery. | BIN `7392FFCA4A08AACE7EE0D9C2109E8CEC71355A875F073F5AB4E25EC4732F8164`; ELF `096DB36FBA67308DCBC72C027FA2756CACADADC5B6C1BE9C0564A47E4FF189D5`. |
| `20261120_04` | Proactively suspends BLE for unknown-demand EPUB work, waits for confirmed teardown, retains the mapped command, renders, releases eligible temporary memory, then rechecks admission at a completed recovery event. It reuses the existing ownership-aware cleanup stages, keeps their bounded/event-driven behavior, and does not change NimBLE pools or memory thresholds. | BIN `96AB90CC2AFFC9CCEB7C91C3961B1363C9F522C1D689071E035CF3D309533A6B`; ELF `D58544D1EC62FB049C7BC8BDA83A3B4E185EC085404190ECF5E8FABB145DF8F4`. Input-manifest SHA-256 `F54D7888F913291DD8E74B0221B012C881FFECF655E0D842E589A81ADFCC15C6` (3,591 source inputs, 1,445 dependency files; content manifest, not Git proof). |

The owner-reported `_04` physical sessions recorded ten captured/executed EPUB forward commands across tested sessions, eleven HID-ready events, repeated shutdown/render/cleanup/reconnect, chapter navigation, two mapped XTC actions, and no runtime-floor fault or panic/watchdog message in that capture. The 20-consecutive-turn target in one EPUB session remains unverified. The raw post-fix `_04` COM3 file and a cryptographic BIN-to-log binding are not present in the preserved validation folder; treat the hardware summary as owner-reported. The archived raw November 20 COM3 log is pre-fix `_03` evidence and must not be mislabeled as `_04`.

## Current memory and lifecycle policy

- General cold admission and the lightweight XTC/XTCH path use **98,304 B free / 36,852 B largest block**.
- EPUB idle admission uses **104,448 B free / 36,852 B largest block**; after startup, the fail-closed runtime floor is **32 KiB free / 16 KiB largest block**.
- The stricter **108,544 B free / 59,392 B largest block** requirement describes simultaneous BLE/rendering headroom. It must not be confused with the idle-only startup requirement.
- Progressive cleanup checks both free heap and largest block after each eligible existing tier and stops when both applicable requirements pass. Ownership checks, event-scoped attempt limits and cooldowns prevent repeated cache purges. Live EPUB page/section/pagination state is not disposable.
- With BLE OFF, BLE-specific admission, cleanup and reconnect policy must remain inert; independent Reader safety remains active. With BLE enabled, retain the connection for safe lightweight work, suspend before memory-intensive rendering when required, confirm worker-owned shutdown, execute the queued input once, then retry only at a meaningful recovery boundary.
- Continuous BLE throughout all EPUB rendering is not established. The measured and physically exercised approach is adaptive disconnect/render/reconnect. XTC continuous BLE has working experimental evidence; XTCH is still a separate physical validation item.
- Existing physical traces show roughly 66.5 KB of startup heap decrease and about 69.5 KB through HID readiness in the sampled settings path. These are measured footprint observations, not a complete allocation-owner breakdown. MSYS pools were not reduced; evidence did not justify speculative pool tuning.
- The original-X4 build enables SPI-array SD I/O. The shared implementation retains byte mode as a selectable fallback; X3 physical acceptance and its default profile remain separate validation questions. The published release contains no X3 firmware.

## Regression and performance evidence

The 2026-11-20 integrated build record reports successful C3 compilation/linking, 97/97 lifecycle contracts, 22/22 HID mapping tests, 5/5 BLE status tests, 9/9 achievement tests, 5/5 shelf-progress tests, 2/2 SDK patch tests, and 3/3 SPI-array contracts. The memory-policy suite had 40 passes and one import error because `NooirSdSpiDiagnostics.h` was absent in both compared trees. Native CMake/GoogleTest was not run because CMake was unavailable. These results do not replace hardware validation.

Earlier physical `20261118.3` samples recorded XTC mapped input to confirmed refresh at approximately 674–736 ms, fast refresh at 502–503 ms, and EPUB exit processing at approximately 2.62–2.67 seconds. These were observational samples, not controlled A/B benchmarks. Do not present them as measurements from `_04` or the clean-label release image.

## Migration and independent workstreams

As of November 24, D: is the authoritative development drive for the original X4 source. The complete legacy C: tree was preserved on D: and its recorded full-tree verification had zero hash mismatches (124,609 files; 9,914,179,724 bytes). The old C: directory was not removed because active Codex processes still had working directories there. Do not use C: for development; any final cleanup must wait until those processes exit. A separate November 19 candidate copy had a 5,969-file / 278,864,682-byte junction/cache discrepancy; that generated data was not claimed migrated. Do not treat that older candidate as a complete mirror or production source.

Keep these workstreams independent from the original-X4 firmware:

- **Web Emulator:** separate D: project and `codex/nooir-web-emulator-beta` branch. The existing notes leave PR #3 in draft and Pages deployment disabled; package/CI/browser acceptance gates remain separate.
- **X4 Pro / X4 Classic:** separate experimental ESP32-S3 port and branch `codex/nooir-x4pro-x4classic-s3-port-v1.6.7-20261124`. Original-X4 tests do not validate this port.
- **Reader Status Bar:** isolated November 18 experiment, not integrated into 1.6.7. Its 17-file patch SHA-256 is `D38FAC135E115B9E9F615D0E53A6C966F1FB09DAB334D5EF6A1FA626947C0638`; source-manifest SHA-256 is `F79196EC0549BF6B198FC4C1CA894BD5757780FCEA732FE3478AABB7DAA2D0A6`. Focused C3 object compiles exist; there is no full firmware link or physical visual acceptance. Its source snapshot lacked Git metadata, so it has no verified source commit. Keep it separate and do not cherry-pick its BLE-input changes blindly.

## Remaining validation and risks

The no-device Interrupt-WDT and partial NimBLE initialization rollback remain unresolved. Do not claim watchdog resolution or release stability from host tests. Remaining hardware gates include the 20-turn single-session EPUB test, backward turns, XTCH, BLE-OFF responsiveness, demanding EPUBs and section boundaries, exact official-image boot/Reader regression, sleep/wake, Reader exit and longer stability. X3 physical SPI-array acceptance is also not established. Preserve the successful `_04`, official release assets and all prior manifests; do not rebuild, flash, retag or publish as part of this record.
