# Folio Nooir 1.6.7 integrated X4 validation build — 20 November 2026

## Identity

- Purpose: clean-label, experimental original-X4 validation image; not a public release.
- Source branch: `codex/folio-nooir-1.6.7-integrated-20261120`.
- Exact firmware source commit: `ad0f269d13aed74d535c619588e1b3bde4f4ecbc`.
- PIO environment: `gh_release_nooir167_official_x4_20261120`.
- Board configuration: ESP32-C3 DevKitM-1 profile used by the original X4, 16 MB flash, DIO, 80 MHz.
- Embedded app version: `1.6.7+ad0f269d`; embedded CrossPoint compatibility label: `1.6.7`.
- Embedded compile time: 2026-11-20 17:02:35 local time.
- This image has not been flashed or physically tested. Do not attribute the `_04` hardware observations to this image.

## Output files

- BIN: `FolioNooir_1.6.7-X4-integrated-validation-20261120-ad0f269d.bin`
  - Size: 6,158,320 bytes.
  - SHA-256: `FA8F2DD28250887BE7BFF177D20195584D5ADFDEA4D1EF9A51DD8904DF78981D`.
- Matching ELF: `FolioNooir_1.6.7-X4-integrated-validation-20261120-ad0f269d.elf`
  - Size: 88,750,420 bytes.
  - SHA-256: `4AE61B86EBDA603E03A63B6B08B48A4955ACCA179C7474AAB41518D9D4EAACFA`.
- Local output directory: `_builds/FolioNooir_1.6.7_X4_integrated_validation_20261120` on D:.
- The BIN header reports the same ELF SHA-256 as the delivered ELF. `esptool v5.1.2 image-info` identifies a valid ESP32-C3 image with valid checksum and validation hash.
- The BIN and ELF are outside Git; no firmware image is tracked or committed.

## Build and dependency inputs

- Full command: `pio run -e gh_release_nooir167_official_x4_20261120 -v`.
- `PATH` began with the verified C3 `toolchain-riscv32-esp/bin`; `PLATFORMIO_CORE_DIR` pointed to the verified D: PlatformIO core; `PYTHONUTF8=1`. Exact local paths are retained in the private build output directory.
- PlatformIO Core 6.1.19; Espressif platform 55.03.37; Arduino-ESP32 3.3.7; framework libraries 5.5.0+sha.87912cd291; ESP-IDF 5.5.2-729-g87912cd291.
- RISC-V compiler 14.2.0+20251107; `riscv32-esp-elf-g++.exe` SHA-256 `6455D6532475E826F78551EC335E94EECEA4B740A989ACA39E5A4683DF353717`.
- esptool 5.1.2.
- FreeInk SDK base commit `958720659ea289ae325e83db20049d0ea844800d`; Nooir patch `patches/freeink-sdk/nooir-1.6.7_20261120_04.patch`, SHA-256 `3D2E37AC22CF8727BD1E6AFF6431617A3F60B071E972A2D01E7857EC048B191F`.
- NimBLE-Arduino is pinned to 2.5.1, matching `_04`. A hash comparison of the 1,445 cached PlatformIO library files found all library content identical to `_04` except generated `integrity.dat` metadata. Other resolved versions include SdFat 2.3.1, ArduinoJson 7.4.2, QRCode 0.0.1, PNGdec 1.1.6, JPEGDEC 1.2.7 at `86282979224c8a32fd51e091ed5a35b0c699a52b`, WebSockets 2.7.3 and Arduino-wolfSSL 5.7.2.
- `platformio.local.ini` was absent; no local override was loaded.
- Input provenance: committed source tree at the commit above; `_04` overlay manifest SHA-256 `6433C6FE3A14ED76DB23EC77BB75FEEF211DEB3D6F0B277856016FC95EAC7757`; original `_04` input manifest SHA-256 `F54D7888F913291DD8E74B0221B012C881FFECF655E0D842E589A81ADFCC15C6` (3,591 source inputs, 1,445 external dependency files; content provenance, not Git provenance).

## Build result and configuration

- Full C3 compilation, linking and ESP32-C3 image generation completed successfully. The final unique-output PlatformIO run ended `SUCCESS`; it used `NOOIR_FIRMWARE_OUTPUT` so the project copy hook wrote a new path without overwriting an existing artifact.
- Static RAM: 65,980 / 327,680 bytes (20.1%).
- App flash use: 6,144,165 / 6,553,600 bytes (93.8%); app slot is `0x640000` bytes at `0x10000`, leaving 409,435 bytes by the linker report. The raw BIN is 6,158,320 bytes and fits the slot.
- `CROSSPOINT_VERSION` redefinition warnings: zero. Release-only raw HID, BLE resource/lifecycle and Reader-transition diagnostic flags are undefined; SPI-array, IINE HID, adaptive EPUB disconnect-render-reconnect behavior and existing memory thresholds/pools remain enabled.
- The full compile emitted existing warnings (wolfSSL macro redefinitions, deprecated `NetworkClient::flush`, volatile increment, `Rect` explicit-constructor conversion, and the Windows long-path advisory). No compiler/linker errors remained.

## Superseded intermediate outputs

- The first linked output at source commit `f7c5ba31874b02838de037571ef73830dff5efc2` used NimBLE-Arduino 2.5.2 because the prior dependency declaration was a caret range. Its BIN SHA-256 is `2E2E440B4F77ADFB345F8406A5974AC133D1B0AB7DC625B98B719CBBCB94A2E3`; ELF SHA-256 is `DB786B518EDD0C8B0E0C12CD5F7D0026A42C358809A4B3B4B0087D2B7333A6FE`. It is preserved for traceability but is not the accepted validation image and must not be flashed.
- An initial macro-ordering build failed compiling `HalSystem.cpp`; it produced no accepted image. The source-profile correction is committed at `f7c5ba31` and the exact NimBLE pin at `ad0f269d`.
- The exact-2.5.1 compile/link attempt first hit the artifact-copy guard because `artifacts/firmware.bin` already existed; the guard prevented overwrite. The final successful PIO run used a unique output path. A staging BIN from before that final image regeneration is retained locally only as a noncanonical staging artifact.

## Integration and validation status

The cumulative source inventory and branch tests are in [the cumulative audit](NOOIR_1.6.7_CUMULATIVE_AUDIT_2026-11-20.md). Host/source results include lifecycle contracts 97/97, HID mapping 22/22, BLE status/usability 5/5, Achievements 9/9, shelf progress 5/5, SDK patch 2/2, and SPI-array contract 3/3. The memory-policy suite had 40 passes and one import error because `lib/hal/NooirSdSpiDiagnostics.h` is absent in both baseline and candidate. Native CMake/GoogleTest was not run because CMake is unavailable. These are not hardware results.

The protected `_04` BIN/ELF remain unchanged. Its physical results are owner-reported; the raw post-fix COM3 capture and exact BIN-to-log hash binding were not supplied. No-device Interrupt-WDT and partial NimBLE initialization rollback remain unresolved. The new integrated image still requires approved original-X4 physical validation, including the 20-consecutive-turn single-session goal, backward turns, chapter boundary, IINE, XTC/XTCH, BLE-OFF, profiles, Reader exit, sleep/wake and stability.