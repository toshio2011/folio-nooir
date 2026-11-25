# Folio Nooir X4 Pro / X4 Classic ESP32-S3 Port Handover

> Artifact checkpoint (2026-11-24; historical): the preserved Pro and Classic v1.6.7 BIN/ELF pairs passed their recorded build checks. The exact Pro pair passed 27/27 and 30/30 Reader checks in native eink-qemu; the Classic SDL2 host build and scripted Reader run passed. These results apply to the earlier artifact source checkpoint, not to the later feature-parity changes now in this branch.
>
> Latest source checkpoint (2026-11-25): the Pro build attempt reached the S3 USB MSC code and then failed on five incomplete-type errors for HalStorage::StorageLock. No new BIN/ELF was produced. The previous certified artifacts and all local evidence remain preserved. No physical device was flashed.

Updated: 2026-11-25

**Workstream status: PAUSED — end-of-day checkpoint.** Resume tomorrow from the authoritative D: worktree below. Today's feature-parity source is not firmware-build-certified; the preserved BIN/ELF and QEMU/SDL2 results remain tied to their earlier source checkpoint. No physical device was flashed; nothing was merged, released, or deployed.

The last artifact-backed software checkpoint was the Classic SDL2 host replay plus the exact-build Pro native eink-qemu Reader replay (30/30 checks in run03; run01 was 27/27). Those runs belong to the preserved earlier artifacts. Physical compatibility, Pro deep sleep/wake, Classic firmware execution, physical touch/swipe, and safe recovery remain open. See the 2026-11-25 closing checkpoint at the end of this file.

## Scope and safety

This isolated source tree is based on the official Folio Nooir `1.6.6` source tag. It adds separate experimental ESP32-S3 build identities and device profiles for X4 Pro and X4 Classic. The original C3 profile and v1.6.6 application sources remain represented by the archived source snapshot. No changes were made to the physical v1.6.7 workspace, BLE project, or browser emulator.

No physical Pro or Classic was flashed. No bootloader, eFuse, security setting, or physical partition was changed. The two S3 outputs are app-only experiments and remain explicitly marked `EXPERIMENTAL-NOT-FOR-FLASHING`; they have not been released or published. The full-flash image below is only for eink-qemu and is explicitly **NOT FOR PHYSICAL FLASHING**.

The official source archive is preserved at `.provenance/folio-nooir-1.6.6-source.zip` (SHA-256 `caa9ec28400ce4cb87178b47f74d5dff8744221d52081556ba9b9e8420d1b9f4`), from commit `b6d428a3187afc25d434fcb475e9ab436099c1ba`. The official release page is [Folio Nooir v1.6.6](https://github.com/toshio2011/folio-nooir/releases/tag/1.6.6). No official release `.bin` asset is present in this isolated `artifacts/` directory; do not treat the C3 intermediate described below as that release asset.

The ambiguous `artifacts/firmware.bin` was verified as a generated C3 `gh_release` intermediate, not an official release asset and not an S3 image. It was renamed in place, without changing its bytes, to:

- `artifacts/QUARANTINED-Folio-Nooir-v1.6.6-C3-gh_release-intermediate-NOT-OFFICIAL-RELEASE-NOT-FOR-S3-NOT-FOR-FLASH.bin`
- Size: `6,082,400` bytes
- SHA-256: `5e05dfc31001aa27cf635f795c77b3447f1bfab70713ad522c850b6a64d6147e`
- Classification sidecar: same filename plus `.manifest.json`

The generic filename no longer exists. The quarantined bytes are preserved; this source tree does not currently contain a separate `.pio/build/gh_release/firmware.bin` copy.

## Source, SDK, and partition identity

- Official source: tag `1.6.6`, commit `b6d428a3187afc25d434fcb475e9ab436099c1ba`.
- Base C3 FreeInk SDK: `7bd6aebe9b40198d7dddf455400cee9225430726`.
- S3 FreeInk SDK: `5e94fe7480f019c89fbf691aef87523eb1b46704`, kept separately under `freeink-sdk-s3/`.
- `partitions.csv` remains byte-identical to v1.6.6; SHA-256 `98e02c50532c388d85cc3b7c96d004142dc713b23368d293c881ecdcb419e44e`.
- Both OTA app slots remain `0x640000` bytes. App0 begins at `0x10000`; app1 begins at `0x650000`.
- `PORT_SOURCE_DELTA.json` compares current source files with the archived official source. It excludes build-generated outputs, diagnostics, binaries, and the separately inventoried SDK snapshots.

## Build identities and preserved artifacts

Windows experimental firmware builds used PlatformIO Core `6.1.19`, Espressif32 platform `55.3.37`, Arduino-ESP32 `3.3.7` / ESP-IDF `5.5.2`, esptool `5.1.2`, and Xtensa toolchain `14.2.0+20251107`.

Both S3 profiles target the ESP32-S3 DevKitC-1 N16R8 definition: 16 MiB flash, DIO flash mode, and 8 MiB OPI PSRAM (`dio_opi`). The existing v1.6.6 partition table is retained. BLE and USB mass storage are disabled. `NOOIR_EXPERIMENTAL_DEVICE_BUILD=1` hides OTA and SD-update menu/recovery routes and makes `OtaUpdater::checkForUpdate()` / `installUpdate()` return `NO_UPDATE`; `NOOIR_DISABLE_FIRMWARE_FLASHING=1` separately blocks SD `FirmwareFlasher` validation and write APIs. These protections are detailed in the latest safety audit below. The PlatformIO upload guard rejects hardware-writing targets for these profiles.

| Output | Profile / role | Size | SHA-256 |
|---|---|---:|---|
| `artifacts/Folio-Nooir-v1.6.6-X4PRO-EXPERIMENTAL-NOT-FOR-FLASHING.bin` | `x4pro`, ESP32-S3 application only | 5,671,248 bytes | `7e4ac1c63a24005383a7ebf2a63fd988a96bbb1bd421c990c334e5466e800494` |
| `artifacts/Folio-Nooir-v1.6.6-X4CLASSIC-EXPERIMENTAL-NOT-FOR-FLASHING.bin` | `x4classic`, ESP32-S3 application only | 5,676,432 bytes | `3309e364639d53ffe1602befb6b2c9fc62e66d088e0bc383833e2cf0c55f804a` |
| `artifacts/QUARANTINED-Folio-Nooir-v1.6.6-C3-gh_release-intermediate-NOT-OFFICIAL-RELEASE-NOT-FOR-S3-NOT-FOR-FLASH.bin` | generated original C3 `gh_release` intermediate; quarantined | 6,082,400 bytes | `5e05dfc31001aa27cf635f795c77b3447f1bfab70713ad522c850b6a64d6147e` |

Each S3 image fits the unchanged `6,553,600`-byte app slot. Their adjacent JSON manifests record target, app-only role, size, hash, `flash_performed: false`, `is_for_flashing: false`, and `physical_validation: pending`. These images are preserved unchanged.

## Automated artifact checks

On 2026-11-06, `py -3 test/python/test_firmware_artifacts.py -v` passed all five checks:

- Both targets retain the original app-slot bound.
- Exported images are target-marked, app-only, and never overwrite an existing artifact.
- The upload guard permits builds and preserves the C3 behavior.
- The upload guard blocks hardware-writing targets for experimental profiles.
- Wrong-target and unsafe artifact metadata are rejected.
## X4 Pro eink-qemu diagnosis

The investigation used official eink-qemu `0.9.2` artifacts, checksum manifest SHA-256 `099feabb7808c5861e54e1d9d0e116c269ba2edf94fb1a1bf903ad295164d501`. The Windows CLI archive SHA-256 is `d1a0dc3c6a3b02af8e197979018a3aa153bb1825082807c244f5acd464571295`; the Xtensa softmmu archive SHA-256 is `dd64bacb45359cacb790822695d68883e53e4172ea63b3a5713d58bc25774e52`. The release source is [eink-qemu](https://github.com/serialx/eink-qemu/releases).

Use the `x4pro` machine (`x4pro,panel=uc8279-68` in the captured runs). Two boot compositions were compared:

1. App-only image supplied to eink-qemu, using its default virtual bootloader and partitions.
2. A 16 MiB virtual flash composed from the matching Pro build directory, using that build's bootloader, partition table, and application.

The emulator-only full-flash image is preserved at `diagnostics/eink-qemu-0.9.2/Folio-Nooir-v1.6.6-X4PRO-EMU-ONLY-NOT-FOR-PHYSICAL-FLASH-16MiB.bin`. It is 16,777,216 bytes with SHA-256 `26ae6518b77cf8c268679f64fd40754212fb1cb6f84f3cc838223f5e18eb39cd`. The app bytes at `0x10000` were compared against the preserved Pro app image and match exactly. Composition used bootloader at `0x0`, partition table at `0x8000`, and app0 at `0x10000` (app1 remains at `0x650000`). The reproducible command form is:

```powershell
eink-qemu flash .pio/build/x4pro diagnostics/eink-qemu-0.9.2/Folio-Nooir-v1.6.6-X4PRO-EMU-ONLY-NOT-FOR-PHYSICAL-FLASH-16MiB.bin --machine x4pro
```

That command composes a virtual flash for QEMU. The output filename and metadata explicitly prohibit physical flashing. Do not pass it to esptool or a device installer.

Both compositions were run headlessly on `x4pro`, with UART0 and USB console captures. Both remained blank; the 480x800 screenshot SHA-256 is `701199968b22a20c1cc68b494543684e73ed1d4d850e2e6b019a9cf528d0857e`, and eink-qemu reported zero panel refreshes. The extended full-flash run lasted 120 guest seconds. Its preserved evidence is in `diagnostics/eink-qemu-0.9.2/pro-full-flash-extended-04/`:

- `log.txt` SHA-256 `4c5088db0c3e63e6a266d39fc7db50f3f742df6c5d39595c1fdacd9b2eeca0b6`
- `console.jsonl` SHA-256 `c6591427d291abf335f5047a75310925f2cfebbdb7e4b35a4ecd87f3d356a0ea`
- `run.jsonl` SHA-256 `6b037bfe290d791e89ab1e1dc825a5afe019bf172301990bd578548c3156baf5`

The console shows ROM startup (`ESP-ROM:esp32s3-20210327`, `rst:0x1 (POWERON),boot:0x4 (SPI_FLASH_BOOT)`) and no Nooir/ESP-IDF application log lines, panic, Guru Meditation, or reset. Captured CPU PCs entered the ESP32-S3 flash-mapped application address range, so application code execution after ROM/bootloader is verified. The register snapshot at 120 seconds has `PC=0x4037a265`, `EPC1=0x4223c974`, `EPC4=0x4037b30e`, `EXCCAUSE=4`, and `EXCVADDR=0`; `EXCCAUSE=4` is the level-1 interrupt path and is not itself a panic.

A WSL rebuild of the same Pro source/config was used for a provisional symbol map. Its key `platformio.ini`, `src/main.cpp`, and device/firmware identity headers matched this worktree byte-for-byte, but the rebuilt app was 5,671,104 bytes with SHA-256 `aaeb1d9d5cdd694b6050c10f9b7497496802dfcca18821b6cb66cc764e263bf7`, 144 bytes shorter than the preserved Pro app. Its ELF mapped the 20–40 second PCs into ESP-IDF `s_test_psram` and the 120-second PC into the FreeRTOS idle hook. Because that rebuild is not byte-identical to the app in the QEMU flash, the symbol attribution and PSRAM-test completion are indicative, not exact-build proof.

### Staged Pro boot status

| Milestone | Result | Evidence / limit |
|---|---|---|
| 1. Bootloader reaches Nooir app | **Confirmed** | Captured PCs entered the S3 flash-mapped app range; full-flash app bytes match the preserved app at `0x10000`. |
| 2. Nooir startup logs | **Not observed** | UART0 and USB captures show ROM only. This is the first failed observable milestone; no panic explains it. |
| 3. PSRAM initializes | **Not fully verified** | Provisional ELF map shows execution in the ESP-IDF PSRAM test and later idle, but the ELF is not an exact match to the preserved app. |
| 4. Runtime profile says X4 Pro | **Not observed** | No Nooir `Hardware detect: X4 Pro` log. |
| 5. Display initializes / Home renders | **Not reached** | Blank 480x800 output and zero panel refreshes. |
| 6. Virtual SD mounts through SDMMC | **Not confirmed** | QEMU reports SDMMC/DWC accesses and `SD: Optional CMD52 not implemented` / `SD: Optional CMD5 not implemented`; there is no mount-success log. |
| 7. Library and EPUB reading | **Not reached** | No Home or library. |
| 8. Reader exit and progress persistence | **Not reached** | No reader session. |

The first failed observable milestone is Nooir startup logging, after confirmed execution in the app address range. The exact first blocking call remains unknown. `src/main.cpp` calls `Storage.begin()` before display setup, so the SDMMC activity is a plausible place to instrument next, but the QEMU messages do not prove that SD initialization is the cause. Display initialization is not established as the cause. The Nooir board-detection log and hardware initialization are also unverified. Do not infer Pro Home/Reader compatibility from this boot.

## X4 Classic Linux/WSL SDL2 simulator

The Windows host initially lacked `sdl2-config`; the Classic simulator was built and run in an existing Ubuntu 24.04.4 WSL environment with SDL2 `2.30.0` and PlatformIO Core `6.1.19`. The application source was kept shared; simulator-only platform compatibility shims were added under `scripts/simulator_*_compat.py` and `simulator_compat/`. They cover host GPIO, clock, storage, heap, network, PNG/string/system/Wi-Fi APIs. The Classic profile reports touch disabled. No app workaround was added for the missing Windows SDL2 tool.

`simulator_x4classic` compiled successfully. A native SDL run used a folder-backed virtual SD directory containing a generated 24-paragraph EPUB. The simulator BMP captures and runtime log are preserved under `diagnostics/eink-qemu-0.9.2/`:

- Runtime log: `classic-simulator-multipage-run.log`, SHA-256 `6fa86af701634ed6862da44219b6d7c2abdedad356b7cf5cff002be1cb3a04bf`
- Sample EPUB: `classic-simulator-multipage-sample.epub`, SHA-256 `c8e8de68968636258d0417b2ca059ec993f88f4c1d6d82779f91dca2cfbb68bc`
- Home: `classic-simulator-multipage-home.bmp`, SHA-256 `db3953f07f2970264c3ae866a324e555bb57c62d0e51871f8afb962c7ee8c911`
- Library: `classic-simulator-multipage-library.bmp`, SHA-256 `16fe87bc1173b44e723f26413bf1db0d14d474b1e7925094df038ac69effc3cb`
- Reader page 0: `classic-simulator-multipage-reader-page0.bmp`, SHA-256 `681e17e852ccfe02e7f68faa38f0c1e4504894c1c82d64dd94e8482d6e3701fd`
- Reader page 1: `classic-simulator-multipage-reader-page1.bmp`, SHA-256 `3db4f98dfb1ed3c0b1158b99b30bed310bd7ef8e234f93b34192d9d0c2584eca`
- After exit: `classic-simulator-multipage-after-exit.bmp`, SHA-256 `87442aa57a4517b293da2c4cf890f4aa8a8dd74c9694bc1c6cc801cf3a46de61`

The run log records `Hardware detect: X4 Classic`, `Display initialized`, Boot → RecentBooks → FolioLibrary → Reader → EpubReader, a page turn from page 0 to page 1, progress saves (10% to 20%), and reader-exit saves for recent/book state/reading statistics. The input sequence used `RIGHT` to change the shelf tab, `LEFT` to select Library, `CONFIRM` to open the EPUB, `DOWN` to turn a page, and `BACK` to exit. No touch events were sent; Classic capability is touch-disabled in the simulator profile. Storage was a host folder, not a real SDMMC bus. This proves the native UI/library/reader flow only for the simulator profile; it does not validate physical Classic storage, panel, PSRAM, or GPIO.

## Reference implementations and remaining device-specific gaps

CrossPoint provides an S3 N16R8 / `dio_opi` build reference and a Classic simulator profile; see [CrossPoint platformio.ini](https://github.com/crosspoint-reader/crosspoint-reader/blob/develop/platformio.ini). CrossInk and the pinned S3 SDK provide the X4 Pro/Classic board, pin, display, touch, and SDMMC references; see [CrossInk](https://github.com/uxjulia/CrossInk) and `freeink-sdk-s3/docs/xteink-x4pro-support.md` / `xteink-x4c-support.md`.

Remaining gaps are physical validation of the Pro and Classic board revisions, actual PSRAM training, SDMMC/card power and mounting, panel/controller variant and orientation, key GPIOs, Pro GT911 touch, power/sleep/wake behavior, battery/RTC, and frontlight. The current Nooir firmware build profiles keep frontlight support disabled. eink-qemu's Pro panel and peripheral model do not replace those hardware checks. The Pro native application simulator has not established Pro Home or Reader behavior.

## Physical-flashing prerequisites

Physical flashing was not authorized or attempted. Before any future device-writing work, require a separate explicit authorization, verified retail model/board/panel identity, a read-only record of current boot/security/eFuse state, a tested recovery method and known-good backup of the existing firmware and user data, and a model-matched bootloader/partition/app composition validated against the device's existing layout. Then validate PSRAM, display, input, touch (Pro), and SDMMC on the physical target in a separately approved session. None of those prerequisites is established by this handover; the preserved S3 binaries and QEMU full-flash image remain non-flashing experiments.

No commit, push, merge, release, deployment, or physical flash was performed.


## 2026-11-10 D: migration and stop handover

### Scope and stop status

The combined migration-validation prompt was accidental. This handover remains focused on the experimental v1.6.6 X4 Pro / X4 Classic ESP32-S3 port. No X4 build, test, emulator, simulator, flash, release, or Git operation was run in this stop turn. The earlier Pro boot failure predates the D: migration; it is not a migration regression without comparative evidence.

At the 2026-11-10 stop checkpoint, the separate v1.6.7 page-aware C3 validation build had exited successfully and the process scan found no build, simulator, emulator, or local web server running. The 2026-11-11 X4 Classic simulator run described below completed later and is now stopped. The Windows SDL2/browser preview was not started. No physical device was flashed; no bootloader, eFuse, security, or partition data on real hardware was changed. No commit, push, merge, release, publish, or deploy was made.

Tomorrow, resume only from `${NOOIR_D_ROOT}\_c-side-migration-20261110\_isolated_nooir_x4_pro_classic_v166`. Keep all Nooir sources, build outputs, caches, logs, and temporary files on D:. Do not recreate or rely on the deleted C: workspace. The protected migration archive must otherwise remain unchanged; this handover is the requested update to the X4 port notes.

### D: paths, migration integrity, and Git state

- Isolated X4 Pro / Classic source: `${NOOIR_D_ROOT}\_c-side-migration-20261110\_isolated_nooir_x4_pro_classic_v166`.
- Migrated primary tree: `${NOOIR_D_ROOT}`.
- Protected migration archive: `${NOOIR_D_ROOT}\_c-side-migration-20261110`.
- Migration log: `${NOOIR_D_ROOT}\_c-side-migration-20261110-robocopy.log`.
- No `.git` metadata exists in the isolated S3 source directory. Git resolves to the parent D: repository, branch `codex/folio-nooir`, HEAD `b846e1550d18fe4e48014fcfec54b35e9da1ab57`. Do not attribute this parent commit to the isolated v1.6.6 port. The parent tree already had tracked edits in `lib/I18n/translations/english.yaml`, `src/CrossPointSettings.cpp/.h`, `src/SettingsList.h`, `src/activities/settings/StatusBarSettingsActivity.cpp`, and `src/components/themes/BaseTheme.cpp`; it also reports `logs/`, the migration archive, its log, and `e_blepa_1106/` as untracked. This session made no Git changes.
- The migration copy log records 223,614 files, zero failed files, and one failed directory. The errors were limited to inaccessible `.venv-platformio` symlink entries (`bin/python*` and `lib64`); do not repair the protected archive. The active D: Python environment and PlatformIO Core are available at `${NOOIR_D_ROOT}\.venv` and `${NOOIR_D_ROOT}\.pio-home` (PlatformIO Core 6.1.19).
- A separate active D: venv was used for the accidental 1.6.7 build, but Python’s base runtime was under `${WINDOWS_USER}\AppData\Local\Programs\Python\Python312`. This is not the deleted workspace, but it is a C: runtime dependency to assess before claiming fully D-contained tooling. Do not change global tools today.

### Preserved X4 port artifacts and evidence

The source is the official Folio Nooir v1.6.6 archive/commit recorded above. `PORT_SOURCE_DELTA.json` reports 21 changed and 17 added files, with zero missing files; the original C3 definition remains separate. Existing Pro and Classic application binaries remain app-only experimental files, unchanged and not for flashing:

- Pro: `artifacts/Folio-Nooir-v1.6.6-X4PRO-EXPERIMENTAL-NOT-FOR-FLASHING.bin`, 5,671,248 bytes, SHA-256 `7e4ac1c63a24005383a7ebf2a63fd988a96bbb1bd421c990c334e5466e800494`.
- Classic: `artifacts/Folio-Nooir-v1.6.6-X4CLASSIC-EXPERIMENTAL-NOT-FOR-FLASHING.bin`, 5,676,432 bytes, SHA-256 `3309e364639d53ffe1602befb6b2c9fc62e66d088e0bc383833e2cf0c55f804a`.
- The ambiguous original C3 intermediate remains quarantined under `artifacts/QUARANTINED-Folio-Nooir-v1.6.6-C3-gh_release-intermediate-NOT-OFFICIAL-RELEASE-NOT-FOR-S3-NOT-FOR-FLASH.bin`, 6,082,400 bytes, SHA-256 `5e05dfc31001aa27cf635f795c77b3447f1bfab70713ad522c850b6a64d6147e`. It is not the official v1.6.6 release image.
- Pro QEMU-only full virtual flash remains at `diagnostics/eink-qemu-0.9.2/Folio-Nooir-v1.6.6-X4PRO-EMU-ONLY-NOT-FOR-PHYSICAL-FLASH-16MiB.bin`, 16,777,216 bytes, SHA-256 `26ae6518b77cf8c268679f64fd40754212fb1cb6f84f3cc838223f5e18eb39cd`. Its bootloader, partition table, and app offsets are 0x0, 0x8000, and 0x10000 respectively. It is strictly for the emulator and must never be passed to an actual device tool.
- Existing artifact safety checks passed 5/5. These are the prior recorded results; no X4 tests were rerun during migration validation.

### Pro and Classic status at the stop point

The X4 Pro diagnosis remains exactly as documented in the earlier section: eink-qemu 0.9.2, machine `x4pro` / panel `uc8279-68`, app-only and correctly composed full virtual flash both show ROM startup and a blank panel. The application flash-mapped address range is reached. The first failed observable milestone is Nooir startup logs; no panic or reset was recorded. PSRAM symbol attribution is provisional because the reference rebuild differs from the preserved application by 144 bytes. No X4 Pro Home, display initialization, SD mount, Library, or Reader milestone is verified. The cause remains unknown; display initialization has not been established as the cause.

Classic has prior Linux/WSL SDL2 simulator evidence: Ubuntu 24.04.4, SDL2 2.30.0, and PlatformIO 6.1.19; Home, Library, EPUB opening/page turn, reader exit, and progress save completed using folder-backed virtual SD. Touch was disabled and no touch event was used. This does not validate physical SDMMC, panel, PSRAM, GPIO, or power behavior. During the combined migration-validation session, `wsl --list` / `wsl --status` returned `WSL/E_ACCESSDENIED`; no WSL state was changed. Tomorrow, confirm the D: native simulator toolchain and run the Classic simulator independently if the authorized environment is available.

The extended Pro console SHA-256 was re-verified on 2026-11-11 against the file in this diagnostics directory; the handoff typo is corrected and the JSON value already matched the file.

### Accidental 1.6.7 build record for its dedicated session

During the combined prompt, a fresh build was run from `${NOOIR_D_ROOT}\e_blepa_1106`, profile `gh_release_spiarray_ble_epub_pageaware_20261106_01`, with D: PlatformIO core/cache, D: temporary directory, and a new output name. Build identity was forced to `1.6.7+unknown` so it would not inherit the unrelated parent Git hash. The first attempt stalled after a Windows cp1252 Unicode output exception and was stopped. The UTF-8 captured retry completed successfully in 1,494.45 seconds (24m54s); no upload target was run.

New, separate validation outputs (preserve; do not treat as release or flash images):

- `${NOOIR_D_ROOT}\logs\post-migration-validation-20261110\folio-nooir-1.6.7-pageaware-postmigration-validation-attempt-02-NOT-FOR-FLASH.bin`, 6,106,512 bytes, SHA-256 `79D60934C7FB0AB2EFED831A3B7BF4419E060C85026A01302414E76606B838A1`.
- `${NOOIR_D_ROOT}\logs\post-migration-validation-20261110\folio-nooir-1.6.7-pageaware-postmigration-validation-attempt-02.elf`, 87,614,728 bytes, SHA-256 `942A79AB4C41AAD414E3C2D02E6A50D1354DE01EA8DBDE2823EED45F343591C7`.
- Captured build logs: `${NOOIR_D_ROOT}\logs\post-migration-validation-20261110\pio-build-attempt-02.stdout.log` and `pio-build-attempt-02.stderr.log`; temporary files are under `${NOOIR_D_ROOT}\logs\post-migration-validation-20261110\tmp`. The earlier interrupted attempt may have left incremental files under the active project’s `.pio` directory; preserve them.

Build hooks regenerated ten source-manifest files and removed two generated profile sdkconfig files. Those 12 files were restored from the verified archived project copy. The active and archived v1.6.7 source each then matched all 3,796 source-manifest entries with zero missing or mismatching files. The prior preserved experimental BIN/ELF and control BIN/ELF hashes were unchanged. The fresh build’s source generators produced the compiled image before the source files were restored; its output is retained for the dedicated 1.6.7 session to assess. The final disk-space observation was 57,294,573,568 bytes free on C: and 246,303,178,752 bytes free on D:. Do not attribute the observed C: usage change without evidence.

### Tomorrow’s X4-only checklist

1. Recheck the isolated source inventory, current `PORT_SOURCE_DELTA.json`, Pro/Classic BIN hashes, partition table, separate device definitions, and the quarantined C3 intermediate.
2. Establish a working S3 toolchain rooted on D:; confirm dependencies and temporary outputs do not use the deleted C: workspace.
3. Independently validate the Classic SDL2 native simulator, including input mapping, touch-disabled behavior, virtual storage, and framebuffer output.
4. Reproduce the Pro serial boot with detailed eink-qemu logs on `x4pro`, first app-only/default composition and then a full virtual flash composed from the matching build’s bootloader, partition table, and app at verified offsets.
5. Record the exact last successful and first failed boot milestones. The known first failure is Nooir startup logging, and it predates migration; do not claim a migration regression without evidence.
6. Keep every full-flash image clearly marked emulator-only. Do not flash Pro or Classic, change eFuse/security/bootloader/partition data, publish firmware, or perform Git operations.
## 2026-11-11 X4-focused resume and verification

### Source integrity, build outputs, and Git

- Work continued only in ${NOOIR_D_ROOT}/_c-side-migration-20261110/_isolated_nooir_x4_pro_classic_v166. All 38 source-delta entries, including today’s compatibility handoff, match their recorded SHA-256 values; 0 mismatches. The official v1.6.6 source identity, C3 profile, S3 Pro/Classic profiles, and partition table remain unchanged.
- The preserved Pro, Classic, quarantined C3 intermediate, emulator-only Pro full flash, and partitions.csv hashes were rechecked and match the earlier manifest. No S3 firmware build or export was run today.
- The Classic native executable is a host simulator output, not ESP32 firmware: .pio/build/simulator_x4classic/program, 15,092,112 bytes, SHA-256 3cb2f787d85be6248cacbfeb3f189fa41ca21946e32a40bec263017698a6e445.
- First native build attempt used one job and was stopped with Ctrl+C after partial compilation. Its log remains at ${NOOIR_D_ROOT}/logs/x4classic-validation-20261111-run01/build.log; status is in build-attempt-01.status. The four-job run completed successfully in 1,529.16 seconds. The successful log and exit status are build-attempt-02-parallel.log and build-attempt-02.exit. PlatformIO re-scanned and recompiled simulator objects during the retry. It emitted non-fatal warnings for date formatting, a nonnull string call, and explicit Rect construction; no source change was made to silence them.
- Pre-build hooks regenerated nine generated HTML headers: ClippingsPageHtml, FilesPageHtml, FontsPageHtml, HomePageHtml, LibraryPageHtml, SettingsPageHtml, StatsPageHtml, ToDoPageHtml, and js/jszip_minJs (all .generated.h). They are intentionally excluded from PORT_SOURCE_DELTA.json’s source comparison. No active source, simulator-compatibility script, or PlatformIO configuration refers to the retired C: workspace. Eight generated Arabic-font headers retain old C: command strings as comments; archived QEMU logs also retain their original C: command lines. These are metadata/history, not active build dependencies.
- WSL Ubuntu 24.04.4, Python 3.12.3, SDL2 2.30.0, g++ 13.3.0, and PlatformIO 6.1.19 were used. The existing PlatformIO launcher remains in WSL’s system environment; its new Core/packages/cache were directed to ${NOOIR_D_ROOT}/.pio-home-wsl-x4classic-20261111. Temporary compiler files and run logs are under ${NOOIR_D_ROOT}/logs/x4classic-validation-20261111-run01/. Emscripten (emcc) is not installed.
- A read-only Git check confirmed the isolated port folder has no .git; Git resolves to parent ${NOOIR_D_ROOT}, branch codex/folio-nooir, HEAD b846e1550d18fe4e48014fcfec54b35e9da1ab57. The parent has the existing edits/untracked migration state listed above in the 2026-11-10 section. Today’s D: PlatformIO Core directory and validation outputs are untracked under that parent. Nothing was staged or committed.

### X4 Classic native simulator result

The D: build used simulator_x4classic and the pinned host simulator library. It compiled Nooir application sources for Linux/WSL and ran with SDL’s dummy video driver. The fresh virtual SD folder was ${NOOIR_D_ROOT}/logs/x4classic-validation-20261111-run01/sd/; it contained the preserved sample EPUB (1,243 bytes, SHA-256 c8e8de68968636258d0417b2ca059ec993f88f4c1d6d82779f91dca2cfbb68bc). The scripted input was RIGHT, LEFT, ENTER, DOWN, ESCAPE, then QUIT; no touch input was sent.

Confirmed in the new log: Hardware detect: X4 Classic, display initialization, RecentBooks → FolioLibrary → Reader → EpubReader, page 0 render/save, page 1 render/save, reader exit saves for recent/book state/reading statistics, and return to RecentBooks. The simulator wrote the expected progress/book-state/statistics files to the virtual card. Initial “open failed” lines are cache-miss probes on the empty card; the cache files were subsequently created and the run exited successfully.

| Capture | SHA-256 |
|---|---|
| Home screen (Finished shelf selected by the first key) | 075261a1acab5c0232bbe3bf4f13333936d93497f11dc33cc12ce867872c136e |
| Library | b365864df890baf57ec47b9611d86353d49a4cab86a6f421c30b0c0d7bece060 |
| Reader page 0 | 681e17e852ccfe02e7f68faa38f0c1e4504894c1c82d64dd94e8482d6e3701fd |
| Reader page 1 | 3db4f98dfb1ed3c0b1158b99b30bed310bd7ef8e234f93b34192d9d0c2584eca |
| After exit | 87442aa57a4517b293da2c4cf890f4aa8a8dd74c9694bc1c6cc801cf3a46de61 |

All captures are 480x800 BMPs. Runtime log SHA-256 is 562ec89ace26042898ed691406e4870ced1a3cc40bfd5a2689bf535ca982d4d2. The new run proves the host simulator flow from D:, not Classic SDMMC, PSRAM, panel controller, physical key GPIO, power, BLE, RTC, battery, or real e-paper behavior.

### X4 Pro boot diagnosis refinement

- The prior tests used x4pro,panel=uc8279-68. App-only/default and matching full virtual flash both failed to render Nooir Home.
- Captures were reviewed by QEMU serial route. ARDUINO_USB_MODE=1 and ARDUINO_USB_CDC_ON_BOOT=1 mean Arduino Serial selects the hardware USB Serial/JTAG CDC route. The 25-second app-only run and 20-second full-flash CPU-state run captured that route; each shows ROM startup but no Nooir application log. Longer 60/120-second captures wired to UART0 are not the application’s primary Serial route, so UART0 silence alone is not conclusive.
- ROM evidence is ESP-ROM:esp32s3-20210327, power-on reset and SPI flash boot, ending at entry 0x403c8898. QEMU CPU snapshots enter the flash-mapped application range (0x42185479, 0x421854d2). The 120-second UART0 trace ends at PC=0x4037a265, EXCCAUSE=0x4, EXCVADDR=0x0; this is not a panic signature. No Guru Meditation, panic, reset loop, or panel refresh was recorded. The 480x800 panel image remained blank.
- QEMU reports SD: Optional CMD52 not implemented and SD: Optional CMD5 not implemented in the extended run. These are emulator/card-model diagnostics, not Nooir serial logs and not proof of a mount failure.
- Source order in src/main.cpp is BoardConfig::holdPowerRails(), serial setup, HalSystem::begin(), GPIO/power/tilt/clock setup, device identity log, Storage.begin(), then settings and display setup. Thus the SDMMC traffic makes storage startup the strongest next breakpoint area, because it is before display initialization; it does not prove the guest is stuck there. Board identity is compile-time selected by the Pro profile, but its Nooir log was not captured.
- The existing PSRAM function-symbol mapping remains provisional: the rebuilt ELF’s app is 144 bytes shorter than the preserved Pro binary. PSRAM initialization is not yet proven against an exact ELF.
- Last confirmed stage: guest execution reached the flash-mapped app; QEMU also observed SD command traffic in the long full-flash run. First failed observable stage: expected Nooir startup logging on the USB Serial/JTAG route. Exact blocking function remains unknown. Display initialization/Home/Reader and successful SD mount remain unverified. This failure predates the D: migration; no migration regression is established.

### Web Emulator handoff

A separate, copy-ready compatibility note is docs/WEB_EMULATOR_S3_COMPATIBILITY_HANDOFF_2026-11-11.md. It recommends a host-HAL browser path for Classic, a separate Pro touch/Home mode, and genuine S3 guest execution through eink-qemu for Pro after the startup block is localized. It specifies which controls/peripherals are simulated, what can be shared safely, the current Classic QEMU gap, and the limits of Espressif’s general S3 emulator. No Web Emulator files were changed.

### Remaining hardware gaps and safety

No Pro or Classic was physically flashed or tested. The app-only S3 images still fit the inherited 6,553,600-byte app slot, but the retail X4 S3 partition/bootloader layout and exact board/panel revision have not been validated against a device. S3 PSRAM training, SDMMC card power/mount, panel/controller and orientation, physical keys, Pro GT911/Home, power/sleep, battery/RTC, BLE and frontlight remain hardware questions.

Any future device-writing session still requires a separate explicit authorization, retail model/board/panel confirmation, read-only existing flash/partition/security/eFuse inventory, a verified full backup and recovery route, and a model-matched app/partition plan validated against that unit. Do not modify a real bootloader, eFuse/security state or partition data. All S3 binaries remain experimental and not for flashing; the 16 MiB QEMU flash is EMULATOR ONLY — NOT FOR PHYSICAL FLASHING. No Git write operation, release, publish, physical flash or Web Emulator change was performed today.

Tomorrow’s next engineering action: build the Pro app and matching ELF from this D: source with dedicated D: PlatformIO/core/temp outputs, then use x4pro QEMU/GDB breakpoints through setup, Storage.begin() and display setup while capturing USB Serial/JTAG and UART0. Preserve Classic simulator success as a separate host-only result; begin Classic guest-emulation work only after a dedicated board model exists.


## 2026-11-11 later resume â€” Pro export evidence and paired-output status

This section supersedes the earlier same-day statement that no S3 firmware build or export had run. A later D: diagnostic record now proves successful Pro-only firmware builds. It does not complete the requested Pro/Classic matched-output set.

- Build record: diagnostics/s3-revalidation-20261111/pio-x4pro-rebuild.log, SHA-256 A553D0EDDDEA3859F8363F744EEC322B122F324236193B0660EB34E03651F4A9; matching exit file pio-x4pro-rebuild.exit contains 0. The log records two successful x4pro environment runs and app-only exports. It identifies the target as board esp32-s3-devkitc1-n16r8 on the pinned pioarduino ESP32-S3 platform.
- Two separately named exports are present: artifacts/Folio-Nooir-v1.6.6-X4PRO-EXPERIMENTAL-NOT-FOR-FLASHING-2.bin and ...-3.bin. Each is 5,671,104 bytes, SHA-256 5C261706D52D35B73879CA65918FB012F36AA65DCDF44B24D8F51251D9B011DC. Their separate manifests identify X4PRO, ESP32-S3 app-only, 6,553,600-byte app slot, and physical validation pending. These are fresh Pro BIN exports; they are not release or flash images.
- The current .pio/build/x4pro/firmware.bin is 5,671,104 bytes with SHA-256 0A0B5BB6EAA47EC4CDACC2A6AF7F1D2A7166FF913658D86C555AAC23AAA69BA9. The current .pio/build/x4pro/firmware.elf is 86,746,476 bytes with SHA-256 BD768427C55B500A87B2FFE3A39542A7053E102139B884AC103A9224765017E2. The live BIN digest differs from both exported BIN digests. Because the exporter copies the image bytes unchanged, the current ELF cannot be certified as the matching ELF for the preserved -2/-3 exports from this filesystem snapshot. Keep the files intact and treat the Pro BIN/ELF pairing as unresolved until a single captured build record ties both exact files together.
- No .pio/build/x4classic output directory or fresh Classic firmware BIN/ELF pair is present. The prior protected Classic app-only BIN remains 5,676,432 bytes, SHA-256 3309E364639D53FFE1602BEFB6B2C9FC62E66D088E0BC383833E2CF0C55F804A. The prior protected Pro app-only BIN remains 5,671,248 bytes, SHA-256 7E4AC1C63A24005383A7EBF2A63FD988A96BBB1BD421C990C334E5466E800494.
- Classic SDL2/native validation remains a host-only pass: the 2026-11-11 simulator build and scripted EPUB navigation/exit/progress-save run are recorded under ${NOOIR_D_ROOT}/logs/x4classic-validation-20261111-run01. Later classic-replay-02 and classic-replay-03 logs under diagnostics/s3-revalidation-20261111 also return exit status 0. None uses the ESP32-S3 firmware BIN.
- The fresh Pro outputs have not been shown to boot in QEMU. The earlier X4 Pro QEMU investigation still stops before captured Nooir startup/Home/Reader; physical X4 Pro/Classic behavior is unverified. No device was flashed.
- Prebuild generated-source record: diagnostics/s3-revalidation-20261111/prebuild-generated-manifest.json covers 12 generated files. All 12 saved backup files match their recorded prebuild hashes. The current versions of nine generated HTML headers differ from those hashes; the three generated I18n files match. These generated files are excluded from PORT_SOURCE_DELTA.json. Preserve the saved copies and current files; do not treat the source snapshot as byte-identical to the prebuild state until the generator inputs and output drift are reviewed.
- The original source comparison remains 38 entries (21 changed, 17 added, 0 missing). This documentation update refreshes only PORT_HANDOFF hash references in PORT_SOURCE_DELTA.json; firmware source-delta entries and their recorded source hashes are otherwise preserved. The prior PORT_HANDOFF SHA-256 was 593A31C09F8E37F633ECA6969D8E9ADF7D064DB034013A730CC4264646A78736; the prior PORT_SOURCE_DELTA.json SHA-256 was F00E7B869DCAA2BBBD612D1305DE6814E55E80AAFF4B54DB16A4849B7289958F.

### Next safe S3 work

1. Preserve the current Pro exports, manifests, build log, ELF, BIN, prebuild backups, and simulator/QEMU records. First reconcile the exported Pro digest with the current .pio BIN/ELF before calling them a matched pair.
2. In a future build session, produce separate fresh Pro and Classic outputs with an ELF and build log tied to each BIN by exact hashes. Keep output names device-specific and never overwrite preserved artifacts.
3. Re-test the fresh Pro candidate in the available QEMU setup and record the first failing startup milestone. Keep the known startup issue open.
4. Keep physical flash, release, push, merge, and publication out of scope unless separately authorized.

## 2026-11-11 D: S3 port revalidation — fresh Classic and Pro results

This section supersedes the earlier same-day statements that no fresh Pro build existed or that Pro had never reached Nooir Home. It records the D: revalidation only. It does not establish physical-device safety.

### Scope, source and preservation

- Authoritative source used: `${NOOIR_D_ROOT}\_c-side-migration-20261110\_isolated_nooir_x4_pro_classic_v166`. The project is based on Folio Nooir v1.6.6 source commit `b6d428a3187afc25d434fcb475e9ab436099c1ba`; no firmware source changes were made during this revalidation.
- The isolated directory has no `.git`; Git resolves to parent `${NOOIR_D_ROOT}`, branch `codex/folio-nooir`, HEAD `b846e1550d18fe4e48014fcfec54b35e9da1ab57`. Parent status marks the whole isolated directory untracked. This is the existing migration/worktree shape, not a clean per-file branch diff. Nothing was staged, committed, pushed or merged.
- Original X3/X4, v1.6.7, BLE, SPI-array, FAT and Web Emulator source were not modified. No device was flashed. Every new build, temporary file, log, card and capture from this work is on D:.
- Existing official/previous experimental artifacts were left intact. Preserved app-only Pro remains 5,671,248 bytes, SHA-256 `7e4ac1c63a24005383a7ebf2a63fd988a96bbb1bd421c990c334e5466e800494`; preserved Classic remains 5,676,432 bytes, SHA-256 `3309e364639d53ffe1602befb6b2c9fc62e66d088e0bc383833e2cf0c55f804a`.

### Toolchain and S3 firmware builds

Both PIO environments built successfully from the D: source with PlatformIO Core 6.1.19, Espressif32 55.03.37, Arduino-ESP32 3.3.7 / ESP-IDF 5.5.2, esptool 5.1.2 and Xtensa GCC 14.2.0+20251107. PlatformIO package/cache and TEMP/TMP were directed to D:. Logs and exit markers are in `diagnostics\s3-revalidation-20261111\pio-x4pro-rebuild.log`, `pio-x4pro-rebuild.exit`, `pio-x4classic-rebuild.log` and `pio-x4classic-rebuild.exit`.

| Target | Build | App slot use | Current PIO app BIN | Current PIO ELF |
|---|---|---|---|---|
| `x4pro` | PASS | 5,670,559 / 6,553,600 B (86.5%); RAM 66,724 / 327,680 B (20.4%) | 5,671,104 B, SHA-256 `0a0b5bb6eaa47ec4cdacc2a6af7f1d2a7166ff913658d86c555aac23aaa69ba9` | 86,746,476 B, SHA-256 `bd768427c55b500a87b2ffe3a39542a7053e102139b884ac103a9224765017e2` |
| `x4classic` | PASS | 5,675,751 / 6,553,600 B (86.6%); RAM 66,724 / 327,680 B (20.4%) | 5,676,288 B, SHA-256 `a1ece7c1897ab39ac24c6e30d0e0331b29a62925d28fcf52657d39cb92dcabab` | 86,770,716 B, SHA-256 `bc412ce4b3587fe6248b80aa5c51793704f7e2d8b228a68c3ed1ccd44549e44c` |

The exact target names remain distinct; both use the ESP32-S3 DevKitC-1 N16R8 build profile, 16 MiB DIO flash and 8 MiB OPI PSRAM. They are not interchangeable with C3 or with each other. Existing partition CSV/hash remains unchanged; physical bootloader, retail partition layout and board revision remain unverified.

Artifact reproducibility is **PARTIAL**, not a bit-for-bit clean-build pass. Fresh app-only exports were append-only and preserved:

- Pro `...X4PRO...-2.bin` and `...-3.bin` are each 5,671,104 B and byte-identical, SHA-256 `5c261706d52d35b73879ca65918fb012f36aa65dcdf44b24d8f51251d9b011dc`.
- Pro `...X4PRO...-4.bin` is 5,671,104 B, SHA-256 `0a0b5bb6eaa47ec4cdacc2a6af7f1d2a7166ff913658d86c555aac23aaa69ba9`; it matches the current `.pio\build\x4pro\firmware.bin` used in the passing QEMU Home capture. Its manifest remains `EXPERIMENTAL-NOT-FOR-FLASHING`, app-only, physical validation pending. The logged PIO build records `-2` and `-3`, not the later `-4` creation, so the full exporter/build provenance chain still needs reconciliation.
- Classic `...X4CLASSIC...-2.bin` is 5,676,288 B, SHA-256 `a1d73922e1c0c1bf3764e37dc5e37464fb945a1e34050956ac35b47105b8b33c`; `...-3.bin` is 5,676,288 B, SHA-256 `a1ece7c1897ab39ac24c6e30d0e0331b29a62925d28fcf52657d39cb92dcabab`, matching the current `.pio\build\x4classic\firmware.bin`. The primary Classic build log records `-2`; the later matching `-3` should not be described as an independently reproducible rebuild until its build log is tied to it.
- Thus both device configurations compile and fit the inherited app slot. The differing same-size Pro and Classic export hashes mean reproducibility across separate invocations is unresolved. Preserve every output; do not replace old artifacts or infer that the hashes differ only due to timestamps.
- `test\python\test_firmware_artifacts.py -v` passed 5/5; record: `diagnostics\s3-revalidation-20261111\firmware-artifact-tests-corrected.log`. A preceding invocation using an invalid unittest import form failed before running; the corrected direct invocation is the passing result.

The build hook regenerated nine compressed HTML headers with gzip time metadata. Before restoring anything, the build-generated versions were copied to `postbuild-generated-backup` with `postbuild-generated-manifest.json`. All 12 files in `prebuild-generated-backup` matched their pre-build manifest; all 12 generated sources were then restored and rehashed to the exact pre-build SHA-256 values. No I18n generated file changed. The generator timestamp is a potential reproducibility input, but it has not been shown to explain the differing app BINs.

### X4 Classic SDL2 simulator — PASS

A fresh run of `simulator_x4classic` completed from D: using WSL Ubuntu 24.04.4, SDL2 2.30.0 and the existing CrossPoint host simulator. Simulator executable: `.pio\build\simulator_x4classic\program`, 15,092,112 B, SHA-256 `3cb2f787d85be6248cacbfeb3f189fa41ca21946e32a40bec263017698a6e445`. Full replay is `diagnostics\s3-revalidation-20261111\classic-replay-03`; its log is `simulator.log`, SHA-256 `ade16960e1e39a45fa984b67f93a4bf41c047644217e3ccd732b64698e4dc8d9`.

With a folder-backed host SD and a 1,243-byte multipage EPUB (SHA-256 `c8e8de68968636258d0417b2ca059ec993f88f4c1d6d82779f91dca2cfbb68bc`), the scripted sequence rendered Home, navigated to Library, opened Reader, rendered page 0 then page 1, saved reading progress/book state/statistics, exited Reader, returned to RecentBooks and quit with exit code 0. Five BMP captures and their hashes are preserved in the run folder. This validates the Nooir host simulator flow only; it does not test the S3 firmware BIN, real SDMMC, PSRAM, panel controller, GPIO, power, BLE, RTC or physical e-paper.

### X4 Pro QEMU startup — Home reached; normal powered interaction still incomplete

The current `.pio\build\x4pro\firmware.bin` and matching current `firmware.elf` were run together in pinned eink-qemu 0.9.2 on `x4pro,panel=uc8279-68`. The command used the valid disposable sparse 2 GiB FAT32/MBR card at `diagnostics\s3-revalidation-20261111\pro-qemu-sd-2g-16k.img`; the card has one 1,243-byte test EPUB and was reused in place. QEMU temporary flash/log paths were directed to D:.

Confirmed sequence in `diagnostics\s3-revalidation-20261111\pro-qemu-fresh-home-capture-12` (exit 0): ROM boot; X4 Pro UC8279 panel detection; SDMMC card mounted; settings loaded as first-boot defaults; `Starting CrossPoint version 1.6.6-x4p-exp-no-flash`; display initialized; Boot activity; RecentBooks activity; shelf `render_ready`; panel refresh completed. The Home frame is `frames\0002-51049ms.png`, 480x800, SHA-256 `ed22e869007ea9531c5abd9840e2dc8e906e697cf4b7b553cf4f620085571f9b`. The same capture is also in `shots\home.png`. This is a software-emulator Home render, not hardware validation.

Root cause of the earlier “no Nooir logs/Home” observation is now localized for this QEMU setup:

1. The fresh ELF maps the slow early S3 path into Espressif OPI PSRAM initialization/test (`s_test_psram`; earlier HMP/ELF mapping recorded in `pro-qemu-exact-elf-03`). Nooir's first application log is deliberately delayed in `src\main.cpp` until after `verifyPowerButtonWakeup`, with a source comment explaining this avoids changing power-button timing.
2. In the fresh QEMU traces, that check is reached at about 48.8 seconds of firmware time after the S3/PSRAM startup path. eink-qemu's X4 Pro default cold-boot hold is 1,500 ms. It releases Power long before Nooir reaches the check. `HalGPIO::verifyPowerButtonWakeup` then rejects the stale/released Power wake and requests deep sleep. The short run therefore stops before the first Nooir log; HMP showed the CPU in the idle/finish-check path, EXCVADDR=0, EXCCAUSE=level-1 interrupt, with no panic/reset evidence.
3. A 50,000 ms hold produced a visible Home frame, but the continued long Power event caused normal power handling to enter low-power mode; it is not a clean interactive boot. A 49,000 ms hold passed the boot check and reached RecentBooks, but still generated a low-power transition after release. This is an emulator input/startup-timing interaction, not a firmware crash and not evidence that retail hardware needs a 49-second physical key hold.
4. The standard eink-qemu `ready` detector is not a reliable Pro oracle here: it reported two of three expected boot refreshes and timed out although logs show `RecentBooks`, `render_ready`, a completed panel refresh and the automatic Home frame. The first log-wait attempt also used the default 60-second timeout, shorter than the observed host runtime; it was rerun with a 240-second bound. The final log-driven sequence (wait for app version, RecentBooks and render; wait 3 guest seconds; capture) exited 0.
5. The `shots/home.png` explicit capture and automatic settled frame share the exact SHA-256 above; the Home UI is confirmed in the automatic frame. Do not rely on the earlier pre-settle `frames/0002-50827ms.png`, which was captured before the shelf waveform completed.

A first disposable card was formatted as a superfloppy and QEMU reported `p1 type=0x00`; this was a fixture defect. The corrected sparse card is MBR/FAT32 type `0x0c`, QEMU `sd ls` sees the test EPUB, and the latest run logs `SDMMC card mounted`. Therefore that earlier mount failure is not a remaining Pro firmware failure. QEMU still logs `[ERR] [GYR] SDK IMU not found`; that optional IMU is not modeled and did not block Home. Pro Library navigation, touch/Home input, EPUB reading, progress, wake/sleep, physical SDMMC and real panel behavior remain untested.

### Migration findings versus existing S3/QEMU limits

- **No S3 source migration regression established:** both PIO targets compile from the authoritative D: tree; the Classic native replay passes; the Pro fresh build reaches Home in its QEMU model. Active project build/temp paths used here are D:.
- **D: fixture issue found and corrected:** initial Pro test card lacked an MBR; no firmware change was required. A first earlier QEMU invocation had used the default C: temporary directory, but that transient was removed after its run; all runs in this revalidation set `TEMP`/`TMP` to the D: diagnostics directory. Existing C: text in archived logs/generated comments is historical, not an active dependency.
- **Pre-existing/current emulator limitations:** S3 OPI PSRAM execution is very slow in this QEMU model; the default 1.5-second Power hold expires before app code. A long synthetic hold later produces the app's normal Power/low-power input. The CLI ready detector expects a refresh sequence not present in this run. No X4 Classic QEMU machine is available. These are not evidence of a D: migration fault.
- The isolated source folder still inherits parent Git metadata and is entirely untracked at parent level. This is a repository/migration state issue, not a firmware regression. Do not repair/reset it as part of S3 validation.

### Web Emulator integration handoff

The copy-ready integration document `docs\WEB_EMULATOR_S3_COMPATIBILITY_HANDOFF_2026-11-11.md` has been updated with the evidence and interface plan. In brief: the C3 QEMU backend cannot execute ESP32-S3 firmware. Start Classic with a source-built host-simulator browser backend (not the Classic S3 BIN); keep Pro as a distinct profile with GT911 tap/hold/swipe and a separate capacitive Home input. For real Pro BIN execution, use a separate S3/eink-qemu WASM backend and carry its measured PSRAM/power-startup behavior into the user experience. There is no verified Classic S3 QEMU machine. Share the logical display surface and test-book/card fixture through a browser storage adapter; do not duplicate multi-gigabyte SD images. No Web Emulator code was opened or changed.

### Next steps

1. Reconcile the byte-identical app BIN, ELF and exporter log for each target. Preserve the older/newer exports; use isolated output/build directories for any repeatability test rather than cleaning or overwriting the current `.pio` evidence.
2. For Pro guest emulation, investigate an emulator-side power event that satisfies the initial wake check and then releases without being interpreted as an ordinary long press. Do not patch Nooir firmware for QEMU. Until resolved, Home-render evidence is partial functionality, not a normal interactive Pro session.
3. Add a log-driven Pro test instead of `ready` until the pinned CLI readiness heuristic is calibrated; test Pro touch/Home and a book only after the key lifecycle is correct.
4. Keep the S3 handoff separate from Web Emulator implementation. Require exact board/revision, pin map, controller, partition/security data and physical recovery evidence before any device writing. No binaries are approved for flashing or distribution.
5. Keep all follow-up build/test output, temporary files, cards and logs on D:. Preserve both generated-source backup trees and this handoff backup.
## 2026-11-11 fresh X4 Pro / Classic firmware builds and Pro guest boot

This latest section supersedes earlier same-day statements that no fresh S3 builds existed, that the matching Pro ELF was unresolved, or that Pro Home had not been reached. The port source remains based on official Folio Nooir v1.6.6 commit b6d428a3187afc25d434fcb475e9ab436099c1ba. Current PORT_SOURCE_DELTA.json source entries were rechecked: 38 entries, 0 missing or mismatching. No application source or device configuration was changed for these builds.

### Fresh device-specific artifacts

Both requested builds succeeded with the isolated D: PlatformIO installation and their existing, separate environments. These are app-only experimental images; no hardware was flashed.

| Device | BIN / size / SHA-256 | Matching ELF / size / SHA-256 | App identity |
|---|---|---|---|
| X4 Pro | artifacts/fresh-20261111/folio-nooir-v1.6.6-x4pro-EXPERIMENTAL.bin; 5,671,104 bytes; 0a0b5bb6eaa47ec4cdacc2a6af7f1d2a7166ff913658d86c555aac23aaa69ba9 | artifacts/fresh-20261111/folio-nooir-v1.6.6-x4pro-EXPERIMENTAL.elf; 86,746,476 bytes; bd768427c55b500a87b2ffe3a39542a7053e102139b884ac103a9224765017e2 | Folio Nooir X4 Pro; 1.6.6-x4p-exp-no-flash |
| X4 Classic | artifacts/fresh-20261111/folio-nooir-v1.6.6-x4classic-EXPERIMENTAL.bin; 5,676,288 bytes; a1ece7c1897ab39ac24c6e30d0e0331b29a62925d28fcf52657d39cb92dcabab | artifacts/fresh-20261111/folio-nooir-v1.6.6-x4classic-EXPERIMENTAL.elf; 86,770,716 bytes; bc412ce4b3587fe6248b80aa5c51793704f7e2d8b228a68c3ed1ccd44549e44c | Folio Nooir X4 Classic; 1.6.6-x4c-exp-no-flash |

Both images identify as ESP32-S3 and use board esp32-s3-devkitc1-n16r8, 16 MiB DIO flash at 80 MHz, dio_opi 8 MiB OPI PSRAM configuration, and the unchanged partitions.csv (SHA-256 98e02c50532c388d85cc3b7c96d004142dc713b23368d293c881ecdcb419e44e). Each app slot is 6,553,600 bytes; the Pro and Classic images occupy approximately 86.5% and 86.6%. Pro touch is enabled; Classic touch is disabled. Original X4/X3 target macros are off in both builds, and the matching Pro or Classic macro is on.

esptool image-info identified each target, project name, version, DIO/80 MHz flash settings, and valid checksum/image hash. The embedded ELF SHA-256 in each BIN exactly matches its standalone ELF. Both builds report 66,724 / 327,680 bytes static RAM. Manifests beside each canonical BIN include these identities, partition settings, build-log hashes, and a no-flash safety declaration.

Build environment was PlatformIO Core 6.1.19 from ${NOOIR_D_ROOT}\.venv\Scripts\pio.exe, PIO home ${NOOIR_D_ROOT}\.pio-home, platform pioarduino/espressif32 55.03.37, Arduino-ESP32 3.3.7, ESP-IDF 5.5.2, Xtensa 14.2.0+20251107, and esptool 5.1.2. Successful logs are:
- Pro: ${NOOIR_D_ROOT}\logs\x4-s3-firmware-build-20261111-run01\build-x4pro-attempt-03-utf8.log; exit 0; 119.60 seconds; SHA-256 ed32d1a71b09411251e8a192392025654c94948a40cb6f32834670a3369c4b5a.
- Classic: ${NOOIR_D_ROOT}\logs\x4-s3-firmware-build-20261111-run01\build-x4classic-attempt-02-serial.log; exit 0; 145.68 seconds; SHA-256 642efa96b0a295f7387c42da764766ac1bd67bc46e505fd6f9e447df52babe53.

The only warning in either final build log was that Windows Long Path Support is disabled. A prior Classic parallel attempt failed with a Windows object-file lock; its log also reported wolfSSL macro redefinitions. The serial retry succeeded using cached objects. A Pro attempt launched from the C: conversation directory failed immediately because that directory was not a PlatformIO project; the next Pro attempt hit a CP1252 logger encoding exception and was interrupted. Logs are retained. No global toolchain or SDK was changed.

The previously preserved app-only BINs remain byte-identical: Pro 5,671,248 bytes, SHA-256 7e4ac1c63a24005383a7ebf2a63fd988a96bbb1bd421c990c334e5466e800494; Classic 5,676,432 bytes, SHA-256 3309e364639d53ffe1602befb6b2c9fc62e66d088e0bc383833e2cf0c55f804a. Their original matching ELF files are not present in the isolated tree or D: build/log inventory; their BIN headers retain previously recorded ELF digests (Pro f68a42e00843ddcc8b22cc88954b074688b3260fc6cf4e78da08a39a3ce63e5b; Classic cbfd346868bcca08d0c63eed947850dbce4e3bc82ae3376122718bb947766408). These images and all collision-suffixed exports remain preserved. Additional auto-export BINs are noncanonical: Pro -2 and -3 each hash to 5c261706d52d35b73879ca65918fb012f36aa65dcdf44b24d8f51251d9b011dc; Pro -4 matches the fresh canonical Pro BIN; Classic -2 hashes to a1d73922e1c0c1bf3764e37dc5e37464fb945a1e34050956ac35b47105b8b33c; Classic -3 matches the fresh canonical Classic BIN. None was deleted or overwritten.

The earlier five firmware-artifact checks remain recorded as 5/5 passed (2026-11-06); they were not rerun in this build turn. Fresh build validation used successful environment-specific builds, esptool image-info, file size and SHA-256 verification, and embedded-ELF-hash comparison. The prior Classic SDL2 host simulator pass was not rerun; it remains host-only and does not execute these S3 binaries.

### X4 Pro exact-build QEMU results

The latest Pro image was composed into a distinct 16 MiB QEMU-only flash and run with its exact matching ELF on eink-qemu 0.9.2, machine x4pro, panel uc8279-68, USB Serial/JTAG console, guest error/unimplemented diagnostics, and a virtual 2050 MiB SD card. This resolves the earlier blank-screen diagnosis: the default virtual Power hold is only 1,500 ms, while this app reaches its power-duration check at about 48.7 guest seconds after the S3 PSRAM startup test. With the default hold, gpio.verifyPowerButtonWakeup(...) fails and the app takes its deep-sleep branch before display setup. Holding virtual Power for 50,000–55,000 ms let the same BIN/ELF proceed. This is an emulator input-timing issue; it is not evidence of a firmware display-init defect.

The successful staged logs contain ROM startup (ESP-ROM:esp32s3-20210327, power-on SPI flash boot, entry 0x403c8898), then Nooir/ESP-IDF logs. The exact fresh ELF maps the early PC into s_test_psram; a 60-second heap capture reports PSRAM with 8,283,560 bytes free, 0 corrupt blocks. Runtime logs identify Hardware detect: X4 Pro, select UC8279 at 800x480, report [SD] SDMMC card mounted, and later [MAIN] Display initialized. The UI emits render_ready theme=Main; the 480x800 Home screenshot is diagnostics/eink-qemu-0.9.2/pro-fresh-build-20261111-run01/qemu-powerhold-55000/shots/after-60s.png (SHA-256 ed22e869007ea9531c5abd9840e2dc8e906e697cf4b7b553cf4f620085571f9b). There was no panic or reset. QEMU reports an unsupported IMU and virtual flash/optional SD commands; these are emulator-model limits.

A separate touch run reached Home and switched to FolioLibrary. Another run opened the virtual Books folder and displayed the staged EPUB row. Its first --put destination accidentally created a folder named Nooir-QEMU-Pro.epub, so the EPUB was nested one level deeper than intended. A follow-up selected its metadata/cover preview but did not enter Reader. The corrected /Books staging run stalled on its first touch and remained at RecentBooks; it was stopped with Ctrl+C after the guest kept running without a screen change. These runs had no guest panic, but EPUB open, page turn, Reader exit, and progress persistence are not confirmed. Best verified boot stage: application entry and Nooir logs, PSRAM allocator available, X4 Pro identity, SDMMC mount, display initialization, Home render, and Library navigation in the successful touch run. First unverified functional stage is opening the EPUB Reader.

QEMU evidence is under diagnostics/eink-qemu-0.9.2/pro-fresh-build-20261111-run01/:
- qemu-exact-build-trace-02/: exact-build USB serial, QEMU diagnostics, heap/PC mapping; run log SHA-256 23fdff676e2c113e05ca50355b88bfa4a938162067e7835d92f244d7fe3b5ee5.
- qemu-powerhold-55000/: Home, 2 panel refreshes, exact ELF heap map; run.jsonl SHA-256 a6db2e4874684f8d44a792e256637a8254cbdb63ab19b7ef324daceb2f4c08c5.
- qemu-home-input-04/: Home and Library touch; run.jsonl SHA-256 52cb256fb3376557f31dcf9797ca0f08a40b0c2bf0a1905165441bfe1416d35f.
- qemu-books-input-05/: Books folder and EPUB row; run.jsonl SHA-256 19e48b567730d5e08f535a2c531a509fac9af1a32021e14889b941298b54a113.
- qemu-reader-06/: preserved mis-staged EPUB/metadata-preview attempt; run.jsonl SHA-256 c09db5c3d5c79b2a235f235dc979bd44186906991d20629845070ede4d58ca54.
- qemu-reader-corrected-07/: corrected card staging, no reliable touch transition, preserved stopped run; run.jsonl SHA-256 a3bbfb5fc7462e481bce67dbd2f5017bda0cc8983234f8edba6dde69138c0db.

The latest emulator-only full flash is diagnostics/eink-qemu-0.9.2/Folio-Nooir-v1.6.6-X4PRO-FRESHBUILD-EMU-ONLY-NOT-FOR-PHYSICAL-FLASH-20261111-16MiB.bin, 16,777,216 bytes, SHA-256 b8389fa4e55be012411a6e5bac9e6a10724e04903b9dbbb0e8764023a61c392d. It uses the matching .pio/build/x4pro bootloader, partitions.csv table, and fresh Pro application at verified offsets 0x0, 0x8000, and 0x10000; app1 remains at 0x650000. Bootloader slice: 18,736 bytes, SHA-256 09c6a477ac9b33721e9ea455fda1e29dc5b2f35c85d0b858b693defcab6f19fb. Partition slice: 3,072 bytes, SHA-256 bd0f7954aca2ef7d925ee21aaa1f3dc8822d1d6ce5cbbd26a135e5886bfff6ce. App slice exactly matches the fresh Pro BIN. Reproducible composition command form:

    eink-qemu flash .pio/build/x4pro diagnostics/eink-qemu-0.9.2/Folio-Nooir-v1.6.6-X4PRO-FRESHBUILD-EMU-ONLY-NOT-FOR-PHYSICAL-FLASH-20261111-16MiB.bin --machine x4pro

This 16 MiB image is for QEMU only and must never be used with esptool or a physical installer.

### Classic status, D: policy, and safety

The fresh X4 Classic S3 BIN compiled successfully with the correct touch-disabled profile. The Linux/WSL SDL2 simulator’s prior EPUB open, page-turn, exit, and progress-persistence result remains valid as a host test; it was not rerun and does not validate this firmware BIN or physical SDMMC/GPIO/panel/PSRAM/power behavior. eink-qemu has no X4 Classic machine, so there is no Classic guest boot result.

Project source, canonical firmware artifacts, manifests, build logs, PIO caches, and QEMU outputs were kept on D:. One completed Pro touch run (qemu-home-input-04) recorded QEMU's temporary synthesized flash under ${WINDOWS_TEMP}\eink-qemu-GbLZmw\flash.bin because that process inherited Windows TEMP. This path is disclosed as a one-run temporary-location exception; it was not a source/build output, no C: workspace was recreated, and no cleanup was attempted. All later QEMU runs explicitly set TEMP/TMP/TMPDIR to the D: task temp directory. The earlier misdirected Pro PlatformIO command also records the C: shell directory in its failed log, but exited before starting a project build.

No Pro or Classic was flashed. No bootloader, eFuse, security setting, or real partition data changed. The original published v1.6.6 source archive, original S3 app-only images, and unrelated v1.6.7/Web projects were not changed. No release, publish, push, merge, or deployment occurred. No Git operation beyond read-only status was done; the isolated source has no .git, and the parent D: repository remains dirty with pre-existing edits/untracked migration folders (branch codex/folio-nooir, HEAD b846e1550d18fe4e48014fcfec54b35e9da1ab57). No build, simulator, or QEMU process remains running.

Physical flashing remains explicitly out of scope. Any future device-writing session requires separate explicit authorization, exact retail board/revision/panel identification, a read-only existing boot/security/eFuse/partition inventory, verified complete backup and tested recovery method, and a model-matched bootloader/partition/app plan validated against that unit. None of those prerequisites is established here.
## 2026-11-11 exact build feature switches and latest Pro boot capture

The `x4pro` and `x4classic` environments share the v1.6.6 S3 configuration: PSRAM-backed framebuffer and block-device support are enabled; BLE HID host, USB mass storage, frontlight, and warm light capabilities are disabled; serial logging is enabled at log level 2. Both profiles set `NOOIR_DISABLE_FIRMWARE_FLASHING=1` and `NOOIR_EXPERIMENTAL_DEVICE_BUILD=1`. The former blocks SD `FirmwareFlasher` APIs; the latter hides updater/recovery routes and makes the OTA updater return `NO_UPDATE` (the updater object still compiles). Pro and Classic device identity macros remain separate; Pro touch is enabled and Classic touch is disabled. These build switches do not establish retail hardware behavior. Pro Reader behavior is now confirmed in the X4 Pro QEMU machine only, as recorded below.

The newest exact-build Pro capture is `diagnostics/s3-revalidation-20261111/pro-qemu-fresh-home-capture-12` (eink-qemu 0.9.2, `x4pro,panel=uc8279-68`, USB Serial/JTAG, 49,000 ms virtual boot Power event, matching current `x4pro` BIN/ELF and 16 MiB emulator flash). Its serial trace records `ESP-ROM:esp32s3-20210327`, POWERON/SPI flash boot, `[MAIN] Hardware detect: X4 Pro`, `[SD] SDMMC card mounted`, `Starting CrossPoint version 1.6.6-x4p-exp-no-flash`, `[MAIN] Display initialized`, and `[SHELF] render_ready theme=Main`. The run completed two panel refreshes and captured Home at `frames/0002-51049ms.png` / `shots/home.png` (SHA-256 `ed22e869007ea9531c5abd9840e2dc8e906e697cf4b7b553cf4f620085571f9b`). `run.jsonl` SHA-256 is `2e9dcb6471947e66edeada8d499179c3bc7ad27fc2f5ed61931d6d44d88d54ae4`; `console.jsonl` SHA-256 is `2654535bd377bc75b69a93b27d7ccedb0fb0a504a0f7a8d6596d796a9348bb45`; `log.txt` SHA-256 is `f9e6dbf7f824d451d19405425294eae30e639a39857905d0abc3b6a99c8bb933`.

This latest capture confirms application startup through Home in the emulator. Earlier exact-build logs and heap capture provide the PSRAM allocator evidence; separate input capture reached FolioLibrary. EPUB Reader entry, page turning, reader exit, and progress persistence remain unverified. The emulator's long virtual Power timing can subsequently trigger its power/low-power path, so this is not a normal interactive session or physical compatibility result. Classic remains a successful prior host SDL2 simulator result only; its S3 BIN has not been run in an ESP32-S3 guest.

## 2026-11-11 Pro Reader validation, Classic review, and update-safety audit

This newest section supersedes older same-day statements that the Pro Reader had not been reached or that its EPUB behavior was unverified. It does not change application source or firmware. No rebuild, physical flash, release, push, merge, or deployment occurred. All new QEMU outputs, screenshots, temporary flash, and logs are under `diagnostics/s3-reader-validation-20261111/` on D:.

### Exact Pro firmware and emulator setup

The run used the certified, unchanged X4 Pro pair:

- BIN `artifacts/fresh-20261111/folio-nooir-v1.6.6-x4pro-EXPERIMENTAL.bin`, 5,671,104 bytes, SHA-256 `0a0b5bb6eaa47ec4cdacc2a6af7f1d2a7166ff913658d86c555aac23aaa69ba9`.
- ELF `artifacts/fresh-20261111/folio-nooir-v1.6.6-x4pro-EXPERIMENTAL.elf`, 86,746,476 bytes, SHA-256 `bd768427c55b500a87b2ffe3a39542a7053e102139b884ac103a9224765017e2`.
- The unchanged canonical X4 Classic pair was re-hashed: BIN `a1ece7c1897ab39ac24c6e30d0e0331b29a62925d28fcf52657d39cb92dcabab`; ELF `bc412ce4b3587fe6248b80aa5c51793704f7e2d8b228a68c3ed1ccd44549e44c`.

Pinned eink-qemu 0.9.2 ran the matching Pro build directory and ELF with machine `x4pro`, panel `uc8279-68`, USB Serial/JTAG logs, guest diagnostics, and a virtual 2050 MiB SD card. It composed its virtual flash from the matching build's bootloader at `0x0`, partition table at `0x8000`, and application at `0x10000`. The preserved QEMU-only 16 MiB composition is `diagnostics/eink-qemu-0.9.2/Folio-Nooir-v1.6.6-X4PRO-FRESHBUILD-EMU-ONLY-NOT-FOR-PHYSICAL-FLASH-20261111-16MiB.bin`, 16,777,216 bytes, SHA-256 `b8389fa4e55be012411a6e5bac9e6a10724e04903b9dbbb0e8764023a61c392d`. Keep this file strictly for the emulator. Do not pass it to a physical flasher.

The cold-boot virtual Power hold remains required: use `--power-hold 50000` (50,000 guest ms) on the supported X4 Pro machine. The default 1,500 ms hold ends before the application finishes its OPI PSRAM startup/power-button check, causing the firmware to take its sleep branch before Home. This is a QEMU boot-input timing workaround; it does not change the image.

### Pro Reader results

| Milestone | Result | Evidence / limit |
|---|---|---|
| Bootloader reaches app; Nooir startup logs | **Pass in QEMU** | ESP32-S3 ROM POWERON/SPI-flash boot followed by application logs. No panic or reset. |
| PSRAM initialization and profile | **Pass in QEMU** | Heap snapshots show a healthy PSRAM allocator; runtime log identifies `Hardware detect: X4 Pro`. This does not validate physical PSRAM training or margin. |
| Display and Home | **Pass in QEMU** | UC8279 800×480 initialization, `Display initialized`, and Home screenshot. |
| Virtual SDMMC | **Pass in QEMU** | `[SD] SDMMC card mounted`; the virtual card served the staged EPUB and accepted progress writes. Physical card/power behavior remains unverified. |
| EPUB open / Reader initialization | **Pass in QEMU** | `Reader` and `EpubReader` activity-entry logs; Reader page-render pipeline completed. The captured Reader screenshot shows page 3/10 because input/progress advanced before the capture; it is not an isolated page-1 screenshot. |
| First content page | **Rendered in the initial session** | Initial progress writes start at spine 0/page 0; the first preserved post-entry frame was later in the book. The logs prove EPUB rendering, but no dedicated screenshot of the exact first page was retained. |
| Next/previous navigation | **Pass in QEMU** | `down` and `up` each caused a completed render; screenshots differ for next and return to the prior page. |
| Reader exit | **Pass in QEMU** | `home` entered RecentBooks; a touch returned to FolioLibrary. The final Library screenshot matches the prior Library capture hash. |
| Progress save / reopen | **Pass in QEMU** | `progress.bin` writes report success at spine 0/page 2, then page 3 and back to page 2. A subsequent QEMU session using the same virtual SD reopened at the saved 3/10 position, then navigated forward and back. |
| Clean deep sleep and wake | **Not verified** | The run logs show CPU low-frequency entry/restore, not a deep-sleep transition. QEMU 0.9.2 exposes a cold-boot Power hold and short post-boot key presses, but no configurable long post-boot power hold for the firmware's sleep action. No clean sleep/wake claim is made. |

Three representative runs are preserved:

- `run02-full-reader-flow/`: timing-control attempt; reached Home, but the scripted tap arrived after the guest had entered low-power mode. Exit 1 at `wait-screen`; this is a harness timing failure, not evidence of a touch defect. `run.jsonl` SHA-256 `dcb8228a030c154b8cbdc10f8a705246943f30f573391214ded96a90caebc4aa`.
- `run03-reader-open-corrected-card/`: Reader opened, rendered EPUB content and saved progress. A synthetic left swipe did not produce a settled screen transition before timeout (exit 1). The screenshot stayed on the rendered page. Treat swipe as an ineffective QEMU gesture in this sequence, not a demonstrated firmware page-render defect. `run.jsonl` SHA-256 `39fd98f22cf80a0be9d7dfbd93ee93570c9244e9af3f37e464c8c10a55084dca`; `console.jsonl` SHA-256 `d669c16f349f9d4a69827628f86bfe514f761832e134cd77725b9aaafc9d93ff8`.
- `run04-reopen-progress-page-keys-exit/`: final 27-step run passed, exit 0, 73.4 seconds. `run.jsonl` SHA-256 `072fc25e9f691724555b43cca8065fb7a842d5e68023f0b07791abe0bdc3f1e2`; `console.jsonl` SHA-256 `867a88b10065d31d9550121520c88851b4b1c4bb1ffb43af33df13cca56fab66`; `metrics.json` SHA-256 `dd849be107baF39421ba8c3e53dc53174b1a81c1c79f34095e4b240abfd67b65`.

The final Reader screenshot is `run04-reopen-progress-page-keys-exit/shots/reader-reopened.png` (480×800, pixel SHA-256 `0cae4b130409464b5cb20f75d879d5480802474dfc7488ded332437bb32d7a8e`). The next-page image hash is `faff833e08d88f5cb2f68f0c4007da4c2e83859166c1470a073177815fb7d650`; the previous-page capture returns to the reopened image hash. QEMU rendered three page events in the final run: first total 2,120 ms, then 1,324 and 1,314 ms; median 1,324 ms. It recorded 24 panel refreshes, 26 screen updates, zero warnings, and two error-level lines: the optional IMU was absent from the emulator model and fresh virtual NVS used defaults. No panic/reset was recorded. These are emulator timings, with no comparable same-settings port baseline for a performance claim.

Heap snapshots during the round trip showed PSRAM free between 8,283,560 and 8,288,684 bytes, largest block 8,283,552 bytes, and no corrupt blocks. Internal DRAM free was 258,404 bytes in Reader (largest 215,408) and 263,892 bytes after Reader exit (largest 222,304). This short run found no allocation failure or corruption; it is not a sustained memory-stress or physical PSRAM test.

### X4 Classic results and boundary

The prior Linux/WSL SDL2 run against the isolated Classic host-simulator profile remains the available result; it was not needlessly repeated in this turn. Its input replay covered button-style navigation, Library, EPUB open/render, page 0→1, Reader exit, and saved progress/book state/reading statistics. The log is `${NOOIR_D_ROOT}/logs/x4classic-validation-20261111-run01/classic-simulator.log` (SHA-256 `562ec89ace26042898ed691406e4870ced1a3cc40bfd5a2689bf535ca982d4d2`). Its host-folder virtual SD and SDL event loop do not execute or certify the fresh Classic S3 BIN, nor test physical SDMMC, GPIO, panel, PSRAM training, power, or buttons. A saved-progress reopen after simulator restart is not established by the preserved replay. eink-qemu 0.9.2 lists x3/x4/x4pro/picco machines and has no Classic machine, so Classic S3 guest boot remains unavailable.

The Classic firmware remains a separate device profile and build identity, with touch disabled. Do not infer Classic compatibility from the Pro QEMU result or SDL2 output.

### OTA, SD flashing, and recovery audit

Both S3 profiles set `NOOIR_EXPERIMENTAL_DEVICE_BUILD=1` and `NOOIR_DISABLE_FIRMWARE_FLASHING=1`. These are distinct guards:

- `NOOIR_DISABLE_FIRMWARE_FLASHING=1` protects the SD `FirmwareFlasher::validateImageFile()` and `flashFromSdPath()` APIs: both return `FLASHING_DISABLED`. This flag alone is not an OTA guard.
- `NOOIR_EXPERIMENTAL_DEVICE_BUILD=1` makes `OtaUpdater::checkForUpdate()` log that remote update is disabled and return `NO_UPDATE`; `installUpdate()` also returns `NO_UPDATE`. The generic OTA updater source/object is still compiled, but its transfer/install path is not reached under this build flag.
- The same experimental flag removes the Check for Updates and SD firmware-update entries/launch routes from Settings and compiles out the boot-time SD recovery key path in `main.cpp`. The SD firmware activity may be compiled, but its normal menu/recovery callers are excluded; direct flasher calls are blocked separately.
- OTA and SD firmware update/recovery were not invoked in QEMU. OTA slots remain in the unchanged v1.6.6 partition table. The experimental profiles therefore have no available in-device OTA or SD recovery route. If future tests flash a unit and it fails to boot, recovery would require an independently prepared external/model-correct restore method.

No OTA source or flag was changed in this validation. Before any future decision to enable OTA, require a device-specific signed/compatible manifest and image, app-slot and boot-selection/rollback validation, and a tested recovery route. Do not point these profiles at the generic public v1.6.6 C3 `firmware.bin`.

### Board-profile review and outstanding physical checks

The S3 SDK reference profiles keep Pro and Classic separate. Both builds configure 16 MiB DIO flash at 80 MHz, 8 MiB OPI PSRAM (`dio_opi`), `FREEINK_FB_PSRAM=1`, and one-bit SDMMC on CLK 41/CMD 42/DAT0 40. The QEMU Pro run demonstrates the configured PSRAM allocator/framebuffer path and virtual SDMMC path only.

- **X4 Pro:** touch enabled; reference profile uses GT911, including its GPIO2 active-low power enable, and a separate GPIO3 power key/side-button mapping. SD-card active-low power enable is GPIO5. The QEMU test selected a UC8279-68 panel variant. Retail X4 Pro batches may use other supported controllers, so controller identification and per-revision refresh/power behavior still need physical verification. Frontlight/warm-light capabilities are compiled out in this experiment.
- **X4 Classic:** touch disabled; button-only mapping is retained. Its active-low SD power enable is GPIO6, while display GPIO/panel command behavior is a separate Classic profile (the SDK reference omits the Pro-specific UC8279 PLL command). It has not been booted in QEMU or on hardware. Frontlight is absent/disabled in this profile.

The SDK docs contain reference pin findings; this validation did not independently identify a retail unit's board revision or probe its pins. Remaining gaps include actual flash/PSRAM module and training, panel controller/orientation/refresh waveform, touch/keys, GPIO rail polarity and sequencing, physical SDMMC read/write and DMA behavior, RTC/battery devices, deep-sleep current and button wake, and stability under repeated Reader/page/SD transitions. No physical compatibility claim is made.

Recommended checklist for a separately approved hardware bring-up:

1. Identify the exact Pro or Classic model/revision and panel-controller batch; keep the two profile pin maps separate.
2. Before any write, obtain read-only chip/flash/partition/eFuse/security inventory and complete verified backups; prepare and test a model-matched external recovery path. The QEMU 16 MiB image is never a device image.
3. After separate explicit authorization, use only a device-specific image/boot plan and a spare unit/card; keep serial logs and current draw visible during the first boot.
4. Verify PSRAM initialization and repeated allocations; panel ID/orientation/full refresh; Pro GT911 or Classic buttons; correct power-rail polarity; SDMMC mount plus read/write on a spare card; and RTC/battery reporting.
5. Exercise Home→Library→EPUB, next/previous, exit, saved-progress reopen, then actual deep sleep and button wake over repeated cycles while watching for brownout, reset, heap loss, or card corruption.
6. Treat OTA as unavailable until its device-specific image/manifest, rollback state, and external recovery method have been separately reviewed and tested.

No physical device was connected, read, or written for this validation. The preserved original and fresh firmware artifacts remain unchanged.

## 2026-11-13 closeout — PAUSED

**Status: PAUSED — awaiting physical hardware testing or a concrete Web Emulator integration requirement.** This is the authoritative closeout status and supersedes earlier same-day text that described Pro Reader interaction as unverified.

This closeout only reconciles stored evidence and documentation. No build or test was rerun; no firmware, source, or test evidence was changed. No physical device was accessed. No flash, commit, push, merge, release, publication, or deployment occurred.

### Evidence classification

| Evidence | Result | What it establishes | Still unconfirmed |
|---|---|---|---|
| X4 Classic SDL2 | **PASS; host simulator, exit 0** | Current isolated host-simulator profile rendered Home/Library and EPUB pages 0 and 1, exercised button-style navigation and Reader exit, and wrote progress, book state, and reading statistics to a folder-backed virtual SD. Screenshots and generated files are preserved. | Does not execute the Classic ESP32-S3 BIN. Physical panel, buttons, GPIO, SDMMC, PSRAM, sleep/wake, and progress restore after simulator restart are unconfirmed. |
| X4 Pro native QEMU | **PASS; actual fresh ESP32-S3 firmware in eink-qemu, 27/27 steps** | Exact Pro BIN/ELF booted on `x4pro,panel=uc8279-68`; startup, PSRAM allocator, X4 Pro profile, display/Home, virtual SDMMC, Reader rendering, next/previous key turns, exit, saved progress, and reopen at saved position were recorded. | A dedicated page-1 screenshot is absent; QEMU swipe did not yield a settled change; physical GT911/touch/swipe and panel batch are unconfirmed. The low-frequency log is not deep sleep. |
| Browser/Web Emulator | **UNCONFIRMED** | The compatibility note defines separate host-simulator and S3 guest backends. | No browser/WASM build or browser runtime result is in this evidence set. |
| Physical hardware | **NOT TESTED** | No device was flashed or probed. | All board-level electrical behavior, storage, display, PSRAM, touch/buttons, power, sleep/wake, and recovery remain open. |

The Classic replay evidence is `${NOOIR_D_ROOT}/logs/x4classic-validation-20261111-run01/classic-simulator.log`, SHA-256 `562ec89ace26042898ed691406e4870ced1a3cc40bfd5a2689bf535ca982d4d2`. Its screenshots and virtual SD result files remain in that same folder. The log records a missing optional cover-thumbnail file, while EPUB page rendering, exit persistence, and the scripted replay are recorded as passing. A restart/reopen check is not evidenced.

The Pro 27-step evidence is `diagnostics/s3-reader-validation-20261111/run04-reopen-progress-page-keys-exit/`: `run.jsonl` SHA-256 `072fc25e9f691724555b43cca8065fb7a842d5e68023f0b07791abe0bdc3f1e2`; `console.jsonl` SHA-256 `867a88b10065d31d9550121520c88851b4b1c4bb1ffb43af33df13cca56fab66`; `metrics.json` SHA-256 `dd849be107baf39421ba8c3e53dc53174b1a81c1c79f34095e4b240abfd67b65`. The prior `run03` Reader screenshot is page 3/10; the final `run04` reopening screenshot matches that saved position. The synthetic swipe timeout and the earlier run02 late-input timeout remain preserved as harness/model limitations, not confirmed firmware defects.

### Validated BIN/ELF pairs

The current PlatformIO build outputs and exported canonical files have matching hashes. Embedded-ELF identity and image metadata were validated in the recorded November 11 build; no rebuild was done to address older hash differences.

| Device | Canonical app-only BIN | BIN bytes / SHA-256 | Matching ELF | ELF bytes / SHA-256 | Build log / SHA-256 |
|---|---|---|---|---|---|
| X4 Pro | `artifacts/fresh-20261111/folio-nooir-v1.6.6-x4pro-EXPERIMENTAL.bin` | 5,671,104 / `0a0b5bb6eaa47ec4cdacc2a6af7f1d2a7166ff913658d86c555aac23aaa69ba9` | `artifacts/fresh-20261111/folio-nooir-v1.6.6-x4pro-EXPERIMENTAL.elf` | 86,746,476 / `bd768427c55b500a87b2ffe3a39542a7053e102139b884ac103a9224765017e2` | `${NOOIR_D_ROOT}/logs/x4-s3-firmware-build-20261111-run01/build-x4pro-attempt-03-utf8.log` / `ed32d1a71b09411251e8a192392025654c94948a40cb6f32834670a3369c4b5a` |
| X4 Classic | `artifacts/fresh-20261111/folio-nooir-v1.6.6-x4classic-EXPERIMENTAL.bin` | 5,676,288 / `a1ece7c1897ab39ac24c6e30d0e0331b29a62925d28fcf52657d39cb92dcabab` | `artifacts/fresh-20261111/folio-nooir-v1.6.6-x4classic-EXPERIMENTAL.elf` | 86,770,716 / `bc412ce4b3587fe6248b80aa5c51793704f7e2d8b228a68c3ed1ccd44549e44c` | `${NOOIR_D_ROOT}/logs/x4-s3-firmware-build-20261111-run01/build-x4classic-attempt-02-serial.log` / `642efa96b0a295f7387c42da764766ac1bd67bc46e505fd6f9e447df52babe53` |

Both builds identify as ESP32-S3, use the separate Pro/Classic device macros, 16 MiB DIO flash at 80 MHz, configured 8 MiB OPI PSRAM, and the unchanged v1.6.6 partition table. The Classic touch capability is off; Pro touch is on. These app-only images remain experimental and are not approved for physical flashing.

### Preserved historical and emulator-only artifacts

All files below and their manifest sidecars remain preserved. They are distinct export generations; identical bytes are duplicates, and different hashes remain historical differences. No attempt was made to rebuild or reconcile them.

- Older Pro app-only image: `artifacts/Folio-Nooir-v1.6.6-X4PRO-EXPERIMENTAL-NOT-FOR-FLASHING.bin`, SHA-256 `7e4ac1c63a24005383a7ebf2a63fd988a96bbb1bd421c990c334e5466e800494`; its matching historical ELF was not found.
- Older Classic app-only image: `artifacts/Folio-Nooir-v1.6.6-X4CLASSIC-EXPERIMENTAL-NOT-FOR-FLASHING.bin`, SHA-256 `3309e364639d53ffe1602befb6b2c9fc62e66d088e0bc383833e2cf0c55f804a`; its matching historical ELF was not found.
- Preserved collision-suffixed exports: Pro `-2` and `-3` each hash `5c261706d52d35b73879ca65918fb012f36aa65dcdf44b24d8f51251d9b011dc`; Pro `-4` is byte-identical to the fresh Pro BIN; Classic `-2` hashes `a1d73922e1c0c1bf3764e37dc5e37464fb945a1e34050956ac35b47105b8b33c`; Classic `-3` is byte-identical to the fresh Classic BIN.
- Quarantined C3 `gh_release` intermediate (not the official v1.6.6 release and not an S3 image): `artifacts/QUARANTINED-Folio-Nooir-v1.6.6-C3-gh_release-intermediate-NOT-OFFICIAL-RELEASE-NOT-FOR-S3-NOT-FOR-FLASH.bin`, 6,082,400 bytes, SHA-256 `5e05dfc31001aa27cf635f795c77b3447f1bfab70713ad522c850b6a64d6147e`.
- Both composed 16 MiB Pro full-flash images remain QEMU-only and **NOT FOR PHYSICAL FLASHING**: earlier image SHA-256 `26ae6518b77cf8c268679f64fd40754212fb1cb6f84f3cc838223f5e18eb39cd`; fresh-build image SHA-256 `b8389fa4e55be012411a6e5bac9e6a10724e04903b9dbbb0e8764023a61c392d`.

### Remaining blockers and restart conditions

Keep these items explicitly open: physical Pro and Classic boot; physical panel/controller and refresh verification; Pro GT911 touch/Home and swipe behavior; Classic buttons on the actual unit; SDMMC/power sequencing and read/write; real PSRAM training and sustained memory behavior; deep-sleep current and wake; Classic firmware execution in a Classic-capable emulator or unit; browser/WASM integration; a dedicated Pro first-page screenshot; and safe external backup/recovery.

The experimental builds are **not physically flash-ready**. OTA operations and the normal SD update/recovery routes are compile-time disabled; OTA slots in the old partition table do not provide a usable recovery path. Any future hardware phase first needs a separately validated, board-matched read-only inventory, complete verified backup, external recovery procedure, and separate authorization before any device write. Never use either QEMU-only full-flash image on hardware.

Resume this workstream only when an X4 Pro/Classic unit is available for an explicitly scoped hardware validation, or when the Web Emulator workstream supplies a concrete integration requirement. At restart, choose host-simulator WASM versus real Pro S3 guest emulation explicitly; do not infer Classic compatibility from Pro results. The board interface/emulator summary is in `docs/WEB_EMULATOR_S3_COMPATIBILITY_HANDOFF_2026-11-11.md`.
## 2026-11-13 GitHub source backup — PAUSED

**Workstream status: PAUSED — awaiting physical hardware testing or a concrete Web Emulator integration requirement.** This section records the separate source-backup task after the earlier closeout; it does not supersede the technical validation limits below.

### Backup scope and review

The dedicated development branch is `codex/nooir-x4pro-x4classic-s3-port-20261113`, based on the official v1.6.6 source commit `b6d428a3187afc25d434fcb475e9ab436099c1ba`. The reviewed scope contains the X4 Pro and X4 Classic S3 profiles and guarded application/HAL changes, native simulator/QEMU compatibility work, artifact safety checks, port handoff, source-delta manifest, and the Web Emulator compatibility handoff. The original C3 profile and partition CSV are inherited unchanged. The branch contains no v1.6.7 development tree and no standalone Web Emulator implementation.

The `freeink-sdk-s3` dependency is represented by a gitlink to `Free-Ink/freeink-sdk` revision `5e94fe7480f019c89fbf691aef87523eb1b46704`. Its local snapshot matched the pinned upstream contents across all 1,034 files. The SDK's MIT license and notices are in that pinned repository.

The v1.6.6 base `freeink-sdk` gitlink points to `7bd6aebe9b40198d7dddf455400cee9225430726`, which the configured `toshio2011/freeink-sdk` remote and the current `Free-Ink/freeink-sdk` remote both reject as unavailable. That inherited gitlink is preserved without substitution. The exact legacy SDK snapshot remains local at `${NOOIR_D_ROOT}/_c-side-migration-20261110/_isolated_nooir_x4_pro_classic_v166/freeink-sdk`; a fresh checkout cannot initialize that legacy dependency until its exact source is recovered or the dependency is deliberately reconciled. This does not invalidate the already completed builds, but it limits a clean remote rebuild.

No experimental BIN/ELF, QEMU full-flash image, build directory, cache, temporary SD image, raw log, screenshot, backup archive, provenance ZIP, or private file is part of the branch. Their identities and validation evidence remain documented in `PORT_SOURCE_DELTA.json`; the actual artifacts and raw evidence remain preserved locally under `${NOOIR_D_ROOT}`. Local paths in these handoffs use `${NOOIR_D_ROOT}`, `${WINDOWS_TEMP}`, and `${WINDOWS_USER}` tokens to avoid publishing machine-specific user paths.

### Reconciled validation checkpoint

- **X4 Pro firmware-in-QEMU:** exact fresh Pro BIN/ELF on eink-qemu 0.9.2 `x4pro`, Reader run04 **27/27**. The record covers app startup, Nooir logs, PSRAM allocator, X4 Pro profile, Home/Library, EPUB Reader rendering, next/previous key turns, Reader exit, progress save, and reopen at saved progress. Run JSONL SHA-256: `072fc25e9f691724555b43cca8065fb7a842d5e68023f0b07791abe0bdc3f1e2`. The separately retained screenshot is not a dedicated first-page capture; synthetic swipe and deep sleep/wake remain unconfirmed. This is emulator evidence only.
- **X4 Classic SDL2:** host simulator replay passed Home/Library, EPUB render, page 0→1, exit, and progress/book-state/statistics writes. Log SHA-256: `562ec89ace26042898ed691406e4870ced1a3cc40bfd5a2689bf535ca982d4d2`. It does not execute the Classic S3 BIN; progress reopen after a simulator restart and all physical behavior remain unconfirmed.
- **Physical validation:** none. No Pro or Classic was flashed or probed. OTA and ordinary SD update/recovery routes are disabled in these experimental profiles, so they are not physically flash-ready without a separately validated external backup and recovery procedure.

### Remaining blockers and restart conditions

Keep physical Pro and Classic boot, board/panel identification, PSRAM training, physical SDMMC and GPIO, Pro touch/swipe, Classic buttons, display/power sequencing, deep-sleep/wake, Classic firmware execution, dedicated Pro page-1 capture, and safe external recovery open. Restart only when a correctly identified physical unit is available for a separately scoped validation, or when the Web Emulator thread provides a concrete integration requirement. Do not infer Classic firmware compatibility from Pro QEMU or SDL2 results. Any future device write still requires separate authorization and verified board-matched backup/recovery prerequisites.

Remote branch verification and commit identity will be recorded here after the push is confirmed.
## 2026-11-24 integrated v1.6.7 S3 upgrade checkpoint

### Scope, provenance, and preservation

This branch upgrades only the experimental X4 Pro / X4 Classic ESP32-S3 port. Its base is the integrated Nooir 1.6.7 candidate `5696a555f9f741e8b80c7360d26a74129750610d`, built on firmware source revision `ad0f269d13aed74d535c619588e1b3bde4f4ecbc`. Port commits through `a6de722635b0ade901b31a1457ef9ae7cc248509` add separate S3 profiles, target identity, S3 SDK compatibility, upload/firmware-update protection, and compatibility guards for BLE APIs unavailable in the S3 stub. The host-simulator compatibility edits are restricted to native simulator profile and shim files; they do not alter the firmware candidate bytes.

The branch is `codex/nooir-x4pro-x4classic-s3-port-v1.6.7-20261124`, based on `codex/folio-nooir-1.6.7-integrated-20261120`. The original X4 C3 release source, the separate `_04` BLE milestone, MihonNooir, standalone Web Emulator source, the v1.6.6 isolated port, and its preserved firmware remain outside this worktree and were not intentionally edited. No board was flashed; nothing was merged, released, or deployed.

### Device profiles and build identities

Both builds used PlatformIO Core 6.1.19, PIO Arduino ESP32 platform 55.03.37, Arduino-ESP32 3.3.7 / ESP-IDF 5.5.2, esptool 5.1.2, and Xtensa toolchain 14.2.0+20251107. The board definition is `esp32-s3-devkitc1-n16r8`, MCU ESP32-S3, 16 MiB DIO flash at 80 MHz, `dio_opi` memory mode with 8 MiB OPI PSRAM configured, and the unchanged `partitions.csv` SHA-256 `98e02c50532c388d85cc3b7c96d004142dc713b23368d293c881ecdcb419e44e`. The build app slot is 6,553,600 bytes; app0 is at `0x10000`, app1 at `0x650000`.

| Target | PlatformIO env / identity | Touch | Build result | App BIN (bytes; SHA-256) | Matching ELF (bytes; SHA-256) |
| --- | --- | --- | --- | --- | --- |
| X4 Pro | `nooir167_x4pro`; `Folio Nooir X4 Pro`; `1.6.7-x4p-exp-no-flash` | on | success, 230.68 s | `FolioNooir_1.6.7-X4Pro-Experimental.bin`; 5,694,720; `3e47ba8ce10901e5a037ecfcd75443615707882e7d2c32856949ba341868d7d5` | `FolioNooir_1.6.7-X4Pro-Experimental.elf`; 87,599,692; `8a123c4a2c636585214af964bcb03abc90f60db1db28e87b12a1e1475200adcb` |
| X4 Classic | `nooir167_x4classic`; `Folio Nooir X4 Classic`; `1.6.7-x4c-exp-no-flash` | off | success, 352.23 s | `FolioNooir_1.6.7-X4Classic-Experimental.bin`; 5,699,376; `c4bf429556c15d1eccc92343352395f3dc36e90634569a17258f48fa4fc9cb10` | `FolioNooir_1.6.7-X4Classic-Experimental.elf`; 87,623,700; `564b1e53a916f7d1aceba1faae6a0b1697f06b21b5b05eb2467b8541ad5480c7` |

The image metadata identifies ESP32-S3, 16 MiB, DIO/80 MHz and the per-device project/version strings. Image checksum and validation hash pass; the embedded ELF hash matches the separately preserved ELF. The application uses 86.9% (Pro) and 87.0% (Classic) of the configured build slot. Static RAM is 67,164 / 327,680 bytes (20.5%). These are application-only experimental candidates, not complete physical flash images and not validated for a retail board's bootloader or partition map.

Successful firmware build logs are `${NOOIR_D_ROOT}\_builds\nooir-s3-v1.6.7-20261124\logs\build-x4pro-v1.6.7-retry04.log` (SHA-256 `f528d95935ee3605b779d20411cb16f9c136ef4f2ce725c4806f6c2873718e0a`) and `...\build-x4classic-v1.6.7-retry02.log` (SHA-256 `36db066685717cfc48492bcd3620c016442ca7a949b2d16f2b29449fde5bfbcb`). Pro warnings: Windows long-path support disabled and an existing `Rect` explicit-constructor conversion warning. Classic also reported wolfSSL macro redefinitions and deprecated WebSockets `NetworkClient::flush()`; the serial retry succeeded. Earlier failed attempts are retained as separate logs and did not replace either successful artifact.

All build outputs and caches are under `${NOOIR_D_ROOT}\_builds\nooir-s3-v1.6.7-20261124`. BIN/ELF pairs, successful logs, test logs, QEMU full-flash, virtual SD, and run evidence are preserved there. Build manifests are local-only and identify the images as experimental / not for flashing.

### Feature scope and firmware-update safety

The shared Nooir 1.6.7 application source comes from the integrated 1.6.7 base. It carries the integrated Reader/EPUB and styling changes, progress/cache behavior, recent/finished shelf work, Main/Folio Lite themes, settings/device information, achievements, XTC/XTCH/CBZ support, and existing Web UI code. See `docs/PORT_1.6.7_FEATURE_PARITY.md` for the included source lineage, device capability switches, and validation boundaries. A feature being present in common source is not evidence that the relevant physical S3 path was tested.

The profiles intentionally compile BLE HID host, USB MSC, frontlight and warm light out. `NOOIR_EXPERIMENTAL_DEVICE_BUILD=1` causes OTA update check and install to return without selecting or writing a generic release image. Separately, `NOOIR_DISABLE_FIRMWARE_FLASHING=1` makes `FirmwareFlasher::validateImageFile()` and `flashFromSdPath()` return `FLASHING_DISABLED`; the SD update/recovery UI may still be present, but does not reach an image write through these APIs. This guard does not establish a working recovery path. Keep both restrictions until a separate backup/recovery procedure has been validated for the exact physical model and partition map.

### Pro exact-build QEMU validation

Native eink-qemu 0.9.2 ran the exact new Pro BIN/ELF on machine `x4pro`, panel `uc8279-68`, USB Serial/JTAG, with the virtual power-hold workaround set to 50,000 guest milliseconds. The virtual SD image is a synthetic 2050 MiB card containing a small multipage EPUB. The app starts with the expected Pro identity; serial evidence shows PSRAM allocation, SDMMC mount, display initialization, Home, Library, and Reader startup.

Run01 passed 27/27 scripted checks. Run02 timed out on a later input after the guest had entered the low-power timer path; no crash or panic was recorded. Run03 used the shorter post-boot wait and passed 30/30 checks: EPUB open/render, forward/back page keys, exit to Library, progress save, reopen at saved progress, then navigation/exit. The reopened-page screenshot hash matched the prior page screenshot. Heap snapshots showed no corrupt blocks; Pro QEMU reported about 8.24–8.29 MiB free PSRAM across Home/Reader/exit. The only logged errors were expected absent optional emulator IMU and default virtual NVS values. These are QEMU-only results. Deep sleep/wake, physical PSRAM training, physical SDMMC/GPIO/panel/touch/power behavior, and retail bootloader compatibility remain unverified.

The 16 MiB QEMU image `Folio-Nooir-v1.6.7-X4PRO-QEMU-ONLY-NOT-FOR-PHYSICAL-FLASH-16MiB.bin` has SHA-256 `08b2e69602d8790a13bcab1c39f39d481194298516bb3a2904c6804e09d78c77`. It was composed for emulator use from the matching Pro build's bootloader at `0x0`, partition table at `0x8000`, and application at `0x10000`, then supplied as a whole dump to QEMU (`flash.kind=dump`, partitions from that dump). Its label is literal: **QEMU ONLY — NEVER FOR PHYSICAL FLASHING**. To reproduce, use the matching build's own `bootloader.bin`, generated partition binary and Pro app BIN; place them at those build offsets into a newly zero-filled 16 MiB image with the eink-qemu 0.9.2 `flash` subcommand for machine `x4pro`; verify every input hash and the final image hash before running. Do not pass this combined image to a hardware flasher.

Retained evidence paths: `${NOOIR_D_ROOT}\_builds\nooir-s3-v1.6.7-20261124\qemu\pro-v1.6.7-reader-run01`, `...\pro-v1.6.7-reader-run02-reopen`, `...\pro-v1.6.7-reader-run03-reopen-fast`, and the matching `logs\qemu-pro-v1.6.7-run0*-cli.log`. Run03 `run.jsonl` SHA-256 is `0651acab8a6f2328f6ff9f9a4e80d70d7aa6ee6c88d97e17b3172f997bcd0bc8`; `console.jsonl` SHA-256 is `6c1582f073c3c4501de6d9820bec67ea60806ad5fa4b26999a7a2ded961cba0a`.

### Classic host simulation and other software tests

The 1.6.6 Classic SDL2 replay remains historical host-application evidence. The new 1.6.7 Classic result below is also host-only: it does not execute the Classic S3 BIN. The focused build used Ubuntu 24.04.4 WSL/WSLg, SDL2 2.30.0, and PlatformIO Core 6.1.19 with D: source/cache/output paths. The separate `simulator_nooir167_x4classic` profile reports touch disabled; the scripted run used buttons only. Its 1 MiB synthetic heap is not physical PSRAM. There is no Classic eink-qemu machine. Host simulation cannot validate S3 boot, SDMMC, panel, PSRAM, GPIO, electrical behavior, sleep/wake or recovery.

`python -m unittest discover -s test\python -v` passed 5/5 (`logs\python-unit-tests-v1.6.7.log`, SHA-256 `6a1c297a15fae36809977c3afaaab01f93319531162f54a29385f72120f67ccd`). A full `pytest test -q` attempt did not run because pytest is absent from the isolated Windows environment (`No module named pytest`); it was not installed globally. No firmware rebuild followed the two successful firmware builds. The Classic host-simulator compile is separate from those firmware artifacts.

### 1.6.7 Classic SDL2 validation results

The `simulator_nooir167_x4classic` native host build completed successfully in Ubuntu 24.04.4 WSL/WSLg with SDL2 2.30.0 and PlatformIO Core 6.1.19. It took 878.87 seconds and produced `.pio/build/simulator_nooir167_x4classic/program` (15,335,496 bytes; SHA-256 `036bab3fff5d47f627b641358d372463b0486b908efb475e0b8bc1c358b86ec7`). Build log: `${NOOIR_D_ROOT}/_builds/nooir-s3-v1.6.7-20261124/logs/wsl-sim-x4classic-v1.6.7-build-retry07.log` (5,868 bytes; SHA-256 `95d31df35b618664c71fd4dea8b7643c112b1c9994206e87eb98a4dbdfe456bb`). This host executable is not the Classic ESP32-S3 firmware BIN.

A fresh folder-backed virtual SD run used the synthetic EPUB `${NOOIR_D_ROOT}/_builds/nooir-s3-v1.6.7-20261124/fixtures/classic-sdl2-v1.6.7-run04/sd/multipage-sample.epub` (1,243 bytes; SHA-256 `c8e8de68968636258d0417b2ca059ec993f88f4c1d6d82779f91dca2cfbb68bc`). It reached X4 Classic identity, Home/RecentBooks, Library, EPUB open, Reader page 0, next page 1, previous page 0, Reader exit and RecentBooks return. Logs show `progress.bin` saves on page renders plus `recent.json`, `book-states.json` and `reading-stats.json` writes on exit. The process exited 0. WSLg emitted non-fatal libEGL/ZINK device warnings; SDL rendering and BMP capture succeeded. A process-restart reopen at saved progress was not tested. One TOC warning belongs to the small synthetic EPUB; its pages rendered.

Input was buttons only: `2400:LEFT;10000:DOWN;11500:ENTER;15000:DOWN;17000:UP;19000:ESCAPE;23000:QUIT`. Run log: `${NOOIR_D_ROOT}/_builds/nooir-s3-v1.6.7-20261124/logs/wsl-sim-x4classic-v1.6.7-run04-reader.log` (23,869 bytes; SHA-256 `ca7f741377928d0ada78b2e9ca8d297a63ef40a72d18bcb46064d6d28df207df`). BMP screenshots are 480×800 at `${NOOIR_D_ROOT}/_builds/nooir-s3-v1.6.7-20261124/simulator/classic-sdl2-v1.6.7-run04-reader/shots/`: `library.bmp` and `book-selected.bmp` share SHA-256 `bbc8faf62e8837bda92b30f6d3c5551670d4d1db1615e3afcaef1c8dae544c52`; `reader-page0.bmp` and `reader-previous.bmp` share `681e17e852ccfe02e7f68faa38f0c1e4504894c1c82d64dd94e8482d6e3701fd`; `reader-page1.bmp` is `3db4f98dfb1ed3c0b1158b99b30bed310bd7ef8e234f93b34192d9d0c2584eca`; `after-exit.bmp` is `e27b5b8e52bb4d2169547bf442577e9ba4f512a73b08e32e5331d89d6c10f5f4`.

Earlier 1.6.7 attempts are preserved but are not counted as Reader passes: the first used a premature key sequence and captured no screenshots; the next used an EPUB nested under a `books/` folder and selected that folder. Run04 corrected the fixture to place the EPUB at virtual-card root and is the passing evidence above. These outcomes are not firmware defects.
### Environment note and remaining checks

A prior attempted PIO command was issued from the C: conversation directory and failed before starting a project build because it could not find `platformio.ini`. During an earlier cache-copy step, existing `${WINDOWS_USER}\Documents\side\.pio\libdeps\nooir167_x4pro` and `...\nooir167_x4classic` directories were involved; they were left intact, and it is not possible to prove whether copy metadata/content changed. No firmware/source/build output was intentionally produced on C:. Subsequent work used the isolated D: worktree, D: caches, D: build logs and D: outputs. Do not recreate or depend on the deleted C: workspace.

Before any future physical bring-up, separately verify exact board/revision and bootloader/partition map; create and validate a full backup/restore and recovery method; use a current-limited supply and serial capture; confirm PSRAM training, profile, panel/rotation/refresh, SDMMC, Pro touch or Classic buttons, power rails, sleep/wake, and sustained EPUB/XTC/XTCH/CBZ/progress behavior. No physical flashing is authorized by this handoff.

## 2026-11-24 GitHub source backup — verified

The reviewed source and documentation are backed up on [`codex/nooir-x4pro-x4classic-s3-port-v1.6.7-20261124`](https://github.com/toshio2011/folio-nooir/tree/codex/nooir-x4pro-x4classic-s3-port-v1.6.7-20261124). The first pushed source checkpoint is commit `99e59f729bab898bacfef24457e15ce4dd9a1046` ([commit](https://github.com/toshio2011/folio-nooir/commit/99e59f729bab898bacfef24457e15ce4dd9a1046)). Fetch verification matched the remote ref to that commit and confirmed the port handoff, source-delta, Pro/Classic S3 profiles and compatibility files were present.

This is a development source backup only. No pull request was created, and nothing was merged, released, deployed or flashed. The experimental Pro/Classic BIN/ELF pairs, successful firmware build logs, QEMU-only full-flash image, QEMU and SDL2 logs/screenshots, virtual SD fixtures and build caches remain local under `${NOOIR_D_ROOT}/_builds/nooir-s3-v1.6.7-20261124`; they were not added to Git. Two RTL test BMP fixtures inherited from the 1.6.7 base are unchanged; no simulator screenshots or generated port binaries were committed. The S3 FreeInk SDK remains a gitlink at `5e94fe7480f019c89fbf691aef87523eb1b46704`.

The backup does not change the remaining physical requirements. Do not consider either image flash-ready until a board-matched full backup and external recovery procedure have been validated. Resume hardware work only with an explicitly scoped test and separate approval for any device write; the Web Emulator source remains outside this branch.


## 2026-11-24 end-of-day checkpoint — PAUSED

**Workstream status: PAUSED — resume from this D: checkout only.** Authoritative workspace: `D:\fatiha\side\_worktrees\nooir-s3-167-upgrade-20261124`. Branch: `codex/nooir-x4pro-x4classic-s3-port-v1.6.7-20261124`. Protected source checkpoint before today’s source/doc commit: `be3f218988ca3e874d68f6d777165be3c521fac4`. S3 SDK checkout: `995d9bd523b9c45b18710bca17a3158ce724a7f4`.

### Reference revisions

- CrossPoint Reader: `92e8714a332c2c1903ea93290cf0b9b4f6f6697c`; FreeInk SDK pin `995d9bd523b9c45b18710bca17a3158ce724a7f4`.
- CrossInk: `9914146eeae7b46b300f475a16c32426fc02ec1f`; FreeInk SDK pin `699370183fa3a0e33c9cb83a36f701bbb6022095`.

### Implemented or investigated in this working tree

- **USB Drive / MSC (Pro and Classic):** new USB Drive activity and mode selection; syncs files, releases SD font handles, unmounts/detaches the filesystem before raw block-device ownership; handles host wait, eject, disconnect/suspend hint, I/O/startup error, and then reboots to remount/re-index. USB transfers are split into at most 16-sector batches with a cooperative yield and conditional current-task watchdog reset. USB OTG-to-Serial/JTAG restoration follows the pinned CrossPoint handoff helper. Existing Wi-Fi Web UI transfer routes remain. The host MSC callback/lifecycle harness passed against the pinned SDK source patched into a D: temporary copy; it covers bounds, large reads/writes, partial sectors, watchdog subscription behavior, eject/disconnect, and startup/I/O failure. It does **not** test TinyUSB, SDMMC, or a real host/device. The firmware build did not reach C++ compilation; device runtime remains unverified.
- **Pro frontlight:** Pro-only persistent on/off, brightness and warmth settings; initializes/restores after accepted wake and parks LED pins before deep sleep. No physical LED/PWM, temperature or sleep/wake test. Warm/cool channels are from the Pro profile. Classic has these controls disabled.
- **Pro touch:** existing application input routes provide Home/Library/Settings/list touch and Reader tap/swipe page-turn paths for EPUB, XTC/XTCH and CBZ, behind the Pro touch capability. This checkpoint adds no new configurable gesture/tap-zone system, accidental-touch filter, or capacitive Home validation; coordinate transforms and all physical GT911 behavior remain open.
- **Classic controls:** Classic touch/light remain disabled; existing SDK/profile button mapping and shared remappable front-button controls are retained. Physical button timing, shortcuts and orientation behavior remain untested.
- **Shared S3:** ESP32-S3 profiles use the board-matched one-bit SDMMC profiles, PSRAM-backed framebuffer, OPI PSRAM settings, shared panel/display abstraction, battery/RTC/IMU and sleep plumbing. Previous Pro QEMU evidence showed the earlier certified build’s profile, PSRAM allocator, display/Home and virtual SD; it is not evidence for this unbuilt source state. Classic firmware execution has no Classic QEMU machine.
- **Fonts:** Nooir still provides built-in fonts and `.cpfont` SD fonts. Native TTF/OTF runtime rendering has **not** been ported. CrossPoint’s FreeInkFont/FreeType adapter is PSRAM-gated and substantially changes registry, renderer, cache and shaping ownership; treat scalable TTF/OTF as open implementation work, not parity.
- **BLE:** S3 BLE HID remains explicitly disabled in both profiles. No NimBLE central feasibility/build/runtime test was performed. Do not carry C3 BLE thresholds, SPI-array changes or SDK patches into the S3 port without a separate compatibility audit.
- **Updates/recovery:** `NOOIR_DISABLE_FIRMWARE_FLASHING` now defaults safely to off for legacy profiles and is wired into S3 OTA availability/install, update menu routes, boot recovery route, and the SD flasher’s validation/write APIs. The experimental-build guard remains too. These settings protect software entry points; they do not provide recovery. Experimental builds remain not flash-ready without an independently verified model-specific backup and recovery method.

The complete per-model comparison is in `docs/PORT_1.6.7_FEATURE_PARITY.md`. All source changes are experimental and unbuilt; review status is not device compatibility evidence.

### Today’s build/test outcome and preserved artifacts

- The USB MSC host test passed. Log: `${NOOIR_D_ROOT}\_builds\nooir-s3-v1.6.7-20261124\logs\host-usb-msc-20261124.log`; SHA-256: `433b2a68f1b1c94e39ab477bd04687a8366037cbac8739df694917778fbf0dc4`.
- Existing Pro firmware-in-QEMU evidence remains historical: exact earlier Pro pair, eink-qemu 0.9.2, run03 30/30 Reader checks; the earlier run01 27/27 is also preserved. Classic SDL2 run04 remains a host-only pass for Home/Library/EPUB page turns/exit and persistence writes. Neither simulator was rerun against today’s source.
- Pinned SDK host runner attempts did not reach test compilation: the checked-in `.sh` files use CRLF and the WSL launch/normalization attempt failed at the shell wrapper. Do not interpret as SDK test failures; use a correctly normalized temporary runner next time.
- The Pro build attempt did not reach application compilation. PlatformIO’s Windows Python output thread raised `UnicodeEncodeError` under CP1252 while emitting a Unicode progress line. It was interrupted safely; Classic build was not started. Failed log preserved at `${NOOIR_D_ROOT}\_builds\nooir-s3-v1.6.7-20261124\logs\build-x4pro-v1.6.7-parity-20261124.log`; SHA-256: `89a02f9d033b3066974b2f8c2c992f1ed7ef281d875a09be562a055ce8fe6796`. No new BIN, ELF or manifest was exported. Do not rerun with the same console encoding. The temporary cache/build output directory is `D:\fatiha\side\_builds\nooir-s3-v1.6.7-20261124\feature-parity-20261124`; it contains only a PlatformIO config/checksum marker and is preserved.
- The temporary `platformio.local.ini` override was restored to the original D: `build-cache` path; the generated patch was removed from the FreeInk SDK checkout, which is clean at the pin above. No preserved firmware artifact was overwritten.

Certified old pairs were re-hashed before the attempted build and remain unchanged:

| Device | BIN bytes / SHA-256 | ELF bytes / SHA-256 |
|---|---|---|
| Pro | 5,694,720 / `3e47ba8ce10901e5a037ecfcd75443615707882e7d2c32856949ba341868d7d5` | 87,599,692 / `8a123c4a2c636585214af964bcb03abc90f60db1db28e87b12a1e1475200adcb` |
| Classic | 5,699,376 / `c4bf429556c15d1eccc92343352395f3dc36e90634569a17258f48fa4fc9cb10` | 87,623,700 / `564b1e53a916f7d1aceba1faae6a0b1697f06b21b5b05eb2467b8541ad5480c7` |

They remain at `${NOOIR_D_ROOT}\_builds\nooir-s3-v1.6.7-20261124\artifacts` and are not part of Git.

### First steps on resumption

1. Confirm branch, local source/docs changes, protected artifacts and the restored local PIO config; do not touch the preserved `build-cache` or any protected build-cache folder.
2. Run PlatformIO only after setting process-local `PYTHONIOENCODING=utf-8` (or use the already configured D: WSL environment), with a fresh D: build/cache/output path. Build Pro and Classic separately; preserve logs and export new, distinctly named BIN/ELF pairs without modifying the certified pairs. Do not flash.
3. If builds succeed, inspect metadata/slot fit/hashes, then validate exact Pro firmware in QEMU and the Classic host simulator; clearly label host versus firmware execution. Keep physical, deep-sleep, touch/swipe, Classic firmware execution and recovery checks open.
4. Resume the native TTF/OTF integration investigation only after these builds and documentation are reviewed; it is a significant renderer/font-cache port, not a quick dependency toggle.


## 2026-11-25 end-of-day feature-parity checkpoint — PAUSED

Authoritative workspace: D:\fatiha\side\_worktrees\nooir-s3-167-upgrade-20261124
Branch: codex/nooir-x4pro-x4classic-s3-port-v1.6.7-20261124
Source HEAD at start: dc123febde5ab7393d64a83aa3fdf500d5f89316. The reviewed source/checkpoint commit is 97b6b291685b692e23dd797f6cada5347e891045; the verified-backup note commit is c7551a8daf5bbe9957a2c53529c8bf8ae91bfab4.

### Safe stop and Git state

The only active firmware build was allowed to finish. It exited failed after 12 minutes 20 seconds and its wrapper completed cleanup. No build, test, simulator, QEMU run, flash, or source investigation is running now. The wrapper restored platformio.local.ini (SHA-256 DB9DA19691325ABE1DC3F06B05A29E738B7CF3360D00EDCF7F37ED345FD793BA), the patched SDK USB source (SHA-256 E7A167602BFD3DBE9FF5A7DF08A987671C47BEDC46AA44C6BD28FBDDF3720B6C), and generated root configuration files. The migration archive and shared/global PlatformIO installation were not changed.

At the start, tracked changes were limited to platformio.ini; Git also reported one pre-existing generated build-cache directory as untracked. That cache remains local and was not staged. The reviewed closing commit contains only platformio.ini, PORT_HANDOFF.md, PORT_SOURCE_DELTA.json, and docs/PORT_1.6.7_FEATURE_PARITY.md. It contains no BIN/ELF, cache, raw log, private file, temporary SD image, or screenshot. The source/checkpoint commit and the follow-up verified-backup note were both pushed without force and fetched back. At the second verification, local and remote heads both matched c7551a8daf5bbe9957a2c53529c8bf8ae91bfab4. The source checkpoint is [97b6b291](https://github.com/toshio2011/folio-nooir/commit/97b6b291685b692e23dd797f6cada5347e891045); the verified-backup note is [c7551a8d](https://github.com/toshio2011/folio-nooir/commit/c7551a8daf5bbe9957a2c53529c8bf8ae91bfab4). Both are on the [dedicated S3 development branch](https://github.com/toshio2011/folio-nooir/tree/codex/nooir-x4pro-x4classic-s3-port-v1.6.7-20261124). Tracked files were clean at that verification; the generated build cache remains untracked and local.

### Feature parity state

| Area | Current implementation / evidence | Status and remaining work |
|---|---|---|
| USB Drive / MSC, both models | USB Drive UI and SD ownership handoff are present in source. The host lifecycle harness passed on 2026-11-24 (log SHA-256 433b2a68f1b1c94e39ab477bd04687a8366037cbac8739df694917778fbf0dc4). Today's clean Pro build compiled the MSC translation unit before failing in HalStorage.cpp. | Partially implemented; firmware unverified. Fix StorageLock class visibility/definition, then build both profiles. TinyUSB enumeration, real host transfers, SDMMC exclusivity and eject/remount require hardware validation. |
| X4 Pro touch and gestures | Pro identity enables touch and existing UI routes. Gesture zones/tuning and GT911 behavior are not proven by the preserved QEMU tests. | Partially implemented; physical touch/swipe untested. Check mapping, rotation, accidental touches and swipe/page-turn on the Pro. |
| X4 Pro frontlight and warmth | Pro-only persistent on/off, brightness and warmth settings plus wake initialization and sleep pin parking are present in source. | Source only; not built in this checkpoint and not hardware tested. Confirm pins, PWM/channel mapping, warmth range, persistence and sleep/wake behavior. |
| X4 Classic buttons and shortcuts | Separate touch-disabled Classic profile and existing button/shortcut mappings remain. Prior SDL2 replay passed keyboard-driven Library/EPUB/page/exit flow. | Host simulation only. Physical key GPIO, polarity, timing, long press, orientation and shortcuts remain open. |
| SDMMC and storage | Per-board SDK storage profiles remain separate; USB mode attempts exclusive raw SD ownership. Prior Pro QEMU used virtual SD; the Classic host simulator used a folder-backed virtual SD. | Emulated/host behavior only. No physical card, bus timing, card-detect or USB/filesystem ownership test. |
| Display and framebuffer | Board-specific display profiles and shared framebuffer path remain; the preserved Pro QEMU image rendered Reader pages. | Older Pro QEMU artifact only. Panel/controller revision, full refresh, rotation, ghosting, contrast and Classic S3 output remain open. |
| PSRAM | Both firmware profiles request 8 MiB OPI (dio_opi). Older Pro QEMU Reader runs reported roughly 8.24–8.29 MiB free PSRAM and zero corrupt blocks. | QEMU evidence only for the preserved older Pro BIN. Today's source produced no firmware image. Physical PSRAM training and allocation behavior remain open. |
| Power management | Existing sleep paths remain; new Pro light state is parked/restored around sleep/wake in source. QEMU run02 previously timed out after entering its low-power timer path; the recorded cold boot uses a 50,000 ms virtual Power-hold workaround. | Partially implemented; physical behavior open. Deep sleep, wake buttons, rails, current draw and repeated wake cycles are untested. Do not repeat the long QEMU wait pattern that timed out. |
| CrossPoint / CrossInk references | CrossPoint Reader 92e8714a332c2c1903ea93290cf0b9b4f6f6697c with FreeInk SDK 995d9bd523b9c45b18710bca17a3158ce724a7f4; CrossInk 9914146eeae7b46b300f475a16c32426fc02ec1f with SDK 699370183fa3a0e33c9cb83a36f701bbb6022095. | Reference-reviewed. These guide implementation; they do not certify Nooir or physical compatibility. |
| S3 BLE | BLE HID is disabled in both experimental S3 profiles. Existing C3-only compatibility stubs do not prove the BLE host stack works on S3. | Feasibility not tested; pending. Review upstream NimBLE/controller memory and coexistence needs before implementation. |
| Fonts and other device capabilities | Native TTF/OTF renderer/registry/cache integration remains unimplemented. Battery/RTC, orientation/IMU, panel detection/refresh, sleep, physical storage and recovery are device-specific gaps. | Pending or unverified. Keep Pro and Classic definitions isolated; shared UI source is not proof of hardware support. |

### Build and test record

The previous successful Pro and Classic app-only pairs remain the last certified firmware outputs, unchanged:

- Pro: D:\fatiha\side\_builds\nooir-s3-v1.6.7-20261124\artifacts\FolioNooir_1.6.7-X4Pro-Experimental.bin, 5,694,720 bytes, SHA-256 3e47ba8ce10901e5a037ecfcd75443615707882e7d2c32856949ba341868d7d5; matching ELF SHA-256 8a123c4a2c636585214af964bcb03abc90f60db1db28e87b12a1e1475200adcb.
- Classic: D:\fatiha\side\_builds\nooir-s3-v1.6.7-20261124\artifacts\FolioNooir_1.6.7-X4Classic-Experimental.bin, 5,699,376 bytes, SHA-256 c4bf429556c15d1eccc92343352395f3dc36e90634569a17258f48fa4fc9cb10; matching ELF SHA-256 564b1e53a916f7d1aceba1faae6a0b1697f06b21b5b05eb2467b8541ad5480c7.
- These images use separate ESP32-S3 profiles, 16 MiB DIO flash, 8 MiB OPI PSRAM configuration and unchanged 6,553,600-byte app slot. Their earlier successful logs and local manifests remain in the artifact folder. Neither pair contains today's later unbuilt feature-parity changes.

Last successful software evidence is historical to those prior source snapshots: Pro QEMU run01 27/27 and run03 30/30; Classic SDL2 Host Reader replay; USB MSC host lifecycle harness; Python artifact tests 5/5. QEMU and SDL2 were not rerun today and do not validate the current source or Classic S3 BIN. No firmware artifact was produced today.

Today's Pro build used a fresh PlatformIO Core at D:\p\core, build root D:\p\run, Espressif32 55.03.37, Arduino-ESP32 3.3.7 / ESP-IDF 5.5.2, and Xtensa 14.2.0+20251107. Log: D:\p\run\logs\build-x4pro-v1.6.7-clean-55.03.37-short-core-20261125.log. It failed after compiling the S3 TinyUSB/MSC path with five errors at lib/hal/HalStorage.cpp lines 120, 152, 161, 170, and 192: HalStorage::StorageLock is incomplete at the five lock instantiations. No artifact was exported to D:\p\run\artifacts. Existing wolfSSL macro and FILE_READ/FILE_WRITE redefinition warnings were also emitted.

Earlier preserved attempts must not be repeated in the same form: stale .pio-link/dependency links resolved libraries from the migration archive and caused USBMSC/USB undeclared errors; fresh libdeps still reused an isolated PIO core missing generated sdkconfig.h; a clean core at a long path failed with Windows WinError 206. The successful mitigation was a fresh short-path core/build/libdeps under D:\p\...; it exposed the separate source compile error above. Attempt logs and partial build outputs remain on D:.

### Tomorrow's continuation, in priority order

1. Resume only in the authoritative D: worktree and verify the dedicated branch push and working-tree state.
2. Inspect StorageLock declaration, definition and conditional compilation in lib/hal/HalStorage.h/.cpp; make the smallest correct source fix while preserving MSC lock semantics.
3. Build Pro and Classic separately with fresh D: build/cache/libdeps directories and short Windows paths. Preserve prior certified pairs and use new output names/hashes.
4. Validate image metadata/slot fit, then run Pro QEMU against the exact new Pro BIN/ELF and rerun Classic SDL2 against current source. Keep the QEMU guest Power-hold workaround at 50,000 ms and use the known short timing.
5. Continue physical-only checks later: Pro touch/swipe/frontlight/warmth, Classic physical buttons, SDMMC/MSC ownership, panel, PSRAM training, deep sleep/wake, BLE feasibility and validated external backup/recovery.

No physical Pro or Classic was flashed. No bootloader/eFuse/security/partition data, original X4/C3 release, Web Emulator, Status Bar experiment, MihonNooir, migration archive, or shared PlatformIO environment was changed. OTA and ordinary SD recovery remain disabled in the experimental profiles; they are not flash-ready until a model-matched backup and external recovery process is independently validated.
