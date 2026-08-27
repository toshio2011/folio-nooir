# Future CBZ/Manga Plan

**Status:** deferred future work; planning only. This document does not
describe currently implemented 1.6.1 functionality. No new CBZ/Manga reader,
preparation, cache, storage, or performance implementation has started.

The immediate 1.6.1 development focus is EPUB reading quality, especially
Arabic/RTL support, text shaping, fonts, and layout. CBZ/Manga work begins only
after that EPUB phase and after this plan has been audited and refined against
the existing reader and device constraints.

## Current baseline

- Nooir already reads `.cbz` files directly.
- External XTC/XTCJ conversion is optional and is never required for CBZ
  reading.
- Existing CBZ reading remains functional without whole-book preparation.
- Normal CBZ reading already performs approximately three-page
  read-ahead/preparation on the supported path. X4 uses the fuller three-page
  behavior; X3 retains a more conservative lookahead subject to memory and
  ownership checks.
- Future work must audit and build upon this existing mechanism rather than
  assuming that CBZ caching or read-ahead does not exist.
- The existing CBZ reader, page cache, navigation, image safeguards, and
  direct-rendering fallback are part of the completed 1.6.0 baseline and must
  remain usable throughout future preparation work.

## Reader and view modes

The preferred/default manga view should be portrait **Fit Width**, while
retaining the following explicit modes:

- Portrait Fit Width as the preferred/default CBZ view.
- Fit Page.
- Landscape Page Width using the available screen width.
- Zoom/Pan.
- Confirm opens View Mode.
- Every mode must preserve the source aspect ratio.
- Manga-oriented scaling should be sharp and should preserve the readability
  of small text and speech bubbles.
- Page changes must not happen accidentally when pan or zoom reaches a
  boundary.

### Controls

| Mode | Controls |
| --- | --- |
| Portrait | Left = previous page; Right = next page. |
| Landscape | Left/Right = horizontal movement; Up/Down = previous/next page. |
| Zoom | Directional buttons pan; long-press Up/Down changes zoom. |

At a pan boundary, the directional input must remain a pan-boundary action and
must not silently become a page turn. This behavior needs explicit testing at
the left, right, top, and bottom edges in both orientations.

## Manga image pipeline

- Keep CBZ processing separate from EPUB image handling.
- Preserve and use the manga-specific `manga_nearest` / center-sample
  downscaler where appropriate.
- Prioritize thin-line and speech-bubble readability over generic image
  pipeline reuse.
- Preserve four-level grayscale output.
- Avoid unnecessary dithering; **Dither Off** is preferred for manga.
- Investigate a full display refresh per page where it produces cleaner manga
  rendering without making page turns unacceptably slow.
- Do not alter EPUB's bounded-area image pipeline merely to optimize CBZ.

The final scaling and refresh choices remain subject to profiling and physical
X3/X4 comparison. Visual sharpness must not be purchased by allocating unsafe
full-resolution source buffers or weakening existing image bounds.

## Existing read-ahead and cache audit

The first implementation step is an audit and profile of the current
approximately three-page read-ahead/preparation path:

- Measure its cache format and on-disk identity.
- Record persistence behavior, lifetime, invalidation, and cleanup rules.
- Trace the decoding and resizing path used for a cache hit and a cache miss.
- Measure RAM use, SD reads/writes, decode time, and page-turn latency.
- Document ownership and interaction with foreground navigation.
- Test how it behaves when the user flips rapidly forward and backward.
- Avoid unnecessarily decoding previously visited pages again.
- Support efficient forward and backward navigation.
- Preserve normal read-ahead as the fallback even after whole-book preparation
  exists.
- Make prepared pages and normal read-ahead coexist without replacing the
  active page with stale or partial work.

Normal read-ahead must remain useful when a book is only partially prepared,
when preparation is unavailable, and when a prepared cache is invalid or
removed. A future whole-book cache must extend or reuse the existing native CBZ
cache/preparation architecture wherever practical rather than creating a
second unrelated reader.

## Large manga pages

The future implementation must test very large source pages, including pages
around **6000 x 2500** pixels.

- Avoid excessive RAM use.
- Do not retain huge decoded source images unnecessarily.
- Determine safe decoding and downscaling behavior through profiling on actual
  X3 and X4 constraints.
- Measure both the largest allocatable block and total free heap at important
  phases; total free heap alone is not sufficient.
- Confirm that large-page failure or fallback never makes the original CBZ
  unreadable when direct rendering remains possible.

## Future “Prepare for Nooir” work

Whole-book preparation is optional future work, not a prerequisite for reading:

- Provide an optional **Prepare for Nooir** flow for a complete CBZ.
- Extend or reuse the existing native CBZ cache/preparation architecture.
- Produce device-appropriate cached pages.
- Resize oversized pages ahead of reading.
- Normalize problematic or progressive JPEGs where necessary.
- Prepare incrementally.
- Keep completed pages valid when preparation stops.
- Never make reading wait for the entire book to finish preparing.

The preparation format, versioning, invalidation, and exact relationship to the
existing page cache remain open design questions for the later audit. No second
cache contract should be introduced without first documenting why the current
one cannot be extended safely.

## Preparation UX

The preparation screen should communicate useful progress without implying that
the device is blocked. A future flow may use wording such as:

```text
Preparing manga
112 / 182 pages · 62%
Cached: 31.4 MB
About 2m 35s remaining
[ Stop for now & Read ]
```

The action must not be called **Cancel**, because completed work is retained.
When the user returns later, the state should make the remaining work clear:

```text
Manga preparation
112 / 182 pages ready
70 pages remaining
[ Resume Preparing ]
[ Start Reading ]
```

Persist enough information for safe resume and useful status display:

- prepared page count;
- total page count;
- percentage;
- remaining page count;
- prepared cache size;
- an ETA or bounded estimate when available;
- source/cache identity and preparation version;
- interrupted or failed state when needed for safe recovery.

The UX must not hide the direct-reading path. Starting reading must be possible
without completing preparation.

## Partial preparation behavior

For example, if 112 of 182 pages are prepared:

- Those 112 prepared pages remain usable.
- **Start Reading** opens the book immediately.
- Prepared pages use their prepared/native cache.
- Unprepared pages fall back transparently to ordinary CBZ rendering and the
  existing approximately three-page read-ahead.
- Resume preparation continues from the existing progress instead of
  restarting.
- A failed or missing prepared page must not invalidate unrelated completed
  pages.
- If the prepared cache is removed or fails validation, direct CBZ rendering
  remains the fallback wherever possible.

## Browser-side preparation

Investigate whether Nooir's web interface should perform some expensive CBZ
preprocessing:

- Browser-side resizing and normalization where practical.
- Device-aware output for X3 and X4.
- Reuse or extension of the existing progressive-JPEG handling where
  appropriate.
- Benchmark browser preparation against on-device preparation before choosing
  the final architecture.
- Consider web controls for starting, monitoring, pausing, resuming, and
  managing preparation.

Browser-side work must not become a hidden requirement. A device receiving a
CBZ without browser preparation must retain the normal direct-reading path.

## Cache lifecycle and storage

The future cache design must address the complete lifecycle:

- Whole-book prepared cache.
- Visible prepared-cache size.
- Removal of prepared data without deleting the original CBZ.
- Retention of completed work after interrupted preparation.
- Protection against prepared manga silently consuming all SD storage.
- Defined finished-book cleanup behavior.
- A completion/end-of-reading prompt may offer to keep or clear prepared data
  and show how much space would be freed.
- Clear-reading-data and per-book cache actions must have explicit, separate
  effects on source files, reading progress, prepared pages, ordinary page
  cache, bookmarks, clippings, and statistics.
- Cache writes must be atomic or otherwise recoverable so a power loss cannot
  make a valid source appear prepared when it is not.

Storage limits, eviction policy, user prompts, and the exact cleanup trigger
must be decided from measurements on real SD cards and both device profiles.

## Reliability and torture testing

The future work must investigate the reported occasional CBZ page becoming
non-responsive or requiring a restart. Required testing includes:

- Long reading sessions with stable memory.
- Rapid forward and backward navigation.
- Sleep/wake and reopen/resume.
- Malformed or corrupt archive entries.
- Missing or corrupt prepared cache.
- Interrupted preparation and power loss.
- Insufficient SD space.
- Cache version mismatch and source replacement.
- Large, progressive, baseline, and unusual JPEG inputs.
- Mixed page sizes and aspect ratios.
- Pan/zoom boundary controls without accidental page changes.

Preparation or cache failure must never make the original CBZ unreadable when
direct rendering is still possible. Every failure path must release decoder,
file, framebuffer, and cache ownership before foreground reading resumes.

## Compact reader information

The compact reader information area should retain the information useful during
manga reading without compromising page content:

- Battery.
- Book/chapter identifier where useful.
- Current page / total pages.
- Reading percentage.

The exact placement and refresh behavior should follow the active X3/X4 safe
area and the selected view mode.

## Profiling requirement

Do not begin future implementation by rewriting CBZ or assuming a new
architecture is needed. First profile:

- archive access;
- JPEG decoding;
- resizing and downscaling;
- rendering and display refresh;
- SD reads and writes;
- cache lookup and cache write;
- existing approximately three-page prefetch/read-ahead;
- foreground navigation latency;
- rapid flipping behavior;
- RAM, largest free block, and ownership transitions.

The profiling result must identify which bottlenecks actually exist on X3 and
X4 before architecture or performance changes are selected. Any proposed
change must preserve X3 safety: bounded memory, serialized foreground and
background work, conservative SD activity, recoverable cache writes, and no
unbounded storage growth.

## Future implementation order

This order is a plan for a later development phase and is not being executed
by the current 1.6.1 EPUB checkpoint:

1. Baseline and torture testing.
2. Profile the existing approximately three-page read-ahead and cache.
3. Reader/view UX, including modes, controls, pan boundaries, and compact
   status information.
4. Manga image pipeline and scaling/refresh comparison.
5. Normal read-ahead improvements.
6. **Prepare for Nooir** prototype.
7. Browser preprocessing investigation and benchmark.
8. Stop/Resume persistence.
9. Storage and cache management.
10. Cleanup and finished-book policy.
11. Final torture testing on X3 and X4.

The plan remains deferred until the EPUB quality phase is complete and the
CBZ-specific audit confirms the smallest safe extension of the existing reader.
