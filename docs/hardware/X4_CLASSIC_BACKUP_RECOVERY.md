# X4 Classic — Backup and Recovery Readiness

**Status (2026-11-27): preparation only.** No physical device was connected, read, or written for this guide. No physical firmware writing is authorized. These notes do not establish that flashing is safe.

## Confirm the actual retail unit

The reviewed FreeInk S3 source describes an ESP32-S3, 16 MiB flash, 8 MiB OPI PSRAM, 800×480 display, board tags `ESP32S3_X4_CLA` / `ESP32S3_X4R2_CLA`, and panel variants SSD1677, UC8179, or UC8279. Classic is buttons-only with no touch or frontlight. The reference maps active-low keys to left GPIO0, right GPIO7, power GPIO3, front-left GPIO5, front-right GPIO2, confirm GPIO8, back GPIO9; GPIO4 is unused. It describes BM8563 RTC (I²C 0x51), CW2017 gauge (0x63), QMI8658 IMU (0x6B or 0x6A), charge status GPIO21, and 1-bit SDMMC CLK41/CMD42/DAT0-40. Card power is GPIO6 (active-low after a short enable pulse); GPIO1 is the peripheral rail. Native S3 USB uses GPIO19/20.

Those are SDK source-level facts, not a reading of the available device. Photograph its external model/revision label and screen version; note serial privately, board marks visible without opening it, reported panel ID, stock version, connector/dock type, USB VID/PID, and serial port. Panel revision and the data-capable connector path remain unconfirmed. Do not force an adapter or assume the connector carries data.

## Read-only backup preparation

1. Keep the user SD card separate from internal flash. Use a read-only copy/image workflow to a new D: evidence folder, hash the result, and do not format or repair the only card. Use a separate expendable test card later.
2. With a verified Classic data path, record esptool version and read-only `chip-id`, `flash-id`, and `get-security-info` results. These should identify target, actual flash size, and security mode. Do not use erase, write, unlock, eFuse, security-configuration, `--force`, or flash-status write commands. If access is denied or reports secure-download restrictions, stop without changing security.
3. If read access is supported, confirm size first and capture one raw read from address 0 to `ALL` using the installed esptool version’s `read-flash` syntax. For a confirmed 16 MiB unit the output should be 16,777,216 bytes. Write only a new device-specific file under D:. Do not pair the read with any write or erase. If encrypted-flash restrictions prevent reading, stop; do not bypass them.
4. Record SHA-256, length, date, model/revision, tool version, port, flash ID, security output, and transcript. Keep two protected D: copies. Treat the dump as confidential because it can include device-specific encrypted contents, credentials, settings, and user state.
5. Inspect headers and partition metadata offline only. A full-size dump, checksum, or plausible table does not prove that the image boots or is restorable.

The experimental S3 build retains the v1.6.6 partition CSV (app slots at 0x10000 and 0x650000). The FreeInk example layout differs, and neither proves the OEM partition map. Do not guess offsets. Espressif separates `read-flash`/`flash-id` and security-info reads from flash-writing/security-changing commands; follow the installed version’s official [basic commands](https://docs.espressif.com/projects/esptool/en/latest/esp32s3/esptool/basic-commands.html), [advanced commands](https://docs.espressif.com/projects/esptool/en/latest/esp32s3/esptool/advanced-commands.html), and [ESP32-S3 security notes](https://docs.espressif.com/projects/esp-idf/en/v5.3.2/esp32s3/security/security.html).

## Recovery decision — not validated

No exact public OEM Classic full-stock image or independently tested restoration path is verified for this retail unit. CrossPoint’s community guide discusses full-flash backup/restore and a Down-key SD picker, but the picker is application-level and cannot recover a broken app/boot chain. This does not certify Nooir recovery.

**No device-write command is included.** Before any future write is separately approved, require: exact unit/revision, flash/security state and OEM layout; a device-matched stock image or proven full backup; an independent recovery route validated on an identical spare/sacrificial unit; verified data backups; stable power and serial capture; and a reviewed target/offset procedure. If the app fails to boot, stop. Do not assume the SD picker, Pro pogo dock, random key sequence, or browser updater can recover it. First confirm a documented Classic ROM/data path. If ROM mode is unavailable or security blocks access, stop for board-specific service research.

The QEMU 16 MiB image is **emulator-only and never for a physical device**. The preserved app-only experimental BIN is not a full backup. With OTA/SD updates disabled and recovery unvalidated, the Classic BIN is not physically flash-ready.

## Sources

- Reviewed FreeInk SDK snapshot: [X4 Classic hardware reference](https://github.com/Free-Ink/freeink-sdk/blob/e7e286d59f9538b1c2f794875a71e9bee2201466/docs/xteink-x4c-support.md).
- [CrossPoint installation guide](https://crosspoint.kr/en/install) is community guidance, not validated for this Nooir build or exact unit.
- [Xteink official firmware page](https://www.xteink.com/pages/firmware). A matching stock recovery package for this retail revision remains unconfirmed.
