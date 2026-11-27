# X4 Pro — Backup and Recovery Readiness

**Status (2026-11-27): preparation only.** No physical device was connected, read, or written for this guide. No physical firmware writing is authorized. These notes do not establish that flashing is safe.

## Confirm the actual retail unit

The reviewed FreeInk S3 source describes an ESP32-S3, 16 MiB flash, 8 MiB OPI PSRAM, 800×480 display, GT911 touch, dual cool/warm frontlight, and panel variants SSD1677, UC8179, or UC8279. Reference display pins are SCLK12/MOSI11/CS13/DC18/RST14/BUSY6. I²C SDA39/SCL38 carries GT911 (0x5D, alternate 0x14), BM8563 RTC (0x51), and CW2017 gauge (0x63). GT911 uses INT10, reset4, active-low power2. Left/right/power keys are active-low GPIO0/7/3; Home is a GT911 status key. Cool/warm light outputs are GPIO8/9. 1-bit SDMMC is CLK41/CMD42/DAT0-40; card power GPIO5 is active-low after a short enable pulse; GPIO1 is the peripheral rail. Native S3 USB uses GPIO19/20.

These are SDK source-level facts, not inspection of a retail Pro. Photograph its external label and screen version; record serial privately, visible revision marks without opening it, stock version, reported panel ID, connector/dock type, host USB VID/PID, and serial port. Published guidance differs about USB-C versus pogo/data-dock access. Do not force an adapter or assume a connector carries data.

## Read-only backup preparation

1. Preserve the removable SD separately using a read-only copy/image workflow to a new D: evidence folder. Hash the result. Do not format or repair the only card; use an expendable test card later.
2. With the exact verified Pro data path, record esptool version and read-only `chip-id`, `flash-id`, and `get-security-info` results. Confirm chip, actual flash capacity, and security state. Do not use erase, write, unlock, eFuse, security-configuration, `--force`, or flash-status write commands. If access is denied or secure-download restrictions appear, stop without changing security or running an unlocker.
3. If read access is supported, confirm size first and capture one raw read from address 0 to `ALL` using the installed esptool version’s `read-flash` syntax. A confirmed 16 MiB unit should yield 16,777,216 bytes. Save only a new Pro-specific file under D:. Do not pair the read with any write/erase. If encrypted-flash restrictions prevent reading, stop; do not bypass them.
4. Record SHA-256, length, date, model/revision, tool version, port, flash ID, security output, and transcript. Keep two protected D: copies. Treat the dump as confidential; it may include encrypted device contents, credentials, settings, and user state.
5. Inspect headers and partition metadata offline only. A full-size dump and valid-looking table do not prove bootability or restorability.

The experimental S3 build retains the v1.6.6 partition CSV (app slots at 0x10000 and 0x650000). The SDK example layout differs; its reported offsets do not prove this Pro’s OEM layout. Never guess offsets. Espressif separates `read-flash`/`flash-id` and security-info reads from writing/security-changing commands; follow the installed version’s official [basic commands](https://docs.espressif.com/projects/esptool/en/latest/esp32s3/esptool/basic-commands.html), [advanced commands](https://docs.espressif.com/projects/esptool/en/latest/esp32s3/esptool/advanced-commands.html), and [ESP32-S3 security notes](https://docs.espressif.com/projects/esp-idf/en/v5.3.2/esp32s3/security/security.html).

## Recovery decision — not validated

No exact public OEM Pro full-stock image or independent restoration method has been verified for this unit. CrossPoint’s install guide and Inx Pro programming notes are community references, not OEM or Nooir recovery certification. A Down-key SD picker, if available, is application-level only and cannot be assumed to recover a damaged application or boot chain.

**No device-write command is included.** Before any future write is separately approved, require: exact Pro board/panel revision, verified connector/data path, flash/security state and OEM layout; a device-matched stock image or proven full backup; an independent recovery route validated on an identical spare/sacrificial unit; verified data backups; stable power and serial capture; and a reviewed target/offset procedure. If the app fails to boot, stop. Do not assume USB-C, pogo, an unlocker, SD picker, or browser updater will recover it. Confirm a documented Pro ROM/USB-Serial-JTAG path first; if unavailable or blocked, stop for board-specific research.

The QEMU 16 MiB image is **emulator-only and never for a physical device**. The preserved app-only BIN is not a full backup. With OTA/SD updates disabled and recovery unvalidated, the Pro BIN is not physically flash-ready.

## Sources

- Reviewed FreeInk SDK snapshot: [X4 Pro hardware reference](https://github.com/Free-Ink/freeink-sdk/blob/c9c3488a048e817078de4809cb27b49e734dc0f3/docs/xteink-x4pro-support.md).
- [CrossPoint installation guide](https://crosspoint.kr/en/install) and [Inx Pro programming notes](https://github.com/obijuankenobiii/inx-pro) are community references, not validated Nooir recovery procedures.
- [Xteink official firmware page](https://www.xteink.com/pages/firmware). A matching stock restore package for this retail revision remains unconfirmed.
