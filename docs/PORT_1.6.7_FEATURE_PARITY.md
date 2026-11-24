# Folio Nooir 1.6.7 X4 Pro / X4 Classic S3 feature parity

Status: experimental port; source is based on the integrated Folio Nooir 1.6.7 revision `5696a555f9f741e8b80c7360d26a74129750610d`, which includes firmware source revision `ad0f269d13aed74d535c619588e1b3bde4f4ecbc`. The port commit adds S3 device profiles and compatibility guards; it does not transplant the original X4 C3 board configuration. Firmware candidates are not approved for physical flashing.

## Included application source

The shared Nooir 1.6.7 application and library code is inherited from the integrated 1.6.7 base. This includes the integrated Reader/EPUB, styling and Force Paragraph Indents, progress/persistence and cache work, recent/finished bookshelf behavior, Main and Folio Lite themes, Settings and Device Info, achievements, XTC/XTCH/CBZ readers, and existing Web UI. These features compile into both S3 profiles unless the device capability gates below exclude a hardware-specific path. The source lineage is verified; compilation does not by itself establish physical runtime parity for each feature.

## Device profiles and compiled capabilities

| Capability | X4 Pro S3 | X4 Classic S3 | Evidence / boundary |
| --- | --- | --- | --- |
| MCU | ESP32-S3 | ESP32-S3 | Matching build metadata; no physical boot performed |
| Flash | 16 MiB, DIO, 80 MHz | 16 MiB, DIO, 80 MHz | Build profile; retail module/bootloader compatibility still needs hardware verification |
| PSRAM | 8 MiB OPI configured | 8 MiB OPI configured | Pro QEMU allocator checks only; physical PSRAM training is open |
| FreeInk device identity | `X4PRO=1`, Classic off | `X4CLASSIC=1`, Pro off | Separate PlatformIO environments and runtime profile IDs |
| Touch | enabled | disabled | Compile-time capability; physical touch/input behavior unverified |
| SD | one-bit SDMMC through S3 SDK | one-bit SDMMC through S3 SDK | Pro QEMU virtual SDMMC mount passed; physical card/GPIO validation open; Classic has no firmware emulator |
| Framebuffer | PSRAM-backed (`FREEINK_FB_PSRAM=1`) | PSRAM-backed (`FREEINK_FB_PSRAM=1`) | Build configuration; physical allocation/refresh behavior open |
| BLE HID host | disabled | disabled | FreeInk S3 no-op compatibility implementation is linked; BLE/NimBLE is not enabled or tested |
| USB MSC | disabled | disabled | Compile-time capability off |
| Frontlight / warm light | disabled | disabled | Compile-time capabilities off; no claim of supported illumination hardware |
| OTA update | disabled for experimental device builds | disabled for experimental device builds | `NOOIR_EXPERIMENTAL_DEVICE_BUILD=1` makes OTA check/install return without network image selection or writes |
| SD firmware update / recovery write | disabled | disabled | `NOOIR_DISABLE_FIRMWARE_FLASHING=1` makes `FirmwareFlasher` validation and flash APIs return `FLASHING_DISABLED`; recovery UI is not a validated recovery path |

Both profiles retain the 1.6.6-derived partition table unchanged. The build's application slot is 6,553,600 bytes; the new app images fit at 86.9% (Pro) and 87.0% (Classic). Matching build/partition metadata is not evidence that a retail X4 Pro or Classic bootloader accepts this layout.

## Validation state

- **Firmware compilation:** both 1.6.7 S3 application builds succeeded and their ESP32-S3 image metadata, checksums, matching ELF identity, partition fit, and SHA-256 values are recorded in `PORT_HANDOFF.md` and `PORT_SOURCE_DELTA.json`.
- **Pro firmware in native QEMU:** exact Pro BIN/ELF ran on eink-qemu 0.9.2 `x4pro` with `uc8279-68`. Reader open, rendering, page keys, exit and persisted-progress reopen passed. QEMU is virtual evidence only; deep sleep/wake and physical hardware remain open.
- **Classic SDL2 host simulation:** host simulation runs application code and does not execute the Classic ESP32-S3 BIN. It cannot certify S3 startup, SDMMC, GPIO, panel, PSRAM, power, or electrical behavior. The latest 1.6.7 run result is recorded in the version-specific handoff; earlier 1.6.6 SDL2 passes remain historical.
- **Physical X4 Pro and Classic:** no board was flashed or tested. Both targets remain experimental and not physically validated.

## Update safety and physical bring-up

The public generic `firmware.bin` points at the released original-X4 C3 image. Experimental builds therefore keep OTA disabled, and SD flashing/recovery APIs return disabled. Do not remove these guards or use the experimental image until a separately validated backup/recovery procedure exists for the exact retail board and partition map.

A future hardware phase must independently verify board revision, bootloader and partition map, full backup/restore, serial boot, OPI PSRAM training, profile identity, display/controller and rotation, SDMMC wiring, Pro touch or Classic buttons, battery/rails/sleep/wake, sustained EPUB/CBZ/XTC/XTCH behavior, progress persistence, and safe recovery from interrupted updates. Firmware must remain unflashed until the owner separately approves the precise hardware test and recovery plan.