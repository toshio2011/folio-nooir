# X4 Pro — Physical Acceptance Checklist

**Template only — 2026-11-27. No Pro hardware tests were run for this checkpoint.** Do not begin firmware writing: the experimental build has OTA/SD update paths disabled, and an independent backup/recovery route is not validated. Complete the companion [backup and recovery readiness guide](X4_PRO_BACKUP_RECOVERY.md) first; any physical write needs separate approval.

## Record before testing

- Date/operator; model, serial kept private, board revision; original firmware version.
- Panel/controller, touch-controller identity, visible revision, frontlight channels; USB/data connector or pogo dock path.
- Flash size and security info; OEM partition map; backup location/hashes; verified independent recovery status.
- Exact firmware target/build ID/BIN and ELF SHA-256, partition CSV hash, toolchain, serial capture path, test card and fixture hashes.
- Stable power, initial battery state, observed current/temperature if safely measurable, and warnings.

**Gate 0:** stop unless exact Pro unit/revision and panel are identified, backups exist, connector/data path is verified, and independent recovery has been validated for an identical board. Use an expendable SD card. Never use the QEMU flash image.

## Acceptance sequence

| # | Test | Pass evidence / notes |
|---|---|---|
| 1 | Serial startup and board profile | Cold boot log identifies X4 Pro, shows app entry and detected panel; no panic or reboot loop. |
| 2 | PSRAM and heap | Confirm OPI PSRAM initialization, framebuffer allocation, free internal/PSRAM heap and task stack watermarks at idle, library, Reader and USB transitions. No corruption or allocation failures. |
| 3 | Display and panel | Home, Library and Reader across multiple starts; full/partial refresh, grayscale, ghosting, orientation, panel busy timing, no clipping or rail faults. Repeat only for the detected panel type. |
| 4 | SDMMC | Repeated cold mounts of test card, directory scans, large/fragmented books and hash-verified reads; verify native 1-bit SDMMC and GPIO5 power sequencing. No SPI fallback assumption, card loss or corruption. |
| 5 | Physical buttons | Verify active-low left GPIO0, right GPIO7 and power GPIO3; short/long press, repeat, wake and orientation mapping. |
| 6 | GT911 touch and Home key | Verify controller ID, coordinate orientation and capacitive Home status; test taps and swipes in each orientation, edge zones, debounce, accidental touches and sleep/wake reset. Test reader gestures for EPUB, XTC, XTCH and CBZ; confirm no lost input or stuck touch. |
| 7 | Frontlight and warmth | Sweep cool GPIO8 and warm GPIO9 independently and mixed at low/mid/high levels. Check polarity, flicker, warmth order, persistence, off state, settings restore, USB/Wi-Fi transitions, sleep and wake; measure battery impact. |
| 8 | Library and reading | Open EPUB, XTC, XTCH and CBZ; next/previous navigation by touch and buttons; reader exit and re-entry; page rendering, partial refresh, bookmarks and error handling. |
| 9 | Progress persistence | Turn pages, exit, cold power cycle and reopen at saved position. Repeat; record progress files/state and verify no corruption. |
| 10 | USB Drive / MSC | Back up test SD first. Host enumerate, scratch file copy in both directions, safe eject, hash verification, application ownership return and remount. Check exclusive SDMMC access, font/cache release, no stale reads or duplicate teardown. No firmware files. |
| 11 | RTC, battery and charging | Compare clock and gauge through cold boot, charge/unplug and USB session. Validate charging indication against known power state; do not treat gauge percentage as calibrated without comparison. |
| 12 | Deep sleep and wake | Test supported power-button, touch, RTC and other wake sources over repeated cycles. Verify GT911/frontlight/SD rails, RTC/progress retention, mount recovery and current draw. QEMU does not cover this. |
| 13 | Long run and performance | Representative long reading session and repeated library/Reader/USB/light transitions. Record heap minima, stack watermarks, render and input latency, temperature/current if available; compare to the same-device baseline. No crashes, leaks, input delay, or data loss. |

## Evidence and stop rules

Mark each row **PASS / FAIL / NOT RUN**. Save raw serial logs, screen photos, panel IDs, timing and memory data under D: and hash them. The run3 native QEMU result is software/emulator evidence, not physical proof; its virtual SD, virtual panel, heap and timing do not certify this Pro. Stop on unexpected reset, touch lockup, memory corruption, SD write errors, unstable light/rail, unsafe temperature/current, or target/profile mismatch. Recovery writing is not authorized by this checklist; it requires a separate approved, independently validated procedure.
