# Folio Nooir Handoff

## Combined X4 stabilization pass — 2026-10-09

Implemented and build-checked in the shared worktree:

- ReadingStats transition releases the retained RecentBooks local copy and
  its transient cover/popup state, then restores the copy once after the child
  is destroyed. Persistent RecentBooks data and navigation state are kept.
- Settings BLE oscillation is fixed with explicit transition/return/stable
  state plus an epoch. The former render-pending predicate was the stop cause:
  BLE's connected notification queued a Settings redraw, which immediately
  reclassified idle Settings as sensitive. Successful admission now consumes
  that transition.
- Web is an exclusive-memory session. BLE remains suppressed until Web server,
  Wi-Fi, and request state are fully torn down; runtime caches are rebuilt only
  once on UI return. `/api/settings` streams through a reused fixed buffer and
  performs a Web-specific preflight guard.

The changes are event-driven: no polling retry loop, timer/cooldown, per-frame
cleanup, cached-page cleanup, or normal shelf rebuild was added. Physical X4
acceptance remains required, especially Settings idle stability, Stats return
topology/BLE admission, ten repeated Web Settings refreshes, Web exit/BLE
return, 10–20 cached Reader turns, RecentBooks scrolling, images, and sleep.

No flash, commit, or push was performed.

## X4 COM audit repair — 2026-10-09

The newest X4 log confirms the Reader next-chapter failure is heap topology,
not the missing bookmark bytes: after section cleanup it reaches about
`95.2/22.5 KB`, while the existing Reader-exit shared-cache release reaches
about `108.8/49.1 KB`. That path owns rebuildable global SD-font advance,
kern/ligature, mini-glyph, and overflow state; the existing Reader-exit order
also clears the built-in decompressor before this shared release. Live Section,
page, CSS, image/PXC, annotation, and EPUB ownership are not part of the
deep rescue. The new rescue is one-shot and gated by return-pending, stable
Reader quiescence, failed normal cleanup, and a cold/incomplete section or
heavy image/scratch event. It never runs on ordinary cached turns.

The log shows repeated image-miss stop/start returning to a stable connected
heap shape rather than progressively worsening, so no NimBLE cycle change was
made. Stable Settings gets one measured candidate only: built-in
`FontDecompressor` page/hot buffers after the BluetoothSettings return frame
is stable. Wi-Fi Settings remains deferred because the low-memory case has no
proven stale child owner.

Stats now releases `StatisticsSnapshot` before `Activity::onExit()`, then the
safe built-in font transient, and logs `after_snapshot_release`,
`after_stats_font_release`, `child_destroyed`, and
`parent_recentbooks_resumed`. The retained RecentBooks local book/string copy
is released only at the Stats child boundary and restored once on parent
resume. Bookmark reclaim and Reader-exit cleanup remain intact; ImageBlock,
PXC, Section/cache semantics, Web, XTC, renderer scratch policy, BLE-OFF, and
the `98,304/36,852` floors are unchanged. Build and physical validation are
still pending; no flash, commit, or push is authorized.

The final offline `gh_release` build succeeded: PlatformIO reports 61,396
bytes static RAM and 5,804,659 bytes linked flash. Physical X4 validation is
still required; the generated artifacts and hashes are reported in the final
handoff below.

## Focused BLE stabilization pass — 2026-10-09

Implemented scope:

- Settings now records the BluetoothSettings return, holds BLE through the parent
  render boundary, and lets the centralized lifecycle retry once the render lock
  is gone and the unchanged admission floors pass. A failed floor check remains
  deferred until a real lifecycle event.
- `bm` is `EpubReaderActivity::cachedBookmarks`, `std::vector<BookmarkEntry>`.
  Its empty load-time reserve is the six-times-observed 960-byte retained
  container. Only empty capacity is swapped out at the existing one-shot Reader
  BLE-return cleanup boundary; live bookmarks are untouched.
- RecentBooks-to-Reader ownership diagnostics cover the request, outgoing shelf
  release, Reader shell, EpubReader entry, and first-render boundaries. The only
  transition release is the departing activity's rebuildable `recentBooks` vector.

Expected physical outcomes: the clean EPUB case can recover about 960 bytes and
cross 98,304 free; the transition case may improve the 34,804-byte largest block.
If it does not, the fragmented case remains deferred and BLE is not forced on.

Protected boundaries: ImageBlock/PXC, Section build/suspend/cache behavior, Web
settings, eager chapter caching, 40-row scratch, Reader exit cleanup, XTC
activity-specific safety, BLE floors, and BLE-OFF behavior. No flash, commit, push,
or dependency update is part of this handoff.

## Uncached section post-persist release — 2026-10-08

Policy: **After uncached section persistence, release builder-only state before
BLE return admission; cached page behavior must remain unchanged.** The
committed section swap is followed by destruction of the section-local parser,
Expat state, page LUT, build strings, and temporary HTML ownership. Because
serialized pages retain resolved layout, the now-empty shared CSS parser may
also release only its retained hash-bucket capacity at this boundary. No live
CSS rules, current page/Section, image pipeline, shared font cache, or reusable
40-row scratch is purged. New concise diagnostics are
`section_post_persist_before_release`, `section_post_persist_after_release`,
and the corresponding `HEAPSHAPE` line. BLE floors remain 98,304 / 36,852 and
the existing one-shot return-pending flow is unchanged.

## BLE return polish — 2026-10-08

This pass is BLE-only. The canonical 1.6.3 ImageBlock/PXC pipeline is
protected and no image source extraction, decode, draw, geometry, or cache
semantics were changed.

Reader resource stops now arm an explicit return-pending state only when BLE
was enabled and running/connected. After a stable render and the existing
normal idle disposable-font cleanup, a failed admission may run exactly once
for that BLE-return event. The audited return items are the UI-fallback font
glyph arenas and empty CSS parser bucket capacity. The current page, active
Section/build, EPUB handle/source, current-page annotation state, image replay
state, and reusable 40-row scratch are not released. A total-free-heap failure
and a largest-block fragmentation failure are logged separately; no per-page
reclaim, polling, timer, delay loop, or repeated retry is used. A failed return
stays pending quietly until a fresh BLE lifecycle event creates a new return
cycle; scratch recovery is intentionally rejected because lazy recreation
would make connected cached turns allocate again.

Successful reconnect, BLE disable, Reader exit, Wi-Fi/Sleep/OTA boundaries
clear the return state. BLE-OFF remains inert, the existing 98,304 / 36,852
floors remain unchanged, and known Reader-exit recovery stays intact. Physical
validation must confirm return without leaving Reader, the just-under-floor
case, fragmented largest-block case without churn, 10+ turns after reconnect,
Roosevelt image stability, BLE-OFF behavior, Reader exit, Sleep, and Web as
separate paths.

## Regression restoration pass — 2026-10-08

Roosevelt images rendered in the pre-BLE version. This pass restores the
canonical 1.6.3-compatible ImageBlock/PXC source-to-decode-to-draw-to-cache
flow and removes the later strict geometry/length validation, temp promotion,
and replay-control changes that could turn a formerly valid cache into a
placeholder. Figure/full-page/CSS code remains layout-only; it supplies the
destination rectangle and page/caption semantics to the original image path.

The FreeInk host's synchronous connection claim remains the ownership boundary;
callers now distinguish a real start from joining an in-flight request. The
BluetoothSettings render gate records one deferred admission and waits for the
render-complete lifecycle signal instead of emitting an 11 ms loop. The
on-demand glyph experiment is reverted to the previous normal font prewarm.
Reader-exit recovery, shared font/cache/scratch cleanup, 40-row scratch,
BLE-OFF inertness, unchanged 98,304 / 36,852 floors, and Web settings work are
preserved. No flash, commit, or push is part of this pass; physical Roosevelt
and BLE checks remain required.

## Stabilization correction — 2026-10-08

The previous physical candidate is not a baseline to preserve for BLE Reader
behavior: its connected cached-render reclaim repeatedly reported zero freed
bytes and then deferred BLE stops. Remove only that pre-render experiment and
retain the earlier cached-page logic, 40-row scratch, Reader-exit hard recovery,
sleep cleanup, event-driven re-admission, and unchanged BLE floors.

The same physical run found that enabling BLE from on-device BluetoothSettings
did not connect until Home because the stacked parent Settings activity blocked
admission. The child context is now treated as lightweight after its render
quiesces, without weakening a Reader parent. Manual connect paths also avoid
duplicate attempts while the host is already connecting.

Roosevelt `img_9_13` / `img_9_14` remain placeholders; no `EPUBIMG`, PXC, or
source-recovery line appeared, so the prior diagnosis is not accepted as proof.
Parser exits, layout placeholders, and ImageBlock failure reasons are now
logged at the top level. Web Transfer/browser Settings remains a P0 hard reset;
repeated missing `thumb_226.bmp` probes are covered on the actual library and
cover routes by a bounded negative cache, and settings/stats/library/thumbnail
routes now report begin/end heap and client state.

The current candidate ELF evidence is `MEPC 0x40389af6` / `RA 0x40389ae6`
inside `esp_cpu_wait_for_intr`, saved `0x4038c1e0` inside
`esp_system_reset_modules_on_exit`, and `MTVAL 0x4038e3b0` inside FreeRTOS
`prvIdleTask`. That is reset-path evidence, not proof that WebServer owns the
fault; a corrupting task/stack or earlier panic remains possible. One new
offline `gh_release` build is required after checks. No flash, commit, or push.

Read [`PROJECT_CONTEXT.md`](PROJECT_CONTEXT.md) for the complete project
history, decisions, and feature inventory.

## 1.6.4 release-candidate checkpoint — 2026-10-07

The active checkpoint is branch `reconstruct/folio-nooir-1.6.4`. It includes
the ownership-safe Transfer startup fix and retained P0 memory cleanup, the
normal-page BLE lifecycle correction, bounded P1 EPUB work, and only the empty
CSS span-spacing plus KOReader bounded-`getProgress()` P2 carryovers. The
authoritative detailed record is maintained in
[`PROJECT_CONTEXT.md`](PROJECT_CONTEXT.md#164-release-candidate-checkpoint--2026-10-07).

The latest RC build succeeded at 5,803,184 bytes (`firmware.bin`), with
5,789,035 / 6,553,600 bytes (88.3%) in the app slot and 60,996 / 327,680
bytes (18.6%) static RAM. Web UI physical validation passed. Final BLE
validation for this exact RC remains pending; release should follow quickly if
cached-page retention, heavy-section recovery, post-Transfer BLE return, and
largest-block behavior all pass.

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

## Folio Nooir 1.6.4 final stabilization handoff — 2026-10-08

Start from local branch `reconstruct/folio-nooir-1.6.4` at safe checkpoint
`d9af683b6f9758c479fefe43aaecc0460c0b5803`. Preserve current work and the
intentional untracked `sdkconfig.gh_release`; do not reset, flash, commit, or
push final-fix work automatically. Hosted durable notes are useful context,
but the local reconstructed checkout is the edit/build source of truth.

The unconditional BLE input handoff is fixed and cached-page coexistence is
physically working. Keep BLE connected for ordinary cached page turns. The
remaining scratch blocker was traced to the tiled grayscale fallback in
`src/activities/reader/EpubReaderActivity.cpp`: an 8,000-byte local allocation
was made per page/render, after other reader allocations, while NimBLE could be
resident. The chosen fix is a bounded reusable `GfxRenderer` scratch reserved
while Reader heavy setup has BLE stopped, reused for cached pages, released
before section/EPUB teardown, and backed by a narrow BLE-stop retry only when
the scratch cannot be guaranteed. This is not permission to add permanent
large page buffers or restore unconditional BLE teardown.

Keep the BLE admission floors exactly at 98,304 free heap and 36,852 largest
block. Total free heap recovery near 98–102 KiB does not prove a usable
largest block; logs as low as 34,804, 25,588, and 21 KiB are valid admission
failures. Failed admission must preserve wanted/pending intent and wait for a
real lifecycle event, not a timer. BLE OFF must have no NimBLE lifecycle,
admission floor, pending intent, render gate, teardown, memory reservation, or
observable effect anywhere in Nooir; runtime OFF must fully stop and release
BLE resources.

The Web Settings/Reading Stats “Wi-Fi disconnect” is a full reset. Preserve
all P0 fixes: ownership-safe file close, HalFile startup assert fix, streamed
stats, reduced Settings descriptor copying, active-only upload buffers,
library JSON reuse, thumbnail/client cleanup, WebSocket cleanup, complete
Transfer teardown, and BLE reevaluation. Preserve all P1 EPUB work and the two
approved P2 carryovers. The pre-final-checkpoint `gh_release` ELF matched the
supplied `b27d8dfff` SHA256 prefix; the rebuilt candidate is different and must
not be used to decode that older panic. Its supplied addresses resolve to
idle/wait framework symbols, not an application handler, so the panic root
cause remains unproven without a complete panic reason/backtrace. The current
small Web fixes harden synchronous WebSocket shutdown and cache missing-
thumbnail 404s without removing covers.

Before handoff, run `git diff --check`, lightweight source/static checks, and
one `pio run -e gh_release` only after the source is coherent. Record exact
BIN/ELF/MAP paths, sizes, SHA256 values, flash/app-slot and static-DRAM usage,
NimBLE/BleKeyboardHost link proof, warnings/errors, and final Git status. The
candidate is not release-ready until physical BLE-on cached pages, heavy
sections, pending re-admission, BLE-OFF workflows, Transfer, Web Settings,
Reading Stats, thumbnails, and a small upload pass without reset.

## Second-pass handoff — 2026-10-08

The current working tree remains authoritative and must be preserved. The
first RC's reusable 8,000-byte grayscale scratch removed the old scratch OOM
and improved largest-block recovery to about 57,332 bytes, but physical logs
showed idle Reader at about 97,756 free / 57,332 largest, 548 bytes below the
98,304 BLE total-free floor. The second candidate changes the existing tiled
loop from 80 to 40 rows (`displayWidthBytes * 40`, about 4 KiB on X4), keeps
+the same strip rendering/AA path, and still releases the renderer scratch
before Reader teardown.

The BLE floors remain exactly 98,304 free and 36,852 largest. Cached-page BLE
retention, pending/re-admission semantics, BLE-OFF inertness, WebSocket
teardown hardening, negative thumbnail caching, and P0/P1/P2 work remain
preserved. No unconditional per-page BLE stop was restored.

The source audit of the extra `connect_start` found that the host's automatic
reconnect condition is `!connected_ && !g_connecting && !scanning_` plus the
permission callback; the application cold-start path also requires the host to
be stopped. No redundant request is proven from the partial physical log, so
no speculative BLE fix was added. Sleep overlay PNG behavior is unchanged;
the allocation-failure path now logs `sizeof(PNG)`, row-buffer size, and
default/internal/DMA capability free and largest blocks. PNGdec's embedded
zlib/inflate/scanline storage makes a large contiguous decoder request the
leading source-based explanation, but physical capability values are still
needed before changing image support.

Run `git diff --check`, lightweight source/static checks, and exactly one
`gh_release` build after source review. Do not flash, commit, or push. The
candidate remains gated on physical Reader/BLE, BLE-OFF, duplicate-connect,
sleep-PNG, Web UI, Transfer, thumbnails, and upload validation.

## Third-pass handoff — 2026-10-08

The 40-row reusable grayscale strip is physically effective: the old scratch
OOM is gone and BLE can reconnect inside Reader after the initial cleanup.
However, repeated BLE-connected page turns progressively fragment the heap.
After BLE stop and idle cleanup, Reader exit still leaves the largest block at
approximately 30,708 bytes through section release, EPUB release, disposable
font release, and activity destruction. RecentBooks then lands near 90–94 KiB
free / 30,708 largest, while clean Home/Recent is near 103 KiB / 65.5 KiB.

The 30,708-byte ceiling is the primary bug. The sleep-PNG request of 59,456
bytes is now considered a consequence of that bad heap shape. Source tracing
found that the global SdCardFontSystem retains Reader-built advance tables and
lazy kern/ligature data after disposable font cleanup; those caches can be
created while BLE is resident and survive both bleinput::stop() and Reader
destruction. The static ImageBlock cache and renderer state remain suspects,
but are explicitly released at the boundary and are not yet proven to pin the
ceiling.

This pass adds a deliberate Reader recovery boundary: section and EPUB,
disposable fonts, renderer/ImageBlock scratch, then Reader-built shared font
caches are released before activity destruction settles RecentBooks. It also
adds low-volume heap-shape checkpoints around cached BLE render, BLE stop,
idle cleanup, exit stages, shared-cache release, activity destruction, and
RecentBooks activation. NimBLE teardown is left unchanged because its source
already deletes the client and calls deinit(true); physical comparison logs
are still required to rule in or out any residual allocation.

The BLE admission floors remain 98,304 free and 36,852 largest. Cached-page
retention, event-driven pending admission, BLE-OFF inertness, WebSocket and
thumbnail hardening, CSS cache v14, KOReader bounded response, and existing
P0/P1/P2 work remain preserved. Do not flash, commit, or push. Release status
stays gated until physical Reader-exit recovery, sleep PNG recovery, BLE-OFF,
and the remaining regression checklist pass.

## Fourth-pass handoff — 2026-10-08

This pass addresses the two reported physical bugs while preserving the accepted
Reader-exit hard boundary, 40-row renderer scratch, BLE-OFF inertness, sleep/Web
hardening, and P0/P1/P2 work.

- BLE-connected cached/light Reader renders now use a local headroom policy. It
  does nothing at healthy headroom; under pressure it releases only residual
  ImageBlock replay memory, previous-page footnote/highlight capacity, and
  rebuildable SD-font glyph arenas. Section/layout, EPUB, CSS, current page,
  stable-page state, and essential font metadata are not discarded.
- If reclaim is insufficient, BLE is stopped only through the existing
  event-driven lifecycle and the same render is replayed after quiescence. BLE
  OFF invokes none of the new thresholds/reclaim/gating path. Cold BLE floors
  remain exactly 98,304 free / 36,852 largest.
- Roosevelt invalid `.pxc` files are now closed, removed, and diagnosed with
  path/reason/dimensions. The original image source is resolved or lazy-
  extracted, decoded immediately, and written through a temporary PXC followed
  by promotion and validation. PXC v1 has no version byte; geometry and exact
  payload length are the compatibility checks. Raw image size is not proven as
  the cause.
- Added concise `EPUBIMG` diagnostics for cache invalid/removal, source
  extraction/resolution, decode begin/failure/success, cache write, and source
  render success. Added host-testable BLE pressure and PXC format policy cases.

The candidate remains not release-ready until physical validation confirms
smooth BLE-on cached turns, last-resort fallback behavior, BLE-OFF unchanged
behavior, Roosevelt `img_9_13` / `img_9_14` source recovery, Reader exit
re-admission, and the existing Web/Transfer/sleep checklist. No flash, commit,
or push is authorized by this handoff.

## Targeted stabilization continuation — 2026-10-08

The latest physical candidate was `firmware.bin` SHA256
`DB5CC1B2ED39022757CA5A524B703E90F4D9A167CA65353B00E3D826BD148F4E` with
ELF SHA256 `291E92734A8C2E286E73242397397425C6A6DB5D53EED6E3D1D7F7EB32117AC7`.
It proved that BLE-connected cached Reader renders can reach historical
minimum free heap of 1,128 bytes and later 704 bytes, while idle heap recovers
to about 29 KiB free / 26 KiB largest. This is transient render pressure, not
evidence of a page-by-page leak. BLE direct connection inside BluetoothSettings
passes, but two `connect_start` records remain to be eliminated. Reader-exit
hard recovery, shared font-cache release, renderer scratch release, 40-row
scratch, BLE floors, BLE-OFF inertness, sleep cleanup, P0/P1/P2 work, and the
24-entry/five-minute thumbnail negative cache remain preservation boundaries.

The targeted source continuation removes the BLE-connected cached-page font
scan/prewarm transient and keeps on-demand glyph loading; BLE-OFF retains the
original scan/prewarm path. It adds stage heap-shape evidence around font,
BW, display, grayscale, and cleanup phases. BLE connection ownership is now
claimed under the host mux before worker notification, with a single
`connect_join_inflight` diagnostic for losing callers. PXC replay now reports
open/header/exact-size validation, replay method, clip, actual framebuffer
writes, and fallback/placeholder transitions; cache geometry is exact rather
than ±1 tolerant. `/api/settings` now uses a bounded direct JSON stream with
route stages, heap/largest/minimum heap, stack watermark, and client-close
diagnostics instead of a dynamic document plus serialization buffer.

The Roosevelt decision point remains generic (`OEBPS/image_rsrc587.jpg` with
cache present and 464x287 geometry); no path is hardcoded. The exact cache
replay/draw result still requires hardware evidence. Build success is not
release proof. This continuation must have exactly one offline `gh_release`
build after source checks, with no flash, commit, or push.

## Reader font / Web-exclusive follow-up — 2026-10-09

The focused source continuation sizes `SdCardFont`'s per-paragraph advance
workspace to the actual UTF-8 byte upper bound and suppresses repeated
same-or-larger allocation failures until heap topology improves or a genuine
font-cache release occurs. Cached Reader turns keep the existing warm mini
arenas; no per-page purge was added.

Web exit now logs memory deltas for the HTTP/request-handler/WebDAV object,
WebSocket clients, discovery UDP, upload/global state, captive DNS, mDNS, Wi-Fi
scan storage, radio deinit, and activity stack. The existing Arduino
`WIFI_MODE_NULL` path already calls `esp_wifi_stop()` and
`esp_wifi_deinit()`. ActivityManager prepares Web exit before constructing a
replacement RecentBooks/Home activity and only clears Web-exclusive BLE gating
after the replacement enters. `GET /api/settings` avoids copied dynamic
setting descriptors and retains bounded direct streaming; the guard is now
20 KiB free / 8 KiB largest.

The parallel working-tree BLE HID navigation changes in
`src/MappedInputManager.cpp` and `src/components/OptionPopup.h` were inspected
and preserved. They are included in the same `gh_release` build; no input-path
changes were needed in this follow-up. Source/build checks do not replace
physical validation: late-section font topology, ten repeated Settings GETs,
owner-by-owner Web exit recovery, cached-turn/shelf snappiness, and BLE return
remain required before release. Do not flash, commit, or push from this pass.

The final Web-exclusive audit also gates ActivityManager's recurring optional
SD interface-font loader while the Web root or one of its children is active,
so Web entry's released UI fallback caches stay released for the session. The
loader resumes after Web teardown and replacement UI entry. Settings POST now
applies base descriptors by reference and handles the dynamic sleep/font keys
directly, avoiding per-save dynamic descriptor copies.

## Physical stabilization follow-up — 2026-10-09

Stats and Calendar return now restore the parent RecentBooks copy from the
already-loaded RecentBooksStore, restore/clamp shelf selection, clear stale
carousel source state, and force a full shelf redraw. The hang was caused by
restoration re-entering `RenderLock` while ActivityManager still held it; Stats
also invalidated the framebuffer while leaving the snapshot fast-path enabled.
BLE return is gated until the resumed `displayBuffer()` finishes. No metadata
rescan or additional Stats cleanup was added.

`/api/settings` emits one fixed 256-byte chunk at a time through synchronous
`NetworkClient::write()`. It now observes short writes and closes rather than
continuing to generate output into a congested transport; the descriptor walk
does not create a response object per setting. The existing 20 KiB / 8 KiB
guard stays unchanged. `SO_SNDBUF` is explicitly unimplemented by this lwIP
build and queued-byte depth is not exposed by the public client API, so those
metrics are logged as unavailable.

Web exit closes the active HTTP client, destroys server handlers/clients and
the activity object, stops Wi-Fi, and checks that `WIFI_STA_DEF` and
`WIFI_AP_DEF` netifs are gone before RecentBooks construction. The global
TCP/IP core is process-lifetime; no unsupported netif/TCP-core deinit is
attempted. The reported 25–26 KiB post-Web deficit is not proven resolved by
source inspection and remains a physical acceptance risk; check for TIME_WAIT
or other lower-layer socket state using the new owner checkpoints.

SDCF mini capacities persist across normal render scopes, mini-kern row scratch
is stack-bounded, failed kern/interval/advance allocations remain quiet until
a new generation/topology event, and replacement allocation failure preserves
the previous arena. Reader exit detaches only the selected Reader-size font's
coverage owner (unless it is itself a required 8/10/12-point UI fallback); the
separate RecentBooks UI fallback owners are retained. The next Reader entry
reloads the Reader-size font once. No page-turn cleanup/retry loop was added.

The eight-page builder pause is consistent with `BUILD_WINDOW_AHEAD=5`: a
partial/fresh section may wait for reader progress by design. New `SCT
build_wait` diagnostics will distinguish that from heap-gated waiting; do not
change suspend/persist semantics without new evidence. The already-fixed
input-sized advance-table workspace remains unchanged. No ImageBlock/PXC,
section cache semantics, XTC, BLE floors, BLE-OFF behavior, or canonical BLE
navigation edits were changed.

Before handoff, verify BLE navigation edits remain in
`src/MappedInputManager.cpp` and `src/components/OptionPopup.h`, inspect the
full worktree diff, run `git diff --check`, avoid any test path that downloads
GoogleTest, and perform a successful offline `gh_release` build. The final
build succeeded: RAM 61,396 / 327,680 bytes (18.7%), flash 5,814,185 /
6,553,600 bytes (88.7%). An initial console-encoding-stalled attempt and a
compile attempt exposing a missing declaration include were corrected before
this successful build. Do not flash, commit, or push. Hardware acceptance is
still required for Stats/Calendar
interaction, Settings transport min-heap across ten reads, post-Web topology,
Reader cached turns/next-section progress/exit, BLE return, Roosevelt images,
and sleep PNG rendering.

## Paused continuation handoff — 2026-10-12

Base remains branch `reconstruct/folio-nooir-1.6.4` at safety checkpoint
`d9af683b6f9758c479fefe43aaecc0460c0b5803`. Continue from the dirty local
reconstructed checkout without discarding anything. No flash, stage, commit,
or push.

Clock policy is now always one bounded NTP attempt per station Wi-Fi session,
independent of the former opt-out. The callback only queues a session event;
the main task consumes it. Valid Weather local time plus `utc_offset_seconds`
sets the same shared UTC epoch. The visible clock toggle is retired, while its
persisted field and `/api/clock-weather` key remain for compatibility. Weather
auto-refresh remains separately optional. Sync does not power Wi-Fi on, does
not keep periodic SNTP alive, and does not write flash on clock ticks.

SleepActivity now performs logged, lock-protected release of rebuildable
renderer/SD-font caches, unreferenced Reader font ownership, the existing
ImageBlock render cache, and grayscale strip scratch before PNG decode. Reader
exit separately releases section/EPUB and Reader SDCF owners. The same 40-row
strip is reserved at Reader entry and consumed in the tiled render. Physical
heap-shape improvement remains unproven.

BLE admission floors are unchanged: 98,304 free and 36,852 largest. BLE-OFF
does not admit/readmit or reserve BLE-only Reader memory; defensive stop with
no running host is a no-op, including at sleep/Web boundaries. Retain the
cached-page fix and do not restore unconditional per-page BLE teardown.

The reported Settings/Stats “Wi-Fi drop” is a full-reset symptom. Local
`.pio/build/gh_release/firmware.elf` SHA256 begins `43e31f8`, not the panic
prefix `b27d8dfff`; it is the only ELF in the checkout, so the supplied panic
must remain unsymbolicated. No route-level Wi-Fi disconnect was found. Web
handlers run synchronously on the activity's task and their closures are owned
by the server; teardown destroys handlers/clients before Wi-Fi deinit. Keep
Stats streaming, cover functionality, and bounded missing-thumbnail caching.
The root cause of either fresh-boot or degraded-heap reset is not proven.

Next: finish read-only source review; run `git diff --check` and only offline
tests; then do exactly one `pio run -e gh_release`. Report artifact SHA/size,
app-slot and DRAM use, BLE host link proof, warnings, and final status. Do not
flash. Physical X4 acceptance (cached/heavy BLE, re-admission, BLE-OFF,
sleep-PNG, Web Settings/Stats/Transfer, covers, upload, and clean Web exit)
still gates release readiness.

### Completed source/build pass — 2026-10-12

The final `gh_release` build succeeded: RAM 61,428 / 327,680 (18.7%) and
app-slot use 5,820,533 / 6,553,600 (88.8%). BIN
`artifacts/firmware.bin`: 5,834,688 bytes, SHA256
`2A4C06489D1F8CC3782DC2F2FA3A706DB6EC0F56426D8DCD13271FF436370E78`.
ELF `.pio/build/gh_release/firmware.elf`: 81,625,368 bytes, SHA256
`259744C6D47D412E95BB208EEDC5DADFA7E599984944A739010BED1D63728F2C`.
MAP `.pio/build/gh_release/firmware.map`: 22,074,775 bytes, SHA256
`A7D8FB12E81FB516213EC1DCB1C4067A760337333873BFDD8173A0B724C95F1A`.
The linked ELF contains `BleKeyboardHost::begin/poll/end`,
`NimBLEDevice::init`, and `nimble_port_init`. No offline unit-test runner
was available (CMake/CTest absent; GoogleTest was not fetched). The existing
`CarouselTheme.cpp` explicit-`Rect` constructor warning remains. The first
PlatformIO invocation stalled in cp1252 output; the same incremental build
completed with UTF-8 in 4m53s. No flash, stage, commit, or push. This build
record supersedes the preceding “Next” paragraph; physical X4 acceptance and
the unresolved Web reset root cause still gate release readiness.
Automatic NTP still uses HalClock's existing bounded up-to-five-second wait
on the main task, so a reconnect may pause an active Web server for that bound.
The previously observed roughly 2.7 KiB decrease during `exit_activity_cleanup`
is not assigned to a proven source owner; compare the new owner-by-owner Web
teardown and netif checkpoints on hardware.

### Current continuation after the paused candidate — 2026-10-12

Do not restart, reset, or discard this checkout. Branch remains
`reconstruct/folio-nooir-1.6.4`, with `d9af683b6f9758c479fefe43aaecc0460c0b5803`
as the safe pre-final-fix checkpoint. Current accumulated changes and
untracked `sdkconfig.gh_release`, `src/util/ReaderRenderBudget.h`, and `work/`
belong to this candidate and must be preserved. The last successful build
artifacts documented above predate this continuation's source edits.

#### BLE Settings rows and Reader return

Settings uses NavNext/NavPrevious on list release and uses continuous input
for tab movement/held navigation. The BLE adapter's switch handled PageForward,
PageBack, and Up/Down/Left/Right aliases, but omitted the direct
`edges[NavNext/NavPrevious]`; the comment claiming that direct edge was
available was incorrect. The direct canonical edge is now included in those
same cases. One decoded mapping still sets one logical edge, so this does not
dispatch PageForward and NavNext twice; physical side mapping and Reader
page-turn handling are unchanged.

Level 1 remains the one-shot release of disposable Reader font state, empty
CSS storage, and empty bookmark capacity. After those releases the code now
rechecks both cold BLE floors. If still short and render-quiescent, Level 2
releases only the renderer-owned 8,000-byte grayscale strip, records its
pointer/size and before/after heap, then rechecks admission immediately. The
strip is rebuildable; when the next grayscale pass finds it absent while BLE
is running, the existing event-driven Reader-priority path waits for BLE stop
before reserving it. Cached BW/page turns do not stop BLE. The recovery attempt
remains once per lifecycle generation; failed admission keeps intent pending
without a timer. Floor values remain exactly 98,304 free and 36,852 largest.
The log now says `render_work_pending_or_inflight`: it is the queued/in-flight
render state or RenderLock, not a static activity class; `hasPendingRenderWork`
is cleared when the render task completes and requests reevaluation.

#### Largest-block evidence and sleep boundary

The persistent 34,804 largest-block shape remains unproven. The known 8 pt
coverage sum (660 + 468 + 468 + 468 = 2,064 B) is still correlation only.
`SdCardFontManager` now reports the 8 pt owner, manager/font addresses, each
style interval pointer, and free/largest at load and unload boundaries. This
is intentionally not global allocator instrumentation. Physical logs must
show whether releasing that owner changes the largest block before assigning
it as the cause. Reader exit already releases rebuildable Reader scratch and
the selected Reader-size owner; Sleep additionally releases the separate
interface-font glyph/layout caches before PNG decode while retaining font
coverage/registration.

#### Web reset and Settings transport

The Settings/Stats “Wi-Fi disconnect” is a device reset. The current panic
prefix `259744c6d` matches the old `.pio/build/gh_release/firmware.elf` SHA256
`259744C6D47D412E95BB208EEDC5DADFA7E599984944A739010BED1D63728F2C`.
The bundled Windows `addr2line` fails to launch (`Illegal System DLL
Relocation`), but pyelftools parsed the exact ELF DWARF rows: MEPC
`0x40389af6` -> `esp_cpu_wait_for_intr`,
`C:/Users/fatiha/.platformio/packages/framework-espidf/components/esp_hw_support/cpu.c:64`;
RA `0x40389ae6` -> the same function, `cpu.c:57`; MTVAL `0x4038e3b0` ->
`prvIdleTask`, `FreeRTOS-Kernel/tasks.c:4576`; saved PC `0x4038c1e0` ->
`esp_restart_noos`, `system_internal.c:115` (`ESP_INFINITE_LOOP`). The idle-task
address is in deleted-task cleanup; its idle hook invokes the CPU wait
function. The panic reason/full backtrace is absent, so the exception class
and application/Web root cause remain unproven. `0x4038c1e0` is not
`esp_system_reset_modules_on_exit`, which starts at `0x4038c1e2`.

`NetworkClient::write()` uses `select()` and `send(MSG_DONTWAIT)`; a returned
count means bytes accepted by the lwIP path, not remote ACK. `NetworkClient::flush()`
clears RX, and the TX queue depth is not exposed (`SO_SNDBUF`/queue introspection
is unavailable here). Physical measurements show the 9,489-byte Settings JSON
can cost about 24,548 bytes at its peak; the bounded writer fixed JSON
truncation, not transport pressure. The Web UI now fetches settings in
sequential descriptor pages, retries smaller pages on a preflight 503, and
retains the full-array no-query endpoint behind a response-size-derived headroom
guard. Per-write logs track requested/accepted bytes, heap/largest/client before
and after, and explicitly mark TX pending as unavailable. Physical repeated
request stability is not yet proven.

#### Final checks and release gate

Preserve the physically proven Stats return and streaming path, cached-page BLE
coexistence, P0/P1/P2 EPUB/Web fixes, and the unified UTC NTP/Weather clock.
Per the user's selection, do one NTP attempt on every existing Wi-Fi session
even if the old auto-clock toggle is Off; do not enable Wi-Fi solely for time
and do not add periodic polling. BLE-OFF remains intended to bypass all BLE
host/admission work; new hardware validation is still required.

Latest Settings BLE report: focus is encoded by `selectedSettingIndex == 0`
(tab bar) versus positive indices (rows); the popup consumes input first.
Continuous tab navigation now runs only with tab focus. This prevents its
long-hold callback from consuming the release used for a row step. BLE
PageForward/PageBack aliases remain, but this is not an alias-only fix. X4 must
still verify row steps, Confirm/Back exactly once, popup navigation, and
physical side-button/Reader behavior.

Run focused source review, `git diff --check`, available offline tests, then
exactly one `pio run -e gh_release` using cached dependencies. Report artifacts,
hashes, flash/DRAM use, link proof, warnings, and final status. Do not flash,
stage, commit, or push. Physical X4 validation (cached/heavy BLE, return
recovery, BLE-OFF, sleep PNG, Settings/Stats/Transfer, thumbnails/uploads,
Web-exit recovery, and Calendar) is required before calling 1.6.4 release-ready.

Final build record (2026-10-12): `gh_release` succeeded with cached packages
under Python 3.12/UTF-8. BIN 5,837,552 B, SHA256
`32B4BDC1F41031F77FFAE5793B9A037FCC253A573A8D960289F3E81EA578DA92`;
ELF 81,633,764 B, SHA256
`254771284DC92529245C470ADBEB5111690504691B53A873A60A4AE70DD373B3`;
MAP 22,076,112 B, SHA256
`CCBA54009D157B4F11611963D9B7C4A25AB0BE0558121C2E30A17D2B7333B561`.
Static RAM 61,428/327,680 B; app slot 5,823,409/6,553,600 B. Link map/symbol
table confirms NimBLE and BleKeyboardHost. Offline GTests were unavailable
(no CMake/CTest or host C++ compiler); Settings JS syntax and `git diff --check`
passed. The build was not flashed; no stage/commit/push was performed. Do not
call this release-ready until the physical X4 checklist passes.
