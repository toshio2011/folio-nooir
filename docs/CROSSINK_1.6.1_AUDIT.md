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

5. Global/per-book reader-setting application audit
   - Compare precedence, migration, per-book overrides, repagination/cache invalidation, and failure recovery.
   - Fix only demonstrated inconsistencies.

6. LibraryIndex scanning/index storage comparison
   - Do not replace Nooir Library/Recent/Finished UI.
   - Study CrossInk LibraryBuilder/LibraryIndexFile/LibraryFormat/LibraryText/LibraryRecentOrder/LibraryFileTypes for large-library scanning, persistence, incremental rebuild, SD-I/O, memory and corruption recovery.
   - Candidate value is backend mechanics for Nooir's large libraries, not CrossInk's Library screen.

7. Memory-aware EPUB layout decisions
   - Study pre-allocation memory gates, rich->compact table/layout fallbacks, incremental work, glyph prewarm limits, buffer limits, and graceful degradation.
   - Prefer adapting proven bounded mechanisms into Nooir's existing renderer over replacing Nooir's EPUB architecture.

8. Tiny parser/cache fixes
   - Continue auditing 1.6.1 changes for isolated correctness, cache validation, corruption recovery, overflow/bounds, allocation, SD-I/O and page-turn fixes.
   - Require a Nooir-relevant failure mode or regression fixture; avoid cosmetic churn.

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
