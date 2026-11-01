# Folio Nooir 1.6.7 — Snappiness, Heap Audit and Current Roadmap

**Date:** 2026-11-01 (MYT)  
**Repository:** https://github.com/toshio2011/folio-nooir  
**Working branch audited:** codex/nooir-1.6.7-refinement  
**Audited remote HEAD:** d8e75cbba88a98452f957d2182f28e140725e9c9  
**Default branch at audit:** codex/folio-nooir (1e10e5deb7b8570e6c8383b43abf83aafcc06275; diverged)  
**Scope:** Read-only source analysis and planning. No firmware code, build, tests, device flash or release performed as part of this audit. Historical hardware traces are not new results.

## CURRENT REFINEMENT ROADMAP — 2026-11-01 (supersedes earlier audit work order)

**Status:** documentation/planning only; no firmware code, tests, device flash, merge or release performed by this update. This is the single active work order; the source audit below is retained as historical evidence. Target: **fast, reliable reading on original X4 ESP32-C3**, with no loss of 1.6.6 features, original X3 support, or Main/Nooir Lite rendering improvements.

### Version baseline and what actually changed

| Area | 1.6.3–1.6.5 | 1.6.6 | Committed 1.6.7 branch / local physical candidate |
| --- | --- | --- | --- |
| Reader exit saves | Three sequential updates (Recent, BookState, ReadingStats) already existed; simpler temp/backup writer | Same three updates, plus streamed/checked temp, validation, backup-preservation/rename and recovery logic | Inspected remote Reader/persistence files equal 1.6.6, but previously reported **local uncommitted changes** alter behavior; inspect those before any edit |
| Home snapshots | Identical `RecentBooksActivity.cpp` across 1.6.3/1.6.4/1.6.5; 4×2 snapshot, in-RAM navigation reuse, selection-outline patch and ~500ms idle write | Validated, more layouts, Main/Lite, separate dynamic updates and targeted patches; some popup closures reread full SD snapshot | Remote Home file matches 1.6.6; local stack/snapshot patches must be included in comparison |
| SD-font retention | Glyph/bitmap/kerning reuse existed; under low heap/underuse, scope reset could release mini buffers | No per-scope low-heap purge; retain for faster reuse until explicit lifecycle release | Determine the retained-memory vs SD-reload/time trade-off on identical books; do **not** revert policy without data |
| Font advance workspaces | Fixed ~16KiB 4098-entry codepoint allocation for each table build | Input-sized, largest-block-gated workspace with retry suppression; remaining mapping/staging may overlap | Illustrative ~80KiB worst-case is **theoretical**, not physical measurement |
| Reader/BLE policy | Limited heap gates and idle glyph warmup; no equivalent dedicated adaptive BLE policy in inspected source | Adaptive built-in cache 512/384/256, render preflight, BLE yielding/survival/backoff and three-stop latch | BLE HID worked on physical candidate, but font prewarm caused survival-stop/replay cycles; do not redesign lifecycle |

**Physical evidence (not apples-to-apples benchmarks):** the captured 1.6.6 EPUB exits spent ~5.52–5.54 s in Reader onExit, while the 1.6.7 BLE-enabled capture recorded ~5.55–6.87 s. Later 1.6.7 exits contain ~2.41 s Recent + ~2.21 s BookState + ~2.20 s ReadingStats. The 1.6.7 BLE-off development capture was ~4.61–4.84 s. There are **no matched 1.6.3/1.6.5 on-device timing captures**; earlier versions being faster remains a user observation with a credible but unquantified source explanation. Tested 1.6.7 Home full draws may take ~2.3–3.1 s; validated snapshot fast paths are dramatically faster. Snapshot open/write failures and `framebuffer_read_failed` are captured, cause unproven. BLE-connected Reader free heap fell ~28KiB -> ~19KiB -> ~3.6–5.6KiB during page load/prewarm; survival stop restored ~73KiB and replayed successfully, but disrupted page-turner continuity.

### Phase 0 — establish exact baseline (mandatory, read-only)

1. Confirm the branch and full **local** working tree; record tracked/untracked diff and preserve it. Prior handover reported 23 modified files plus an untracked test contract. The remote docs commit does **not** encompass those changes. Confirm build configuration (`gh_release` BLE enabled vs default BLE disabled).
2. Reconcile source revision, local diffs, compiled binary and exact flashed SHA-256. A previously reported firmware SHA repeated after claims of extra fixes: prove clean-build provenance before trusting any physical comparison. Preserve old device results and label workloads/BLE state.
3. Record repeatable X4 baselines on the same SD card, font, book, layout and BLE state. Separate: Exit button -> `onExit`, three individual saves and subphases, cleanup, Home restore/rebuild, e-paper refresh -> visibly interactive Home. No speculative edit before a baseline.

### P0 / Phase 1 — Reader-exit responsiveness (first implementation)

- Compare 1.6.3/1.6.5 lightweight `.tmp`/`.bak` flow with official 1.6.6 and complete local 1.6.7. Three logical saves were present in old versions; latency is more likely in extra per-file SD work than in the number of update records.
- Use existing `reader_exit_save`, `reader_exit_profile`, `save_profile`, `save_backup` and `save_io` diagnostics. Distinguish JSON encode/write, flush/close, temp readback, existence probes, `.bak` preservation, rename and cleanup. Later captures: backup phases for BookState/Stats rose roughly 0.16s -> 0.64s each, but exact SD root cause is not yet proven.
- Simplify **only demonstrably redundant** successful-save operations. Maintain staged writes, validation adequate to detect incomplete data, one recoverable known-good generation, failure/rename rollback and boot-time restoration. Don't replace the entire writer with 1.6.3, remove essential durability, or create a journal/background-write architecture just to hide latency.
- Ensure that all three stores remain consistent with their established data semantics, and test temp truncation, failed write/flush, failed primary/backup rename, existing `.bak`, recovery from `.tmp`, repeated exits, and unchanged-session fast paths. Run narrow tests + before/after same-card physical timing; if no safe measurable gain, stop and report.

### P1 / Phase 2 — Home snapshot reliability, selection, Settings return and wake

- Preserve existing 1.6.3–1.6.5 design principles *already present* in 1.6.6: only load snapshot on entry, navigate from in-RAM framebuffer, erase/repaint selection outline, defer snapshot writes until idle. Keep 1.6.6's richer Main/Lite layout identity, dynamic progress/details patching and three-row planner stack repair.
- Diagnose `framebuffer_read_failed` and `/.crosspoint/folio_home.bin` write/open errors first: log directory-creation outcome, file size, expected/read/written bytes, flush/close and available filesystem status. Other SD writes succeeded in the earlier capture, so do not assume general card damage.
- Measure SD 48KB restore vs scoped repaint on popup dismissal and child-screen return; changes must preserve popup cleanup and frame correctness. Direct live-snapshot overwrite can leave a truncated cache on interruption: consider `.tmp` validation/rename preserving the previous valid snapshot **only after** I/O evidence, with no second framebuffer or needless metadata churn.
- Distinguish necessary invalidations (theme/layout/geometry) from avoidable invalidations. Benchmark Main and Lite separately, plus Settings exit, sleep/wake and repeated page selection. Keep or improve visible responsiveness, not just CPU microseconds.

### P2 / Phase 3 — measured heap and BLE safety

- Contrast the old SD-font mini-buffer underuse/low-heap release policy with the newer explicit lifecycle policy. Retaining buffers can improve page-turn speed but hold heap; releasing them per page can increase SD reads, heap fragmentation and stutter. Do **not** restore a per-scope purge or shrink SD 512-glyph cap to 256 without controlled measurements.
- Add narrow, rate-limited tracing of actual simultaneous SD-font prewarm allocations (glyph/bitmap/interval/mapping/read-order/kerning) and advance-table workspace/mapping/staging/merge, with free heap, minimum heap, **largest contiguous block**, failed allocations, SD reads/seeks and prewarm latency.
- The built-in compressed-font prewarm uses a Reader allocation-admission callback, while the SD-font path has separate handling and on-demand glyph fallback. Investigate an **early**, bounded SD preflight based on real allocation requests; don't abort text rendering or compromise Arabic/RTL, CJK, ligatures or mixed styles.
- Only if device results support it, adjust BLE Reader return/admission to account for observed page demand while preserving cold floors, survival stop/replay, safe return, cooldown, per-book three-stop latch, and BLE-OFF path. Reading wins over optional BLE; avoid reconnect loops and global steady-state purges.

### P3 / Phase 4 — tests, release gating and outcomes

- Make one minimal scoped change at a time, with focused tests and comparison to a recorded baseline. Run the full C++ host suite and Python contracts, then `gh_release` **clean build** with proof of exact local source/binary SHA and BLE-on link flags.
- Physical original-X4 acceptance: normal/warm/cold/CSS-heavy EPUB, PNG/JPEG text-image mixtures, Arabic/RTL/CJK and styled text, XTC/XTCH, CBZ, BLE disabled/enabled/connected/reclaim, Nooir/Lite, Home paging and popup, Settings exit, sleep/wake, repeated book openings/exits, backup recovery; verify original X3 where possible.
- Record button-to-first-visible-response, button-to-usable-Home, page-turn total/display time, Reader save-phase durations, SD operations, free/min heap + largest block, allocation failures, font cache and SD read hits, BLE stop/replay counts and memory after exit. **A larger free-heap number with slower page turns is not a win.**
- Decision gate: no data-loss/corruption or missed glyphs, no regression in BLE-OFF reading, preserved 1.6.6 functionality and Main/Lite fast paths, visible improvement or measured safety benefit. If a change fails, revert that specific experiment. No merging, tagging, flashing or release without explicit approval.

### Scope rules

**No new features. No speculative full-frame caching, giant persistent scratch, wholesale renderer rewrite, broad cleanup loops, silent save deferral or alternative transaction subsystem. Do not touch MihonNooir Android or Windows.** This version targets predictable responsiveness and reliable reading, not maximum reported free heap.

---

## Archived source audit and earlier investigation priorities (historical context)

The sections below reflect the first November 1 audit and its earlier SD-font-first priority ordering. They remain useful as evidence but **are superseded by the active roadmap above**. Source inspection alone does not establish hardware speed or current uncommitted worktree behavior.

## Objective and external reference

Improve actual reading snappiness and heap safety on **XTEINK X4 ESP32-C3**, especially EPUB page turns, Reader entry/exit, BLE coexistence, Home/Recent, Settings and wake. Reading comes before optional BLE. Preserve **Nooir and Lite**, their fast paths, and the original X3 compatibility.

Licorice evidence: https://github.com/kycube/x4pro-emu/blob/main/docs/stock-licorice.md and https://github.com/kycube/x4pro-emu/blob/main/docs/reading-fonts.md . This is reverse engineering of stock **X4 Pro ESP32-S3** behavior in an emulator, NOT the original Licorice source or verified X4/C3 memory figures. Do not infer a portable page-cache policy from its XIC image references or add a second full framebuffer.

## ALREADY IMPLEMENTED — DO NOT REDO

1. **SD font reuse and retry guards.** lib/EpdFont/SdCardFont.cpp has keep-if-fits glyph, interval, bitmap and mini-kern buffer reuse; reusable 2 KiB codepoint workspace for page prewarm, explicit disposable release, and failed-allocation retry suppression.
2. **Compact per-page kerning.** Class lookups are loaded separately; a mini matrix for used classes avoids retaining a full ~36 KiB kerning matrix per page.
3. **Bounded font advance lookup.** buildAdvanceTableRange() replaced the old fixed 4,098-slot workspace with an input-byte-sized allocation (4,096 unique codepoint cap + reserved slots). Largest-block preflight, retry memoization and a persistent per-style 768-entry advance cache already exist.
4. **Adaptive built-in font caches.** src/util/ReaderRenderBudget.h and FontCacheManager have Normal/Constrained/Heavy budgets of 512/384/256 glyphs and 4096/3072/2048 scan bytes per font.
5. **BLE reader survival controls.** src/activities/reader/EpubReaderActivity.cpp and src/util/BleMemoryPolicy.h have render preflight, largest-request tracking, one-shot staged reclaim, BLE yielding during heavy work, 15–120s survival backoff and a **three-stop per-book readmission latch until Reader exit**. BLE-OFF must remain the ordinary non-BLE path.
6. **EPUB.** Serialized Section layout/page caches; skip loading phase for ready pages; idle next-page glyph prewarm when BLE preference is off; image warmup before font prewarm for uncached images; render-lifetime image-pixel-cache cleanup; queued-turn grayscale/AA replay deferral.
7. **Exit and durability.** EPUB exit frees Section/EPUB and multiple Reader-owned/global font/image/gray scratch owners in an ordered sequence; EPUB/XTC/CBZ progress saves skip identical positions and use atomic/recovery logic; per-store exit save profiling exists. Do not trade away data integrity.
8. **Home.** Persistent validated snapshot, targeted dynamic patches and fast waveforms, delayed (~500ms idle) snapshot writes, Lite and Nooir 3-row layout behavior, and planner stack-safety fix. RecentBooks exits release their local book vector.
9. **CBZ/XTC.** CBZ persistent per-page cache, prefetch cancellation and memory/battery gates, X4 lookahead 3 / X3 lookahead 1; XTC/XTCH streaming and metadata/cache behavior already implemented.
10. **Host coverage already exists:** test/ble_memory_policy, test/home_snapshot_policy, test/settings_persistence, test/epub_image_recovery, test/epub_page_turn_queue, test/section_cache and test/x4_shelf_perf.

## SOURCE-VERIFIED GAPS / MEASURE-FIRST CANDIDATES

### Earlier P0-A — SD font advance temporary allocation peak (measure-first candidate)

Path: lib/EpdFont/SdCardFont.cpp, buildAdvanceTableRange() -> fetchAdvancesForCodepoints() -> mergeIntoAdvanceTable().

- The input-sized uint32 workspace can overlap CpIdx[cpCount] and staged AdvanceEntry[needCount], followed by a merged-table replacement while old cache still lives.
- Even though persistent advance cache has only **768 slots per style**, transient mapping and staging can be based on a much larger candidate count (up to 4,096). Worst-case illustrative *capacity* of first three allocations: ~16 KiB + ~32 KiB + ~32 KiB before other heap owners. This is **not measured device usage**; actual needCount may be smaller.
- Audit actual per-stage allocation requests, temporary overlap, total vs largest heap block, SD reads/seeks, and time. If a bottleneck is confirmed, consider remaining-cache-capacity-aware bounded work, chunked processing and/or modest reusable scratch. DO NOT add a permanent ~80 KiB scratch arena.
- collectUniqueCodepoints() deduplicates by linear scan (quadratic worst case). Benchmark before replacing; ordinary Latin pages may not benefit.
- Preserve *all* text fallback, Arabic/RTL shaping, CJK, ligatures, style and page-layout correctness. The 768-entry advance cache limit is not a limit on readable characters.

### Earlier P0-B — SD-card font prewarm budget boundary

- SdCardFont::prewarm() uses MAX_PAGE_GLYPHS=512 and has existing buffer reuse/failure handling, but its internal mapping, bitmap growth, read-order and kern/lig allocations do not uniformly participate in the same pre-allocation callback as the built-in compressed-font path.
- Investigate an actual-allocation preflight using free **and largest contiguous** heap blocks, preserving render/ble teardown ownership. Do not blindly shrink SD glyph capacity to 256; that may cause slower overflow SD reads or missing/precarious glyph coverage on CJK/Arabic.
- Scope BLE-off and BLE-connected behavior separately; an optional feature must not disrupt normal reading.

### Earlier P1-A — Confirm BLE readmission versus known heavy-book peak

- Current policies **already** have adaptive render budgets, backoff, safe one-shot return and a three-stop book latch. Do not redesign them or add a polling reconnect loop.
- Historical X4 traces: BLE can return near ~97 KiB free at idle; later font work falls to ~3.6–5.6 KiB free and triggers survival stop/replay. This may indicate quiescent admission not sufficiently accounting for next-page SD-font demand, but is only a hypothesis.
- Correlate BLEMEM / HEAPSHAPE / SDCF / ERS for light vs heavy pages; use history/hysteresis only if device evidence supports it. Do not relax global cold BLE floors or reopen repeated stop/start churn.

### Earlier P1-B — Home snapshot I/O on overlay close

- RecentBooksActivity::restoreSnapshotAfterOverlayDismissal() re-reads the validated saved framebuffer from SD (approximately 48KB for 480x800x1bpp). This may beat a redraw; **measure it** with SHELFPERF and I/O diagnostics, including Nooir and Lite.
- writeSnapshot() currently writes directly to /.crosspoint/folio_home.bin, not a temp+promotion scheme. Investigate interrupted-write robustness separately from latency. Snapshot validation and fallback already exist. No second full-screen framebuffer.

### Earlier P2 — Reader entry/exit persistence, only after profiling

- Entry updates app/recent state and loads bookmarks and clippings; exit already profiles Recent, BookState, Stats, progress and ordered release. Determine if save validation, SD flush or display transition dominates the latency before considering changes.
- Preserve backup and crash-recovery behavior. No deferring or removing essential progress durability merely to improve a benchmark.

## Previous audit work order — historical, superseded by active roadmap

1. Confirm the 1.6.7 refinement branch/commit; default branch diverged. Read this note before suggesting changes.
2. Add narrow **diagnostics** for P0-A allocation sizes, high-water overlap, SD time and largest block; capture baseline on the actual X4 for representative content. No giant persistent buffers.
3. If measured, implement **one bounded P0-A improvement** with host tests, then compare baseline vs candidate. Otherwise do not change that path.
4. Examine P0-B SD-font prewarm admission in the same instrumented workload; preserve BLE-OFF and font correctness. Add a test for failure/retry and degraded contiguous heap.
5. Evaluate BLE readmission only after font memory is understood; preserve cooldown, book latch, stage boundaries, existing cold floors and no steady-state cache purge.
6. Profile Home overlay close, Nooir/Lite shelf selection, Settings exit, reader exit, wake and repeated books. Optimize only proven hotspots.
7. Run full C++ host tests and Python contracts, gh_release compile, then **physical X4** tests. Record pass/fail and raw numbers; do not equate source review with firmware validation.

**Required matrix:** EPUB warm and cold pages, CSS-heavy chapters, PNG/JPEG mixed images, Arabic/RTL and CJK, bold/italic/ligatures, highlights/clippings, XTC/XTCH, CBZ, BLE off/on/connected/yielded, Nooir and Lite Home, repeated Reader exits, Settings return, sleep/wake and X3 regression where feasible.

**Measure:** button-to-visible-page timing, CPU and display-refresh phases, smallest free heap, largest allocatable block, allocation failures, SD seeks/reads/flush times, page/cache hits, BLE survival stops/reconnects/replays and post-Reader-exit heap. A bigger free-heap number with slower pages is NOT a win.

## Guardrails / audit status

No proprietary Licorice firmware copied. No extra full-screen raster cache, global purge, broad renderer rewrite, Android/Windows MihonNooir edits or unrelated code changes. This document is a **documentation-only** Nov 1 audit/handover; no source compilation or hardware verification was performed.
