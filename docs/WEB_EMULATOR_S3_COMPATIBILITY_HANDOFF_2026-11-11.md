# Folio Nooir X4 Pro / Classic Browser Compatibility Handoff

Date: 2026-11-11
Scope: firmware/simulator-side findings only. The separate Web Emulator repository was not opened or modified.

## Recommendation

Build the browser UI simulator in layers. Start with X4 Classic using the existing Nooir app sources and a browser port of the host simulator HAL. Add Pro touch and Home controls next. Keep genuine ESP32-S3 firmware execution as a separate emulator path.

A native SDL2 simulator compiles Nooir application C++ for the host. It exercises the actual UI, library, EPUB and progress code, but substitutes host implementations for ESP32 hardware. A browser build of that backend would compile the host simulator to WASM; it would not execute the S3 .bin or Xtensa instructions.

## Phase A — Classic browser simulator

The existing simulator_x4classic PlatformIO profile uses the pinned CrossPoint simulator library with SIMULATOR_DEVICE_X4_CLASSIC. Today’s WSL build and scripted run passed from the migrated D: source. The simulator presents an 800×480 logical device profile and its saved BMP files are 480×800 pixels.

Reusable pieces include Nooir activities, library/EPUB parsing, progress/settings serialization, Classic profile selection, and the simulator’s display/input abstractions. A browser target would need an Emscripten SDL/canvas backend and browser storage in place of the native window and folder-backed CROSSPOINT_SIM_SD. IndexedDB or OPFS can provide a persistent virtual card. WSL currently has SDL2 but no emcc; an Emscripten build has not been attempted.

Represent Classic controls as a device drawing with two side page keys, four lower navigation keys, and Power. Map them through the Classic profile: side Left/Right for previous/next, lower Left/Right, Confirm, Back, and Power. Classic has no touch, capacitive Home, or frontlight. Reference pin mapping from the current FreeInk S3 notes is side Left GPIO0, side Right GPIO7, Power GPIO3, and lower Left/Right/Confirm/Back GPIO5/2/8/9. Confirm each board revision before claiming electrical fidelity.

The SDL simulator’s touch-disabled behavior and a host-folder SD are useful UI tests; neither validates physical GPIO, SDMMC, PSRAM, panel timing, power, BLE or e-paper waveforms.

## Phase B — Pro browser simulator

Use a separate Pro profile and controls. Retain side page keys; map pointer/touch input to GT911-style tap, hold and swipe events, and expose the capacitive Home pad as its own button. Keep touch and Home unavailable in Classic mode.

The host simulator can model framebuffer output and frontlight state, but these remain software abstractions. Nooir’s present Pro firmware profile sets FREEINK_CAP_FRONTLIGHT=0, so frontlight behavior is not currently exercised by the app. RTC, battery, sleep and power behavior also require explicit simulated services if user-visible features depend on them. Display controller variants matter: the hardware references include SSD1677, UC8179 and UC8279 batches.

## Phase C — ESP32-S3 firmware emulation

The current [eink-qemu project](https://github.com/serialx/eink-qemu) already provides a browser WASM path and an x4pro ESP32-S3 machine. Its current board documentation lists 16 MiB flash, 8 MiB OPI PSRAM, SDMMC/FAT32, SPI e-paper UC8279 revision 0x68 or UC8179, GT911 touch, keys, RTC, battery gauge, Wi-Fi and sleep. The WASM backend executes the guest S3 firmware under QEMU’s CPU emulator; the modeled board peripherals remain virtual. Frontlight, charger status and USB OTG are listed as missing.

No X4 Classic machine is listed. Classic firmware emulation would need a separate board model with its GPIO/button wiring, power rails, SDMMC/card behavior, display controller variants and no touch/Home/frontlight. The S3 WASM CPU support can be reused, but Pro success does not establish Classic wiring or panel compatibility.

Nooir’s existing local eink-qemu 0.9.2 runs used the correct Pro machine and a matching full virtual flash, but did not reach Nooir Home. The next guest-emulation step remains a byte-matched ELF and a QEMU/GDB trace of the early startup path. Any merged/full-flash image remains emulator-only and must not be passed to a physical flashing tool.

Espressif’s [ESP emulator](https://github.com/espressif/esp-emulator) also documents S3 WASM execution and octal PSRAM, but its current feature list does not include SDMMC or an e-paper board model. It is not a drop-in Nooir X4 emulator. It would need additional device/peripheral models.

## Shared-code boundary

Keep one explicit build/board selector per device and leave existing C3 X3/X4 paths unchanged. Share app-level navigation, book parsing, stored progress formats and logical display operations. Keep the browser canvas, browser SD persistence, device drawings and event adapters outside the existing X3/X4 emulator path. A test report must identify whether a result came from host-simulator code with mocked HAL or from guest ESP32-S3 firmware execution.

An independent [Reader Simulator](https://github.com/PewPewzxc/reader-simulator) demonstrates source-built firmware with a simulator HAL in browser/WASM. Its current supported-firmware page lists Folio Nooir 1.6.6 only for X3 under its separate emulator path; it is feasibility evidence, not proof of this S3 port.

## References

- [CrossPoint simulator README](https://github.com/crosspoint-reader/crosspoint-simulator) and [simulator architecture/input notes](https://github.com/crosspoint-reader/crosspoint-simulator/blob/main/CLAUDE.md)
- [CrossInk platform configuration](https://github.com/uxjulia/CrossInk/blob/main/platformio.ini)
- [eink-qemu board support, WASM and peripheral list](https://github.com/serialx/eink-qemu)
- [Espressif ESP emulator](https://github.com/espressif/esp-emulator)
## Evidence update — 2026-11-11 D: validation

This update replaces the earlier status that Pro had not reached Nooir startup/Home. It does not claim a complete Pro interactive or physical-device pass.

### Confirmed port-side results

- **X4 Classic:** `simulator_x4classic` rebuilt and replayed successfully from D: using the host SDL2 simulator. Home/Library, EPUB open, next-page render, saved progress/book state/statistics, Reader exit and RecentBooks return all passed with the folder-backed sample card. This is application/UI validation; it is not ESP32-S3 firmware emulation or real hardware validation.
- **X4 Pro:** fresh `x4pro` app/ELF pair ran on eink-qemu 0.9.2 `x4pro,panel=uc8279-68`. With a valid sparse FAT32/MBR card, it logged `SDMMC card mounted`, `Starting CrossPoint version 1.6.6-x4p-exp-no-flash`, `Display initialized`, `Entering activity: RecentBooks`, and `render_ready`. The automatically captured 480x800 Home frame is `diagnostics/s3-revalidation-20261111/pro-qemu-fresh-home-capture-12/frames/0002-51049ms.png`, SHA-256 `ed22e869007ea9531c5abd9840e2dc8e906e697cf4b7b553cf4f620085571f9b`. No Pro touch, Home-pad, Library or EPUB interaction was tested.
- Exact tested Pro build pair: current `.pio/build/x4pro/firmware.bin`, 5,671,104 bytes, SHA-256 `0a0b5bb6eaa47ec4cdacc2a6af7f1d2a7166ff913658d86c555aac23aaa69ba9`; `.pio/build/x4pro/firmware.elf`, 86,746,476 bytes, SHA-256 `bd768427c55b500a87b2ffe3a39542a7053e102139b884ac103a9224765017e2`. The QEMU run consumed that build directory. The matching `...X4PRO...-4.bin` app-only export has the same BIN SHA and remains marked experimental/not-for-flashing/pending physical validation.
- Both S3 PIO targets compiled and passed app-slot size checks under PlatformIO 6.1.19 / Espressif32 55.03.37 / Arduino 3.3.7 / IDF 5.5.2. Separate exports currently differ by hashes despite equal lengths; do not call independent rebuilds bit-reproducible yet. Exact logs and manifests are in the port handoff and `diagnostics/s3-revalidation-20261111`.

### Why Pro initially appeared silent or blank

The S3 test path spends about 48.8 seconds of firmware time in early OPI PSRAM initialization/test before Nooir reaches its first application log. Nooir intentionally delays `Serial` output until after `verifyPowerButtonWakeup` to avoid changing power-button timing. eink-qemu 0.9.2 holds Pro Power for 1,500 ms by default, so the key is released long before the app check and the firmware takes its normal invalid-wake/deep-sleep path. This is not a C: migration error, QEMU console routing failure, or proof of a firmware panic.

A synthetic 49-second hold lets the app pass that check and draw Home. The same long input then causes the normal power handler to enter low-power mode; therefore this is evidence of startup/render capability, not proof of a usable prolonged interactive session. QEMU's generic `ready` helper also expects three boot refreshes; this Nooir run produced two, even though its logs and captured frame show Home. Use log-driven settle checks for Pro until the helper is calibrated. No firmware timing changes are recommended for this QEMU-only effect.

The fresh Pro log reports `[ERR] [GYR] SDK IMU not found`; QEMU does not model that optional sensor. Its absence did not block SD mount, display setup or Home. The first invalid superfloppy card was a test-fixture error; the corrected MBR/FAT32 card mounts.

### Frontend/backend contract for future support

Keep the current X3/X4 C3 firmware backend unchanged. Do not pass either S3 firmware image to the C3 QEMU machine. Treat device selection as a profile plus an explicitly typed backend, rather than a device-name alias:

```ts
interface DeviceProfile {
  id: 'x3' | 'x4' | 'x4classic' | 'x4pro';
  architecture: 'esp32-c3' | 'esp32-s3' | 'host-wasm';
  backend: 'c3-qemu' | 'host-simulator' | 's3-qemu';
  display: { width: number; height: number; rotation: number; controller?: string };
  inputs: { buttons: string[]; touch: boolean; capacitiveHome: boolean };
  storage: { persistent: boolean; implementation: 'file' | 'indexeddb' | 'opfs' };
  simulated: string[];
}

interface EmulatorBackend {
  boot(profile: DeviceProfile, firmware?: File, card?: StorageHandle): Promise<void>;
  sendInput(event: { type: 'press' | 'release' | 'tap' | 'swipe'; control: string; x?: number; y?: number });
  onFrame(callback: (frame: ImageData) => void): void;
  onSerial(callback: (line: string) => void): void;
  stop(): Promise<void>;
}
```

The concrete API belongs in the Web Emulator codebase; this is a boundary proposal only. `press` and `release` must be separate semantic events for power timing, while touch taps/swipes must not be collapsed into key presses. Backend identity and simulated features should remain visible to users/tests.

### Phased integration plan

1. **Classic source-simulator browser path:** compile the host simulator build of Nooir to WASM; adapt SDL drawing to canvas and the folder-backed `CROSSPOINT_SIM_SD` interface to IndexedDB/OPFS. Keep Classic buttons-only: side Left/Right page keys, lower Left/Right/Confirm/Back and Power; no touch, capacitive Home or frontlight. This runs real Nooir UI/library/EPUB source with a simulated HAL, not the S3 `.bin`.
2. **Pro host-simulator profile:** reuse the same app/display/storage adapters, but expose Up/Down/Power plus GT911 tap, hold and swipe and a distinct capacitive Home pad. Report battery, RTC, sleep, frontlight and IMU as simulated/unavailable; do not claim that a touch event reaches hardware-accurate guest firmware.
3. **Pro real-firmware path:** after the local startup/control lifecycle is understood, package the pinned eink-qemu S3 WASM backend as a distinct worker/backend. Require exact device firmware input, 16 MiB flash/8 MiB OPI PSRAM profile, correct panel choice, SDMMC and power timing. Validate WASM execution independently in browser before enabling selection. The local native Pro Home result does not prove browser/WASM support.
4. **Classic real-firmware path:** defer until a distinct X4 Classic QEMU board exists with its own pins, panel/controller variants, power rails, SDMMC and lack of touch/Home/frontlight. `x4pro` is not an alias for Classic even though both use ESP32-S3.

### Shared display and SD test data

Share one small test EPUB and generated filesystem manifest across backends. Give each browser profile a storage adapter over its own IndexedDB/OPFS namespace; allow a deliberate shared-card mode only for cross-profile persistence tests. In local integration tests, reference the existing D: fixture/card or create a sparse/copy-on-write temporary image instead of copying the 2 GiB Pro test image. The UI may share a logical 480x800 surface, but each backend owns rotation, panel controller and refresh timing.

### Required input from the S3 port before implementation

The Web Emulator work should request and pin: exact source/SDK revision; build flags and matching BIN/ELF hashes; board revision and verified pin map; Pro GT911 and capacitive Home semantics; Classic key wiring; panel/controller and rotation by hardware batch; PSRAM mode and measured startup; SDMMC/partition and virtual storage needs; power/wake event expectations; and an explicit table of modeled versus unmodeled BLE, frontlight, battery, RTC, sleep and USB functions. Physical firmware, bootloader, eFuse, security and recovery remain outside browser-emulator scope.

### Current readiness

- Classic SDL2 host simulator: **PASS** for the recorded scripted reader flow; this did not execute the Classic S3 BIN.
- Pro exact-build S3 firmware in native eink-qemu: **PASS** through Reader render, key page navigation, exit, progress save and saved-position reopen (27/27 steps).
- Browser/WASM execution: **UNCONFIRMED**; no browser build or runtime evidence is recorded.
- Physical Pro/Classic behavior, Pro deep sleep/wake, Classic S3 execution, physical touch/swipe and external recovery: **OPEN**.
- Workstream: **PAUSED — awaiting physical hardware testing or a concrete Web Emulator integration requirement.** The C3 Web Emulator implementation remains a separate workstream; no files in that repository were changed.

## Closeout — 2026-11-13 S3 compatibility status (PAUSED)

This is the current S3 integration handoff. It supersedes earlier statements in this document that Pro Reader interaction was unverified. Porting is **PAUSED — awaiting physical hardware testing or a concrete Web Emulator integration requirement**. This change only updates documentation and manifest hashes; no Web Emulator code was changed.

### Evidence and validation boundary

- **Classic host simulator — PASS:** WSL Ubuntu 24.04.4 / SDL2 2.30.0 host-simulator profile used folder-backed virtual storage. The preserved replay exercised Classic-style buttons, Home/Library, EPUB open and render, page 0→1, Reader exit, and progress/book-state/reading-statistics writes. Runtime log: `${NOOIR_D_ROOT}/logs/x4classic-validation-20261111-run01/classic-simulator.log`, SHA-256 `562ec89ace26042898ed691406e4870ced1a3cc40bfd5a2689bf535ca982d4d2`. This is host code with simulated HAL, not the S3 BIN. Saved-progress reopen after simulator restart is not evidenced. One optional cover-thumbnail file lookup failed; page render and the replay still passed.
- **Pro native firmware QEMU — PASS:** exact fresh Pro firmware and matching ELF ran on pinned eink-qemu 0.9.2, `x4pro,panel=uc8279-68`. The final replay passed 27/27 steps and reached startup, PSRAM allocator, Pro identity, display/Home, virtual SDMMC, EPUB Reader, page content, next/previous key navigation, Reader exit, progress save, and reopening at saved position. `run.jsonl` SHA-256 `072fc25e9f691724555b43cca8065fb7a842d5e68023f0b07791abe0bdc3f1e2`; console SHA-256 `867a88b10065d31d9550121520c88851b4b1c4bb1ffb43af33df13cca56fab66`. Synthetic swipe did not produce a settled transition; physical GT911/touch/swipe remains unconfirmed. The retained Reader image is page 3/10, so a dedicated screenshot of page 1 is not available. CPU low-frequency events are not proof of deep sleep or wake.
- **Browser/WASM — UNCONFIRMED:** no browser build or execution is present in the port evidence. Verify current Emscripten/WASM toolchain and pinned eink-qemu browser support when there is a concrete integration request.
- **Physical firmware/hardware — NOT TESTED:** neither device was flashed or electrically probed. Nothing in host SDL2 or QEMU certifies real panel, GPIO, SDMMC, PSRAM training, input, power, or recovery.

The canonical fresh app-only firmware pairs remain unchanged and hash-verified:

| Target | BIN SHA-256 | Matching ELF SHA-256 | Identity |
|---|---|---|---|
| X4 Pro | `0a0b5bb6eaa47ec4cdacc2a6af7f1d2a7166ff913658d86c555aac23aaa69ba9` | `bd768427c55b500a87b2ffe3a39542a7053e102139b884ac103a9224765017e2` | `1.6.6-x4p-exp-no-flash` |
| X4 Classic | `a1ece7c1897ab39ac24c6e30d0e0331b29a62925d28fcf52657d39cb92dcabab` | `bc412ce4b3587fe6248b80aa5c51793704f7e2d8b228a68c3ed1ccd44549e44c` | `1.6.6-x4c-exp-no-flash` |

Full paths, sizes, build-log hashes, historical image inventory, test evidence, and OTA/recovery analysis are in `PORT_HANDOFF.md`. Older S3 exports without matching historical ELF files and hash-different collision-suffixed exports remain preserved as separate generations; no rebuild was done to reconcile them.

### Board-profile details for an emulator

The following pin data comes from the isolated S3 SDK profiles and reference notes. It is a profile contract, not a fresh measurement of the user's retail unit. Confirm exact hardware revision/controller before claiming physical fidelity.

**Shared S3 configuration:** ESP32-S3; board definition `esp32-s3-devkitc1-n16r8`; 16 MiB DIO flash at 80 MHz; 8 MiB OPI PSRAM using `dio_opi`; `FREEINK_FB_PSRAM=1`. Both use one-bit native SDMMC (not SPI): CLK GPIO41, CMD GPIO42, DAT0 GPIO40. Their logical panel geometry is 800×480; current QEMU captures are 480×800, so the browser backend must make rotation explicit rather than assume it. Frontlight/warm-light capability is compiled out of the experimental app profiles.

**X4 Pro profile:**

- Display: 800×480 SPI e-paper. Reference batches include SSD1677, UC8179, and UC8279. Pro pin map: SCLK 12, MOSI 11, CS 13, DC 18, RST 14, BUSY 6. The verified native QEMU run selected UC8279 revision `uc8279-68`; that does not cover the other panel batches.
- Input: touch enabled in the firmware profile. GT911 is on the shared I²C bus SDA 39/SCL 38; reference touch pins are INT GPIO10 and RST GPIO4, with the touch rail enabled by GPIO2 low. The capacitive Home pad is a GT911 key event, not a standalone GPIO. Side Left GPIO0 maps to previous/up; Right GPIO7 maps to next/down; Power GPIO3. Taps reached Library/Reader in native QEMU; synthetic swipe did not pass the settled-screen check. Physical controller, gestures, Home pad, calibration, and pin behavior remain open.
- Storage/power: SD power-enable GPIO5 is active-low. SDK mount logic power-cycles the card before mounting and keeps the enable low while in use; block I/O stages through DMA-capable memory. QEMU mounted and wrote the virtual card; no physical SD test occurred. GPIO1 is the reference master rail held high.

**X4 Classic profile:**

- Display: 800×480 UC8279 reference profile. Classic SPI pins: SCLK 12, MOSI 11, CS 13, DC 14, RST 10, BUSY 18. The Classic reference omits the Pro-specific UC8279 PLL `0x30` command. eink-qemu 0.9.2 has no Classic machine.
- Input: touch disabled; eight reference active-low keys. Side Left GPIO0 = previous/up, Right GPIO7 = next/down, Power GPIO3. Bottom GPIO5 = Left, GPIO2 = Right, GPIO8 = Confirm, GPIO9 = Back. Do not reuse Pro's touch/Home events; GPIO8/9 are key inputs here, not warm/cool channels.
- Storage/power: same one-bit SDMMC signals; Classic SD power-enable GPIO6 is active-low. GPIO1 is the reference master panel/peripheral rail held high. The SDL2 host replay did not exercise these physical signals.

### Emulator requirements and restart blockers

1. Keep separate backend identities: Classic host-simulator WASM, Pro host-simulator WASM, Pro real-firmware S3 QEMU, and any future Classic guest machine. Never make `x4pro` an alias for `x4classic`.
2. For a Pro firmware guest, pin the exact BIN/ELF and source/SDK/build identity; use the `x4pro` machine and selected panel; compose virtual flash only from that build's bootloader (`0x0`), partition table (`0x8000`), and app (`0x10000`); provide a FAT32/MBR virtual SD card and serial/frame/heap evidence. The preserved 16 MiB full-flash images are **QEMU ONLY — NOT FOR PHYSICAL FLASHING**.
3. The native QEMU Pro startup requires a 50,000 ms virtual cold-boot Power hold in this test setup. The 1,500 ms default releases before the application’s PSRAM startup/power check. Preserve separate press/release semantics for power; do not infer a hardware timing requirement from this emulator workaround.
4. The Classic source simulator can validate app/UI behavior with SDL2 and a host-folder/IndexedDB storage adapter, but it cannot certify the S3 BIN. A real Classic firmware backend requires a distinct QEMU board model or physical validation.
5. Report host-simulator, guest-firmware QEMU, browser/WASM, and physical results as different evidence classes. List modeled peripherals and missing models visibly.

Open blockers before resuming: physical Pro/Classic board revision and panel identification; physical touch/swipe and buttons; PSRAM training; panel/power/GPIO/SDMMC; deep-sleep current/wake; Classic S3 execution; browser/WASM build/runtime; device-matched recovery. Because experimental OTA operations and normal SD-update/recovery paths are compile-time disabled, the firmware is not physically flash-ready. Require a validated read-only inventory, complete verified backup, and tested external model-correct restore path before any separately authorized hardware write.

Resume only for physical hardware testing with those safety prerequisites, or for a concrete Web Emulator integration requirement. This checkpoint does not authorize flashing, builds, source changes, releases, commits, pushes, merges, or deployment.
## 2026-11-13 backup and pause checkpoint

The firmware-side source and compatibility findings are being preserved on `codex/nooir-x4pro-x4classic-s3-port-20261113` in the Folio Nooir repository. This document remains advisory input for the separate Web Emulator project; no browser or emulator source is included here. The port remains **PAUSED**.

The verified software evidence is separated by execution type: exact fresh X4 Pro ESP32-S3 firmware passed 27/27 scripted Reader steps in native eink-qemu 0.9.2 (`x4pro`); the Classic SDL2 result is a host-simulator pass and does not execute its S3 BIN. Browser/WASM runtime, Classic guest execution, and all physical-board behavior remain unconfirmed. See `PORT_HANDOFF.md` and `PORT_SOURCE_DELTA.json` for artifact hashes and the retained local-only evidence locations.

Resume only for a specifically scoped physical validation with safe external recovery prepared, or a concrete Web Emulator integration requirement. Keep the browser host-simulator backend distinct from a real ESP32-S3 guest backend.

Remote backup verification will be added after the push is confirmed.
## Evidence update — v1.6.7 S3 candidates (2026-11-24)

This update supersedes the 2026-11-13 pause text for source/build evidence only. It does not change the standalone Web Emulator. The experimental S3 port now uses the integrated Nooir 1.6.7 source and has separate X4 Pro and X4 Classic ESP32-S3 build profiles.

### Pro native QEMU

The exact newly built Pro BIN/ELF ran with eink-qemu 0.9.2, machine `x4pro`, panel `uc8279-68`, USB Serial/JTAG, and a synthetic 2050 MiB SD image. The 16 MiB flash dump was composed from the matching build's bootloader, partition table, and Pro application. It is labeled QEMU-ONLY / NEVER FOR PHYSICAL FLASHING.

- Run01: 27/27 scripted checks, including app startup, profile identification, PSRAM allocator, SDMMC mount, Home/Library, EPUB open, first render, page forward/back, and reader exit.
- Run02: failed a later reopen tap because the guest entered its low-power timer state before the harness input; no crash/panic was observed.
- Run03: 30/30 checks after reducing the post-boot wait. It reopened the EPUB at saved progress; the reopened-page screenshot hash matched Run01's next-page screenshot. Forward/back navigation and exit to Library passed. Heap checks reported no corrupt blocks and PSRAM free-space stayed around 8.24–8.29 MiB in this emulator.

The startup power-hold workaround is 50,000 guest milliseconds. It is required to pass the emulated boot hold; it is not a physical power-button recipe. QEMU exercised CPU low-power frequency behavior only. Deep sleep, wake-from-sleep, real PSRAM training, physical display refresh, touch, retail bootloader compatibility, and all physical SD/GPIO behavior remain unverified.

Evidence is under `${NOOIR_D_ROOT}\_builds\nooir-s3-v1.6.7-20261124\qemu\pro-v1.6.7-reader-run01`, `...run02-reopen`, and `...run03-reopen-fast`; CLI captures are in the sibling `logs` directory. The exact firmware and evidence hashes are in the main port handoff and source-delta manifest.

### Classic simulator boundary

The Classic firmware compiles as an ESP32-S3 image with the Classic device identity and touch disabled. eink-qemu has no Classic machine. SDL2 simulation exercises host application code only; it does not execute this BIN and cannot certify S3 boot, PSRAM, SDMMC, GPIO, panel, buttons, sleep, or recovery. Current v1.6.7 SDL2 validation is recorded in `PORT_HANDOFF.md`; the earlier 1.6.6 replay remains historical evidence.

### Frontend/backend requirements and unsupported capabilities

A Web Emulator integration should keep X4 Pro and Classic as separate profiles, preserve the Pro-only touch behavior, and represent Classic side page buttons plus lower navigation/confirm/back and power controls. Keep display orientation explicit (logical 800×480 versus QEMU capture 480×800). Model SDMMC as a virtual storage service; QEMU's Pro mount is evidence for firmware/emulator integration, not for physical electrical timing. The S3 application profiles compile out BLE HID host, USB MSC, frontlight, and warm light. OTA and SD firmware flashing/recovery writes are disabled in these experimental builds. Do not present unsupported controls as working hardware features.

No browser-side change is authorized by this compatibility note. A concrete integration request must specify the target emulator, control schema, display-frame contract, virtual storage contract, and how unsupported power/sleep behavior is surfaced.
## 2026-11-24 v1.6.7 simulator and firmware evidence

- **Pro guest firmware in QEMU:** the exact v1.6.7 X4 Pro BIN/ELF pair ran on `eink-qemu 0.9.2`, machine `x4pro`, with the 50,000 ms virtual Power-hold workaround. The first Reader run passed 27/27 checks; the corrected reopen run passed 30/30 including EPUB render, next/previous page keys, exit, saved progress and reopen at that position. Run03 `run.jsonl` SHA-256: `0651acab8a6f2328f6ff9f9a4e80d70d7aa6ee6c88d97e17b3172f997bcd0bc8`; `console.jsonl`: `6c1582f073c3c4501de6d9820bec67ea60806ad5fa4b26999a7a2ded961cba0a`. This is virtual firmware execution; physical PSRAM, SDMMC, panel, touch and deep-sleep/wake remain unverified.
- **Classic native host app:** `simulator_nooir167_x4classic` compiled in Ubuntu 24.04.4 WSL/WSLg, SDL2 2.30.0, and PlatformIO Core 6.1.19. A button-only replay on a fresh folder-backed card opened the synthetic EPUB, rendered page 0, turned to page 1 and back, exited, and wrote progress/recent/book-state/reading-statistics data. Run log SHA-256: `ca7f741377928d0ada78b2e9ca8d297a63ef40a72d18bcb46064d6d28df207df`; page 0 SHA-256 `681e17e852ccfe02e7f68faa38f0c1e4504894c1c82d64dd94e8482d6e3701fd`; page 1 `3db4f98dfb1ed3c0b1158b99b30bed310bd7ef8e234f93b34192d9d0c2584eca`. The Classic profile has touch disabled and the run sent no touch events. It did not execute the Classic S3 BIN; no Classic firmware QEMU machine exists.
- **Build artifacts:** the matching experimental Pro and Classic BIN/ELF pairs and successful logs are preserved locally under `${NOOIR_D_ROOT}/_builds/nooir-s3-v1.6.7-20261124`. Firmware binaries, logs, SD fixtures, QEMU dump, and screenshots are not part of this Web Emulator source tree.
- **Boundary:** do not label the Classic host run as firmware emulation or physical certification. Keep the host simulator backend separate from Pro S3 guest execution and from any future Classic guest model. The full 16 MiB Pro dump is QEMU-only and never for physical flashing.

## 2026-11-27 hardware-library and evidence update

The isolated feasibility branch now contains candidate FreeInk SDK pin `de19f154ef0963879592f3baf6da68606122797c` with the Nooir 1.6.7 app retained and separate X4 Pro/Classic profiles. Both experimental S3 BIN/ELF pairs compile. The exact Pro candidate runs on eink-qemu 0.9.2 through Home, EPUB navigation, exit and progress reopen. This is a Pro virtual guest result; Classic remains a host SDL2 result, and there is no Classic QEMU machine.

The before/after Nooir Pro Recent captures show no meaningful UI change from this hardware-layer trial. Existing screenshots and timing evidence are indexed in `docs/HARDWARE_FOUNDATION_FEASIBILITY_2026-11-27.md`. Requested Lite/Featured/Spine and several format/settings/USB/light screens are absent; do not expose them as verified emulator capabilities.

Profiles remain distinct: Pro enables GT911 touch and separate cool/warm frontlight; Classic is touchless, frontlight-disabled and uses physical keys. Both use native 1-bit SDMMC and request 8 MiB OPI PSRAM. Exact retail pin/panel revision and power assumptions remain source-reference facts pending hardware. QEMU's Pro SD mount is virtual. The emulator's USB MSC path is not a real host transfer route. OTA and SD firmware-write APIs remain disabled in these builds.

No standalone Web Emulator source, API, control mapping, rendering backend or storage contract changed. The candidate did not establish a performance win: the available small QEMU sample puts candidate Nooir input-to-settled median at 1,405 ms versus stable CrossPoint 2.0.0 at 1,276.5 ms. Keep browser-host simulation separate from the Pro ESP32-S3 guest, and do not claim Classic BIN execution or physical parity.
