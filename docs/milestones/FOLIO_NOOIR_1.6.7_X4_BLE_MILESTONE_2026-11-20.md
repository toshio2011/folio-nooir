# Folio Nooir 1.6.7 X4 BLE milestone — 20 November 2026

## Protected _04 artifacts

- BIN: `FolioNooir_1.6.7-exp-X4-SeqBLE-20261120_04.bin`
- BIN SHA-256: `96AB90CC2AFFC9CCEB7C91C3961B1363C9F522C1D689071E035CF3D309533A6B`
- ELF: `FolioNooir_1.6.7-exp-X4-SeqBLE-20261120_04.elf`
- ELF SHA-256: `D58544D1EC62FB049C7BC8BDA83A3B4E185EC085404190ECF5E8FABB145DF8F4`
- Size: 6,173,776-byte BIN; 88,805,580-byte ELF.
- Resolved build profile: `gh_release_nooir167_seqble_x4_20261120_04`; original-X4 ESP32-C3; SPI-array enabled.
- _04 input manifest SHA-256: `F54D7888F913291DD8E74B0221B012C881FFECF655E0D842E589A81ADFCC15C6`; 3,591 source inputs and 1,445 external dependency files. The isolated snapshot has content-manifest provenance, not a verified source commit. Its embedded `b846e155` text is not commit proof.

The artifacts remain in the protected local `_04` archive under `artifacts/20261120_04`. The integration worktree does not contain or overwrite the binaries.

## Physical result reported by the owner

The owner reports that the original X4 physically ran _04 and completed ten captured/executed EPUB forward commands across tested sessions, with eleven HID-ready events, repeated BLE shutdown/render/cleanup/reconnect, chapter navigation, no observed runtime-floor faults or panic/watchdog messages in that run, two mapped XTC forward actions, and sleep cleanup/BLE shutdown. This establishes meaningful physical progress. It does not establish the 20-turn single-session acceptance target, backward turns, XTCH, X3, BLE-OFF performance, demanding EPUB behavior or long-duration stability.

## Raw capture provenance

The saved file the archived November 20 pre-fix COM3 file in the _04 validation folder is preserved from the earlier run. Its three `runtime_floor_fault` events and content match the pre-fix _03 investigation; it is not the raw post-fix _04 capture and must not be presented as such. The newer _04 physical results above are recorded from the owner's supplied milestone report. That report does not include a raw COM3 attachment or an embedded BIN hash, so the report-to-image identity is owner-reported rather than cryptographically proven.

## Development lineage

- **_02 — sequential EPUB BLE:** automatic idle admission and worker-owned asynchronous shutdown were introduced. XTC connected; EPUB's largest block fell short of the then-current threshold by 12 bytes.
- **_03 — phase-aware admission:** separated idle startup from post-start/runtime requirements; physically connected and received EPUB HID input. Pre-fix physical evidence showed below-floor rendering and missing readmission.
- **_04 — proactive suspension and recovery:** pre-suspends BLE for unknown-demand EPUB work, waits for confirmed teardown, retains the command, renders, then rearms bounded cleanup/readmission on a completed recovery event. Existing ownership-aware cleanup, thresholds, NimBLE pools, SPI-array path and BLE-OFF early behavior remain.

## Continuing safety gates

The no-device watchdog and partial NimBLE initialization rollback remain unresolved. Do not call 1.6.7 release-stable until those risks receive explicit assessment and the final release image is physically validated.
