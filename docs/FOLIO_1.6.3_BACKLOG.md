# Folio Nooir 1.6.3 Investigation Backlog

This is the actionable post-1.6.2 investigation queue. It is **not approval to implement every item in 1.6.3**. Folio Nooir 1.6.2 is released; tag `1.6.2` resolves to `25df494020874150a047e2a4b3be62c3e40151e8`. The known-good firmware/source milestone is `85dda52a102163b40fd4a2ddfde65e6cdc23af36`.

## Immutable comparison baseline

Normal `gh_release` for the released 1.6.2 source baseline:
- linked flash: **6,492,279 / 6,553,600 B**
- padded `firmware.bin`: **6,506,128 B**
- app-slot margin: **47,472 B**
- static RAM: **53,492 / 327,680 B**
- preferred production app-slot cushion: about **40 KB**
- `SECTION_FILE_VERSION = 41`
- host tests: **211/211**
- Spine tests: **13/13**
- WSL `simulator_x4` and `simulator_x3`: pass to RecentBooks
- physical X4: passed the 1.6.2 validation/torture cycle
- physical X3: **not claimed**
- Font Manager install: physically confirmed
- Nooir ↔ real KOReader KOSync: physically confirmed

Do not change the partition layout to create apparent headroom. Every production source change must be compared with this baseline using a normal `gh_release` build.

## Upstream audit policy

Primary references:
- CrossPoint Reader: https://github.com/crosspoint-reader/crosspoint-reader
- CrossInk: https://github.com/uxjulia/CrossInk
- InkPointX: https://github.com/yokki-vans/InkPointX
- CrossPDF, PDF architecture only: https://github.com/davemessew/CrossPDF
- CrossLink, Bluetooth/X4 reference: https://github.com/DaisonChun/crosslink

For each upstream candidate record **TAKE NOW / INVESTIGATE / LATER / SKIP / ALREADY COVERED**, source commit/PR, Nooir equivalent if any, firmware delta, static-RAM delta, peak free-heap/largest-block effect where relevant, X3/X4 risk, and required tests. Never wholesale-merge upstream.

The September 2026 audit is the starting point for 1.6.3. Re-run a delta audit during every Nooir release cycle so future work resumes from the last inspected upstream state instead of starting from memory.

## Current research checkpoint

The completed flash audit establishes the official clean 1.6.3 comparison
baseline: 6,493,837 linked bytes, 6,507,680-byte firmware.bin, 45,920 app
bytes remaining, and 53,140 static RAM. The synchronized 1.6.3 source tip is
09f95ffd9aab4f84690aa28137363492e71abf3d. The earlier
6,492,279/6,506,128/53,492 values remain historical 1.6.2 documentation
because their ELF/map artifacts are not preserved. The larger 6,497,905 linked
result came from stale ignored generated sdkconfig.defaults state, not tracked
production-source changes. See the audit note for the Kconfig, linker-section
and generated-resource evidence.

Detailed research-only findings and the handoff plan for the next build-capable/Codex session are recorded in [`docs/FOLIO_1.6.3_AUDIT_NOTES.md`](FOLIO_1.6.3_AUDIT_NOTES.md).

Key corrections from the source audit:
- built-in font/generated data is the first **measurement target**, not a claimed saving;
- the i18n generator already exposes a `--strip-unused` path that should be traced and measured before deleting translation coverage;
- Nooir already contains an inherited `OpdsParser`, so OPDS must first be reconciled as **existing parser vs. wired/reachable product flow**, not treated as greenfield;
- Nooir already contains `BleInput` scaffolding around FreeInk BLE HID behavior, so Bluetooth work must first determine compile/link reachability and exact incremental cost rather than assuming a from-scratch implementation;
- repository file/header size is never a firmware-size result; the normal release ELF/map and `firmware.bin` are authoritative.

While a build-capable Codex/local session is unavailable, continue source/upstream tracing and prepare the exact symbols, callers, guards and candidate commits for later measurement. Do not make speculative production deletions merely to create headroom.

## P0 — flash recovery first

Generate a linker/map-level breakdown before adding another large subsystem. Audit:
- compiled-in themes and whether non-default themes can move to SD;
- bundled/fallback fonts and CrossInk-style downloadable/font trimming opportunities;
- icons, bitmaps and other compiled assets;
- inherited unused activities/features;
- translations and generated web assets;
- duplicate theme/rendering paths;
- dead linked functionality and build flags/LTO/garbage collection;
- generated hyphenation data and safe language/representation reductions;
- resources that can safely move to SD without harming boot/recovery.

Keep Folio Nooir built in unless an SD-loaded default is separately proven safe. Investigation target: recover meaningful headroom, preferably **100–200 KB or more**, but do not claim savings until measured.

**Optional-resource preservation rule:** if a measured flash-recovery change removes an existing user-facing resource solely because it is expensive to embed, prefer preserving the capability as an installable/on-demand SD resource where technically reasonable. Languages/hyphenation/fonts are the first candidates. Keep boot/recovery/default fallback resources embedded. A future GitHub-backed versioned resource manifest / Nooir Resource Manager is a candidate only if the measured savings exceed the firmware and maintenance cost of the download/validation infrastructure; manual SD installation should remain possible. Dead/duplicate/unreachable code does not require a downloadable replacement. See the audit notes for the full reminder.

## P1 — EPUB/font/image/memory delta

Re-diff current Nooir against current CrossPoint/CrossInk. Revisit:
- CrossPoint #3521 font-cache fragmentation;
- CrossPoint #3501 SD/SPI batching;
- CrossPoint #3398 / `9d2f234` packed Font Manager catalog, only if diagnostics justify it;
- `3555ff5` image-related fragmentation;
- `06b5d5b` SD-font ligature-view cleanup;
- `f4b4ff0` partial font-cache space-width recovery;
- CrossInk deferred SD-font discovery;
- debounced progress writes;
- streaming EPUB table rendering;
- framebuffer lending during chapter indexing;
- cancellable section pre-indexing;
- dictionary low-heap guards;
- overlay image/font-cache cleanup;
- XTCH memory fixes.

Do not port work already covered by 1.6.2. Preserve pagination, Arabic/Quran behavior, cache compatibility, image quality and working KOSync unless a separately measured fix requires otherwise.

## P1 — SD/SPI benchmark

Benchmark #3501-style SD/SPI batching on physical X4. Exercise EPUB, CBZ, XTC/XTCH, covers, metadata, dictionaries, SD fonts, sleep images and caches. Record latency plus heap/largest-block behavior. Treat it as a candidate, not an automatic port.

## P2 — XTC / XTCH

Keep Nooir's streaming/retained B/W-plane architecture. Audit CrossPoint/CrossInk for concrete chapter-listing, token scanning, settings, cache, memory and SD-I/O fixes. Prefer small isolated adaptations.

## P2 — Quick Actions

After flash/memory stabilization, consider the already-audited CrossInk model: one configurable trigger, five persisted action slots, shared popup, context-aware filtering and existing Nooir dispatchers. Measure flash cost before merging. Full UI/System Dark Mode is a separate larger project.

## P2 — FB2 prototype

Audit InkPointX's current FB2 implementation. Prefer normalization into Nooir's existing reflow/chapter/layout machinery rather than a second full reader. Reuse typography, images, progress, bookmarks, statistics, dictionary/clipping and existing lifecycle/cache infrastructure where safe. Prototype separately and measure flash/RAM before production integration.

## P3 — OPDS

First source-audit Nooir's current inherited OPDS status because documentation historically references CrossPoint OPDS while the 1.6.3 audit treats richer OPDS as a future candidate. Do not advertise or redesign it until current source behavior is reconciled.

Then compare CrossPoint and InkPointX for saved servers, search, pagination, authentication, direct download, cancellation, XML parsing, persistence and library refresh. Prefer the smallest useful delta and measure linked flash plus peak TLS/parser heap.

## P3 — PDF experiment

PDF is not a released Nooir reader. Compare separately:
1. InkPointX fixed-layout/raster/zoom.
2. CrossPDF-style text/reflow with prepared data cached on SD.

For Nooir's novel use case, investigate reflow/one-time SD preparation first. Keep fixed-layout raster PDF separate. Do not promise scanned/image-heavy, complex-layout, forms or arbitrary PDF compatibility. Measure parser/raster flash, preparation peak heap/largest block, SD cache size, page latency and failure cleanup.

## P3 — Bluetooth page turner experiment

Do not merge a full BLE stack into normal firmware while 1.6.2 has only 47,472 B app-slot margin. Audit current CrossPoint Bluetooth/page-turner work, CrossInk where relevant and CrossLink because CrossLink physically worked on X4 but showed disconnect behavior.

Start with a separate experimental build/profile. Measure:
- exact firmware increase;
- static and idle RAM;
- connection-time free heap/largest block;
- pairing/reconnect/disconnect behavior;
- chapter-indexing interaction;
- reader input ownership;
- sleep/wake and battery impact;
- physical X4 reliability.

Only then decide whether Bluetooth can fit normal Nooir.

## Later / evidence-triggered

- CrossPoint packed Font Manager arena if Font Manager allocation pressure returns.
- Additional image/dither fragmentation work only if physical traces show a remaining problem.
- KOSync redesign only for a concrete interoperability/protocol fix; current Nooir ↔ KOReader behavior is working.
- Full UI/System Dark Mode as a dedicated palette/architecture project; Reader Dark Mode already exists.
- X4 Classic and X4 Pro support/simulators follow the mature X3/X4 core pass. Begin with an explicit CrossPoint/CrossInk/FreeInk hardware delta and recovery-safety plan; simulator/build support may precede hardware ownership, but physical support must not be claimed without real-device validation.
- Broader Arabic/Quran shaping changes only with a specific bug and dedicated regression evidence.

## Recommended 1.6.3 sequence

### Phase A — mature the existing X3/X4 core first

**Workspace migration/toolchain validation → freeze clean 1.6.3 X3/X4 ELF/map baseline → flash map/recovery → EPUB/font/image/memory upstream delta → SD/SPI benchmark → small safe fixes → Quick Actions only if recovered headroom comfortably allows → OPDS hardening/reconciliation and other already-audited bounded improvements → full X3/X4 simulator regression → physical X4 torture/regression validation → freeze the mature X3/X4 core.**

The September upstream/source audits already provide the harvest queue. Do not restart broad archaeology before executing it; resume source-only audit only for a concrete candidate, a new upstream delta, or a linker/map question.

### Phase B — port the mature core to additional XTEINK hardware

After the X3/X4 core is stable, begin an explicit **X4 Classic + X4 Pro hardware-portability phase**. Do not mix this port with unrelated core reader changes.

For each new device:
- trace current CrossPoint/CrossInk/FreeInk board support and exact hardware detection rather than guessing;
- record MCU, flash/partition/OTA/recovery layout, PSRAM where applicable, display/controller, SD interface, buttons/input, touch/frontlight where applicable, power/sleep behavior and flashing/recovery assumptions;
- reconcile the current pinned FreeInk deliberately; do not update the pin or wholesale-merge upstream merely to obtain a board definition;
- add a distinct build target and capability-driven HAL/FreeInk boundary without scattering device-name conditionals through Nooir readers/UI;
- add simulator_x4classic / simulator_x4pro where useful for application/UI/capability-path validation;
- keep X3/X4 builds and behavior as regression controls;
- separately audit partition, OTA, flasher and recovery safety before producing a hardware-test candidate.

Simulator/build success is **not** evidence that a firmware is safe to flash. Until real hardware validation exists, X4 Classic/X4 Pro outputs must remain explicitly experimental/unverified and must not be described as physically supported. Locked-device flashing/recovery requires separate proof; do not recommend an unverified Nooir image where a failed flash could remove the user's recovery path.

### Phase C — larger/new subsystems later

FB2, PDF and Bluetooth remain later measured prototypes/experiments rather than prerequisites for the X4 Classic/X4 Pro port. Do not delay the hardware-portability phase merely to finish those large subsystems. Bluetooth continues to require a separate experimental profile first.

This sequence is a priority guide, not a promise that every candidate ships in 1.6.3.

## Non-negotiable regression boundaries

Preserve unless a separately approved task explicitly changes them:
- partition layout and recovery path;
- `SECTION_FILE_VERSION = 41` and compatible persisted data;
- Arabic/Quran rendering behavior;
- KOSync interoperability;
- XTC/XTCH current working architecture;
- user SD data;
- FreeInk exact tested pin;
- physical X4 stability.

Physical X3 evidence remains outstanding and must not be inferred from simulator success.

---

## Consolidated Phase A execution queue — authoritative

This queue consolidates the flash/linker, CrossPoint, CrossInk and resource-pack
reports. It is an execution order, not blanket approval to implement every
candidate. The official comparison control is the clean 1.6.3 baseline above.
Every experiment uses one variable, an isolated branch/worktree, a clean
gh_release build, a saved ELF/map/size report, host tests, and a documented
rollback decision. No item may change the partition table, user data, cache
format, FreeInk pin, or SECTION_FILE_VERSION 41 without a separate approval.

### Evidence summary

- Largest measured separable flash candidate: reader Noto Sans 12–18,
  approximately 1,033,234 B of the 1,087,118 B Noto Sans family; Noto Sans 8
  (approximately 53,884 B) remains the small UI/fallback safety font.
- Hyphenation payloads total 350,397 B; the map grouping for the hyphenation
  implementation is approximately 358,671 B. German alone is 206,259 B.
- Generated i18n data/tables measure approximately 297,935 B, but English is
  boot/recovery critical and optional-language fallback semantics must remain
  intact.
- Embedded web HTML/JS data measures approximately 102,429 B; JSZip alone is
  approximately 28,379 B and is used by EPUB/CBZ/file-management workflows.
- Built-in font totals are Arabic 556,193 B, Ubuntu 305,766 B, Noto Serif
  1,079,862 B and Noto Sans 1,087,118 B. Arabic, Ubuntu, Noto Serif and Noto
  Sans 8 remain embedded until an explicit compatibility experiment proves
  otherwise.
- TLS/wolfSSL, WebDAV, mDNS and the network server are shared live
  infrastructure. BLE HID host code is already capability-disabled/stub-only
  in the normal release; there is no honest large BLE removal to harvest.

### A. SAFE SMALL FIXES

| ID / source | Nooir subsystem and benefit | Flash / RAM / risk | Tests, hardware and rollback |
|---|---|---|---|
| A1 — CrossPoint f4b4ff06cd75508e0317695ab5d81c78272c3f73 | GfxRenderer SD-font space advance fallback in getSpaceWidth/getSpaceAdvance; prevents a partial font-cache miss from producing zero-width spaces. | Tiny flash; RAM neutral; low risk if the existing advance state is preserved. | Partial-cache host test, SD-font EPUB on X4 and X3. Arabic/Quran only as a shared-renderer smoke check. Roll back if space metrics, pagination or cache recovery differ. |
| A2 — CrossPoint 6eda8f0b7f204b7c51f8f79ddfd37bed688a74df | ChapterHtmlSlimParser treats boolean HTML hidden as display:none; fixes hidden trailing/inline content without changing cache format. | Tiny flash/RAM cost; low parser risk. | Fixtures for hidden div/span/p/heading and trailing EOF content; full host suite; X4/X3 malformed-EPUB smoke test. Roll back on visible-content or final-page regressions. |
| A3 — CrossPoint 03c484778bc6d4eb5376c7210b69d8d33aaee13e | CrossPointWebServer path normalization and escaped upload/move/delete/rename data; reduces traversal, quoting and Unicode filename failures. | Small web-only flash cost; RAM neutral; low-to-medium web regression risk. | Host path/HTML tests plus X4 direct-IP Transfer, WebSocket upload, WebDAV and Calibre smoke tests; X3 shared path. Arabic/Quran unaffected. Roll back if any file-management workflow or cache invalidation changes. |
| A4 — CrossInk 5148544b and ddb58903 | KeyboardEntryActivity UTF-8 cursor rendering, deletion and wrapping; fixes byte-based handling of multibyte text fields. | Likely under 1 KB; RAM neutral; low risk with ASCII unchanged. | Accented Latin, Cyrillic, Arabic and multibyte cursor/wrap host tests; X4/X3 keyboard smoke. Reader Quran rendering unaffected. Roll back on byte-boundary or input regressions. |
| A5 — CrossPoint ecec7e8d5e76ed5f53854c3df41f0f623f2f1889 | OPDS child Wi-Fi/password/keyboard activities keep sleep inhibited while work/input is active. | Tiny flash/RAM cost; low power-state risk. | OPDS scan, password, search and download activity tests; physical X4 sleep/wake and X3 shared ActivityManager smoke. Roll back if sleep is prevented after exit or resumes during network work. |
| A6 — CrossPoint f437bb5069a43edef82fc162914d90c6a129a28e | Display-only Hangul NFC composition for decomposed filesystem names while preserving raw SD paths. | Tiny flash; RAM neutral; low risk. | UTF-8/Hangul host tests and X4/X3 SD filename smoke. Arabic/Quran unaffected. Roll back if raw path lookup or non-Hangul normalization changes. |
| A7 — CrossPoint b5fb406f505144c6c4b5703664889455ae6a7538 | Static web ETag/304 and reproducible generated web asset handling; reduces repeated browser transfers. | Small flash; negligible RAM; low-to-medium web risk. | Generated-header test and X4 browser cache/304/direct-IP smoke. X3 same web path. Roll back if stale pages, uploads, captive portal or versioned assets are served. |

These are correctness/safety adaptations, not a reason to wholesale merge
CrossPoint or CrossInk. A1/A2/A4 are the preferred first production candidates
after the flash control is preserved.

### B. FLASH A/B EXPERIMENTS

Each row is a separate treatment. Do not combine rows or infer savings from
generated-source sizes. The treatment must retain enough behavior to boot,
open books and recover to the control image.

| ID / one variable | Exact footprint and purpose | RAM / risk / required evidence | Physical and rollback gate |
|---|---|---|---|
| B1 — reader Noto Sans 12–18 | Remove only the alternate reader Noto Sans 12–18 generated family; retain Noto Sans 8, Noto Serif, Ubuntu and all Arabic fonts. Gross modeled saving approximately 1,033,234 B. | Static RAM likely neutral; fallback/prewarm and pagination risk is high until tested. Map must show the exact removed symbols and any new fallback code. | X4 and X3: UI, Latin/Cyrillic/Vietnamese/Hebrew/Arabic EPUB, SD-font fallback, malformed EPUB, cache reopen. Roll back on any missing glyph, changed pagination, Arabic/Quran difference, or fallback boot failure. |
| B2 — German hyphenation only | Exclude the German trie, approximately 206,259 B payload, while retaining generic fallback and other languages. | RAM neutral in the image; pagination changes are expected for German and must be treated as a layout/cache contract issue, not a free optimization. | German EPUB on X4/X3 with cache clear/reopen and line-break comparison. Roll back if fallback is not explicit, cache semantics become ambiguous, or latency/quality regresses. |
| B3 — all hyphenation payloads | Exclude all ten tries, approximately 350,397 B payload / approximately 358,671 B grouped map cost, leaving generic fallback. | RAM neutral but broad pagination/quality risk; not a production proposal without a resource/cache design. | Only after B2 evidence; multilingual EPUB matrix on X4/X3. Roll back on any unacceptable line-break or cache mismatch. |
| B4 — optional non-English i18n data | Isolate optional translations while keeping English and the existing O(1) fallback contract. Maximum gross data opportunity is approximately 287 KB, not a guaranteed saving. | RAM neutral to small loader cost; high settings/recovery/translation risk. | Simulator plus X4/X3 settings, boot, recovery/update, language switching and missing-pack fallback. Roll back if any boot/recovery screen or persisted language path depends on removed data. |
| B5 — JSZip only | Compile out only browser JSZip and record all callers before changing behavior; measured live data is approximately 28,379 B. | Small flash recovery; RAM neutral; functional risk is high because EPUB inspection, image preview, conversion and CBZ progressive-JPEG normalization use it. | X4 web file, EPUB/CBZ workflows and direct upload/download; X3 shared path. Roll back if any caller loses required behavior. |
| B6 — duplicate Nooir logo | Deduplicate the two linked NooirLogo360 copies; approximately 4,590 B candidate. | RAM neutral; very low risk if pixel identity is proven. | Host binary/resource identity and X4 boot/home/sleep image smoke; X3 same. Roll back on any image polarity or refresh difference. |
| B7 — WebDAV, mDNS, UDP discovery, separately | Three isolated convenience experiments: WebDAV off, mDNS off, UDP discovery off. Ordinary HTTP/WebSocket Transfer remains the control path. | Savings are map-dependent; RAM neutral or component-specific. WebDAV and discovery are user-facing and shared-path risks. | X4 browser Transfer, WebSocket, Calibre, direct-IP and WebDAV tests; X3 shared network path. Roll back if the measured saving is small or a supported workflow breaks. |

The first A/B is **B1 reader Noto Sans 12–18**, in an isolated worktree, because
it is the largest cleanly separable candidate while leaving Arabic/Quran, UI
fallback, Noto Serif and the recovery-safe small font set intact. A winning
experiment must explain its map delta and pass the physical gates; it is not
automatically merged.

### C. MEMORY / CACHE RECONCILIATION

| ID / source | Nooir path and expected benefit | Flash / RAM / risk | Required validation and rollback |
|---|---|---|---|
| C1 — CrossPoint c80c537f287506dbac4f99f0b97a89cffbe36302 and CrossInk 3a59c61c | Reconcile explicit non-accumulating/complete-render SD-font prewarm semantics with Nooir's retained metadata, fallback and failure recovery. | Small code cost; may reduce transient fragmentation but changes prewarm lifetime. High Arabic/fallback risk. | EPDMEM before/after on X4 and X3: uncached EPUB, Arabic/Quran, SD-font, image-heavy pages, rapid turns. Roll back if minimum heap/largest block, glyph coverage, pagination or responsiveness worsens. |
| C2 — CrossInk eeb4beaa | XtcParser reusable streaming chunk instead of a new vector per loadPageStreaming call. | Small flash; approximately 1 KB retained per open parser, less churn. Medium XTC risk. | XTC/XTCH host fixtures, repeated four-pass rendering, sparse/malformed rows, X4 and X3 heap/latency. Roll back if retained RAM costs more than fragmentation saved or failure cleanup changes. |
| C3 — CrossPoint 3555ff5569754933be2e0839a583748b42dbe941 / CrossInk 3555ff55, 636c5a62 | Diff remaining image scratch lifetimes against Nooir's contiguous/no-throw PixelCache and XTC safeguards; adapt only a proven non-overlap. | Potential RAM-positive, small flash; generic renderer/XTC risk. | JPEG/PNG/image-cache/PixelCache EPDMEM, XTC, rapid turns and low-heap X4/X3 tests. Roll back on image quality, dark-mode polarity, cache or MaxAlloc regression. |
| C4 — CrossPoint e33e3cf39d66a35905782866b3cf04a7223b2e31 | Isolated USE_SPI_ARRAY_TRANSFER=1 test for SdFat transfer batching. | Near-zero flash expected; approximately 512 B transfer-path stack use; SD timing/starvation risk. | Physical X4 first: sequential reads/writes, EPUB sections, images, fonts, dictionaries, caches; repeat on X3. Roll back if stack/heap floor, filesystem integrity or latency worsens. |
| C5 — CrossPoint dc9c3eabc4ac9cd9553064b67dd64d93b103dd08 | Compare rowProvider/lazy FileBrowser rows with Nooir's materialized raw filename vector. | Possible dynamic-RAM benefit only on 500–1000-file folders; SDK compatibility risk and no assumed flash saving. | Large-folder host/simulator and X4/X3 SD test. Roll back if ordering, selection, refresh, Unicode names or path safety changes. |
| C6 — CrossPoint 6230eba2d8b757b2d480d5a151b1786d0537c7a6 / CrossInk af930f2b | Validate whether Nooir's progressive JPEG patches still miss 4:2:0 component/scan cases; do not replace the existing decoder path without a failing fixture. | Flash/RAM neutral until a real gap is proven; image correctness risk. | 4:4:4, 4:2:2, 4:2:0 progressive, baseline, PNG, cover and CBZ fixtures plus X4/X3. Roll back any decoder change that affects grayscale or cache output. |

Already-covered cache work (EPUB ownership, final-page flush, cumulative
spine-size cache, font-cache release before layout, dictionary disposable-cache
release, and the presentation-form guard) is not to be reimplemented.

### D. PHYSICAL X4 BENCHMARKS

| Order | Benchmark / measurements | Acceptance and X3 relevance |
|---|---|---|
| D1 | Establish control telemetry from the official normal gh_release: page/section latency, free heap, minimum free heap, MaxAlloc/largest block, SD read timing and image/font cache behavior. | No source change. Repeat on X3 for every shared SD/renderer candidate; X4 is the primary gate. |
| D2 | Run C4 SD batching A/B across EPUB text/uncached sections, image-cache generation, JPEG/PNG, SD fonts, dictionaries, CBZ, XTC/XTCH and cache writes. | Keep filesystem integrity and stack margin; reject any unexplained X3 regression. |
| D3 | Run C1/C3 EPDMEM matrix: cached text, uncached section, chapter transition, image-heavy, JPEG, PNG, Quran/Arabic, SD font, rapid forward/backward turns. | Identify minimum-free-heap/MaxAlloc windows, not just recovered steady heap. Arabic/Quran must remain byte-for-byte behaviorally unchanged. |
| D4 | Run C2 XTC/XTCH repeated streaming and sparse chapter tests. | Confirm retained scratch does not consume the X3 margin it is meant to protect. |
| D5 | Run C5 large-folder FileBrowser tests at 500 and 1000 entries, including Unicode and long names. | Selection/hit order, refresh, path safety and RAM must remain stable on X4/X3. |
| D6 | Run F-network tests: OPDS sleep states, WebDAV/Transfer, mDNS/direct-IP, WebSocket, OTA and KOSync. | No TLS/OTA/recovery regressions; no user data changes. |

### E. EPUB / XTC READER IMPROVEMENTS

1. **E1 — hidden HTML and EOF safety:** execute A2 first; retain the existing
   final-page/anchor/hidden-element regression suite. SECTION_FILE_VERSION 41
   remains fixed.
2. **E2 — XTC streaming scratch:** execute C2 only after D4; preserve the
   retained B/W-plane architecture and no pinch/zoom redesign.
3. **E3 — progressive JPEG 4:2:0:** fixture-first reconciliation under C6.
4. **E4 — compact tables and malformed table hardening:** CrossInk
   2e61e081, 9d36a067, 94d13eb2, 6163ef5c, 7f3a14d4 and b06fd027 are **LATER**.
   Nooir currently flattens simple tables; a full grid changes page geometry,
   clipping and likely cache behavior. Take only a demonstrated caption/null
   safety fix, with an explicit cache analysis.
5. **E5 — queued image/text-AA deferral:** CrossInk 60e96fd5 and 6037033c are
   **LATER** because they change turn scheduling and require physical
   responsiveness evidence.
6. **E6 — preserve existing EPUB equivalents:** incremental section building,
   final-page serialization, cumulative spine sizes, image safeguards,
   fallback/prewarm recovery and malformed-XHTML handling are already present.

### F. OPDS / WEB HARDENING

1. Reconcile current OPDS end-to-end behavior first: saved servers, streamed
   feeds, search, pagination, cancellation, Basic credentials, downloads,
   .part replacement, cache invalidation, redirects, MIME types, malformed
   feeds and post-download visibility. This is hardening, not greenfield OPDS.
2. Execute A3 path/escaping and A5 sleep guards before considering richer OPDS.
3. Execute A7 ETag only if its generated-header and stale-content tests pass.
4. Keep wolfSSL/TLS, WebDAV, mDNS/captive portal and WebSocket infrastructure
   until a one-variable map experiment proves a worthwhile safe removal.
5. Keep OTA and Font Manager HTTPS paths intact. Do not treat shared TLS bytes as
   removable merely because they are large.

### G. OPTIONAL QUICK ACTIONS DECISION

Quick Actions remain deferred. CrossInk's one-trigger/five-slot model is useful
reference, but adding settings, popup, input ownership and strings is not a
Phase A default while the app margin is about 45.9 KB. Reconsider only after
accepted flash experiments, with one measured build and X4/X3 input/sleep tests.
Rollback if the recovered headroom is not comfortable after all safety margin and
if settings/persistence or button ownership changes.

### H. PHASE A REGRESSION / FREEZE

The freeze gate is ordered:

1. clean synchronized 1.6.3 source and baseline ELF/map;
2. accepted small fixes with focused host tests and full host suite;
3. accepted flash A/B results with section/map explanations;
4. X4 physical torture matrix and X3 shared-path regression;
5. XTC/XTCH, CBZ, Arabic/Quran, EPUB pagination, KOSync, OTA, Font Manager,
   sleep/wake and recovery fallback checks;
6. simulator_x4 and simulator_x3 full suites;
7. freeze the mature X3/X4 core before any X4 Classic/X4 Pro port;
8. document exact firmware, static-RAM, heap and physical evidence.

X4 Classic/X4 Pro begins only after this freeze. PDF, FB2 and Bluetooth remain
Phase C experiments. Quick Actions remain conditional. No physical X3 support
claim may be made from simulator results alone.

### Explicitly deferred or skipped

- Full compact EPUB tables, table-aware clipping/highlighting and queued-turn
  rendering.
- Styled dictionary HTML; current fallback, successful-source picker, lazy
  six-source cap, invalid-folder suppression and disposable-cache lifecycle
  remain the 1.6.2 behavior.
- Full CrossInk UI-font regeneration, Arabic/Quran font replacement or shaping
  replacement, and paragraph-direction inheritance without a dedicated defect
  and cache migration plan.
- CrossInk retained PSRAM image strategy, XTC pinch/zoom, CrossPoint Library
  view, on-device rename, timezone/DST, Arabic keyboard and wholesale merges.
- SPIFFS/resource-pack implementation before the isolated A/B economics and
  ownership/recovery design are proven.
- Quick Actions, X4 Classic/X4 Pro work, PDF, FB2 and Bluetooth production
  integration.
