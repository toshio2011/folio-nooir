# Folio Nooir PDF + FB2 feasibility investigation

**Status:** investigation complete; implementation intentionally not started
**Investigation date:** 2026-08-24
**Target firmware line:** Folio Nooir 1.6.0 development line
**Nooir baseline:** branch `feat/cbz-persistent-read-ahead`, commit `1feb5ae1ccae738dd6a5620d2b0280cef7020e8d`
**InkPointX comparison source:** `dev` branch, commit `794f435c34a80df32e814487f43da140c840d7b8` (shallow, read-only checkout outside the repository)

This report is deliberately source-led. It separates what is present in code from documentation claims and from recommendations for a future implementation.

## Evidence labels

- **OBSERVED** — directly confirmed in the checked-out source, configuration, or existing build artifacts.
- **MEASURED** — a value obtained from a command or artifact during this investigation.
- **INFERRED** — a conclusion drawn from observed architecture; it still needs validation when implemented.
- **PROPOSED** — a recommended future scope or design decision, not an implemented feature.
- **UNKNOWN** — not established by the available source or by a runnable test in this environment.

## Executive decision

| Format | Feasibility | Recommended order | Main reason |
|---|---|---:|---|
| FB2 | **Feasible with moderate integration work** | 1 | InkPointX already converts FB2 through a streaming two-pass parser into the existing EPUB package/reader model. The parser and Expat add less device-specific complexity than PDF rasterization. |
| PDF | **Feasible, but conditional and high risk** | 2 | InkPointX has a bounded PDFio adapter and native-width rasterizer, but PDF is a large input space. Memory fragmentation, unsupported PDF content, conversion time, SD wear, and flash budget all need device/corpus validation. |

**PROPOSED:** implement FB2 first, stabilize the shared loose-EPUB cache contract and format-independent integration, then add the bounded PDF path behind the same reader boundary. Do not port the two features as a single undifferentiated change.

The useful InkPointX technique is not a second reader. Both formats are converted into an unpacked EPUB-compatible package and then passed to the existing EPUB metadata, section pagination, rendering, progress, bookmark, statistics, and settings paths. That keeps format-specific work in conversion and cache validation instead of multiplying reader activities.

## 1. Baseline and investigation boundary

### Nooir working tree

**OBSERVED:** before this report was created, the working tree already contained the requested active version update in exactly four tracked files:

- `platformio.ini` — active `version = 1.6.0`.
- `README.md` — current release-candidate display changed to `v1.6.0`; historical release sections remain unchanged.
- `HANDOFF.md` — active development-line and next-release references changed to 1.6.0.
- `PROJECT_CONTEXT.md` — active development-line and next-release references changed to 1.6.0.

Historical changelog/release references were not changed. The existing scratch directories and build logs are pre-existing untracked working-tree material and were preserved.

**OBSERVED:** no Nooir PDF/FB2 implementation exists on this branch. This investigation adds only this document; it does not add libraries, PlatformIO configuration, source files, reader routes, translations, or tests.

### Comparison source

**OBSERVED:** the InkPointX `dev` checkout contains `lib/Fb2`, `lib/Pdf`, `lib/Pdfio`, and the reader/library changes that use them. The public development repository is [InkPointX on GitHub](https://github.com/yokki-vans/InkPointX/tree/dev). Its README is useful for feature intent, but this report treats source behavior as authoritative when the README and code differ.

**UNKNOWN:** the shallow checkout does not establish the complete commit history or the exact upstream-to-vendored diff for every PDFio source file. The local library metadata and source headers are recorded below; provenance questions that require a full history audit remain open.

## 2. Current Nooir architecture and integration gaps

### Format recognition and opening

**OBSERVED:** `lib/FsHelpers/FsHelpers.cpp:166-176` recognizes EPUB, XTC/XTCH, CBZ, TXT, and Markdown. There are no active `hasFb2Extension` or `hasPdfExtension` helpers.

**OBSERVED:** `src/activities/reader/ReaderActivity.cpp:50-239` routes BMP, CBZ, XTC, TXT/Markdown, and otherwise EPUB. The current EPUB path can lend the framebuffer during the first cache/index build (`ReaderActivity.cpp:75-87`), which is a directly reusable memory-management pattern for conversion work.

**OBSERVED:** `ActivityManager::goToReader` provides the common reader entry point, and `HomeActivity` sends selected library paths there. The reader boundary is therefore a good place for format dispatch, but format recognition must happen before the current “otherwise EPUB” fallback.

### Library, shelf, metadata, and web surfaces

**OBSERVED:** the format list is repeated across several independent surfaces:

| Surface | Current evidence | PDF/FB2 implication |
|---|---|---|
| Shelf/library membership | `src/activities/home/FolioLibraryActivity.cpp:45-54` | Both extensions must be admitted to filtered book lists. |
| Directory listing | `FolioLibraryActivity.cpp:158-180` | Both extensions must be visible in the shelf/file list, not only openable from a raw path. |
| Shelf metadata/cover | `FolioLibraryActivity.cpp:318-418`, `:649-666`, `:731-766` | Metadata extraction must have cached-only and full-build paths; cover generation must not block selection indefinitely. |
| Recent books/home | `src/RecentBooksStore.cpp:74-147`, `src/activities/home/HomeActivity.cpp` | A successful open must add the original source path and retain title/author/cover/synopsis. |
| Book actions | `FolioLibraryActivity.cpp:1036-1120` | EPUB-only bookmarks/clippings actions should remain format-specific unless the generated package has a safe annotation contract. |
| Web book classification | `src/network/CrossPointWebServer.cpp:103-107` | Upload/list/book APIs currently classify only EPUB/XTC/CBZ/TXT/Markdown. |
| Web metadata | `CrossPointWebServer.cpp:627-658` | PDF/FB2 metadata must be available without forcing a full reader open, or the web path should remain filename-only in v1. |
| Cache clearing | `src/util/BookCacheUtils.cpp:48-87` | New cache prefixes and progress-preserving clear behavior must be defined. |
| MIME serving | `src/network/WebDAVHandler.cpp:801-812` | PDF MIME is already present for WebDAV, but that is not reader/library support. FB2 MIME is not a reader implementation. |
| Visual classification | `src/components/UITheme.cpp:146-151` | PDF/FB2 need book-card classification if they appear in recent/library views. |

**INFERRED:** adding only `ReaderActivity` branches would create a partially supported format: it could open from a manually supplied path while remaining invisible or filename-only in the library, recent books, web shelf, cache tools, or metadata flows.

### Existing cache and state contracts

**OBSERVED:** Nooir already separates source-format caches from global annotation/state stores:

- EPUB cache is keyed by the source path and contains `book.bin`, CSS/section/page data, cover/thumbnail files, and reader `progress.bin`.
- CBZ has a persistent page-cache manifest and per-page files in `src/util/CbzPageCache.cpp`, with bounded prefetch in `src/activities/reader/CbzReaderActivity.cpp:42-60` and `:165-194`.
- `BookStateStore::recordReading` (`src/BookStateStore.cpp:59-79`) persists status, percentage, elapsed time, sessions, and pages turned.
- `ReadingStatsStore::recordSession` (`src/ReadingStatsStore.cpp:59-78`) persists daily totals.
- `RecentBooksStore::recordReading` (`src/RecentBooksStore.cpp:120-147`) persists recent-book progress and statistics.
- `BookmarkFile` and `ClipFile` live outside per-book caches, as documented by `BookCacheUtils.cpp:64-68`.
- Reader resume uses the generic `APP_STATE.openEpubPath` field even for TXT/CBZ (`TxtReaderActivity.cpp:47-52`, `CbzReaderActivity.cpp:216-218`); the name is historical, but the value is a path.

**INFERRED:** a generated PDF/FB2 package can inherit most reader behavior if the source path remains the key passed to `Epub`, `RecentBooksStore`, `BookStateStore`, and the global statistics store. The cache layer must not accidentally key state to the generated package path.

## 3. InkPointX shared conversion architecture

The observed flow is:

```text
source file on SD
      │
      ├─ FB2: Expat streaming scan + render pass
      └─ PDF: PDFio callbacks + per-page extraction/rasterization
      │
      ▼
/.crosspoint/epub_<hash>/package/
  META-INF/container.xml
  OEBPS/content.opf, toc.ncx, style.css
  OEBPS/text/*
  OEBPS/images/*
      │
      ▼
Epub(..., looseItemRoot=package).load(...)
      │
      ▼
existing EpubReaderActivity / Section / Page / annotations / progress
```

**OBSERVED:** InkPointX `lib/Epub/Epub.h:21-24` and `Epub.cpp:844-862` support an optional unpacked package. Item reads bypass ZIP access and read ordinary SD files. `BookMetadataCache.cpp:219-325` also explicitly avoids opening a ZIP for loose packages.

**OBSERVED:** the FB2 and PDF constructors both use an `epub_<hash(path)>` cache directory and a `package` child directory. Their generated OPF/NCX files are intentionally shaped for the existing EPUB parser.

**INFERRED:** this is the lowest-risk reuse boundary for Nooir. It does not mean that every EPUB feature automatically works: generated HTML must match the existing slim parser’s supported markup, and cache/progress invalidation must preserve user state.

## 4. FB2 deep investigation

### Source flow

**OBSERVED:** `lib/Fb2/Fb2.cpp:250-320` performs the following:

1. Verify the source exists and compute a source size plus sampled fingerprint.
2. Accept an existing package only when `fb2_package.bin` has the expected package version, source signature, and positive chapter count.
3. Otherwise remove/recreate the format cache.
4. Run `parseSource(ParsePass::Scan)`.
5. Run `parseSource(ParsePass::Render)`.
6. Write the loose EPUB container, stylesheet, OPF, and NCX.
7. Save metadata/signature and remove temporary TOC/anchor records.

**OBSERVED:** `parseSource` reads 4 KiB chunks (`Fb2.cpp:14`, `:365-404`) through Expat callbacks. It does not load the FB2 document into one heap buffer.

### Scan pass

**OBSERVED:** the scan pass collects:

- book title, language, and author components;
- cover image reference;
- `<binary>` image IDs and supported JPEG/PNG media types;
- body/section/chapter boundaries;
- TOC titles and levels;
- anchors for later cross-chapter links;
- chapter byte estimates used to split very large sections.

**OBSERVED:** an individual generated chapter is capped at `MAX_CHAPTER_TEXT_BYTES = 48 * 1024` at safe direct-paragraph boundaries (`Fb2.cpp:14-19`, `:636-662`). This prevents a common “entire novel in one FB2 section” shape from becoming one large EPUB section.

### Render pass and feature coverage

**OBSERVED:** the render pass streams XHTML directly to SD. It supports, in the observed callback implementation (`Fb2.cpp:700-888`):

- nested sections and headings;
- paragraphs, subtitles, emphasis, strong, strike, code, superscript, and subscript;
- poems, stanzas, annotations, cites, epigraphs, verse, text authors, and dates;
- empty lines;
- embedded images decoded from FB2 base64 `<binary>` data;
- intra-document anchors and links, including links resolved to another generated chapter;
- tables and ordered/unordered lists.

**OBSERVED:** `Fb2Encoding` handles UTF-8 pass-through plus several single-byte encodings, and `unknownEncoding` maps Expat’s 256-byte table (`Fb2.cpp:906-943`). The host tests include CP1251, KOI8-R, Windows-1252, ISO-8859-1, ISO-8859-5, CP866, UTF-8, BOM, and unsupported-encoding cases.

**INFERRED:** FB2 is a good first candidate for Nooir because its hard part is bounded streaming XML-to-XHTML conversion, while typography, pagination, images, TOC navigation, and reader controls can remain in the existing EPUB pipeline.

### FB2 cache and invalidation risks

**OBSERVED:** the package signature is version 4 plus source size, sampled fingerprint, and chapter count (`Fb2.cpp:18-20`, `:185-206`).

**OBSERVED:** cache creation starts by removing the entire `epub_<hash>` directory (`Fb2.cpp:275-286`). Nooir’s EPUB progress cache is also inside a per-book cache directory. The external bookmark/clipping stores are safer, but the in-cache resume position is not automatically protected by this operation.

**INFERRED:** a direct port must preserve the user’s reading position across a converter-version or source-change rebuild. The future cache clear/rebuild path should either preserve and restore the source-path progress record or place converter artifacts in a child directory that can be replaced without deleting progress.

**UNKNOWN:** whether every FB2 package seen in the target corpus uses only the encodings and tags exercised by the InkPointX tests. Namespaced tags, malformed but tolerated XML, external entities, very large binary images, and unusual link forms require corpus testing.

### FB2 failure matrix

| Failure | InkPointX behavior observed | Nooir v1 recommendation |
|---|---|---|
| Missing source | `load()` fails | Show the existing book-open failure UI and leave library state unchanged. |
| Malformed XML | Expat parse error; package build fails | Fail atomically; remove only incomplete converter outputs. |
| Unsupported legacy encoding | `unknownEncoding` returns an Expat error | Show a clear unsupported-encoding failure; do not silently corrupt text. |
| Invalid base64 or failed image write | Binary output is closed/deleted on failure | Keep the book readable only if the policy explicitly allows missing images; otherwise fail with a visible error. |
| No readable chapters | Scan rejects zero chapters | Fail rather than creating an empty reader package. |
| More than `UINT16_MAX` chapters | Build rejects the source | Fail with a bounded diagnostic. |
| SD full or write failure | Build returns false and removes the cache directory | Preserve prior progress and avoid leaving a package that looks current. |
| Same-size source replacement | Sample fingerprint is intended to catch it, but is not cryptographic | Accept as a practical cache key; document residual collision risk. |

### Proposed FB2 v1 scope

**PROPOSED — include:** `.fb2` recognition in the file/library/web paths; title/author/language metadata; JPEG/PNG cover and inline images; the observed core FB2 text/structure tags; UTF-8 and tested single-byte encodings; TOC; in-book anchors; shared EPUB pagination and reader settings; recent/book-state/global reading statistics; cache clear; visible open/build errors; and cache invalidation that preserves resume state.

**PROPOSED — defer:** external resources, encrypted/protected content, arbitrary unsupported encodings, a dedicated FB2 bookmarks/clippings UI, semantic footnote conversion beyond ordinary in-book links, and full conformance claims for every FB2 namespace/tag.

## 5. PDF deep investigation

### Dependency and I/O model

**OBSERVED:** `lib/Pdfio/library.json` identifies a local `PdfioReadOnly` library as version `1.6.4-x4`, “Memory-tuned read-only PDFio 1.6.4 with callback I/O for XTEINK X4”, licensed Apache-2.0, with `PDFIO_READ_ONLY=1`, `PDFIO_PNGDEC_ZLIB=1`, and section-garbage-collection flags.

**OBSERVED:** `lib/Pdf/Pdf.cpp:163-261` supplies PDFio with SD-backed read/seek/close callbacks and a persistent temporary xref file. The xref file is extended sequentially because SdFat rejects seeking beyond EOF. The document is reopened per page after the first page so PDFio’s loaded page dictionaries/resources/fonts do not accumulate across a large document.

**OBSERVED:** PDF conversion is foreground/synchronous. `PdfRuntime.h` temporarily removes the current task from the ESP32 watchdog during conversion and services it from bounded loops, then restores the subscription.

**INFERRED:** this avoids a full PDF document buffer, but it does not make conversion interactive. A large or pathological PDF can keep the reader task busy for a long time and may write many SD files before the first page is available.

### Cache lifecycle

**OBSERVED:** `Pdf.cpp:17-30`, `:277-300`, and `:331-376` use:

- package version 11;
- source size and sampled fingerprint;
- page count;
- raster width and height;
- `pdf_package.bin` plus metadata and loose EPUB package files.

The cache is current only when the source signature, package version, and device raster geometry all match.

**OBSERVED:** `Pdf.cpp:379-528` creates a fresh package, records images, writes each page, closes the temporary xref, writes OPF/NCX/style, saves metadata/signature, and removes the image-record index. If a stale cache directory cannot be removed, it is quarantined under `.stale1` through `.stale16` before a fresh build is attempted.

**INFERRED:** geometry-aware invalidation is essential for an SD card moved between X3 and X4. The generated package must remain keyed to the original PDF path so reader progress and recent-book state continue to refer to the source, not to a device-specific generated filename.

### Page conversion modes

**OBSERVED:** each page is handled as follows:

1. Ask `PdfRasterizer::pageNeedsRasterization` whether path painting or Form XObjects require a graphics pass.
2. If needed, rasterize at active portrait geometry into caller-provided framebuffer scratch and write a monochrome PNG.
3. If rasterization cannot handle the page, fall back to DCT/JPEG image extraction and PDF text-token extraction.
4. Write one XHTML page per PDF page.
5. If no usable content remains, write an explicit unsupported-content notice instead of publishing a blank page.

**OBSERVED:** the rasterizer is deliberately bounded (`PdfRasterizer.cpp:21-33`, `:1246-1324`): maximum width 528 pixels, bounded path/contour counts, maximum form/glyph/graphics-state depths, a 256 KiB embedded-font cap, and a small SD-backed font read cache. It supports vector paths, Bézier curves, selected graphics operators, Form XObjects, and embedded TrueType outlines, but it is not a general PDF viewer.

**OBSERVED:** the caller lends the framebuffer. InkPointX configures X4 as 480×760 and X3 as 528×752 in the host harness, and the reader uses the active display geometry minus reader chrome (`ReaderActivity.cpp:189-198`). A small path-point heap allocation can still be needed when the framebuffer tail is insufficient (`PdfRasterizer.cpp:1290-1305`).

**OBSERVED:** PDF text fallback uses PDF font names, ToUnicode CMaps, glyph-name mappings, bounded CMap entries, and tokenized content streams (`Pdf.cpp:677-868`, `:934-1080`). It is a best-effort text extraction path, not faithful layout preservation.

**INFERRED:** “PDF support” here means a useful fixed-layout reading fallback, not full fidelity. Music scores, diagrams, forms, vector pages, and scanned pages need different code paths; any v1 claim must state the supported subset and the explicit fallback behavior.

### PDF reader UI

**OBSERVED:** InkPointX routes a PDF through the same `EpubReaderActivity` after conversion (`src/activities/reader/ReaderActivity.cpp:183-224`, `:304-311`). It stores a PDF zoom preference in `pdf_view.bin`, with 100/125/150/200% options (`EpubReaderActivity.cpp:63-64`, `:1128-1167`). `PdfViewport.h` and its host tests calculate overlapping tiles that reach the page edges.

**OBSERVED:** only a single-image page enters the tiled PDF viewport path (`EpubReaderActivity.cpp:1554-1585`). Text/reflow fallback pages continue through the regular EPUB renderer.

**INFERRED:** the zoom preference and tile navigation should be treated as PDF-specific state. It should not be added to ordinary EPUB/FB2 reader menus unless the current document is known to be a raster-backed PDF page.

### PDF failure matrix

| Failure | InkPointX behavior observed | Nooir v1 recommendation |
|---|---|---|
| Missing/ unreadable PDF | `load()` fails with an error string | Visible open failure; no recent/state record on unsuccessful open. |
| PDFio open/xref failure | Temporary xref is removed and conversion fails | Clean temporary files and preserve any old valid package until replacement is complete. |
| Page count zero or over 4096 | Conversion fails | Enforce a hard page limit in v1. |
| Unsupported vector complexity | Rasterizer returns a bounded capability error | Try extraction fallback; otherwise render a visible unsupported-content page and log the reason. |
| JPEG image XObject | Extracted to SD and referenced from generated XHTML | Preserve supported JPEGs; define behavior for PNG/JPX/CCITT/JBIG2/soft masks. |
| Text font without usable mapping | Best-effort decoder/fallback text | Accept degraded text only when the page still has useful content; never claim layout fidelity. |
| Encrypted/password PDF | No password UI or v1 policy was observed in the adapter | **PROPOSED:** reject clearly in v1 unless password handling is designed and tested. |
| Conversion reset/watchdog | Watchdog is serviced; cache is rebuilt on next open | Use atomic completion markers and quarantine/cleanup; do not treat a partial package as current. |
| Flash/heap pressure | Not measured in Nooir because the source was not ported | Require both build-size gate and device conversion corpus before approval. |

### Proposed PDF v1 scope

**PROPOSED — include:** `.pdf` recognition; source metadata when available; bounded native-width conversion; cached page package; JPEG extraction; supported vector/Form rasterization; best-effort text fallback; original PDF path in recent/book-state/statistics; visible conversion progress; geometry-aware cache invalidation; PDF-only zoom for raster-backed pages; and explicit unsupported-page/error behavior.

**PROPOSED — defer:** password/encrypted PDFs; faithful annotations and forms; PDF outlines/bookmarks as a separate navigation model; arbitrary image filters such as JPX/JBIG2/CCITT until independently validated; full transparency/soft-mask fidelity; selectable text; reflow mode; and a guarantee that every PDF page is visually faithful.

## 6. Cross-cutting integration plan

| Integration point | Current Nooir state | InkPointX technique | Recommended Nooir v1 treatment |
|---|---|---|---|
| Detection | No FB2/PDF extension helpers | Dedicated helpers and reader branches | Add format helpers and route before EPUB fallback. |
| Library visibility | Five book formats are hard-coded | Library advertises EPUB/FB2/PDF | Add both extensions to shelf filters, directory lists, search, and web book classification. |
| Metadata | EPUB/XTC/CBZ extractors; TXT/MD filename-first | Converter writes title/author/language metadata cache | Add cached-only metadata access where practical; otherwise use filename-first until the cache exists. |
| Covers | Existing EPUB/CBZ/XTC thumbnail pipeline | Generated OPF marks cover image | Reuse EPUB cover generation from the loose package; verify PDF first-page fallback separately. |
| Reader | Separate native readers plus EPUB reader | FB2/PDF become `Epub` with `looseItemRoot` | Prefer the common EPUB reader, with PDF-specific zoom/progress state only. |
| Progress | Per-reader progress plus global stores | Shared EPUB reader progress | Keep source path as identity; preserve progress during package rebuild. |
| Bookmarks/clippings | Menu actions currently EPUB-only | Shared reader supports some annotations | **PROPOSED:** FB2/PDF may inherit page bookmarks only after path/position stability is proven; keep clippings EPUB-only in v1. |
| Cache clear | `BookCacheUtils` has format-specific branches | Converter cache shares `epub_` prefix | Add a format-aware clear path that preserves progress and external annotations. |
| Sleep/resume | `openEpubPath` stores the opened source path | PDF restart persists source path before silent restart | Use the existing path field but audit boot resume routing for converter-in-progress and post-build restart. |
| WebDAV | PDF MIME already exists; reader classification does not | InkPointX source advertises more book formats | Add upload/list metadata only when it does not force a blocking conversion; reader support must not be inferred from MIME. |
| Failure UX | EPUB/CBZ show loading/indexing feedback | InkPointX shows PDF preparation progress and FB2 errors | Use bounded, cancellable or clearly acknowledged progress; do not leave a blank screen during long conversion. |

## 7. Memory, flash, SD, and performance assessment

### Firmware flash

**MEASURED:** the existing Nooir default build artifact `.pio/build/default/firmware.bin` is 5,948,928 bytes. The active `app0`/`app1` partition size in `partitions.csv:4-5` is 0x640000 = 6,553,600 bytes. This is **90.77%**, leaving **604,672 bytes** in the OTA slot.

**MEASURED:** raw source size in the InkPointX checkout is approximately:

- `lib/Pdfio/src`: 446,166 bytes;
- `lib/expat`: 592,868 bytes;
- `lib/Pdf` adapter/rasterizer: 111,070 bytes;
- `lib/Fb2`: 61,846 bytes.

These are source bytes, not linked firmware size. They are included to show scale only; dead-code elimination and compiler/linker behavior determine the actual flash increase.

**INFERRED:** the current 604,672-byte headroom is tight for adding both a PDF parser/rasterizer and a new XML parser if all code paths link into the firmware. A future implementation must build the real target early, inspect the map, and keep a hard flash gate. It is not safe to assume the source checkout’s full feature set fits.

### Runtime memory

**OBSERVED:** Nooir’s existing EPUB first-index path lends the display framebuffer (`ReaderActivity.cpp:81-87`). InkPointX’s PDF path uses the same idea and passes roughly 48 KiB/52 KiB of framebuffer scratch to rasterization, with a small additional path buffer when needed.

**OBSERVED:** PDF conversion can simultaneously involve PDFio object state, xref buffering, font decoder tables, PDF token buffers, raster pixels, path points, and temporary font files. InkPointX explicitly unloads SD fonts and restarts after a newly built PDF package (`ReaderActivity.cpp:30-62`, `:199-218`) because total free heap was not enough to guarantee a large contiguous decoder allocation after conversion.

**OBSERVED:** FB2’s parser uses 4 KiB XML chunks and streams generated XHTML/images to SD, but it still holds metadata, image descriptors, anchors, and current-output state in RAM. Its memory profile is expected to be materially simpler than PDF’s, but no Nooir measurement exists.

**PROPOSED:** instrument minimum free heap and largest allocatable block at: converter start, parser allocation, first page/chapter, image decode, package completion, and reader handoff. Record X3 and X4 separately. A total-free-heap number alone is insufficient because PNG/image/PDF allocations need contiguous blocks.

### SD storage and wear

**OBSERVED:** both converters produce one or more derived files per source book and use temporary files during cache creation. PDF raster pages can be close to a full panel’s worth of monochrome samples per page before filesystem overhead; extracted JPEGs add source-sized copies. FB2 binary images are decoded from base64 into separate files and each generated chapter is stored separately.

**INFERRED:** conversion is write-heavy and should be one-time per source/geometry/parser version. It must use completion signatures, temporary names, cleanup after reset, and no repeated page-by-page regeneration during ordinary reading.

**PROPOSED:** add a cache-size policy and telemetry in the future implementation. At minimum, log source size, generated package bytes, page/chapter count, conversion duration, cache hit/miss, and failure phase. Do not put converter progress into the ordinary page-turn path.

### Performance and available validation

**OBSERVED:** InkPointX includes:

- `test/pdf_native/PdfHarness.cpp`, which builds a host PDF adapter harness and accepts `PDF CACHE_BASE [x3|x4]`;
- `test/pdf_viewport/PdfViewportTest.cpp`, which checks 100/125/150/200% geometry and edge coverage;
- `test/fb2_encoding/Fb2EncodingTest.cpp` and `Fb2ParserTest.cpp`;
- CMake registration for the native PDF harness and FB2 tests in `test/CMakeLists.txt`;
- README release-validation language mentioning host tests, PDF conversion checks, and a flash budget.

**MEASURED:** those InkPointX host tests were not run because `cmake` is not installed/available in this Windows PowerShell environment. Therefore there are no new host pass results, conversion timings, or corpus measurements to report.

**OBSERVED:** Nooir has an SDL2 X3/X4 simulator documented in `docs/simulator.md`, with virtual SD support and normal reader/activity flow. The documentation requires WSL/Linux and SDL2 prerequisites. No simulator run was performed here, and no PDF/FB2 corpus is currently available in Nooir’s documented virtual SD setup.

**UNKNOWN:** real conversion time, minimum heap, largest allocation, SD package size, page fidelity, and failure rate on target X3/X4 hardware. These cannot be derived responsibly from source inspection.

## 8. Dependency and licensing notes

| Component | Evidence | License/provenance status |
|---|---|---|
| PDFio | `lib/Pdfio/library.json`, `lib/Pdfio/src/pdfio.h`, `LICENSES/PDFio-LICENSE.txt`, `LICENSES/PDFio-NOTICE.txt` | Apache License 2.0 with the local PDFio notice/exception text. The local metadata calls it a memory-tuned read-only 1.6.4-X4 build. Exact local modifications/commit provenance are **UNKNOWN**. Upstream reference: [PDFio 1.6.4 release](https://github.com/michaelrsweet/pdfio/releases/tag/v1.6.4). |
| Expat | `lib/expat/library.json` and source headers | Version 2.7.3, MIT notices in source headers. The upstream project documents the 2.7.3 release and security fixes in its [release history](https://github.com/libexpat/libexpat/blob/master/expat/Changes). |
| FB2 converter | `lib/Fb2/library.json`, `lib/Fb2/Fb2*.{h,cpp}` | No per-library license field or separate copyright/notice was observed. The InkPointX root is MIT, but the precise ownership/attribution for the FB2-specific implementation should be confirmed before copying it. |
| Project integration | InkPointX root `LICENSE` | InkPointX root project license is MIT. Any port must retain the required third-party notices and correctly describe local modifications. |

**PROPOSED:** before implementation is merged, perform a full license/provenance audit of the exact PDFio bundle and FB2 converter commit, record attribution in the Nooir license/notice inventory, and do not rely only on a `library.json` field.

## 9. Recommended implementation sequence after approval

This sequence is intentionally not being executed in this investigation.

1. **FB2 foundation:** add extension recognition and a streaming FB2-to-loose-EPUB converter, but first define a cache layout that does not delete progress on converter invalidation.
2. **Shared integration:** wire shelf/library/recent/web metadata/cover/cache clear/open-failure behavior for FB2; verify the original source path remains the identity everywhere.
3. **FB2 validation:** run host parser/encoding tests, the Nooir simulator with UTF-8 and legacy-encoding fixtures, image/cover/TOC/link cases, malformed input, cache replay, source replacement, and resume/book-state/statistics checks.
4. **Flash/memory gate:** build X4 and X3 environments, record linked size, free heap, largest block, and cache size before adding PDF.
5. **PDF dependency gate:** import only the required PDFio subset and preserve its notices. Build the host harness before reader routing so PDF parsing can be tested independently.
6. **PDF bounded path:** add PDFio callback I/O, xref cache, per-page reopen, native geometry, raster scratch lending, JPEG/text fallback, package signatures, and visible progress.
7. **PDF-specific UI:** add zoom/tile state only for raster-backed pages; keep ordinary EPUB/FB2 pagination and menu behavior unchanged.
8. **PDF corpus validation:** test text PDFs, scanned JPEG PDFs, vector scores/diagrams, mixed image/text, rotated/cropped pages, malformed files, encrypted/password files, unsupported filters, huge page counts, and interrupted/full-SD conversions on both device profiles.
9. **Release gate:** require host tests, simulator smoke tests, physical X3/X4 tests, flash headroom, cache replay, restart recovery, and no regression in existing EPUB/CBZ/XTC/TXT/Markdown flows.

## 10. Final recommendation

**FB2:** approve for a focused v1 implementation after the cache/progress contract is settled. It has a clear streaming design, bounded parser input, manageable integration surface, and a strong reuse story through the current EPUB reader.

**PDF:** approve only as a bounded-subset feature with explicit unsupported-content behavior and a hard resource gate. The InkPointX implementation is a credible starting point, especially its framebuffer lending, xref persistence, per-page reopening, native-geometry cache signature, and restart-after-build technique. It is not evidence that arbitrary PDFs are supported or that the feature fits Nooir’s current OTA slot without measurement.

**Order:** FB2 first, PDF second. The investigation stops here pending explicit approval for implementation.
