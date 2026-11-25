# Folio Nooir 1.6.7 — X4 Pro / X4 Classic S3 parity checkpoint

**Status: PAUSED — end-of-day 2026-11-25 checkpoint; no physical validation.** The 2026-11-24 firmware/QEMU/SDL2 results below are historical evidence for the preserved earlier artifact/source snapshot. The later feature-parity source has not passed a complete ESP32-S3 build; today's Pro build stopped at the StorageLock compile error recorded below. Reference revisions: CrossPoint Reader `92e8714a332c2c1903ea93290cf0b9b4f6f6697c` with FreeInk SDK `995d9bd523b9c45b18710bca17a3158ce724a7f4`; CrossInk `9914146eeae7b46b300f475a16c32426fc02ec1f` with FreeInk SDK `699370183fa3a0e33c9cb83a36f701bbb6022095`.

| Model / capability | CrossPoint reference | CrossInk reference | Folio Nooir 1.6.7 S3 status / evidence |
|---|---|---|---|
| Pro profile / display | X4 Pro S3 profile, 800×480 controller variants, touch/frontlight pins | Pro profile and panel/touch initialization | Separate `X4PRO` profile; profile/display paths compile historically; new source not built today; physical panel and refresh pending |
| Pro touch / Home | GT911 input and UI routes | GT911 input path | Existing Home, library, settings, list and reader touch routes; EPUB/XTC/XTCH/CBZ tap/swipe page-turn path present. Configurable zones/gestures and accidental-touch tuning incomplete; physical GT911/Home/rotation untested |
| Pro illumination | Dual-channel warm/cool light manager | Brightness/warmth UI and restore behavior | New Pro-only persisted on/brightness/warmth settings; init on wake and park before sleep. Not compiled or hardware tested today |
| Classic profile / controls | Classic hardware profile, active buttons and orientation-aware mapping | Classic button profile/navigation | Separate `X4CLASSIC` profile; touch and lights disabled; existing mapped controls/shortcuts remain. Prior SDL2 run is host-only; real button timing, mapping and orientation pending |
| USB Drive / MSC | TinyUSB MSC over native SDMMC; unmount/sync, eject, host handoff and restart | MSC handoff lifecycle | New shared USB Drive UI and exclusive raw SD ownership; sync/unmount, error/eject/disconnect/suspend timeout, restart/remount/reindex, Serial/JTAG restore. Host callback harness passed; firmware build, TinyUSB and hardware unverified |
| SDMMC/storage | Board-specific 1-bit SDMMC profiles and guarded ownership | SDMMC lifecycle | Uses per-board SDK wiring; Pro virtual SD worked in prior QEMU. Physical SD/transfer performance untested; Classic firmware execution absent |
| PSRAM/framebuffer | S3 PSRAM-aware allocators and frame buffers | PSRAM-backed rendering | Both profiles configure 8 MiB OPI and PSRAM-backed framebuffer. Prior Pro QEMU allocator evidence applies only to old certified BIN; today’s changes unbuilt; physical training/performance pending |
| Fonts | FreeInkFont/FreeType TTF/OTF streaming adapter and PSRAM-aware caches | Static/runtime font features vary by implementation | Nooir built-ins and `.cpfont` remain. Native TTF/OTF runtime support is not integrated; renderer/registry/cache/shaping port remains open |
| Panel detection/refresh | Profile-specific panel variants, refresh and sleep controls | Profile-specific display handling | Shared Nooir display abstraction and SDK profiles retained; current physical controller, grayscale/refresh and sleep-screen behavior unverified |
| Battery / RTC / orientation | Board battery gauge, BM8563 RTC, Classic IMU/tilt | Device profile support where wired | Existing SDK/profile components and app plumbing retained; physical gauge/charging/RTC/tilt/rotation calibration untested |
| Power / deep sleep | Board power manager, wake and peripheral sequencing | Sleep/wake integration | Existing Nooir sleep path retained; Pro light parks before sleep and initializes after wake. No new deep-sleep/wake or current measurement |
| BLE HID | S3 BLE host implementation available upstream | BLE support depends on target | Disabled in both experimental S3 builds; feasibility and coexistence remain open. Do not transplant C3-specific thresholds or SPI-array assumptions |
| OTA / SD recovery | Device-specific firmware update path | Device-specific recovery/update path | OTA menu/check/install and SD/recovery entry/write paths gated by `NOOIR_DISABLE_FIRMWARE_FLASHING` plus experimental guard. No safe physical recovery procedure validated |
| Library / formats / Web UI | Shared application behaviors and transfer routes | Shared reader/storage behaviors | Nooir Main/Folio Lite, EPUB styling, XTC/XTCH/CBZ, dictionaries, clippings, achievements, shelves, persistence and Wi-Fi Web UI retained in source. New source unbuilt; host/QEMU evidence is from earlier revisions |

## Evidence and limits

The USB MSC host harness passed on 2026-11-24, covering bounded reads/writes, partial sectors, watchdog yielding/reset only for subscribed tasks, eject/disconnect and error/startup handling. This is a host stub test, not TinyUSB or physical storage validation. Existing Pro QEMU Reader run03 passed 30/30 checks on the earlier certified Pro pair; run01 passed 27/27. Existing Classic SDL2 run04 passed scripted Home/Library/EPUB page turn/exit/persistence writes, but does not execute its S3 BIN. No emulator was rerun after today’s source changes.

The Pro rebuild failed before compilation because PlatformIO’s Windows output thread could not encode a Unicode status line under CP1252. Classic build was not started. The prior certified BIN/ELF pairs remain hash-verified and unchanged. Current source changes are not build-certified.

Never flash either device until a board-matched full backup and independently validated external recovery method exist. No physical Pro or Classic boot, display, touch/button, SDMMC, PSRAM, battery, sleep/wake, BLE or recovery result is claimed.


## 2026-11-25 closing update

This update supersedes any reading of the table above that could imply the newly added feature-parity source was certified by the preserved BINs or emulator runs. The certified Pro/Classic firmware pair and Pro QEMU/Classic SDL2 evidence predate today's unbuilt changes.

- USB MSC: source and host lifecycle harness exist; the last host harness passed on 2026-11-24. Today's clean Pro build compiled the MSC implementation but stopped at five incomplete HalStorage::StorageLock instantiations. No Pro or Classic firmware BIN was created today.
- Pro touch/gestures: touch profile and existing UI routes are present. Gesture-zone tuning and GT911 hardware behavior remain unverified; the QEMU swipe result is unconfirmed.
- Pro frontlight/warmth: Pro-only persistence and sleep/wake source integration are present but were not compiled in a successful current build and have no hardware evidence.
- Classic buttons/shortcuts: touch remains disabled; existing button mappings and shortcuts remain in source. Prior SDL2 navigation used host key events, not the physical buttons or the Classic S3 BIN.
- SDMMC, display, PSRAM and power: existing device definitions and older Pro QEMU evidence remain useful references. Physical SDMMC, panel variants, PSRAM training, rails and sleep/wake are open. Classic S3 firmware execution remains open because no Classic QEMU machine is available.
- CrossPoint/CrossInk reference revisions are listed at the top; they guide implementation only.
- S3 BLE remains disabled and feasibility has not been tested. Native TTF/OTF integration remains unimplemented. Battery/RTC/orientation/panel-detection and safe external recovery remain open.
- Do not repeat the stale dependency-link build attempt, the reused core missing sdkconfig.h, the deep Windows build path, or the QEMU long-wait pattern that timed out. Use short D: paths and fresh build/dependency directories after resumption.
- No physical flash, merge, release, deployment, or work on the original C3 release, Web Emulator, Status Bar experiment or MihonNooir occurred in this checkpoint.
