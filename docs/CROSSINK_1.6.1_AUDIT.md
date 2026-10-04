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
   - Classification: **worth prototyping for 1.6.3 only as an isolated backend/search enhancement**. Full LibraryIndex parity belongs later.

7. Memory-aware EPUB layout decisions
   - Study pre-allocation memory gates, rich->compact table/layout fallbacks, incremental work, glyph prewarm limits, buffer limits, and graceful degradation.
   - Prefer adapting proven bounded mechanisms into Nooir's existing renderer over replacing Nooir's EPUB architecture.

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
