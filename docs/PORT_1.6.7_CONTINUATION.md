# Folio Nooir S3 1.6.7 — Continuation Notes

**Updated:** 2026-11-27. **State:** hardware-readiness and parity work resumed for documentation/research. Physical writes remain unauthorized. No firmware rebuild, simulator rerun, or physical test was performed in this continuation.

## Authoritative project state

- Workspace: `D:\fatiha\side\_worktrees\nooir-s3-167-upgrade-20261124`
- Branch: `codex/nooir-x4pro-x4classic-s3-port-v1.6.7-20261124`
- Base HEAD for this docs-only continuation: `315840fdb4b22ef3709a317f75f98703f2ae407d`
- D: output/evidence roots remain `D:\p\run3`, `D:\p\run4`, and `D:\p\run5`; all preserved. The generated build cache at `D:\fatiha\side\_builds\nooir-s3-v1.6.7-20261124\build-cache` is untracked and must be preserved/excluded.
- Current worktree before this continuation had no tracked source diff. The only untracked item was the cache. Keep any unrelated workspace/release, Web Emulator, Status Bar, and MihonNooir files out of this branch.

## Certified run3 artifact checkpoint (preserve unchanged)

| Device | BIN SHA-256 | ELF SHA-256 | Output |
|---|---|---|---|
| X4 Pro | `03dfc9e157df4d18adb864d87e2bc5a05df34c76e5e77a2304e166f156ba6002` | `3364471f3d65a2a745221a7a31dcae865f1751ed7f166c0c3af849434ce7bb8d` | `D:\p\run3\artifacts\FolioNooir_1.6.7-X4Pro-Experimental.{bin,elf}` |
| X4 Classic | `9fdee5dbd9c5fe9d3bd355627cd74773e71f3ab564d56b2aa39894e7df86ff6b` | `3bc85fdf23f04f9df6d047a42b1509b477f3ec5b4a46aea94b74ddcde12354a7` | `D:\p\run3\artifacts\FolioNooir_1.6.7-X4Classic-Experimental.{bin,elf}` |

Both builds previously succeeded with PlatformIO 6.1.19, Espressif32 55.03.37, Arduino-ESP32 3.3.7 / ESP-IDF 5.5.2, Xtensa 14.2.0+20251107, 16 MiB DIO flash, 8 MiB OPI PSRAM and the v1.6.6 partition CSV. This continuation re-hashed all four files; hashes matched. No rebuild or overwrite occurred. These are app-only experimental images, not cleared for physical flashing.

## Verified software evidence — do not repeat without a reason

- Exact Pro run3 BIN in eink-qemu 0.9.2, machine `x4pro`, panel `uc8279-68`, known 50,000 ms virtual Power-hold workaround: Reader flow passed 30/30; separate cold QEMU process reopened saved progress and passed 14/14. Exact logs, metrics, screenshots and hashes are in `D:\p\run4\qemu\pro-run3-reader-reopen-fast` and `...\pro-run3-progress-reopen-after-cold-boot`. The older 27/27 is historical and refers to an earlier BIN.
- Run3 QEMU measured Reader page-render median 1,323 ms (samples 2,132/1,310/1,323), BW median 44 ms, 23 panel refreshes in 12,257 ms; QEMU showed about 7.69 MiB allocatable SPIRAM and zero corrupt blocks. These are QEMU baselines, not physical throughput or PSRAM training. Internal heap minima and stack high-water marks were not recorded as comparable baseline metrics.
- USB Drive host harness passed against the activity control flow on 2026-11-26; preserved at `D:\p\run5\logs\usb-drive-activity-host-20261126-final.log`, SHA-256 `4e065e8ea0eb08c44c8b9e571a2e8ca201a51bde095cfc93881fbbb8e41effd0`. It uses stubs and does not test S3 execution, TinyUSB, host transfers, or real SDMMC ownership.
- Classic SDL2 host simulator earlier passed Home/Library, EPUB open, page navigation, Reader exit, and progress-write flow. It is not the Classic S3 BIN; there is no Classic QEMU. No Classic retest was run.
- No physical testing, firmware writes, QEMU reruns, Classic simulator reruns, or firmware builds were performed in this continuation.

## Current implementation and upstream review

The S3 profiles keep Pro and Classic definitions separate. Pro enables GT911 touch, frontlight/warm light and USB MSC; Classic disables touch/light and enables USB MSC. Both request OPI PSRAM and native 1-bit SDMMC. Firmware update routes are disabled in the experimental profiles: OTA check/install return `NO_UPDATE`; the SD FirmwareFlasher APIs are blocked by `NOOIR_DISABLE_FIRMWARE_FLASHING=1`. This is a software write guard, not a recovery system.

The branch’s local `freeink-sdk-s3` submodule remains pinned at `995d9bd523b9c45b18710bca17a3158ce724a7f4`. Current upstream source references reviewed on 2026-11-27:

- CrossPoint Reader `fb72db9614fd474153edb0a58ec78ed0f6b7e8c2`, SDK submodule `de19f154ef0963879592f3baf6da68606122797c`.
- CrossInk `d53849e1c620b7d600d9fe02b544fdf1534d4f51`, SDK submodule `f7fa9b6871192ea27d8aa37a03beaa749462c3ab`.
- Inx Pro `c5a6c7b75e9c6fbd8ce9741e6accd519ab6032e0`, SDK submodule `c996d1b90d5725519f3e479eed99c592c5b081b7`.
- FreeInk SDK main reviewed at `57cd928e7e2e59c109b4389d708e24633be8c7e5`.

These references are MIT licensed. No upstream source was copied in this docs-only continuation. Preserve their notices and attribution if code is imported later. Links and feature comparison are in the updated parity matrix and handoff.

## Open gaps and implementation decisions

- Runtime TTF/OTF is not implemented. Existing firmware reads converted `.cpfont`; FreeType appears in the host conversion pipeline. Do not add runtime parsing/shaping/cache work without a bounded PSRAM/DRAM design, font-license review, same-fixture performance comparison and regression coverage.
- S3 BLE HID is disabled (`FREEINK_CAP_BLE_HID_HOST=0`; BLE excluded in the S3 profile). Do not enable by analogy to C3. First measure BLE/Wi-Fi/USB/Reader coexistence, heap/stack, connection loss and power on the S3.
- USB MSC code and host stubs are present and run3 builds succeeded. Do not repeat/reimplement it absent a concrete defect. TinyUSB enumeration, host transfers, safe eject, real cache release, SDMMC ownership and recovery remain physical-only gaps.
- Battery/charge, RTC, Classic IMU/tilt, orientation, panel variation, Pro touch/frontlight, SDMMC, physical PSRAM, USB, sleep/wake and current draw are source- or SDK-supported to varying degrees but not validated on either Nooir device. Checklists mark these separately.
- No source change was justified by current evidence. Preserve reading stability and the existing BIN/ELF pairs; no build was run.

## Restart priority

1. Identify the exact retail Classic and Pro hardware revisions, panel, connection method, flash/security state and OEM partition maps with read-only collection.
2. Back up SD/user data and internal flash where read access permits; keep confidential D: copies. Validate an independent stock/ROM recovery route on identical spare hardware. Until then, neither BIN is physically flash-ready.
3. Use the model-specific checklists with serial logging and expendable cards only after a separately approved hardware session and recovery gate.
4. Capture physical PSRAM/heap/stack, SDMMC, display, controls, power, USB/eject and persistence baselines separately for each device.
5. Only then consider discrete source changes: first hardware-proven fixes; next font runtime or BLE as isolated opt-in experiments with benchmarks and full reader-format regression.
6. Preserve all earlier artifact/evidence paths. Any justified rebuild gets a new directory/name and hashes; never overwrite run3.

No merge, public release, deployment, physical write, or unrelated project work is part of this checkpoint.
