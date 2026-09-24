# Folio Nooir Handoff

Read [`PROJECT_CONTEXT.md`](PROJECT_CONTEXT.md) for the complete project
history, decisions, and feature inventory.

## Durable EPUB/CSS research record

The documentation-only EPUB/CSS research is preserved in
[`docs/EPUB_CSS_RESEARCH.md`](docs/EPUB_CSS_RESEARCH.md). It records Nooir's
current verified CSS/parser/layout and cache behavior, Microreader/CrossPoint/
PapyriX findings, selector and inheritance gaps, malformed-EPUB recovery,
incremental/cooperative section building, memory gates, benchmark requirements,
torture fixtures, candidate dispositions, and the phased implementation order.
It is a planning reference only; it does not authorize firmware/source changes.

## 1.6.3 development line opened (2026-09-18)

The Folio Nooir **1.6.3 development line is officially open** at checkpoint
`12e66d191fe87a0b0006305e2ea661528b4efb65`. Released 1.6.2 remains the
compatibility and measurement baseline; the first 1.6.3 work is the clean
flash/headroom audit. No functional firmware change is implied by opening the
line.

## Workspace migration — authoritative locations (2026-09-18)

The C: -> D: migration is complete. Use these locations for all future work:

- **Primary/authoritative Windows workspace:** `D:\fatiha\side`
- **Native WSL build/simulator mirror:** `/home/fatiha/side-wsl`
- Windows source is visible in WSL as `/mnt/d/fatiha/side`; the native WSL mirror must mirror **D:**, not C:.
- Windows production `gh_release` builds must run from `D:\fatiha\side`.
- Linux `simulator_x4` / `simulator_x3` validation must run from the fresh native mirror `/home/fatiha/side-wsl`.

Preserved references/safety copies — **not active workspaces**:

- Old Windows location: `C:\Users\fatiha\Documents\side`. This is the pre-migration safety checkout and is retained only for recovery/reference.
- Archived pre-migration dirty WSL mirror: `/home/fatiha/side-wsl-pre-d-migration-20260918`, preserved at original HEAD `90c42ce35ad3cc594a8f70ebac6d7e05bc283ada` with its dirty tracked/untracked work intact.
- Do **not** develop, build, sync into, reset, clean, or delete either safety copy. Read-only inspection is allowed when reconciling historical work.

Migration validation was performed while firmware version source remained **1.6.2**, `SECTION_FILE_VERSION = 41`, and FreeInk remained pinned to `958720659ea289ae325e83db20049d0ea844800d`. At migration validation HEAD `124581ff14db309ffca52c4d334fd08409b43d96`, D: and the fresh WSL mirror were clean and matched the remote. Windows used Python 3.12.10 / PlatformIO 6.1.19; WSL used Python 3.12.3 / PlatformIO 6.1.19 / SDL2 2.30.0.

The fresh D: `gh_release` build succeeded at **6,497,905 linked flash**, **6,511,760-byte firmware.bin**, **41,840-byte final app-slot margin**, and **53,576-byte static RAM**. Both fresh WSL simulators built and ran through Boot to RecentBooks. These fresh numbers are recorded separately from the historical released 1.6.2 measurement (6,492,279 linked / 6,506,128 bin / 47,472 margin / 53,492 RAM); the approximately 5.6 KB discrepancy must be investigated before selecting the reproducible 1.6.3 starting baseline.

## Current position

### Phase status and current gate — 2026-09-24

- **Phase A — headroom/foundations:** FROZEN / COMPLETE.
- **Phase B — EPUB foundation/performance:** FROZEN / COMPLETE. The temporary
  B1/B2 diagnostic work was removed and was not merged into production.
- **Phase C — EPUB CSS correctness:** COMPLETE / FROZEN.
  - **C1:** deterministic source-order/per-property cascade — committed as
    `de299fd541906646f278ccdb28af94928fbe1111` and physically validated on
    the old-model XTEINK X4.
  - **C2:** transactional CSS-cache publication — committed as
    `538d19e692f28eb42feca3dcc4fae2d959580bac` and physically
    validated through normal cache creation/reuse/reboot on X4. Destructive
    interruption or power-loss testing is not claimed.
  - **C3:** bounded compound selectors — committed as
    `1ee54f63fe14d3201936bbaa424a4d1af7439427` and physically validated on X4.
  - **C4:** per-property `!important` cascade — committed as
    `601b7005a0d8eb0f975ed0a77ee02f0fb9d4b12c` and physically validated on X4.
  - **C5:** targeted inheritance — not implemented; deferred because no
    meaningful reading failure was demonstrated.
  - **C6:** additional resilience hardening — not implemented as a separate
    Phase C slice; deferred into evidence-driven Phase D work.
- **Phase D — EPUB resilience + richer rendering:** NEXT / ACTIVE PLANNING.

Immediate next action is a short Phase D source re-check followed by focused,
evidence-driven resilience work. Do not repeat the completed Phase C selector
and cascade audit unless new source or fixture evidence contradicts this
freeze. Preserve the frozen firmware baseline and the rule that Nooir can get
smarter, but not sluggish.

## Phase C complete / frozen — 2026-09-24

The integrated standalone validation EPUB passed on a real old-model XTEINK
X4. It covered the reviewed C1/C2/C3/C4 cascade, specificity, compound,
inline, `display:none`, HTML `hidden`, cache reopen/reuse, and resilience
cases. The fixture had 10 chapters, 62 visual tests, 7 CSS files, and was
approximately 11.9 KB. ZIP/container/XML structure was checked; EPUBCheck
was unavailable and is not claimed. No destructive power-loss test was
performed.

Phase C leaves Nooir with a deliberately bounded CSS subset: deterministic
per-property source order, bounded specificity, repeated selector blocks,
ordinary inline styles, supported tag/class/ID compounds, up to three required
classes, class-token-order-independent matching, per-property `!important`,
importance > specificity > source order, supported inline/important
interaction, `display:none` cascade interaction, and transactional CSS-cache
publication. This is not browser-complete CSS and does not add descendant,
child, sibling, attribute, pseudo, universal, generic-AST, full-inheritance,
font-family, arbitrary-specificity, or user-origin support.

Final C4 engineering state: 5,470,841 linked bytes, 5,484,688-byte
`firmware.bin`, 1,068,912 padded app-slot bytes remaining, and 53,448 bytes
static RAM. The C4 delta versus C3 was +3,176 linked flash, +3,168 padded
bytes, -3,168 app-slot margin, and 0 static RAM. The shared `gh_release`
configuration compiles the X3/X4 paths; physical validation here is X4 only.

- Active development line: Folio Nooir **1.6.3**
- Latest released baseline: Folio Nooir **1.6.2**
- Authoritative branch: `codex/folio-nooir`
- Published 1.6.2 tag/release commit: `25df494020874150a047e2a4b3be62c3e40151e8` (`1.6.2`)
- 1.6.2 firmware/source milestone: `85dda52a102163b40fd4a2ddfde65e6cdc23af36` (`feat: stabilize Nooir readers, sync and Spine Shelf`)
- Previous published 1.6.1 tag/release commit: `2c817a73f1a1143d7f62ac1e768501280abacaa3`
- Remote Quran fixture commit: `e0478714 Quran Epub`; synchronization merge:
  `5efe0695`. The merge added only three repository Quran EPUB fixtures and is
  not a new firmware/source baseline. Documentation commits after the merge
  must remain distinct from the firmware milestone.
- Documentation-only commits after the 1.6.2 tag are 1.6.3 planning/context work and do not change the released 1.6.2 firmware baseline.
- FreeInk is a real Nooir dependency through the `freeink-sdk` submodule.
- The 1.5.10 baseline uses the Nooir-specific FreeInk commit
  `958720659ea289ae325e83db20049d0ea844800d` (`9587206`).
- The completed 1.6.0 source, translations, and feature inventory are
  integrated into `codex/folio-nooir` at merge `0f1bd556`; the branch also
  contains the dedicated 1.6.0 README update at `b12f2732` and the latest
  README-only remote update at `ccddded2`.
- The previous safety checkpoint was `safety/1.6.0-carousel-layouts-hq` at
  `9c8e9751`, which is an ancestor of the current development branch.
- The normal `gh_release` build, combined diagnostic build, host suite, Spine
  tests, WSL simulators, and physical X4 validation have all passed for the
  known-good milestone. No new firmware implementation is authorized by this
  documentation task.
- The existing CBZ reader and cache behavior listed below are 1.6.0 baseline
  functionality.
- The completed 1.6.1 focus was EPUB reading quality, especially Arabic/RTL
  support, text shaping, fonts, and layout. The Arabic/EPUB foundation, typography work, EOF finalization, glyph-bound compensation, and warm-turn path are part of the released baseline.
- The detailed future CBZ/Manga plan is preserved in
  `docs/CBZ_MANGA_PLAN.md` and remains deferred. Quick Actions are not
  implemented and may be explored only in a later release. Full UI/System
  Dark Mode is not implemented and remains deferred.

## 1.6.1 released baseline

- The release retains the completed 1.6.0 baseline and the planned
  CBZ/Manga work remains future work in `docs/CBZ_MANGA_PLAN.md`.
- The 1.6.1 EPUB work includes Arabic fallback, shaping/RTL integration,
  Quranic-mark bounds compensation, malformed-XML recovery, generic block
  flow, bounded CSS typography, EOF/page finalization, and the warm page-turn
  fast path. Section cache version is `41`, with shaping/fallback contract
  discriminators retained.
- Temporary physical missing-glyph and EPUB memory-pressure diagnostics were
  used during investigation and have been removed from the release source.
  The functional fallback, font grouping, and rendering fixes remain.
- Physical X4 validation covered the EPUB fixes and Arabic/Quran reading paths.
  Physical X3 validation is not claimed here; the shared X3/X4 code paths and
  simulator validation remain useful but do not replace that hardware check.
- The current normal `gh_release` build is linked at `6,492,279 / 6,553,600`
  bytes, with `61,321` linked bytes remaining; padded `firmware.bin` is
  `6,506,128` bytes, leaving `47,472` bytes in the app slot. Static RAM is
  `53,492 / 327,680` bytes. This leaves `7,472` bytes above the preferred
  approximately 40 KB production cushion.
- The combined `gh_release_diag` profile enables
  `NOOIR_EPUB_DIAGNOSTICS=1` and `NOOIR_KOSYNC_FONT_DIAGNOSTICS=1`; its linked
  size is `6,506,717` bytes and padded image is `6,520,560` bytes, leaving
  `33,040` bytes. Normal `gh_release` does not enable these diagnostics.
- The host regression suite is `211/211` passing and the focused Spine suite is
  `13/13` passing. WSL `simulator_x4` and `simulator_x3` pass and reach
  RecentBooks. Physical X4 validation is recorded; physical X3 hardware
  validation is not claimed. Windows simulator builds are blocked before
  compilation when `sdl2-config` is unavailable.
- Existing ignored build outputs must not be treated as release assets unless
  their source/configuration is positively verified.

## 1.6.2 released implementation baseline

- Dictionary lookup keeps the remembered/configured dictionary as the fast
  path and continues to the first valid prepared fallback when it misses or
  cannot be opened. A stale, renamed, deleted, truncated, or unsupported
  preferred folder therefore does not block another working dictionary.
- The first detailed open diagnostic for a failed folder is retained while
  repeated validation of the same folder during the reader/dictionary session
  is suppressed. Settings and dictionary history are not rewritten, and the
  existing 32-byte settings field / 31-byte history-name limit is unchanged.
- The definition screen discovers alternate matches only when its Dictionary
  action is opened. It keeps at most six lightweight source records and loads
  only the selected definition body; the former combined multi-definition
  buffer is no longer used. Source switching replaces the body and resets
  definition pagination.
- The result header identifies the matched headword, source, preferred/fallback
  status, page position, and source position where multiple matches exist.
- The Sources picker contains only successful matching sources, up to six, and
  discovers alternates lazily. Invalid/no-match folders are skipped. The first
  successful result remains immediate.
- The current Spine layout is available independently for Recent and Finished:
  bounded left-to-right pagination, deterministic dimensions and grayscale
  tones, restrained binding styles, UTF-8-safe title/author fallback, shared
  render/hit rectangles, shelf/support styling, and an optional decoration-only
  plant. Library and Carousel are untouched.
- Font Manager installation is physically confirmed. KOReader Sync is
  physically interoperable with actual KOReader; Filename mode requires equal
  actual filenames, Binary mode retains the KOReader partial-MD5 identity, and
  the public server default is `https://sync.koreader.rocks:443`.
- StarDict and `.qidx` formats, persistent settings, EPUB caches, fonts,
  Arabic/Quran behavior, `SECTION_FILE_VERSION = 41`, the FreeInk SDK pin,
  partitions/SPIFFS, and user SD data remain unchanged.

## Completed work

- Direct CBZ reader with ComicInfo metadata, covers, thumbnails, bounded page
  indexing/extraction, Fit Width/Fit Page/Landscape/Zoom, page picker,
  RTL/LTR navigation, bookmarks, persistent cache replay, queued navigation,
  X4 three-page read-ahead, and conservative X3 read-ahead.
- CBZ read-ahead uses the same cache across Fit Width, Landscape, and Zoom;
  it shows reserved-area progress and a Next-ready state, yields to foreground
  navigation, and cancels/queues actions instead of silently blocking input.
- CBZ Reset Progress removes `progress.bin`; per-book Clear Reading Cache
  removes generated reader pages without removing progress; global Clear
  Reading Data retains its broader metadata/statistics cleanup behavior.
- Web transfer can optionally normalize progressive CBZ JPEGs to baseline
  JPEGs; baseline JPEGs and PNGs are not re-encoded.
- EPUB bounded JPEG/PNG handling, large/progressive-image safeguards, safe
  PixelCache behavior, and shared long-operation loading feedback.
- EPUB clipping/highlight restoration preserving regular, bold, and mixed
  styles through save/load and reflow.
- Library/Recent metadata behavior fixes, XTC exit/cover preservation, and
  KOReader Sync 2xx handling.
- Web To-Do editing avoids refresh collisions and duplicate saves; Clock &
  Weather sync guards duplicate requests, feeds the watchdog during slow
  network work, and temporary station-Wi-Fi loss no longer ends web mode.
- Power-lock transitions avoid redundant active requests while retaining the
  existing sleep and deep-sleep paths.
- Native X3/X4 simulator support and documentation are available through the
  shared source and `simulator_x3`/`simulator_x4` environments.
- The independent Carousel theme supports persisted 3-Cover and 5-Cover
  layouts, circular navigation, small-collection handling, screen-derived
  trapezoidal cover geometry, opaque far-to-near rendering, and the graphical
  Library path.
- Folio Nooir Recent and Finished each support independent `3 Covers`,
  `4x2 Grid`, `3-Cover Carousel`, and `5-Cover Carousel` layouts. Folio
  Carousel rendering remains inside the shelf region; the graphical Folio
  Library remains unchanged.
- Carousel centers and Statistics Sleep screens prefer an explicitly prepared
  valid `thumb_360.bmp` and immediately fall back to `thumb_220.bmp`. Side
  covers, Folio shelf cards, and Statistics Books use the existing 220px
  source. Navigation and sleep never synchronously generate HQ covers.
- Carousel source reuse keeps a six-handle cache, protects only the current
  frame's source paths from LRU eviction, caches unavailable-HQ probes during
  the activity session, and uses shared perspective-rendering fast paths.
- Reading Statistics provides Overview, Calendar, Books, and Achievements
  tabs, persistent time/session/page data, bounded 730-day history, streaks,
  averages, circular navigation, and twenty derived achievements. Reading
  Stats Sleep and Minimal Stats Sleep use bounded cached-cover layouts, while
  legacy Cover, Overlay, and Clipping sleep modes retain their full-screen
  rendering behavior.
- Settings persist the independent Carousel layout and Folio Recent/Finished
  layouts separately, with migration/default handling and translations added
  through the normal i18n source workflow.
- PDF and FB2 readers are not implemented; the repository contains feasibility
  documentation only.

## FreeInk dependency and publishing state

The nested `freeink-sdk` contains the only Nooir-specific SDK change used by
the 1.5.10 baseline:

```text
JD_FASTDECODE 1 -> 0
local commit: 958720659ea289ae325e83db20049d0ea844800d
local branch: nooir-1.5.10-tjpgd
```

This selects TJpgDec's portable Huffman path, which was needed to avoid the
grayscale artifacts seen with the fast path. FreeInk must therefore be
fetchable for anyone cloning Nooir; the parent must pin this exact tested
commit rather than track a moving branch.

The nested checkout is now configured conventionally:

```text
origin   https://github.com/toshio2011/freeink-sdk.git       (user fork)
upstream https://github.com/Free-Ink/freeink-sdk.git         (official)
branch   nooir-1.5.10-tjpgd
```

The fork was created under `toshio2011`, and branch `nooir-1.5.10-tjpgd` was
pushed successfully. Remote verification resolves it to
`958720659ea289ae325e83db20049d0ea844800d`. The parent tree points
`.gitmodules` at the fork and pins the exact tested `9587206` submodule
commit. The pin is committed in the authoritative history.

## 1.6.3 development constraints

- Normal production profile: `gh_release`; current measurements are
  `6,492,279` linked flash, `6,506,128` padded `firmware.bin`, `47,472` app
  bytes remaining, and `53,492` static RAM. The preferred app-slot cushion is
  about 40 KB, so new firmware work must measure its flash delta.
- Diagnostic profile: `gh_release_diag` with
  `NOOIR_EPUB_DIAGNOSTICS=1` and `NOOIR_KOSYNC_FONT_DIAGNOSTICS=1`. It is for
  measurement only and is not the release image. Diagnostic memory result
  semantics remain `result=0` = policy/dependency skip, `result=-1` = actual
  allocation failure, and `result=1` = success.
- EPUB ownership/lifecycle, image/font cleanup, cumulative spine sizing,
  bounded temporary allocations, and ditherer allocation work are production
  adaptations from the known-good milestone. Do not alter pagination, cache
  format, Arabic/Quran behavior, or user SD data without separate approval.
- JPEGDEC progressive-component patching is kept in the committed patch stack;
  patches `0001` through `0004` are applied by `scripts/patch_jpegdec.py`.
- Full UI/System Dark Mode is **not implemented**. Reader Dark Mode is
  implemented. Quick Actions are **not implemented**; they remain future
  investigation only. No X4 Pro simulator exists.


## Post-1.6.2 upstream plan — 1.6.3 development line

Folio Nooir **1.6.2 is released**. Any new source change belongs to the **1.6.3 development line** unless explicitly scoped otherwise. The first 1.6.3 objective is to recover flash and preserve/expand heap safety before adding another large subsystem.

### Upstream sources and policy

Track CrossPoint Reader, CrossInk, InkPointX, CrossPDF (PDF architecture), and CrossLink (Bluetooth/X4 reference). Repeat an upstream-delta audit during each Nooir release cycle and classify work as **TAKE NOW / INVESTIGATE / LATER / SKIP / ALREADY COVERED**. Upstream is a source of fixes and ideas, not Nooir's target state; never wholesale-merge simply to catch up.

### 1. Flash recovery — first priority

The 1.6.2 production baseline is 6,492,279 linked flash, 6,506,128 padded firmware.bin, 47,472 app-slot bytes remaining, and 53,492 static RAM. With a preferred ~40 KB production cushion, this is too little headroom for casually adding OPDS, PDF, FB2, or Bluetooth. Before large features, generate a linker/map-level breakdown and audit compiled-in themes, bundled/fallback fonts, icons/assets, inherited unused activities, translations, web assets, duplicate theme/rendering code, dead linked functionality, LTO/garbage collection, and resources that can safely move to SD. Compare CrossPoint's SD-theme direction and CrossInk's font/build-size reductions. Keep Folio Nooir built in unless separately proven safe. Never enlarge/change the partition. Investigation target: recover meaningful headroom, ideally 100–200 KB or more, but claim only measured normal gh_release savings.

### 2. EPUB / fonts / images / memory

The durable EPUB/CSS research record is
[`docs/EPUB_CSS_RESEARCH.md`](docs/EPUB_CSS_RESEARCH.md). It must be read
before any future EPUB/CSS implementation so current Nooir equivalents are not
reimplemented and every candidate is measured against the X3/X4 constraints.

Re-diff Nooir against current CrossPoint and CrossInk before adding formats. Re-evaluate CrossPoint #3521 font-cache fragmentation, #3501 SD/SPI batching, #3398 / 9d2f234 packed Font Manager catalog, 3555ff5 image-fragmentation work, 06b5d5b SD-font ligature-view cleanup, and f4b4ff0 partial font-cache space-width recovery. Some older upstream ideas are already adapted in 1.6.2; never port them twice. Also compare CrossInk deferred SD-font discovery, debounced progress writes, streaming EPUB tables, framebuffer lending during indexing, cancellable pre-indexing, low-heap dictionary guards, overlay-image fixes, XTCH memory fixes, and font-cache release around overlay PNG work. Preserve cache version 41, pagination, Arabic/Quran rendering, image quality, and working KOSync unless separately tested evidence requires a change.

### 3. SD/SPI performance

Benchmark CrossPoint #3501-style SD/SPI batching because EPUB, CBZ, XTC/XTCH, covers, metadata, dictionaries, fonts, sleep images, and caches are SD-heavy. Measure before/after on X4 and treat it as an optimization candidate, not an automatic port.

### 4. XTC / XTCH

Keep Nooir's streaming/retained B/W-plane architecture. Diff current CrossPoint/CrossInk for concrete chapter-listing, token-scan, settings, memory, cache, or SD-I/O fixes. Prefer small isolated fixes and re-test XTC/XTCH after shared SD/image/font changes.

### 5. FB2 — strong candidate after flash recovery

Audit InkPointX FB2 in detail. Prefer normalizing FB2 into Nooir's existing reflow/chapter/layout machinery instead of shipping a second full reader, reusing typography, images, progress, bookmarks, statistics, dictionary/clipping, Arabic/Bidi where valid, and cache/lifecycle infrastructure. Prototype separately and measure flash/RAM before integration.

### 6. OPDS — after headroom exists

Audit CrossPoint and InkPointX OPDS for saved servers, search, pagination, authentication if present, direct download, cancellation, XML parsing, persistence, and library refresh. Prefer the smallest useful implementation. Measure linked flash and peak TLS/parser heap before deciding whether it belongs in normal firmware.

### 7. PDF — experimental branch, reflow first

PDF remains unimplemented. Compare InkPointX fixed-layout/raster/zoom with CrossPDF-style text/reflow and SD-prepared caches. For Nooir's novel use case investigate reflow/one-time SD preparation first so the existing reading experience can be reused. Keep fixed-layout raster PDF separate. Do not promise scanned/image-heavy or arbitrary PDF compatibility. Measure parser/raster flash cost, preparation peak heap/largest block, cache size, latency, and failure behavior.

### 8. Bluetooth remote input / BLE — research first, experimental implementation only

Do not treat this merely as "Bluetooth page turner support" and do not merge a full BLE stack into normal Nooir before flash/memory headroom exists. First perform a comparative BLE/HID audit across relevant XTEINK firmware implementations (including CrossLink, the Chinese X3/CrossLink lineage where source can be obtained, CrossInk, CrossPoint/current BLE work, CrumBLE/community implementations, and other working X3/X4 BLE firmware). Record the exact stack/library and build configuration used, supported HID report types, pairing/bonding, reconnect behavior, sleep/wake lifecycle, failure recovery, and how each implementation hands input to its UI.

The key research question is the **remote's actual input behavior**. Some remotes may emit simple keyboard/consumer-control clicks, while swipe-style/touch-oriented remotes may emit mouse/pointer movement, wheel data, press/release sequences, or another multi-report HID pattern. Nooir should eventually capture and decode the complete relevant HID report/sequence before deciding what it means. Do not prematurely collapse the first report into Next/Previous Page. Investigate a bounded pipeline such as: **BLE/HID transport -> raw report capture -> HID decoder -> bounded sequence/gesture recognizer -> normalized trigger -> configurable Nooir mapping -> existing Nooir action/input dispatch**. This should allow supported remote inputs to be mapped to useful actions without making the BLE layer EPUB-specific. A development/diagnostic BLE Input Inspector may log report type, usage/key/button, modifiers, movement/wheel values, press/release/repeat, and recognized sequence; it need not ship in production if its flash cost is undesirable.

Study **memory and battery architecture as seriously as compatibility**. For every firmware/stack, measure or determine linked-flash cost, static RAM, BLE initialization delta, idle-connected heap and largest free block, scan/pair/reconnect peaks, per-event allocation behavior, fragmentation over repeated connect/disconnect cycles, task/stack sizes, whether buffers/pools remain resident, coexistence with Wi-Fi, and whether BLE can be fully or partially released when disabled. Compare BLE-only host-stack choices and compile-time feature trimming rather than assuming the largest/default stack is appropriate. Investigate bounded/on-demand allocation and reusable pools only where they improve Nooir's actual measurements.

Battery/power study must include radio duty cycle, scanning policy and timeout, connection interval/latency/supervision choices where exposed, always-on vs reader-only/on-demand BLE, reconnect/backoff policy, CPU frequency/wake-lock behavior, light/deep-sleep interaction, whether a bonded remote can reconnect after device sleep/wake and remote power cycling, and the battery cost of leaving BLE enabled during a normal reading session. Avoid continuous aggressive scanning or needless CPU wakeups. Measure physical X4 battery/current behavior where practical rather than inferring efficiency from code alone.

Prototype BLE only in an isolated experimental profile/branch after Phase A establishes comfortable headroom. Compare at minimum: no-BLE baseline, BLE compiled but disabled, enabled/disconnected, scanning, connected-idle, active remote input, repeated reconnect, reader page turns, dictionary/menu use, EPUB/CBZ/XTC activity, Wi-Fi interaction, and sleep/wake. Validate button responsiveness and lost/duplicate/repeated events. X3 remains the shared resource budget; physical X3 support is not claimed without hardware evidence. The eventual goal is a small, robust **Bluetooth Remote Input** subsystem that understands the remote faithfully, maps inputs flexibly to existing Nooir actions, reconnects reliably, and has measured/acceptable flash, memory, and battery cost.

### 9. Quick Actions / UI Dark Mode

Quick Actions remain a good isolated candidate after flash/memory work: preserve the audited CrossInk model of one configurable trigger, five persisted actions, shared popup, context filtering, and existing Nooir dispatch. Full UI/System Dark Mode remains a larger separate project; Reader Dark Mode already exists.

### 10. 1.6.3 sequencing and evidence rules

Recommended order: **flash map/recovery -> EPUB/font/image/memory upstream delta -> SD/SPI benchmark -> small safe fixes -> Quick Actions if headroom allows -> FB2 prototype -> OPDS -> PDF experiment -> Bluetooth experiment**. This is planning, not authorization to implement all of it in 1.6.3. Every upstream adaptation must record source commit/PR, why Nooir needs it, whether Nooir already has an equivalent, measured flash impact, RAM/largest-block effect where relevant, regression risk, and affected X3/X4/shared tests. Never trade recovery safety, cache compatibility, Arabic/Quran behavior, or physical X4 stability merely to match upstream.

## Immediate next steps

1. Begin Phase D with a short source re-check focused on malformed/problematic EPUBs, huge or hostile CSS/value cases, huge paragraphs, image-heavy behavior, tables/layout edges, low-memory failure behavior, and graceful degradation.
2. Use the failure ladder: full supported styling -> reduced safe styling -> default styling -> readable text. C5 inheritance and C6-style hardening remain evidence-driven, not automatic.
3. Preserve the Phase A/B/C freeze boundaries, the partition, cache formats, Arabic/Quran behavior, KOSync interoperability, user SD data and the exact FreeInk pin.
4. Keep future changes scoped and measured. Every approved normal firmware change must report linked flash, padded `firmware.bin`, app-slot margin, static RAM, and relevant X3/X4 evidence against the frozen baseline.
5. Continue physical X3 regression validation when hardware is available; X4 physical validation and X3/X4 simulators do not substitute for X3 hardware evidence.

## Resource map

- Repository: <https://github.com/toshio2011/folio-nooir>
- Authoritative development branch: `codex/folio-nooir`
- 1.6.1 release tag commit: `2c817a73f1a1143d7f62ac1e768501280abacaa3`
- Known-good firmware/source milestone: `85dda52a`
- Remote Quran fixture commit: `e0478714`; synchronization merge:
  `5efe0695`
- Released 1.6.2 tag: `1.6.2` -> `25df494020874150a047e2a4b3be62c3e40151e8`
- 1.6.2 firmware/source milestone: `85dda52a102163b40fd4a2ddfde65e6cdc23af36`
- Previous 1.6.0 safety checkpoint: `safety/1.6.0-carousel-layouts-hq` at
  `9c8e9751`
- WSL simulator mirror: `/home/fatiha/side-wsl`
- WSL simulator branch: `safety/wsl-cbz-before-carousel-merge-20260824`
- WSL synchronization backups: `/home/fatiha/nooir-sim-sync-backup-20260917`
- Simulator instructions: `docs/simulator.md`
- Cache/format reference: `docs/file-formats.md`
- Nested SDK: `freeink-sdk/`
- SDK fork target: <https://github.com/toshio2011/freeink-sdk>
- SDK official upstream: <https://github.com/Free-Ink/freeink-sdk>
- CrossPoint reference: <https://github.com/crosspoint-reader/crosspoint-reader>
- CrossInk reference: <https://github.com/uxjulia/CrossInk>
- CrossLink reference: <https://github.com/DaisonChun/crosslink>
- vCodex/Codex reference: <https://github.com/marcoand75/cpr-vcodex-steroids>
- Flowe OS reference: <https://github.com/andrewjiang/flowe-OS>
- Xteink X3 source/releases:
  <https://gitee.com/daixinchun/xteink-x3/releases#release-v20260617>

Local EPUB/CBZ test files are external fixtures, not project resources; do
not stage or copy them into the repository.

## Working-tree rules

- Preserve the committed `freeink-sdk` pin at the verified fork commit.
- Keep `origin` pointed at the user fork and `upstream` pointed at official
  FreeInk. Never push Nooir work to `Free-Ink/freeink-sdk`.
- Keep the parent submodule at one exact tested SDK commit; do not use
  `git pull` blindly inside the submodule.
- Leave `.codex-*`, `_epub-inspect*`, `codex-work-monitor/`, logs, probes,
  caches, binaries, and build output untouched and unstaged.
- Preserve the released 1.6.1 Arabic/EPUB fixes and keep future diagnostics,
  probes, logs, caches, and arbitrary build outputs out of release commits.
- WSL-only Carousel work is outside the authoritative Windows milestone and
  must not be overwritten or described as merged unless explicitly integrated.
- Treat the existing 1.6.0 CBZ reader as baseline functionality. CBZ/Manga
  preparation and cache work is deferred until after the EPUB phase and its
  separate planning and architecture audit.
- Use explicit Git staging; never use `git add -A`, reset, checkout, or
  force-push for this handoff. Keep documentation updates separate from
  source changes when preparing the next checkpoint.

## Deferred Nooir UI refresh — design decisions captured 2026-09-19

This is a **post-Phase-A / later implementation phase**, not authorization to start the large UI rewrite now. Finish the original 1.6.3 flash/headroom, memory, regression, and physical-X4 work first. Recovered headroom is a safety margin first and a feature budget second. X3 remains the resource budget for shared UI.

### Non-negotiable preservation contract

The UI refresh is a **presentation project, not a functionality-reduction project**. Preserve all existing information, actions, button mappings, short/long presses, touch gestures, tabs, popups, paging/scrolling, conditional states, status indicators, Back behavior, selection wrapping, orientation behavior, and dynamic button hints unless a separate change is explicitly approved. Before changing or mocking any screen, inspect the actual current Nooir activity/source and make a behavior/information inventory. Existing source behavior wins over generated mockups.

Generated mockups from the 2026-09-19 exploration are visual references only. They repeatedly invented controls, labels, rows, summaries, and footer actions. **Do not implement invented mock content.** In particular, utility screens must use their real current rows/states and real mapped footer labels.

### Visual direction

Keep Nooir's existing identity: monochrome/e-ink geometry, thin rules and dividers, strong typography hierarchy, restrained spacing, simple line/rectangle icons, soft selected rows, compact status/value presentation, and existing cover cache. Prefer reusable primitives over bitmap assets. Avoid texture packs, custom bitmap icon packs, animations, extra full-screen framebuffers, or redraw-heavy effects. Use chevrons only for genuine submenu/detail/selector behavior; booleans may use compact toggle/check visuals; direct/cycled values remain right-aligned without implying a submenu.

Create/reuse a small shared Nooir presentation layer where it actually reduces duplication (header, section title, row, selected row, divider, popup, tab, progress, metadata, button hints). Every visual addition must be measured for linked flash, RAM/largest free block where relevant, redraw cost, button responsiveness, and X3/X4 parity.

### Screens already Nooir — protect their composition

- Library, Recent, and Finished remain recognizably as they are. Keep Featured Book, the middle book-display section, compact statistics, tabs/navigation/actions/info, and existing behavior.
- Existing middle layouts remain. The requested Spine addition means a **2-row Spine View** in the middle book section, not a replacement shelf/theme. The current Spine implementation must be traced/extended because the audited planner/rendering appears to provide one shelf row; do not claim two rows until implemented and tested.
- Existing Statistics information architecture remains: Overview, Calendar, Books, Achievements, with the current selection/navigation/touch/button behavior. Polish it; do not replace it with a new dashboard.
- Existing To-Do information architecture and actions remain. No dates, categories, notes, subtasks, Today/Upcoming tabs, or due dates were approved.

### Special UI treatments approved for later implementation

- **Reader Menu:** floating Nooir panel over the visible book page, retaining every current action and its conditional visibility. Same brain, new clothes. No action may disappear merely because a mock moved it.
- **Reader Settings/Text Settings:** may share the reader-overlay family; retain the real tabs (Font, Size, Layout, Style, Dict., Controls), live book preview where technically safe, and current behavior.
- **Dictionary result:** floating panel over the book where feasible. Show real dictionary/source information above the headword, real plain-text StarDict definition, source/page status, and the activity's real dynamic four-button hints. Do not invent pronunciation/audio/synonym/source tabs.
- **Book Info:** evolve Synopsis into Book Info while preserving full synopsis paging/navigation. Approved additions are cover/title/author, useful real metadata, real progress/statistics where available, and editable star rating. Prefer Nooir-owned rating metadata rather than modifying the EPUB.
- **Statistics:** retain the exact existing four-tab structure and data; apply Nooir polish only.
- **To-Do:** retain the exact current task model and eight-option action popup; apply Nooir polish only.
- **Main Settings:** retain exactly the four persistent categories Display / Reader / Controls / System and their real conditional rows/actions. Preserve tab-level/list-level Back and Confirm semantics. Footer labels are dynamic and must come from the existing mapped-input behavior, not a hard-coded mock.
- **Reading Stats Sleep / Minimal Stats Sleep and To-Do sleep presentation:** include existing sleep-screen variants in the visual refresh, but inspect the exact current renderer first. Preserve existing content/functionality. Custom sleep images remain custom.

### Shared utility treatment

Wi-Fi/network, KOReader Sync, OPDS, OTA/update, Font Manager, keyboard, shared popups/dialogs, file browser, and other utilities do **not** need bespoke redesigns. Give them the shared Nooir visual language while retaining each activity's existing state machine, content, actions, and mapped button hints.

Source-audited examples that must be preserved:

- **Wi-Fi:** scanning/auto-connect, saved-network ordering, network list, hidden network entry, password keyboard, connecting, save-password prompt, forget-network prompt, failure handling, retry/rescan, signal/encryption/saved indicators. Network-list mapped hints are Back / Connect / conditional Forget / Retry; other states use their own existing mappings.
- **KOReader Sync settings:** exactly Username, Password, Sync Server URL, Sync Device Name, Document Matching, Send Metadata, Sync Behavior, Sign Up, Authenticate. Preserve the real right-side values/status and Back / Select / Up / Down mapping. Do not add Enable Sync, Sync Now, Last Sync, or Sync Help from mockups.
- **OPDS server settings/list:** preserve saved server rows plus Add Server, Download Folder, Filename Format; editor fields are Name, URL, Username, Password, with Delete for an existing server. Preserve browser/search/download/error/loading flows. Do not add invented catalog-management menu rows from mockups.
- **OTA:** preserve the existing check / confirmation / Cancel-or-Update / progress / no-update / failure / completion / restart flow.
- **Font Manager:** preserve the existing online family manifest/list, Download All/Update All when applicable, per-family download/update, progress/error/completion, Wi-Fi handoff, and memory safeguards. Do not add an invented top-level font menu.

### Resource and implementation gates

Do not start the large UI implementation until Phase A proves comfortable production headroom. First recover/measure flash, then prototype one representative shared-style screen (Reader Menu is a good high-value candidate), compile A/B, and benchmark physical X4 plus X3/X4 simulators. Overlay designs must not assume an extra full-screen framebuffer; reuse the existing rendered page or another bounded strategy only after measurement. Preserve fast paths such as DictionaryWordSelect's lightweight highlight snapshot/FAST_REFRESH and OTA/network render throttling.

The goal is: **make Nooir look much more like Nooir while keeping it lightweight enough for X3 and at least as responsive as it is now.**

## BLE research dossier

The detailed Bluetooth Remote Input research, HID-capture architecture, firmware comparison notes, memory/power test matrices, evidence gaps, and future implementation sequence are preserved in [`docs/BLE_REMOTE_RESEARCH.md`](docs/BLE_REMOTE_RESEARCH.md). Treat that dossier as research input only; re-audit upstream refs before implementation because BLE stacks and firmware branches may change.

The BLE research is intentionally ecosystem-wide rather than limited to obvious page-turner forks. See [`docs/BLE_ECOSYSTEM_SURVEY.md`](docs/BLE_ECOSYSTEM_SURVEY.md) for the 47-entry catalog screen, additional BLE lineages, transferable memory/power/input patterns, and the expanded future audit queue.
