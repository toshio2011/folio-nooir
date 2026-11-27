# X4 Classic — Physical Acceptance Checklist

**Template only — 2026-11-27. No Classic hardware tests were run for this checkpoint.** Do not begin firmware writing: the experimental build has OTA/SD update paths disabled, and an independent backup/recovery route is not validated. Complete the companion [backup and recovery readiness guide](X4_CLASSIC_BACKUP_RECOVERY.md) first; any physical write needs separate approval.

## Record before testing

- Date/operator; device model, serial kept private, visible board revision; original firmware version.
- Detected panel/controller and orientation; USB/data connector and dock; flash size and read-only security information.
- Stock partition map; backup folder on D:, raw dump size/hash, SD image/hash, and independent recovery status.
- Exact firmware target/build ID/BIN and ELF SHA-256, partition CSV hash, toolchain, serial capture path, test card and fixture hashes.
- Start condition, power source, observed boot current/temperature if safely measurable, and any warnings.

**Gate 0:** stop unless exact unit and panel are identified, backups exist, and an independent recovery method has been validated for an identical board. Use a dedicated expendable SD card. Do not use a QEMU flash image.

## Acceptance sequence

| # | Test | Pass evidence / notes |
|---|---|---|
| 1 | Serial startup and board identity | Capture cold boot; expected X4 Classic profile, no panic/reboot loop, identify actual panel revision. |
| 2 | Memory | Confirm OPI PSRAM initialization, framebuffer allocation, minimum free internal heap/PSRAM and task stack watermarks under library and reader load. No allocation failure, corruption, or watchdog. |
| 3 | Display | Home/Library/Reader over multiple cold starts; full and partial refresh, rotation, grayscale/ghosting checks, no stale blocks, clipping, or rail instability. Record panel ID and timings. |
| 4 | SDMMC | Repeated cold mount/unmount of test card, directory scans, large and fragmented books, deterministic file hashes, sustained EPUB/XTC/XTCH/CBZ reads. Confirm native 1-bit SDMMC and card-power sequencing; no SPI fallback assumption. |
| 5 | Physical keys | Test all eight controls separately: left GPIO0, right GPIO7, power GPIO3, front-left GPIO5, front-right GPIO2, confirm GPIO8, back GPIO9. Check active-low polarity, debouncing, short/long holds, repeat, orientation-aware mapping, sleep/wake. Touch must remain disabled; no frontlight expected. |
| 6 | Library and reader | Open representative EPUB, XTC, XTCH and CBZ; next/previous using controls; back/exit; confirm focus, shelves, error recovery, no freezes. Repeat navigation at low and high heap pressure. |
| 7 | Progress | Turn pages, exit normally, cold power cycle, reopen; verify saved location and deterministic persistence over repeated cycles. Preserve before/after state and logs. |
| 8 | USB Drive / MSC | With test-card backup and checksums, enumerate on host; copy files both directions using a scratch directory; safe-eject; verify hashes; return to firmware and remount. Check no simultaneous SDMMC ownership, corruption, stale cache, or duplicate teardown. No firmware files. |
| 9 | RTC, battery and charging | Compare clock and battery state after cold start, charging and unplugging; verify charge status/USB behavior against known meter/host state. Do not infer calibration from a percentage alone. |
| 10 | Tilt/orientation, if supported | Confirm QMI8658 detection and orientation in device info. Only enable tilt page-turning if physical axis, debounce and accidental-motion checks pass. Otherwise keep feature disabled. |
| 11 | Sleep and wake | Repeated sleep/wake through each supported button; verify rail sequencing, display state, RTC, SD remount, settings/progress, no deep-sleep loops. Measure sleep current only with appropriate safe equipment. |
| 12 | Long run and regression | Run a representative reading session and repeated library/reader/USB transitions. Capture min heap and stack watermarks; compare timed navigation/rendering to same-device baseline. No crash, data loss, unresponsive control, or increasing resource leak. |

## Evidence and stop rules

Record each row as **PASS / FAIL / NOT RUN**, link raw serial logs/screenshots under D:, and hash evidence files. A host SDL2 replay is not a pass for any hardware row. No Classic QEMU machine is available. Stop on unexpected resets, memory corruption, SD write errors, unsafe temperature/current, battery anomaly, panel rail instability, or any mismatch between the image target and board identity. Restore testing itself is not authorized by this template; only perform it under a separately approved, independently validated procedure.
