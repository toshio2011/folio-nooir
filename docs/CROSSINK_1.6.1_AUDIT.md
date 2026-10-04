# CrossInk 1.6.1 -> Folio Nooir 1.6.3 audit

Date: 2026-10-04
CrossInk audit ref: `3b67ad6` (1.6.1 RC line inspected)
Nooir target: 1.6.3 development line

## Policy

CrossInk/CrossPoint is an upstream idea/fix source, not a merge target. Classify every candidate as **ALREADY COVERED**, **TAKE/ADAPT**, **INVESTIGATE**, **LATER**, or **SKIP**. Preserve Nooir's X3/X4 resource budget, EPUB/CSS/Arabic behavior, cache safety, reader responsiveness, and bookshelf identity. Measure flash/RAM and regressions before accepting firmware changes.

## Accepted audit scope for 1.6.3

1. Configurable reader status bars
   - Nooir already has status-bar rendering, EPUB layout reservation/redraw, chapter page/count, book progress %, book/chapter title, battery, progress-bar mode/thickness, XTC top/bottom overlay, and X3 RTC clock support.
   - CrossInk 1.6.1 adds flexible composition: independent top/bottom configs; seven slots per bar (3 left, center, 3 right); Clock, Battery, TimeLeftBook, TimeLeftChapter, ChapterPageCount, StablePageNumber, BookProgressPercentage, TitleBook, TitleChapter, Empty; percentage precision; progress mode/thickness; collision-aware width fitting; live settings preview; XTC hide/bottom/top/both.
   - Direction: evolve Nooir's existing status bar rather than wholesale-port CrossInk. Keep UI Nooir-styled and bounded for 800x480 X3/X4. Consider exposing a simpler 3-position UI initially while keeping a forward-compatible internal representation.
   - Time-left should preferentially reuse/adapt Nooir reading-statistics data rather than duplicate a second pace subsystem.
   - XTC/XTCH dual-overlay mode needs obscured-content testing before adoption.

2. Unknown XML namespace-prefix regression — **ALREADY COVERED**
   - CrossInk issue #790 is specifically OPF elements arriving with prefixes such as `ns0:manifest`, `ns0:spine`, and `ns0:itemref`, causing the parser to miss manifest/spine data and land at End of Book.
   - Nooir already centralizes local-name matching in `lib/XmlParserUtils/XmlParserUtils.h`: `xmlLocalName()` strips the prefix and `xmlNameIs()` compares the local name. Current `ContentOpfParser` uses this for package/metadata/manifest/spine/item/itemref handling.
   - No source port required. Retain/add a prefixed-OPF regression fixture when the parser test batch is touched.

3. Empty CSS span / inline spacing behavior — **TAKE/ADAPT candidate**
   - CrossInk issue #748 preserves inline `padding-left` even when an element contains no text by queuing pixel padding and attaching it to the following real token.
   - Nooir's current token/layout path has no equivalent queued inline-padding state.
   - Do not copy CrossInk's per-word representation literally. Nooir's serialized `TextBlock` stores final word X positions, so this can likely remain transient parser/layout state with **no cache-format migration**.
   - Preferred next step: focused fixture equivalent to `EERO:<span class="spacey"></span>Kappusiwai!`; compare a transient width-offset approach against a synthetic attached spacing token. Preserve BiDi, justification, hyphenation, and Phase-C cascade behavior.

4. KOReader oversized-response handling — **ADAPT, but target progress responses rather than auth**
   - CrossInk issue #783 hardened authentication by capping the expected-small auth JSON response at 4096 bytes and rejecting oversized, incomplete, or allocation-failed bodies before JSON parsing.
   - Nooir's current `authenticate()` does not read/parse the auth body; it accepts successful HTTP 2xx, so the exact #783 crash path is already avoided.
   - The more relevant Nooir risk is `getProgress()`: it currently calls unbounded `http.getString()` before JSON parsing.
   - FreeInk `SecureHttpClient` already supports streaming `GET(callback)`, 2 KiB delivery chunks, Content-Length inspection, `responseComplete()`, and callback-abort detection. No SDK change is required.
   - Direction: bound progress response reads locally. Proposed starting cap: **8 KiB** (normal KOSync progress objects are tiny; this leaves ample room for Nooir/CrossPoint rich-position fields while rejecting accidental HTML/proxy/error bodies). Reject known Content-Length above the cap where possible, stop unknown/chunked bodies at the cap, require a complete response, then parse only the bounded buffer.
   - Add diagnostic classification for empty/blank, HTML, non-JSON, malformed JSON, oversized, incomplete, and allocation-failed responses. Do not log credentials or full server bodies.
   - Validate 8 KiB against representative reference KOSync and CrossPoint extended responses before freezing the constant.

5. Global/per-book reader-setting application audit — **ALREADY COVERED / NOT APPLICABLE**
   - CrossInk's 1.6.1 fixes address its two-level model: global defaults plus per-book `reader_settings.bin` overrides. Safe Mode could accidentally persist an override, and global changes could fail to reapply immediately when no genuine override existed.
   - Nooir currently has no per-book reader-settings store. `TextSettingsActivity` edits the singleton `SETTINGS` directly and persists the same `/.crosspoint/settings.json` whether opened globally or from an EPUB.
   - Nooir also explicitly refreshes the live book after in-reader settings: `saveToFile()` -> `refreshAfterReaderSettings()` -> drop live `Section` -> preserve approximate position -> clear stale layout/highlight state -> render using a fresh `ReaderRenderSpec`.
   - Do not introduce per-book settings merely to match CrossInk. Treat that as a separate future product feature with explicit override/reset UX and migration design.

6. LibraryIndex scanning/index storage comparison — **TAKE/ADAPT architecture, not wholesale port**
   - CrossInk CLX1 is a persistent SD-backed catalog, not merely a filename cache. One bounded recursive walk streams records to a staging file; it does not retain the whole library as heap strings/objects.
   - The primary resident sort array is 14 bytes/book. The on-disk format uses fixed 128-byte records plus compact u16 sort permutations and variable blobs. Rebuilds reuse prior EPUB metadata when path, size, modification time, metadata mode, and format conditions still match.
   - Reconciliation preserves arrival order and recognizes unchanged/added/removed entries; installation is transactional (`.new` / live / `.bak`) with interrupted-install recovery and strict header/offset/file-size validation.
   - Defensive scanning includes depth capping, duplicate-dirent suppression, unreadable-entry skipping, bounded/fallible allocations, and periodic scheduler yields.
   - Nooir already solves the **bulk retrieval RAM** problem independently: Retrieve All recursively scans in small batches, streams paths to SD queues, then performs metadata and thumbnail phases without retaining hundreds/thousands of book paths in RAM.
   - Nooir's normal Folio Library remains directory-centric: the current folder is materialized as `std::vector<std::string>`; Search All Folders performs a fresh recursive traversal and accumulates matching relative paths in RAM. This is the clearest gap a persistent catalog can improve.
   - Recommended Nooir direction: a **lightweight persistent catalog underneath Folio Library**, preserving the existing bookshelf UI, BookState/Recent semantics, per-book metadata caches, metadata overrides, covers, and Retrieve All pipeline. Do not copy CrossInk's full metadata database by default.
   - First catalog scope should be identity/navigation data only: full/relative path (or folder+basename), file type including **CBZ** (which CrossInk CLX1 currently does not index), size, modification time where available, and a stable path hash. Optional tiny folded search keys can be evaluated after profiling.
   - Reuse Nooir's existing per-book metadata caches for title/author/synopsis instead of duplicating rich metadata into the catalog. Persist selected display keys later only if profiling proves per-row metadata-cache opens dominate shelf/search latency.
   - Global search is the strongest first consumer: query the catalog incrementally/page-wise instead of recursively walking the card and retaining every matching path. Folder browsing can remain direct initially.
   - Borrow transactional rebuild, strict validation, and metadata-independent reconciliation. Do not blindly inherit CLX1's 4096-record limit, 255-byte folder/name assumptions, five sort permutations, series/genre metadata, author-spelling harmonisation, or arrival-order model unless Nooir UX needs them.
   - Refresh semantics must be explicit: external SD edits are unknowable while powered off, so mark the catalog dirty on boot and after Nooir file-changing operations; refresh manually/explicitly or incrementally before global-search results are trusted. Catalog refresh must not force full metadata retrieval.
   - Measure 400, 1000, and 2000 mixed-format books: scan wall time, peak heap/max alloc, index bytes, SD reads/writes, first-result and complete-search latency, and UI responsiveness.
   - Deeper reconciliation finding: CrossInk's most useful optimization is not merely indexed search. Existing-path records compare source size + modification time (plus index/schema conditions); unchanged sources reuse prior metadata/sort material, and a completely unchanged rebuild can discard its staging files without replacing the live index.
   - Nooir's Retrieve All already avoids ZIP parsing when its per-book metadata cache is valid, but it must still recursively enumerate the card and inspect each book/cache on every run. A catalog generation can instead classify the library as unchanged/new/modified/removed before expensive per-book work; only changed/new sources need cache inspection/rebuild beyond the normal shelf preview path.
   - Correctness benefit: current Nooir EPUB `metadata.bin` (v1) and CBZ `metadata.bin` (v1) store metadata/version but no source file size or modification timestamp. Replacing a book in place under the same path can therefore leave lightweight shelf metadata apparently valid until explicit refresh/cache deletion. A catalog source fingerprint can centrally detect that stale-cache condition without immediately version-bumping every format's metadata cache.
   - Prefer a shared catalog fingerprint over duplicating freshness fields into every cache format initially: `pathHash + fileSize + modificationTime` where the filesystem provides reliable mtime. Path identifies normal unchanged books; size+mtime detect in-place replacement. Do not use size alone for identity.
   - Rename preservation is optional for Nooir 1.6.3. CrossInk heuristically matches unmatched old/new entries by exact file size to preserve `firstSeen`; Nooir has more sensitive path-keyed state (reading status, Recent, stats/cache paths), so **do not migrate user state on size-only rename guesses**. If rename migration is added later, require a stronger content identity/fingerprint.
   - Integrate catalog generation with Retrieve All rather than running two independent recursive scans: one bounded traversal can write/update the catalog and simultaneously queue only new/modified/missing-cache books for metadata/thumbnail preparation. This preserves Nooir's responsive phased pipeline while eliminating redundant discovery work.
   - Global Search can consume the same catalog incrementally. Thus the catalog has three justified consumers: source-freshness validation, Retrieve All delta planning, and recursive/global search.
   - Classification: **worth prototyping for 1.6.3 as a shared discovery/freshness backend**, not merely search. Full CrossInk LibraryIndex parity belongs later.

7. Memory-aware EPUB layout decisions — **TAKE/ADAPT as a shared headroom policy**
   - CrossInk centralizes heap admission in `MemoryBudget`: decisions consider both total free heap and the largest contiguous allocation (`maxAllocHeap`). This is more useful than reacting only after `nothrow` fails, especially when BLE/NimBLE reduces or fragments internal heap.
   - CrossInk uses operation-specific budgets rather than one global "low memory" threshold. Current C3-oriented examples include text-layout floor (~44 KiB free), inline image gates (source-dependent; non-JPEG 72 KiB free / 48 KiB max allocation), rich-table admission (96 KiB free / 56 KiB max allocation), section prewarm (80 KiB free / 24 KiB max allocation), and optional rebuild/prefetch gates. **These numbers are evidence, not Nooir constants**; tune from Nooir X4/X3 diagnostics, especially with BLE enabled.
   - Graceful fallback order is the valuable design: skip optional whole-section prewarm -> choose smaller HTML stream chunks -> release rebuildable SD-font caches -> avoid/suppress expensive inline-image work -> use lower-memory table models / flatten unsupported tables to readable paragraphs -> finally abort the section build safely if the minimum text-layout floor cannot be maintained.
   - CrossInk's rich-table path is particularly instructive: rich buffered layout is admitted only with strong headroom; lower-memory compact/streaming paths release row state incrementally; structures that exceed bounded capacities degrade to paragraphs rather than OOM/crash.
   - Nooir already has several reclaimable-memory mechanisms that fit this model:
     - `FontCacheManager::releaseSdFontCaches()` drops disposable glyph/decompressor/hot-group state while preserving advance/coverage/kerning/ligature metadata.
     - `GfxRenderer::FrameBufferLoan` lends the existing ~48 KiB framebuffer allocation in place during memory-hungry build phases, avoiding free/realloc fragmentation.
     - grayscale BW preservation is already chunked (~8 KiB chunks) instead of requiring one ~48 KiB contiguous allocation.
     - parser/page/image allocations already use fallible allocation paths and persistent inflated HTML reduces repeated ZIP/inflate pressure.
     - BLE disable performs full NimBLE teardown specifically to return controller/host RAM to the heap.
   - The missing piece is policy/orchestration: Nooir mostly notices memory pressure at allocation failure sites. Add a small Nooir-owned budget layer that snapshots `freeHeap` + `maxAllocHeap`, logs stage transitions, admits optional work, and can invoke registered reclaim steps before a mandatory allocation.
   - Design the policy for **future BLE coexistence**, not as EPUB-only constants. Suggested conceptual API: `MemorySnapshot`, `MemoryRequirement {minFree, minLargest}`, operation IDs (EPUB text start, inflate, image decode, rich table, prewarm, BLE start/reconnect), and a reclaim tier. Keep actual reclaim actions owned by their subsystems.
   - Reclaim tiers should be explicit:
     1. **Optional work off**: skip prewarm/prefetch/rich embellishments.
     2. **Drop rebuildable caches**: SD-font disposable caches and other proven reloadable scratch.
     3. **Choose low-memory algorithms**: smaller stream chunks, compact/streaming tables, image placeholder/alt-text path.
     4. **Fail safely**: abort/retry the section or refuse BLE start with a clear diagnostic rather than corrupting reader state.
   - Do **not** treat the framebuffer loan as a general BLE memory source: it is safe only during controlled non-render build windows and the display cannot render while lent. BLE is long-lived; its steady-state reserve must coexist with the restored framebuffer and normal page-turn rendering.
   - Likewise, do not automatically purge the live Section/page cache merely to connect BLE. First measure BLE's init/connected-idle largest-block requirement and reclaim only disposable state. If steady-state BLE leaves too little headroom for reliable text layout, prefer disabling expensive reader features while BLE is connected over repeated teardown/reconnect churn.
   - Instrument before freezing thresholds. Capture `freeHeap` and `maxAllocHeap` at: Reader idle, section build start/end, before/after SD-font release, inflate start, CSS load, table admission/fallback, image decode, page render, BLE init, BLE connected-idle, BLE key event/page turn, and BLE teardown. Compare BLE off/on on old X4; X3 build/simulator guards remain mandatory.
   - First implementation candidate for 1.6.3: **policy + diagnostics + safe optional-work gates**, then adapt only the cheapest proven fallbacks. Do not port CrossInk's full rich/compact/streaming table subsystem in this release; Nooir already has a bounded readable row renderer, so table sophistication is lower priority than predictable headroom.
   - Classification: **high-value TAKE/ADAPT**. It improves EPUB resilience now and creates the correct foundation for BLE Reader coexistence later.

8. Tiny parser/cache fixes — **SWEEP COMPLETE for advertised 1.6.1 fixes; continue only when source evidence is specific**
   - The advertised 1.6.1 EPUB/parser correctness fixes are #748 (empty inline CSS spacing) and #790 (prefixed OPF XML names). #790 is already covered; #748 remains the only direct small EPUB behavior gap found in the release fix list.
   - CrossInk's #748 implementation carries CSS `padding-left` as pixel geometry attached to the following token; it does not inject a literal whitespace character. Its focused test uses `EERO:<span class="spacey"></span>Kappusiwai!` with `padding-left: 2em`.
   - Nooir adaptation should likewise preserve geometry without changing extracted/source text. Prefer transient layout state because Nooir serializes final word X positions; avoid changing `TextBlock` on-disk format unless a focused test proves transient state cannot handle wrapping/BiDi/justification correctly.
   - Source-diff inspection also exposed larger defensive work in CrossInk Section/Page/CSS/OPF code (bounds, cache recovery, memory-aware allocation), but these are not tiny isolated 1.6.1 ports. Nooir already independently hardens section/page deserialization, incomplete-build commit semantics, LUT offsets, and cache rebuild behavior. Evaluate the remaining memory/OPF pieces under the dedicated memory-aware and large-library/parser audit items rather than cherry-picking them here.
   - Non-EPUB advertised 1.6.1 fixes (OTA/Wi-Fi crash, Home navigation, carousel loading) are outside this parser/cache batch.

## Explicitly not part of this audit batch

- CrossInk Cover Grid / Library UI replacement.
- Runtime TTF on original X3/X4: CrossInk's scalable TTF path targets S3-class devices; Nooir X3/X4 should retain the bounded bitmap/cpfont strategy unless separate evidence changes that conclusion.
- Quick Actions are still later unless scope/headroom is explicitly reopened.
- Wholesale CrossPoint/CrossInk merge.

## Evidence to record for each accepted change

- CrossInk source ref/commit and exact files/functions.
- Nooir equivalent or gap.
- Why the change is needed.
- Focused regression fixture/test.
- Linked flash and padded firmware delta.
- Static RAM and runtime heap/largest-block impact where relevant.
- X4 physical result; X3 shared-build/simulator result, with physical X3 not claimed unless actually tested.
- Cache/settings migration impact.
