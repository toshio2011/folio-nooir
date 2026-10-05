# Folio Nooir Handoff

## Current development status — 2026-10-05

The active line is **Folio Nooir 1.6.3 RC2.3**, still in old-X4 physical
validation; it is not a final release. The normal `gh_release` build includes
BLE HID central support (Yiser and Generic HID), shared Toggle Bluetooth,
bonded reconnect, and the bounded no-device auto-off option. There is no
separate user-facing BLE firmware. The current cold-start admission remains
98,304 free bytes / 36,864 largest block. Reader rendering and Section
indexing stop BLE first; indexing finishes normally before BLE is reconsidered.
The physically proven Reader idle-cache rearm request is checkpointed locally
at `a174f2045c7b9eda557ef2cddfc1cc9af825b63f`.

Recent X4 evidence: ordinary Reader HID handoff/reconnect, Yiser Confirm,
Generic HID, sleep teardown, Reader exit cleanup, and full large-Section
completion have passed. A large uncached spine released its builder/parser
state on completion; the established disposable SD-font-cache release then
raised free heap to 98,040 bytes, 264 bytes below cold admission, so no further
cache purge or Section time-slicing is accepted without a measured safe
benefit. A separate one-shot Clock/Weather sync left only about 82–84 KB free
and a 29,684-byte largest block; this remains an unresolved BLE recovery
issue. Current source stops SNTP after its one-shot sync and emits bounded
`NETMEM` checkpoints; this source change still needs physical confirmation.
The latest clean `gh_release` build completed at 61,420/327,680 B RAM and
5,773,815/6,553,600 B flash (779,785 B margin). The 5,787,968-byte image at
`artifacts/firmware.bin` hashes to
`339A9C339E13D0962ED71DABDB885F66A715C4D37EE9AB81F683D521CA1BA00E` and
matches `.pio/build/gh_release/firmware.bin` byte-for-byte. Relative to the
diagnostic-build comparison point for the spine 31–33 logging pass, RAM is
unchanged, linked flash is down 1,188 bytes, and the padded image is down
1,184 bytes.

Treat dated RC1/RC2 planning text below as historical unless this section or a
newer dated checkpoint says otherwise. Preserve the untracked generated
`sdkconfig.gh_release` and the intentional FreeInk child checkout state.

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

## Folio Nooir 1.6.3 RC1 recovery status

**FULL RECOVERY COMPLETE**
**FINAL MICRO-AUDIT PASS**
**SOURCE FROZEN FOR PHYSICAL X4 VALIDATION**

The verified RC1 candidate is the recovery worktree at
`C:\Users\fatiha\Documents\Codex\folio-nooir-1.6.3-full-recovery`, branch
`recovery/folio-nooir-1.6.3-full-2`, recovered from baseline
`b846e1550d18fe4e48014fcfec54b35e9da1ab57`. The broad last-week integration
evidence remains preserved in the other recovery worktrees; none are to be
cleaned until physical validation is complete.

### Verified integrated feature matrix

- Baseline EPUB Phase A/B/C and Phase-D resilience.
- A2 SD-font ownership/lifecycle, UI fallback cache release, and RenderLock
  protections.
- B bounded queued EPUB page turns, reversal cancellation, stale-state clears,
  intermediate rendering, and final-quality queue recovery.
- Unlimited Recent and geometry-driven two-row Spine layout.
- Book Info/Synopsis metadata evolution supported by the recovered source.
- Web library pagination with legacy `/api/library` no-query compatibility.
- Interface Font with separate UI ownership and A2-safe lifecycle behavior.
- E1-E4 UI polish across BaseTheme, Lyra, Folio Nooir, and RoundedRaff.
- UI Scale picker: 80/90/100/110/120 percent.
- Quick Actions runtime plus four-slot configuration/persistence. Defaults are
  Bookmark, Dictionary, Dark mode, and Refresh.
- Status precision in Whole, One decimal, and Two decimal modes with shared
  progress sanitization; only exact finite completion may display 100 percent.
- Existing XTC/XTCH/TXT/CBZ, dictionaries/history, clipping, stats/calendar,
  web functionality, sleep, dark mode, OTA, KOReader Sync, themes,
  orientation, screenshots, and settings/persistence are preserved.

BLE is **EXPERIMENTAL ONLY**. Normal production firmware keeps BLE disabled;
the experimental profile remains separately gated. Diagnostic-only A2.1/A2.2,
A2.4 operation-scope experiments, and bounded SDMEM/EPDMEM/font lifecycle
instrumentation are not production features.

Focused validation is **40/40 PASS**: 37/37 existing recovery tests and 3/3
status progress tests. The aggregate host suite still has unrelated legacy
`NOT_BUILT` and CSS-stub infrastructure limitations; these are not RC1 repair
failures.

### Compatibility and frozen artifacts

- FreeInk: `958720659ea289ae325e83db20049d0ea844800d` (clean).
- `JD_FASTDECODE=0`.
- `SECTION_FILE_VERSION=41`; no persistence/schema bump.
- Production BLE OFF and production diagnostics OFF.

Production artifact `artifacts/firmware-163-full-final-production.bin`:

- 5,505,104 bytes; SHA-256
  `97E89AF912528F48EE118BD3957191FFE186619D86BDAD788504D3B32868AA4E`.
- Static RAM 53,676/327,680; linked flash 5,491,251/6,553,600; margin
  1,062,349 bytes.

Diagnostic artifact `artifacts/firmware-163-full-final-diag.bin`:

- 5,520,704 bytes; SHA-256
  `6D02ABDDE9FF4918BCBB44551B0C91D52EC6C522A07318C8BCE5C6E2C3F70857`.
- Static RAM 53,724/327,680; linked flash 5,506,855/6,553,600; margin
  1,046,745 bytes.
- This is the exact first physical old-X4 test target.

Older recovery binaries and hashes are **SUPERSEDED - DO NOT FLASH**. Do not
test an unhashed or rebuilt binary as RC1; a later rebuild is a different
artifact and requires a new hash and RC designation.

### Deferred and experimental boundaries

The following are not part of normal 1.6.3 RC1: production BLE, editable Book
Info rating, A2.1/A2.2 production diagnostics, A2.4 operation-scope diagnostics,
PDF, major CBZ redesign, X4 Pro support, broad FreeInk upgrades,
`JD_FASTDECODE=1`, redundant FreeInk exists/open optimization, SecureNet
timeout adaptation, X3 initial-sync optimization, time-left status, tap-to-hide
status, stable page-number redesign, and unrelated CrossInk features.

### Physical validation plan

The first target is the **old X4**, using only
`firmware-163-full-final-diag.bin` with the SHA-256 recorded above. Capture
cold-boot serial output and observe free heap, minimum heap, largest free block,
font memory, coverage, advance cache, kern/ligature state, UI fallback memory,
and reader-font retention.

Validate cold boot/Home, Library, normal and image-heavy EPUBs, rapid forward
and backward queueing, reversal, final-quality repaint, images/highlights/
progress, Quick Actions and persistence, status precision, UI Scale, Interface
Font, Unlimited Recent over ten books, two-row Spine, Book Info, E1-E4
appearance, web pagination over ten books, orientation, Home return, and
repeated Home -> Library -> Reader -> Home cycles. Do not perform this testing
as part of source preparation.

## RC2.3 resource-reclaim addendum — 2026-10-05

This addendum records the current working-tree work; it does not rewrite the
historical RC1 freeze above. BLE admission remains globally gated at
98,304 / 36,864. The new pressure paths do not start BLE directly and do not
run with Bluetooth disabled.

The physical X4 log shows three distinct boundaries:

- Ordinary Reader page idle continues to use the existing one-render Reader
  disposable-font cleanup. At the large spine-9 build, the Section completed
  naturally (`section_build_complete ... free=90612 largest=55284`), then the
  existing Reader cleanup raised free heap to 101,444 before normal BLE
  admission and reconnect. No repeated Section suspend/resume is introduced.
- A true stable chapter transition may make one additional BLE-only attempt
  to release the current-page `highlightMatches` vector capacity. It is
  derived from the serialized page and clipping records, and can be recomputed
  on a future render. The Section, current page, annotation records, and cache
  format remain intact. The attempt is gated on Bluetooth ON, memory still
  below normal admission after existing cleanup, quiescent rendering, and
  Wi-Fi off.
- After Reader destruction, RecentBooks first constructs and renders normally.
  Only at a stable idle loop, with Bluetooth ON and normal admission blocked,
  one bounded attempt releases reconstructible Carousel source handles/header
  objects/path capacity and then invokes the shared reconstructible-cache
  helper if still needed. The visible framebuffer, RecentBook model copy,
  global recent store, and SD thumbnail files are retained. Any measured
  improvement requests one ordinary lifecycle reevaluation; admission remains
  authoritative.

The attached X4 log repeatedly shows Reader destruction around 100,296–103,248
bytes free followed by RecentBooks around 93,544–96,692, while the largest
block remains unchanged within each run. It also shows the shared UI fallback
candidate returning zero in Reader, so its benefit in RecentBooks is not
assumed. Sleep-quiesce recovers about 9.1–9.7 KB in RecentBooks cases, but
ActivityManager logs this after it calls the outgoing activity's `onExit()`
and destroys that activity, before entering Sleep. That recovered RecentBooks
model memory is not safe to take while the shelf remains active. See the
physical X4 checklist below for separate ordinary-page, chapter-transition,
RecentBooks-idle, and sleep cases; record actual candidate bytes rather than
assuming either reclaim meets the BLE floor.

For this follow-up X4 run, explicitly record these separate cases:

1. With Bluetooth ON, turn one ordinary Reader page. Confirm the existing
   light cleanup runs after render, and that no chapter-only reclaim runs.
2. Turn across a real chapter boundary. Let Section indexing finish naturally;
   record `section_build_complete`, the normal Reader cleanup, any single
   `[CHMEM]` / `reader_page_highlight_matches` attempt, and the subsequent BLE
   admission result. Do not interrupt indexing to make BLE return sooner.
3. Exit Reader. Wait for RecentBooks' first full render and stable idle. Record
   `[MEMP] recentbooks_idle before`, `reclaim item=carousel_source_cache`,
   any shared `recentbooks_idle_ble` helper result, and one normal admission
   result. Verify there was no reclaim before the RecentBooks frame completed.
4. Repeat the RecentBooks and chapter cases with Bluetooth OFF; there must be
   no BLE-specific reclaim log or cache release.
5. Sleep from stable RecentBooks and compare `sleep_quiesce_begin` with
   `sleep_quiesce_complete`. Treat the recovery as outgoing-activity teardown,
   not as evidence that active RecentBooks may discard its book list.

Representative exact serial evidence (timestamps are device uptime):

```text
[48329] [INF] [MEM] reader_exit stage=activity_destroyed free=103248 max=45044
[48388] [INF] [BLEMEM] stage=before_cold_admission activity=RecentBooks free=96768 largest=45044 floors=98304/36864
[50571] [INF] [MEM] activity=RecentBooks free=96692 total=243252 minFree=28328 largest=45044 bleRunning=0 bleConnected=0
[60746] [INF] [MEM] activity=RecentBooks free=96692 total=243252 minFree=28328 largest=45044 bleRunning=0 bleConnected=0

[157129] [INF] [MEM] reader_exit stage=activity_destroyed free=100572 max=45044
[157188] [INF] [BLEMEM] stage=before_cold_admission activity=RecentBooks free=94080 largest=45044 floors=98304/36864
[168927] [INF] [MEM] activity=RecentBooks free=94004 total=243252 minFree=29240 largest=45044 bleRunning=0 bleConnected=0
[174072] [INF] [BLEMEM] stage=before_cold_admission activity=RecentBooks free=93544 largest=45044 floors=98304/36864

[630559] [INF] [MEM] reader_exit stage=activity_destroyed free=100296 max=49140
[630620] [INF] [BLEMEM] stage=before_cold_admission activity=RecentBooks free=93812 largest=49140 floors=98304/36864
[631446] [INF] [BLEMEM] stage=before_cold_admission activity=RecentBooks free=93736 largest=49140 floors=98304/36864
[634299] [INF] [MEM] activity=RecentBooks free=93736 total=243252 minFree=24356 largest=49140 bleRunning=0 bleConnected=0
```

The spine-10 case below is the observed chapter-complete-but-still-blocked
pattern; this pre-patch firmware did not yet emit `[CHMEM]`:

```text
[594199] [INF] [SCT] section_build_complete spine=10 pages=1 free=98380 largest=55284
[595610] [INF] [ERS] reader_quiescent reason=render_complete spine=10 page=0 free=94016 largest=55284
[595650] [INF] [MEM] reader_idle_cache_release spine=10 page=0 free=94016->97824 max=55284->55284
[595651] [INF] [MEMP] reclaim level=2 item=sd_ui_fallback_glyph_caches freed=0 largest_gain=0 free=97824 largest=55284
[595651] [INF] [MEMP] stop result=no_gain free=97824 largest=55284
[613596] [INF] [BLEMEM] stage=admission_deferred reason=resource activity=EpubReader sensitive=1 render=1 reader=1 free=98112 largest=55284
[614163] [INF] [SCT] section_deserialize_begin spine=11 pages=0 free=97864 largest=55284
```

This misses the 98,304-byte free floor by 480 bytes after the light cleanup;
largest-block fragmentation is not the blocker in this particular sample.
The new chapter candidate is a bounded attempt, not a guarantee that every
chapter will have enough clipping scratch to recover the deficit.

The large spine-9 build is the contrasting successful natural-completion
case:

```text
[33910] [INF] [SCT] section_build_complete spine=9 pages=39 free=90612 largest=55284
[33911] [INF] [SCT] section_transients_released spine=9 pages=39 free=90612 largest=55284
[33911] [INF] [ERS] reader_quiescent reason=section_complete spine=9 page=0 free=90612 largest=55284
[33991] [INF] [MEM] reader_idle_cache_release spine=9 page=0 free=90612->101444 max=55284->55284
[34062] [INF] [BLEMEM] stage=cold_admission result=allow activity=EpubReader free=101444 largest=55284
[36422] [INF] [BLEMEM] stage=connected_idle free=32448 largest=29684
```

The log contains the same zero-gain shared UI-fallback result on ordinary
Reader pages as well, for example:

```text
[116928] [INF] [MEMP] request reason=reader_ble_admission target=102400/40960 free=95692 largest=32756
[116928] [INF] [MEMP] reclaim level=2 item=sd_ui_fallback_glyph_caches freed=0 largest_gain=0 free=95692 largest=32756
[116928] [INF] [MEMP] stop result=no_gain free=95692 largest=32756
[380503] [INF] [MEMP] request reason=reader_ble_admission target=102400/40960 free=95692 largest=32756
[380503] [INF] [MEMP] reclaim level=2 item=sd_ui_fallback_glyph_caches freed=0 largest_gain=0 free=95692 largest=32756
[380503] [INF] [MEMP] stop result=no_gain free=95692 largest=32756
[414088] [INF] [MEMP] request reason=reader_ble_admission target=102400/40960 free=95692 largest=32756
[414088] [INF] [MEMP] reclaim level=2 item=sd_ui_fallback_glyph_caches freed=0 largest_gain=0 free=95692 largest=32756
[414088] [INF] [MEMP] stop result=no_gain free=95692 largest=32756
```

For RecentBooks sleep, the activity-destruction recovery is visible before
SleepActivity rendering; the subsequent overlay probe consumes temporary
memory and final cleanup returns nearly to the quiesce checkpoint:

```text
[195278] [INF] [SLEEP] sleep_quiesce_begin free=93544 largest=45044
[195280] [INF] [SLEEP] sleep_quiesce_complete section_stopped free=103116 largest=45044
[195892] [INF] [SLP] sleep_asset_candidate path=/sleep/20260427_205949_0000.png reason=overlay_pool free=101844 largest=45044
[199497] [INF] [SLP] sleep_final_cleanup free=103040 largest=45044
[199497] [INF] [SLEEP] sleep_screen_ready free=103040 largest=45044

[640857] [INF] [SLEEP] sleep_quiesce_begin free=93736 largest=49140
[640859] [INF] [SLEEP] sleep_quiesce_complete section_stopped free=102840 largest=53236
[645074] [INF] [SLP] sleep_final_cleanup free=102764 largest=53236
[645075] [INF] [SLEEP] sleep_screen_ready free=102764 largest=53236

[5226] [INF] [BLEMEM] stage=before_cold_admission activity=RecentBooks free=104880 largest=65524 floors=98304/36864
[5228] [INF] [BLEMEM] stage=cold_admission result=allow activity=RecentBooks free=104880 largest=65524
[BLEMEM] stage=pre_init free=104880 largest=65524
[BLEMEM] stage=connected_idle free=35452 largest=32756
[7315] [INF] [BLEMEM] stage=connected_idle free=35452 largest=32756
```

## Superseding X4 heap-shape follow-up — 2026-10-05

The later full serial capture supersedes the earlier RecentBooks-cache
recommendation above. Keep the active shelf model and do not retry that reclaim:
the measured source cache was empty and both cleanup candidates returned zero.

Reader chapter checkpoints after ordinary idle cleanup were:

```text
[111060] [INF] [BLEMEM] stage=before_cold_admission activity=EpubReader free=99420 largest=65524 floors=98304/36864
[146676] [INF] [BLEMEM] stage=before_cold_admission activity=EpubReader free=98432 largest=63476 floors=98304/36864
[166944] [INF] [BLEMEM] stage=before_cold_admission activity=EpubReader free=98336 largest=65524 floors=98304/36864
[187784] [INF] [SCT] section_build_complete spine=30 pages=5 free=98144 largest=36852
[189373] [INF] [ERS] reader_quiescent reason=render_complete spine=30 page=0 free=87872 largest=36852
[189420] [INF] [MEM] reader_idle_cache_release spine=30 page=0 free=87872->98192 max=36852->36852
[189421] [INF] [MEMP] reclaim level=2 item=sd_ui_fallback_glyph_caches freed=0 largest_gain=0 free=98192 largest=36852
[189421] [INF] [MEMP] transition fromSpine=29 toSpine=30 bleEnabled=1 free=98192 largest=36852
[189422] [INF] [MEMP] reclaim item=reader_page_highlight_matches released=0 capacity=0 freed=0 largest_gain=0 free=98192 largest=36852
```

For spine 30, the post-render cache release recovered 10,320 bytes of total
free heap, bringing it back to within 48 bytes of `section_build_complete`,
but did not change the 36,852-byte largest block. That largest block is
12 bytes below the unchanged 36,864 hard floor. The sharp largest-block drop
occurs during the Section build/finalization episode; the serial log does not
identify which allocation placement causes it. The preceding chapter
checkpoints show a smaller total-free drift (99,420 -> 98,432 -> 98,336 ->
98,192) separate from that largest-block collapse.

The cache error is also not a repeated retry of one bad file. Spines 28, 29,
and 30 each report `Parameters do not match mask=0x040`, then
`section_cache_invalidated ... bin_remains=0 part_remains=0`, followed by one
fresh `section_build_begin`. The mask is the hyphenation-enabled header
parameter. Invalidation occurs before page LUT/deserialized-page state is
allocated.

Reader exit and RecentBooks evidence:

```text
[222424] [INF] [MEM] reader_exit stage=activity_destroyed free=100232 max=36852
[222483] [INF] [BLEMEM] stage=before_cold_admission activity=RecentBooks free=93732 largest=36852 floors=98304/36864
[222539] [INF] [MEM] activity=RecentBooks free=93564 total=242820 minFree=6784 largest=36852 bleRunning=0 bleConnected=0
[223259] [INF] [BLEMEM] stage=before_cold_admission activity=RecentBooks free=93656 largest=36852 floors=98304/36864
[223261] [INF] [MEMP] recentbooks_idle before free=93656 largest=36852 sources=0 path_capacity=0
[223262] [INF] [MEMP] recentbooks_idle reclaim item=carousel_source_cache freed=0 largest_gain=0 free=93656 largest=36852
[223262] [INF] [MEMP] reclaim level=2 item=sd_ui_fallback_glyph_caches freed=0 largest_gain=0 free=93656 largest=36852
[248870] [INF] [SLEEP] sleep_quiesce_complete section_stopped free=102776 largest=36852
```

RecentBooks owns a value-copy of the persisted `RecentBook` records while it
is active; each record contains five strings. The new one-shot
`recentbooks_model` diagnostic reports vector storage and string capacities
when Bluetooth is on, but string capacities are not an exact heap-byte count
(small-string storage and allocator overhead vary). The carousel cache is a
separate six-entry render-source LRU; the failing capture had zero entries.
The sleep checkpoint follows outgoing activity destruction, so its +9,120
bytes do not prove an active-shelf reclaim is safe. After wake, a fresh heap
layout reaches 104,372 free / 65,524 largest.

The three repeated `mask=0x040` failures are each invalidated and rebuilt
once; no same-episode failed-cache retry loop is evidenced. `Section` destroys
its parser, LUT and build strings on successful finalization. Its
Epub-owned `CssParser` calls `clear()` on success, which removes rule nodes
but retains hash buckets; size and cross-spine effect are not yet known. New
bounded `[HEAPSHAPE]` lines report builder LUT/string capacities, CSS rule and
bucket counts, heap/largest block around builder destruction, Reader-owned
clipping/bookmark/footnote capacities at quiescence, and RecentBooks model
capacity. `BleInput::stop()` also records one post-`BleHid.end()` checkpoint
only when the host was actually running, allowing the next capture to compare
pre-stop and post-deinit heap shape. These are measurement only; no CSS or
Reader cache lifetime was changed.

The chapter `highlightMatches` candidate remains a conditional, recomputable
scratch release for a page that actually has clipping matches; it measured
capacity zero in this spine-30 failure and is not credited with helping this
case. The RecentBooks Carousel reclaim hook was removed because its source
cache and shared fallback reclaim both measured zero in the tested shelf
state. The cache itself and active RecentBooks data are unchanged.

The reported static-RAM change from 60,980 to 61,420 bytes (+440) cannot be
attributed exactly: the 60,980-byte build's matching map/ELF is unavailable.
The current map accounts for 61,417 bytes before final alignment. The removed
RecentBooks hook had only an activity-instance flag and no global/static
storage, so it cannot explain a 440-byte `.data`/`.bss` delta. Do not infer
the missing symbols from the similar ~432-byte runtime total-heap difference.

For the next X4 run, retain the existing admission floors and capture:

1. `[HEAPSHAPE] stage=section_finalize` on spines 29 and 30, comparing
   finalize-start, pre-CSS-clear, post-CSS-clear, and post-build-context free
   heap/largest block, LUT capacity, path-string capacity, CSS rules/buckets.
2. `[HEAPSHAPE] stage=reader_quiescent` at the stable page on both spines,
   including clipping/highlight/bookmark/footnote vector capacities.
3. `[HEAPSHAPE] stage=recentbooks_model` on return to the shelf, then compare
   with the normal periodic `activity=RecentBooks` readings after the first
   render. Confirm there is no `recentbooks_idle` reclaim attempt.
4. Compare each `[HEAPSHAPE] stage=ble_stop` before/after pair with the next
   `before_begin` checkpoint. The stop line is emitted only for a running host.
5. Compare `sleep_quiesce_begin`, activity destruction, and
   `sleep_quiesce_complete`; then compare the fresh-wake baseline.

Do not lower 98,304 / 36,864, alter NimBLE lifecycle, purge active shelf data,
or change successful CSS bucket retention until those measurements identify
an owner. BLE may remain off when admission fails; Nooir correctness and
reader responsiveness take priority.

## Superseding X4 run: spines 31–33 and compact heap-shape logging

The 2026-10-05 follow-up has a different failure shape from the earlier spine
30 run. All three chapters retained a large healthy block; spine 33 missed
only the 98,304-byte free floor by 60 bytes:

| Spine | Pages | Section post-context free / largest | Reader-idle free / largest |
|---|---:|---:|---:|
| 31 | 5 | 98,300 / 59,380 | 98,348 / 59,380 |
| 32 | 2 | 98,276 / 59,380 | 98,324 / 59,380 |
| 33 | 6 | 98,196 / 61,428 | 98,244 / 61,428 |

The post-context free differences are 24 bytes and 80 bytes. The largest
block actually rises by 2,048 bytes at spine 33, so this run does not support
a fragmentation-only explanation or a leak diagnosis. The old one-line
capacity diagnostics were cut off by adjacent serial output. They have been
split into short records: `section_core_a` (free values in order start,
pre-CSS, post-CSS, post-context), `section_core_b` (largest values in that
same order plus minimum), and `section_caps` (LUT bytes, path-string capacity,
CSS rules and bucket counts). Reader capacities are now split into
`reader_core`, `reader_caps_a`, and `reader_caps_b`.

Source ownership narrows the candidates. `Section::BuildContext` owns the
parser, LUT vector and build path strings and is destroyed by `build_.reset()`
on successful finalization. Each built `Page` is serialized from a temporary
`unique_ptr` and is not retained as an in-memory page list. The `Epub` object,
however, owns one `CssParser` across Section changes; successful Section
finalization calls `CssParser::clear()`, which clears rule nodes but keeps
unordered-map buckets for reuse. Its measured bucket counts were truncated in
this capture, so its contribution to the 24/80-byte differences is unknown.
Reader bookmark storage was consistently `0/16` entries / 960 bytes; the
tested highlight scratch capacity was zero. No monotonically growing Reader
container is proven by the available output. Treat the small differences as
chapter/allocator variation unless the compact follow-up measurements show
otherwise; do not release CSS buckets or lower admission floors on this
evidence.

The chapter-only `page_highlight_matches` pressure reclaim and its five
transition-latch fields have been removed. It measured capacity/release/free
gain zero in repeated cases, and keeping its high-water vector capacity is the
normal fast path. The actual highlight matcher/cache remains unchanged and
continues serving clipping rendering. The ordinary shared Reader pressure
reclaim and post-cleanup lifecycle reevaluation remain intact.

The new post-`BleHid.end()` samples show substantial recovery without proving
a NimBLE leak: e.g. connected idle `31,344 / 28,660` became
`96,744 / 36,852`; on later turns `28,596 / 25,588` became
`93,736 / 32,756`. After the Reader render completed, the largest block rose
to `65,524` (or `59,380`/`61,428` in the measured chapter cases). Source
explicitly cancels a connect, deletes the disconnected client, and calls
`NimBLEDevice::deinit(true)`. No Nooir client/worker is intentionally retained
after end; the remaining free-heap delta is not enough to identify a live
NimBLE allocation, and the later block coalescing occurs after render work.

RecentBooks reports 17 books, 2,516 bytes of vector payload and 3,434 summed
string-capacity characters, with free heap moving from 100,276 at Reader
destruction to 93,844 at model construction. This supports the active shelf
copy as the main cost, though string capacity is not exact allocated heap
(small-string storage and allocator overhead apply). No RecentBooks reclaim
was added. The same log separately shows Wi-Fi selection moving from
100,248 / 65,524 to `wifi_deinit_done` at 81,912 / 42,996 and shelf idle near
85–94 KB; this remains a separate Wi-Fi investigation and was not changed.

Next physical run: use the short Section/Reader records on spines 31–33 and
compare `css_buckets` and LUT/path capacities; retain the exact BLE-stop to
post-render sequence; keep Bluetooth ON for the memory samples. Do not flash
or treat this diagnostic build as a fix until those fields identify an owner.

## CSS capacity / same-Section page variation pass — 2026-10-05

The newer X4 evidence changes the immediate question. Cached spine 33 settles
around 99,500 / 65,524, while fresh spine 34 settles at 98,384 / 65,524 and
fresh spine 35 at 98,288 / 65,524. Fresh spines 34 and 35 report identical
Section capacities (`lut=32 path=209 css=71 buckets=142>142`), so the 96-byte
difference is not explained by those exposed capacities. In the same active
spine 13, pages 0–2 settled at 98,388 / 55,284 and page 3 at 98,080 / 55,284;
the unchanged largest block shows that the 308-byte change is page/runtime
state or allocator variation, not a largest-block collapse. No leak is proven.

Source tracing proves `CssParser::resolveStyle()` is called only from
`ChapterHtmlSlimParser` during Section parsing. A completed Section serializes
resolved styles into `Page`; page deserialization/rendering, clipping,
highlighting, images, footnotes, and settings have no CssParser call site.
Successful Section finalization clears the parser maps but intentionally keeps
their unordered-map buckets. The new `releaseRetainedStorageIfEmpty()` path
therefore releases only empty parser buckets and refuses to act while rules
remain. On the 32-bit ESP32 target, raw bucket-pointer capacity is
approximately 568 bytes for 142 total buckets or 776 bytes for 194 total
buckets, before allocator overhead; exact recovery must be measured on X4.

The release is pressure-only: Bluetooth must be enabled, the Reader must be
quiescent with no build/render/pending turn, normal BLE admission must still
fail after existing Reader/shared cleanup, and the current Section is allowed
one attempt. It does not start BLE directly; any improved heap requests the
existing lifecycle reevaluation. A per-spine latch prevents page-to-page CSS
free/rebuild churn. Compact `RIDLE` and `CSSMEM` records now separate SD-font
cleanup from CSS-capacity recovery.

The normal PlatformIO Python 3.12 runtime rebuilt `gh_release` successfully;
the preserved clean target was not run because the worktree safety guard
rejects cleaning. Result: 60,980/327,680 B RAM, 5,774,705/6,553,600 B flash,
778,895 B margin, 5,788,848-byte firmware, SHA-256
`B85FE478B0B0F0563AE33FE635A6FFD70F15E4955115050B344109B2F57D6C9B`.
The artifact and `.pio` image match. No floor, Wi-Fi, BLE lifecycle, or
persisted format change was made.

Next X4 run: compare `CSSMEM before/after` on a fresh Section with 142/194
buckets; compare `RIDLE after_sd_font_cache`, shared MEMP, and CSSMEM on spine
13 pages 2 and 3. Verify rendered styles and next-page behavior after a CSS
release, and confirm no repeated CSSMEM attempt within the same spine.

## Allocator boundary and final CSS evidence — 2026-10-05

The physical `maxAlloc=36,852` result is not a 12-byte fragmentation loss.
This `gh_release` sdkconfig enables ESP-IDF light heap poisoning. On the
32-bit target, each candidate allocation carries an 8-byte poison header
(`uint32_t` canary plus `size_t` requested size) and a 4-byte tail canary.
`heap_caps_get_largest_free_block()` reports the largest user payload after
subtracting those 12 bytes. Accordingly, a raw 36,864-byte free region is
reported as 36,852 bytes. The policy keeps its 36 KiB *logical region*
reserve and translates it to the configured API value at compile time:
36,852 with poisoning, 36,864 without. The separate cold-start total-free
floor remains 96 KiB (98,304 bytes). This is a measurement-semantics
correction, not a reduction in the logical region reserve.

The 36 KiB bound was not selected as a NimBLE single-allocation requirement.
The original lifecycle rationale was to retain contiguous capacity against
fragmentation and later Reader pressure; startup measurements also showed
large aggregate BLE residency. Current configuration evidence shows a
5,120-byte NimBLE host stack and fixed packet pools, with the largest obvious
pool allocation around 7.5 KiB payload before allocator bookkeeping. No
identified Nooir/NimBLE allocation requires one 36 KiB block. Exact runtime
largest-allocation demand was not measured; therefore retain the logical
36 KiB reserve and only account for the proven poisoning subtraction.

The completed CSS pressure path is now backed by physical results, replacing
the earlier estimate-only conclusion above. With an empty parser, releasing
retained buckets changed 194 buckets to the empty-map baseline and recovered
808 total-free bytes; the 142-bucket case recovered 616 bytes. Largest-block
readings did not improve. Later Section parsing correctly repopulated maps
(for example, 71 rules / 142 buckets), and rendered pages remained normal.
The latch is once per actual Section instance, reset only when a new Section
is constructed—not once per spine number—so revisiting the same spine after a
new Section can make one fresh attempt without page-to-page churn. This is a
small proven reclaim, not enough to explain or guarantee BLE admission by
itself. It stays Bluetooth-on, quiescent, and pressure-only.

A same-source, no-clean repeat build before this final code adjustment
reproduced 60,980 bytes of static RAM (`.dram0.data` 20,089 + `.noinit` 3 +
`.dram0.bss` 40,888) and 5,774,705 bytes of flash use. The older reported
61,420-byte build has no matching map/ELF/config snapshot in the preserved
worktrees, so its 440-byte difference cannot be attributed to symbols or
source. Reported total-heap values differ by 432 bytes (243,252 vs. 242,820),
with the remaining difference likewise unproven. Do not infer a leak or a
specific code owner from those numbers; keep the historical comparison open
until its exact build evidence is available. Wi-Fi deinit memory loss remains
a separate unresolved physical issue and was not changed here.

Final closure follow-up: the pressure cleanup now also returns immediately
while `WiFi.getMode() != WIFI_MODE_NULL`. This mirrors BLE's existing Wi-Fi
exclusion and prevents releasing Reader/font/CSS caches for a BLE start that
cannot currently be admitted; it does not start, stop, or otherwise alter
Wi-Fi. Two final no-source-change `gh_release` builds both reported 60,980 B
RAM, 5,774,729 B flash, and identical `.dram0.data/.noinit/.dram0.bss`
sizes. Their 5,788,880-byte images had different SHA-256 values:
`98167649F2DC0FC7E619612437254D3CE92BF3472B958008FA252945104D0F14` then
`A843B52D976B547FA8E60ECC5DC544F826F7A7725AFEDE3FEE6245ACE06E3FA8`.
The generated gzip headers are a supported source of this byte variation:
`scripts/build_html.py` calls `gzip.compress()` without a fixed `mtime`, and
the regenerated arrays are compiled into `CrossPointWebServer.cpp`. The first
binary was overwritten by the repeat, so the exact byte diff was not retained;
the hash variance is explained at source level but not byte-for-byte isolated.
The current `.pio` image and `artifacts/firmware.bin` are identical at the
second hash. The historical 61,420/242,820 build remains unattributable and
therefore the release gate remains open despite current-state RAM metric
reproducibility.
