# Folio Nooir 1.6.7 — X4 Pro / X4 Classic S3 parity checkpoint

**Status: PAUSED — end-of-day checkpoint 2026-11-25.** Both corrected v1.6.7 X4 Pro and X4 Classic ESP32-S3 builds succeeded from base commit 91e1fac80375bf568558b5684f8245e2ab46caa5 plus the local source diff affecting three tracked files recorded in the handoff. The source files are present in remote checkpoint commit 375173cfd4e6d721a8406562ae3bdcc0dc641b77. Their fresh app-only BIN/ELF pairs are preserved under D:\p\run3\artifacts. No QEMU or SDL2 runtime test was repeated against the new pairs. Earlier Pro QEMU 27/27 and 30/30, Classic SDL2, and USB host-harness results remain tied to older source/artifacts. Physical validation remains open. Reference revisions: CrossPoint Reader 92e8714a332c2c1903ea93290cf0b9b4f6f6697c with FreeInk SDK 995d9bd523b9c45b18710bca17a3158ce724a7f4; CrossInk 9914146eeae7b46b300f475a16c32426fc02ec1f with FreeInk SDK 699370183fa3a0e33c9cb83a36f701bbb6022095.

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

### Resumed source correction

The five USB methods that instantiate HalStorage::StorageLock now appear after the nested class definition. Their implementation and mutex behavior are unchanged. This corrects C++ type-completeness ordering only; firmware build and runtime validation are pending.

## 2026-11-25 end-of-day status — PAUSED

The current feature-parity source is committed at source baseline 325f3ac8dc0bfcb1d5e81910052932a258567308, but both fresh firmware builds fail in the shared USB Drive activity. The successful BIN/ELF pairs and simulator runs below predate the latest source. Do not attribute those results to the current source.

| Capability | Current implementation / evidence | Status and remaining boundary |
|---|---|---|
| USB Drive / MSC, both models | Shared activity implements sync/unmount, exclusive raw SD handoff, eject/disconnect/error handling, timeout, restart/remount/reindex, and Serial/JTAG restoration. The host callback lifecycle harness passed on 2026-11-24. | Partially implemented and currently unbuildable: both S3 profiles fail because SdCardFontSystem::releaseForStorage and MappedInputManager::consumePowerGesture do not exist. The host harness does not execute TinyUSB or prove physical SD ownership. Review renderer/font cleanup and power-edge dispatch before changing APIs. |
| X4 Pro touch and gestures | Pro profile enables touch and existing UI/reader routes include tap and swipe/page-turn handling. | Source only at this checkpoint; current source is not build-certified. Gesture zones, accidental-touch tuning, GT911 behavior, rotation, and physical swipes are untested. Any prior QEMU swipe claim is unconfirmed. |
| X4 Pro frontlight and warmth | Pro-only persistent on/brightness/warmth settings and sleep/wake hooks are present in source. | Not compiled successfully at the current source checkpoint; channel wiring, temperature behavior, persistence across real power cycles, and wake restoration are untested on hardware. |
| X4 Classic buttons and shortcuts | Separate Classic profile, touch-disabled capability, existing mapped buttons and shortcuts remain. | Prior SDL2 replay used keyboard events. Physical GPIO mapping/polarity, timing, long press, orientation, and shortcut behavior remain untested. |
| SDMMC and storage | Per-model SDK wiring remains separate. Preserved Pro QEMU mounted a virtual SD; Classic SDL2 used a folder-backed virtual card. | Emulator/host evidence only. No physical card, bus timing, card-detect, transfer reliability, or MSC/filesystem handoff test. |
| Display and framebuffer | Device profiles and shared framebuffer path remain. Preserved Pro BIN rendered Home and Reader in QEMU; Classic host simulator rendered pages. | No current-source firmware evidence. Physical panel/controller revision, refresh mode, rotation, grayscale, ghosting, contrast, and Classic S3 output remain open. |
| PSRAM | Both profiles request 8 MiB OPI PSRAM with PSRAM-backed framebuffer allocation. Earlier Pro QEMU reported about 8.24–8.29 MiB free PSRAM without corrupt blocks. | QEMU allocator evidence belongs to the preserved older Pro BIN. Physical PSRAM training, margins, allocation under load, and both current firmware builds remain unverified. |
| Power management | Existing sleep paths remain; Pro light state is parked/restored around sleep/wake in source. Pro QEMU run02 timed out after entering the virtual low-power timer path; run03 used the known shorter timing. | Deep sleep, wake buttons, rail sequencing, current draw, and repeated cycles are not validated on either device. Preserve the known virtual Power-hold workaround. |
| CrossPoint / CrossInk references | CrossPoint Reader 92e8714a332c2c1903ea93290cf0b9b4f6f6697c and SDK 995d9bd523b9c45b18710bca17a3158ce724a7f; CrossInk 9914146eeae7b46b300f475a16c32426fc02ec1f and SDK 699370183fa3a0e33c9cb83a36f701bbb6022095. | Reference-reviewed only. Use their separate Classic/Pro interfaces and initialization flows as implementation guidance; they do not establish Nooir compatibility. |
| S3 BLE | BLE HID host is disabled in both S3 profiles. | Feasibility/coexistence is untested. Do not infer S3 support from the C3 project or transplant C3-specific assumptions. |
| Fonts | Nooir built-ins and .cpfont remain. | Native TTF/OTF FreeType renderer, registry, cache, and shaping integration is not implemented. |
| Battery, RTC, orientation, panel detection | Existing SDK/profile components and application hooks remain where defined. | Gauge/charging, BM8563 RTC, Classic IMU/tilt, orientation calibration, panel detection, and per-revision refresh are not physically verified. |
| OTA, SD update and recovery | Experimental build guard suppresses OTA update check/install; NOOIR_DISABLE_FIRMWARE_FLASHING blocks SD FirmwareFlasher validation/write APIs. | These are software write guards, not a recovery procedure. Neither model is flash-ready until a model-matched backup and independent recovery route are validated. |
| Shared application features | The 1.6.7 source includes its integrated reader, styling, progress, shelves, formats, dictionaries, clippings, achievements and Web UI work. | Presence in shared source is not proof that the latest Pro or Classic S3 build works; both current firmware builds stop in UsbDriveActivity. |

### Evidence reconciliation

- Successful firmware builds: the preserved Pro/Classic app-only pairs built 2026-11-24. Pro BIN SHA-256 3e47ba8ce10901e5a037ecfcd75443615707882e7d2c32856949ba341868d7d5; ELF 8a123c4a2c636585214af964bcb03abc90f60db1db28e87b12a1e1475200adcb. Classic BIN c4bf429556c15d1eccc92343352395f3dc36e90634569a17258f48fa4fc9cb10; ELF 564b1e53a916f7d1aceba1faae6a0b1697f06b21b5b05eb2467b8541ad5480c7. The closeout re-hashed these files; no new BIN/ELF was exported on 2026-11-25.
- Pro firmware-in-QEMU: exact preserved Pro pair, eink-qemu 0.9.2, run01 27/27 and run03 30/30. This is actual firmware-in-QEMU evidence for the earlier pair; it is not physical hardware proof or current-source proof.
- Classic SDL2: native host build and run04 passed keyboard-driven Home/Library/EPUB open, page forward/back, Reader exit, and persistence writes. This host executable does not run the Classic S3 BIN; there is no Classic QEMU machine. Process-restart reopen was not demonstrated.
- USB MSC host lifecycle harness passed 2026-11-24. It is host-stub evidence only.
- Latest current-source firmware builds: both failed at the same two missing UsbDriveActivity APIs. Pro log SHA-256 B1F72C8712D31015D83540D6A5BA07D8484C828834DD9EAFF7A9DBEE77C88234; Classic log SHA-256 2C9DA36698820EC7686723BC00059D5D3384BBD2BB540498E25B0242EF5CE1B0. The preserved pairs above remain unchanged.
- No physical Pro or Classic validation is claimed. Deep sleep/wake, physical touch/swipe and buttons, recovery, and all electrical/peripheral behavior remain open.

### Restart sequence

1. Resolve the two real USB Drive API mismatches after reviewing font-cache ownership and power-button event dispatch; do not add no-op shims.
2. Rebuild both targets on the pinned toolchain in fresh short D: paths; keep prior pairs untouched.
3. Verify artifact metadata and hashes, then run exact-new-Pro QEMU checks and focused current-source Classic SDL2 checks.
4. Only later, validate physical behavior using identified boards and a tested backup/recovery plan. Keep S3 BLE and TTF/OTF as separate follow-up investigations.

## 2026-11-25 final end-of-day checkpoint — PAUSED

This checkpoint supersedes earlier wording that treated the prior clean source commit as the current working tree. At checkpoint start, source HEAD was 82a669ba69475709dc21621c447620ca8e801055 and matched origin. The local tree contains an unfinished, uncompiled font-release helper in src/SdCardFontSystem.cpp and src/SdCardFontSystem.h; the MappedInputManager::consumePowerGesture call remains unresolved. The source diff is preserved locally and at D:\p\run2\handover\pending-usb-font-storage-fix-20261125.patch (SHA-256 52305c0025bed8cd20d3a74c3b555ce1dbdf54e54f3a42389223cd9cba107030), excluded from the docs checkpoint commit. A generated build cache is also untracked and excluded.

No build/test process was active. No tests, builds, simulators or emulator runs were started for this closeout. The last Pro and Classic builds failed on those two API calls and produced no BIN/ELF. Their logs and partial outputs remain under D:\p\run2. The 2026-11-24 Pro and Classic experimental BIN/ELF pairs remain unchanged and are the last successful firmware builds; they predate current feature-parity source.

| Area | Status at pause |
|---|---|
| USB Drive / MSC | Partially implemented. Earlier host lifecycle harness passed; current S3 firmware has no successful build. The local helper addresses only the font-release API and is unverified. TinyUSB, physical host transfers, SDMMC exclusivity, eject/remount and recovery remain open. |
| Pro touch / gestures | Existing Pro input routes are in source. Physical GT911 behavior, tuning, rotation and swipe are untested; earlier QEMU swipe claim is unconfirmed. |
| Pro frontlight / warmth | Persistent Pro settings and sleep/wake hooks remain in source, but current source is unbuilt and light behavior is physically untested. |
| Classic buttons / shortcuts | Mapped controls remain in the separate touch-disabled profile. Prior SDL2 tests use keyboard events, not physical buttons. |
| SDMMC / display / PSRAM / power | Prior Pro QEMU and Classic SDL2 results apply only to earlier artifacts or host code. Physical buses, panel revisions, memory training, rails, deep sleep and wake remain unverified. |
| CrossPoint / CrossInk | Reference revisions and SDK pins are recorded in PORT_HANDOFF.md and PORT_SOURCE_DELTA.json. They guide implementation and do not certify Nooir. |
| BLE and other device-specific work | S3 BLE remains disabled and feasibility untested. Native TTF/OTF remains unimplemented. Battery/RTC/orientation/panel detection and safe external recovery remain open. |

Last successful evidence remains: Pro exact-pair QEMU run01 27/27 and run03 30/30; Classic host SDL2 run04 navigation, Reader exit and persistence writes; USB host lifecycle harness; earlier Python artifact tests 5/5. These were not rerun today and do not certify current source or physical devices. See PORT_HANDOFF.md for exact BIN/ELF hashes, failed build-log hashes, test limits and prioritized restart steps.

The dedicated S3 branch is the only Git destination. The closeout stages documentation only; it excludes the partial source patch, build cache, firmware, logs and temporary artifacts. The workstream is PAUSED. No physical flash, merge, release, deployment, or changes to the original release, Web Emulator, Status Bar experiment or MihonNooir.

## 2026-11-25 final feature-parity reconciliation — PAUSED

This section supersedes earlier statements in this file that the latest Pro/Classic builds failed or that no fresh BIN/ELF was produced.

| Feature | Current implementation / completed evidence | Remaining status |
|---|---|---|
| USB Drive / MSC — Pro and Classic | Shared USB Drive lifecycle compiles in both S3 builds. SdCardFontSystem::releaseForStorage releases interface and reader SD-backed fonts before raw storage ownership. The Power edge now relies on the controlled teardown/restart in the exclusive storage loop instead of a missing input-consumption API. | Build-validated only. The older 2026-11-24 host callback harness passed, but no host lifecycle rerun, TinyUSB enumeration, host transfer, physical SDMMC ownership, eject/remount, or recovery validation was done against this source. |
| Pro touch and gestures | Separate touch-enabled Pro profile and existing Home/Library/Settings/Reader paths, with tap/swipe page-turn handling for supported readers, remain in source. | New Pro BIN was not run in QEMU. QEMU swipe is unconfirmed. GT911, capacitive Home, rotation, configurable zones, accidental-touch behavior, and physical EPUB/XTC/XTCH/CBZ gestures are pending. |
| Pro frontlight and warmth | Pro-only persistent controls and sleep/wake hooks compile; Classic lighting UI remains disabled. | No physical channel, warmth calibration, persistence, Wi-Fi/USB transition, sleep/wake, or battery impact test. |
| Classic buttons and shortcuts | Separate touch-disabled Classic profile and mapped button/shortcut code compile. | Prior SDL2 uses host keyboard input, not physical buttons. GPIO/polarity, long-press timing, orientation, and menu shortcut behavior remain unconfirmed. |
| SDMMC and storage | Separate board SDK pin/configuration paths remain. Earlier Pro QEMU mounted virtual SD; earlier Classic SDL2 used folder-backed host storage. | No physical SDMMC or MSC ownership/transfer validation. The new builds only prove compilation/linking. |
| Display, framebuffer, and PSRAM | Device-separated profiles compile with the expected panel abstractions, framebuffer path, 16 MiB flash and 8 MiB OPI PSRAM configuration. | Earlier Pro QEMU and Classic SDL2 rendering apply only to earlier firmware/host checkpoints. New BINs not runtime tested; physical panel variants, refresh/grayscale, PSRAM training and sustained allocation remain open. |
| Power, sleep, and wake | Existing model-specific sequencing remains; Pro lighting parks/restores in source. The earlier Pro QEMU run02 timeout occurred in the emulator low-power timer path; use the known short timing and virtual Power-hold workaround. | No physical deep-sleep, wake, current draw, or cycle evidence. |
| CrossPoint and CrossInk references | CrossPoint Reader 92e8714a332c2c1903ea93290cf0b9b4f6f6697c, SDK 995d9bd523b9c45b18710bca17a3158ce724a7f4; CrossInk 9914146eeae7b46b300f475a16c32426fc02ec1f, SDK 699370183fa3a0e33c9cb83a36f701bbb6022095. | Reference only; upstream behavior does not certify Nooir hardware. |
| S3 BLE HID | Disabled in both profiles. | Feasibility/coexistence remains untested; do not transplant C3-specific implementation assumptions. |
| Other 1.6.7 functionality | Main and Folio Lite themes, EPUB styling, formats, dictionaries/clippings, achievements, shelves, progress persistence, and Web UI remain in common source and compile. | No reader/theme regression tests were started for the new builds. Native TTF/OTF, battery/charging, RTC, Classic tilt/orientation calibration, and automatic panel detection remain unimplemented or hardware-unverified as noted in the existing matrix. |
| OTA, SD update, and recovery | Experimental OTA and SD firmware-write guards remain enabled. | No safe physical backup/recovery route has been validated. Neither BIN is ready for physical flashing. |

**Evidence classification:** Pro QEMU 27/27 and 30/30 are actual firmware-in-QEMU runs on the earlier preserved Pro pair. Classic SDL2 and the USB MSC callback harness are host-only tests on earlier source. The two run3 results are successful S3 compile/link only; neither BIN was booted in QEMU or executed in the Classic simulator. No physical hardware result is claimed.
