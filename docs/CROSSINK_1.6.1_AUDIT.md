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

2. Unknown XML tag regression fix
   - Trace exact CrossInk parser change and compare against Nooir's existing malformed-XML recovery/generic block flow.
   - Add a focused regression fixture before changing source.

3. Empty CSS span / inline spacing behavior
   - Trace preservation/spacing semantics in CrossInk.
   - Compare against Nooir Phase C cascade/parser behavior; port only the missing edge case.
   - Must not reopen frozen selector/cascade work without contradictory fixture evidence.

4. KOReader oversized-response handling
   - Compare authentication/HTTP response handling against Nooir's physically interoperable KOSync baseline.
   - Take only bounded response/crash handling that preserves existing matching/auth behavior.

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
