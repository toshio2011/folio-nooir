<img width="324" height="450" alt="Folio Nooir on Xteink X4" src="https://github.com/user-attachments/assets/cf0a9cd3-6783-417a-986c-0ec2fdf89ba4" />

<img width="241" height="402" alt="1" src="https://github.com/user-attachments/assets/4f2836ea-894f-4850-8af2-862581fbaeb4" />
<img width="240" height="403" alt="2" src="https://github.com/user-attachments/assets/7fcc16d9-1e15-4c8c-ace4-ead43df7bb34" />
<img width="243" height="404" alt="3" src="https://github.com/user-attachments/assets/34d89a00-2bdc-4e59-bfd9-c7a7442c87da" />
<img width="244" height="416" alt="4" src="https://github.com/user-attachments/assets/2f819edf-f09a-4b37-8a51-136402d751ee" />
<img width="241" height="401" alt="5" src="https://github.com/user-attachments/assets/75ee65a0-ccb0-4862-a8a6-b61be1b58329" />
<img width="239" height="416" alt="6" src="https://github.com/user-attachments/assets/362576fa-eb2c-44d0-a519-60b02781dda6" />
<img width="240" height="406" alt="7" src="https://github.com/user-attachments/assets/aa0f3fd7-c5d0-42e1-8f37-2afa92d8e106" />
<img width="239" height="401" alt="8" src="https://github.com/user-attachments/assets/8ed8ec97-3cdb-4094-a130-11c32b3f0936" />
<img width="240" height="401" alt="9" src="https://github.com/user-attachments/assets/0896b309-f380-4667-90f4-36d7f6ef04c4" />
<img width="239" height="403" alt="10" src="https://github.com/user-attachments/assets/c075cd38-3501-4492-bb75-04e490f788fe" />

# Folio Nooir

Current stable release: **v1.5.10**. Release candidate: **v1.5.11**.

## Hardware warning

> **Check your panel before flashing.** Folio Nooir has been physically tested
> only on the older Xteink X4 revision available to the maintainer.
>
> The firmware now uses a shared X3/X4 hardware-detection path. Newer X3
> panels are probed before SPI starts for the UC8279d controller; confirmed
> results are cached, an explicit override is respected, and an inconclusive
> probe falls back to the original UC8253 path. X4 keeps the known SSD1677
> path by default, with the newer X4 battery-latch handling retained. The
> optional X4 controller probe is only for separately validated hardware.
>
> This improves compatibility but is not a guarantee for every production
> revision. Keep a known-good recovery image and test carefully. X4 Pro/S3
> hardware is not supported. Do not replace a working CrossInk or CrossPoint
> installation without a recovery path.

Folio Nooir is an experimental, bookshelf-focused e-reader firmware for Xteink
devices. It is a personal fork of [CrossPoint Reader](https://github.com/crosspoint-reader/crosspoint-reader),
keeping the core reader, wireless transfer, sleep, and settings features while
adding a Folio Nooir interface and reading tools.

## Native Simulator

Folio Nooir provides native PlatformIO profiles for `simulator_x4` and
`simulator_x3`. They can test the Nooir UI, library behavior, EPUB rendering
and navigation, and supported simulated inputs without flashing a physical
device. The X3 profile also includes simulated tilt testing. Real-device
testing is still recommended. See the complete [native simulator guide](docs/simulator.md)
for WSL/Linux setup, build/run commands, controls, virtual SD-card use, and
troubleshooting.

## v1.5.11 changes

This release candidate builds on the released v1.5.10 baseline. The main
changes are:

- Persistent CBZ page caching with atomic page publication, manifest
  validation, cache reuse after leaving and reopening a book, and safe
  cleanup of incomplete cache files.
- CBZ background read-ahead of up to three pages on X4 and a conservative
  one-page lookahead on X3, gated by available memory and reader ownership so
  foreground reading remains the priority.
- CBZ cache sharing across Fit Width, Landscape, and Zoom modes, with
  half-screen pan steps for the larger views. A cache miss shows the current
  page/progress state instead of silently blocking the reader.
- Reserved-area cache feedback such as `Preparing page 18/23 | cached 9/23`
  and `> Next ready`; Next, Back, and Confirm cancel or queue around
  background work so repeated input does not appear to freeze the device.
- Explicit cache controls: Reset Progress removes CBZ `progress.bin`; the
  global Clear Reading Data action remains metadata/progress/statistics
  cleanup; per-book Clear Reading Cache removes generated reader pages while
  preserving reading progress.
- Optional web-transfer normalization of progressive CBZ JPEGs to baseline
  JPEGs. Baseline JPEGs, PNGs, and other files are left unchanged, so the
  conversion is only needed for CBZs that show poor grayscale/color output on
  the device.
- Clear exit/saving feedback for XTC and CBZ readers, preserved covers when
  leaving XTC, and end-of-book actions that do not leave the reader appearing
  unresponsive.
- More reliable web To-Do and Clock & Weather actions: duplicate saves/syncs
  are prevented, To-Do refreshes do not interrupt editing, slow network work
  feeds the watchdog, and a temporary station Wi-Fi loss no longer ends the
  web session automatically.
- Power-management transitions now avoid redundant active locks and reduce
  unnecessary CPU-frequency bouncing while preserving the existing sleep and
  deep-sleep behavior.
- Persistent page read-ahead is currently a CBZ feature. EPUB, XTC/XTCH, TXT,
  and Markdown continue to use their existing reader/cache paths.

## v1.5.10 changes

This development cycle adds direct CBZ support and hardens the existing EPUB
reader without changing the normal XTC/TXT reading paths.

- Added direct CBZ/Comic Book reader support with ComicInfo.xml metadata,
  cover and thumbnail caching, Library/Recent/Finished integration, metadata
  retrieval, and Recent updates only when a CBZ is actually opened for reading.
- Added bounded CBZ page indexing and extraction limits for X3/X4, including
  safe handling of oversized archives, long page paths, malformed archives,
  failed images, and temporary extraction cleanup.
- Added CBZ Fit Width, Fit Page, Landscape, Zoom, Reset View, page picker,
  manga RTL/LTR navigation, per-book page bookmarks, and direct bookmark/page
  jumps. Landscape panning remains separate from normal page navigation.
- Added conservative one-page-ahead CBZ prefetch on the validated path, with
  serialized ownership, stale-candidate protection, atomic cache staging,
  queued navigation, and safe fallback rendering. Unsupported hardware stays
  on the conservative path.
- Improved bounded CBZ cache replay and added a shared, static long-operation
  indicator for user-initiated CBZ and EPUB loads without continuous refreshes.
- Hardened EPUB JPEG/PNG rendering with bounded downsampling, overflow and
  geometry guards, progressive/long-Huffman fallback handling, fail-soft image
  placeholders, safer PixelCache replay, and cleanup of incomplete caches.
- Fixed EPUB clipping/highlight restoration across reflow and font changes,
  preserving regular, bold, and mixed-style selections independently from the
  highlight overlay.
- KOReader Sync now accepts all successful HTTP 2xx responses, including
  bodyless successful updates, while retaining existing payload validation.

## v1.5.9 changes

This development cycle starts after the tested v1.5.8 baseline.

- Recent/Finished now reuses a validated Folio shelf snapshot when returning
  from Settings, avoiding unnecessary cover decoding and shelf reconstruction.
- Shelf snapshot validation includes presentation metadata, reading progress,
  and book status, so the fast path cannot display stale progress or metadata.
- The snapshot format was versioned so older frames are rebuilt safely once.

## v1.5.8 changes

This release's change list starts with the **Stable Pages** enhancement.
Earlier v1.5.7 work remains documented in the complete feature list below.

- Added optional **Stable Pages** reader mode. CrossInk-processed EPUBs can
  import `META-INF/x-locations.json`; the firmware stores a compact per-book
  `stable_pages.bin` map beside the book cache.
- Stable-page preparation is streamed and bounded for X3/X4 memory. It shows
  progress, supports stopping, reuses an existing valid map, and releases its
  temporary memory after preparation. EPUBs without a compatible map use the
  safe local fallback instead of changing Current Pages behavior.
- The Stable Pages choice is included in Settings Profiles. The profile saves
  the mode, while each book's `stable_pages.bin` remains in its own cache.
- Recent and Finished shelf startup now checks lightweight metadata only.
  Missing thumbnails no longer trigger automatic image decoding; the shelf
  shows a placeholder and stays responsive.
- **Refresh Book Cache** remains the explicit cover-rebuild action. Manual
  refresh allows bounded sources up to 3 MB and keeps a placeholder when the
  cover format, dimensions, or available heap cannot be safely converted.
- Reader exit now shows a saving state before returning home and avoids an
  unnecessary full state-file rewrite during normal exits, reducing the delay
  when returning to Recent or Library.
- KOReader Sync now prefers portable XPath/percentage mapping, uses rich
  CrossPoint position data as a fallback, supports an editable sync device
  name (default: `Folio Nooir X4`), and keeps generic KOReader payloads
  protocol-compatible.
- EPUB image rendering now rejects invalid dimensions and overflow-prone pixel
  counts, clips unsafe geometry, uses fail-soft placeholders, and protects PNG
  and framebuffer cache writes from out-of-range coordinates. Valid images keep
  the existing fast path.
- Clipping/highlight restoration now survives CSS, font, and line-spacing
  reflow. Existing clipping files remain compatible; saved text is matched with
  the same punctuation/whitespace sanitization used when it is stored, and the
  resolver cache is invalidated after clipping-list edits.

- Added **Edit book metadata** beside EPUB/XTC/TXT/Markdown files in Transfer.
  Title, author, and synopsis edits are stored in a lightweight device-side
  override and are applied to Library/Recent without rewriting the book or
  disturbing reading progress, bookmarks, or clippings.

The following retained Web Transfer behavior is from the earlier baseline and
is not a new v1.5.8 change:

- Web Transfer now yields regularly during sustained uploads/downloads so the Wi‑Fi and SD tasks keep running; the Bookshelf page no longer parses EPUBs or decodes missing covers while it is open.

## Features

### CrossPoint functions retained

Folio Nooir is an interface and feature layer on top of CrossPoint rather than a replacement reader. The existing CrossPoint workflows remain available:

- EPUB, XTC/XTCH, TXT, Markdown, and PDF/file-browser workflows.
- EPUB chapter navigation, footnotes, bookmarks, go-to-percent, auto page turn, orientation control, screenshots, and custom fonts.
- Image preview from the file browser, plus the existing X3 tilt-page-turn path where supported.
- Wi-Fi setup, browser-based file transfer, and the built-in web server.
- OPDS browsing, KOReader Sync, and OTA update support.
- Sleep cover, battery/status screens, SD-card firmware update, and recovery tools.
- One-shot Clock & Weather sync with cached clock/date/weather data; device-started sync powers Wi-Fi back off when finished.
- Persistent To-Do List storage at `/.crosspoint/todo.json` with on-device add, edit, delete, reorder, complete, priority, and clear-completed actions.
- Existing input mappings, themes/settings storage, status-bar controls, localization, and device configuration.
- Settings Profiles for saving, applying, and deleting named device-setting snapshots without copying reading data.
- **Clear Reading Data** clears Recent entries, Book State records, reading statistics, and the Folio shelf snapshot while preserving covers, thumbnails, `metadata.bin`, `book.bin`, bookmarks, clippings, and highlights.
- Per-book **Clear Reading Cache** removes that book's generated reader cache while preserving its saved reading position.

### Folio Nooir bookshelf

- Folio Nooir boot logo and visual theme.
- Direct CBZ/Comic Book support with `ComicInfo.xml` metadata, cover/thumbnail caching, bounded page indexing, and Library/Recent/Finished integration.
- Normal `.cbz` files can be copied directly to the SD card and read without conversion or Web UI preprocessing.
- CBZ metadata retrieval remains lightweight: metadata can be retrieved without treating the book as opened, while Recent is updated only when the CBZ is actually opened for reading.
- Three bookshelf views: **Library**, **Recent**, and **Finished**.
- Library acts as a folder/file browser and loads metadata lazily as books are highlighted.
- Library search is available from the menu and performs fast, case-insensitive filename filtering in the current folder, with an optional recursive **Search All Folders** mode.
- Featured-book panel with cover, title, author, HTML synopsis, progress, status, reading minutes, and session count.
- Compact 4 x 2 cover grid with percentage progress ribbons.
- Right-aligned battery icon and percentage in Library, Recent, and Finished headers, using the existing battery visibility setting.
- Cover cache warm-up with visible retrieval feedback, cache reuse, and invalid/blank-BMP recovery.
- **Retrieve All Book Details** from the Library menu, with streaming metadata progress, a resumable missing-thumbnail pass, selected-book priority, valid-cache skipping, and **Stop for now** support for large SD-card libraries.
- Long-press actions for opening, status changes, progress reset, cache refresh, full synopsis, book statistics, and removing a book from the list without deleting the file.
- Bookmark, clipping, and highlight managers are available from the home menu and book actions; entries can be reviewed, edited, or deleted, and selecting a bookmark opens its book at the saved location.
- Automatic movement to Finished when a book reaches 100%.
- Shelf buttons stay context-aware: Library opens the menu, while Recent and Finished provide direct Library/Recent/Finished navigation without leaving the shelf.
- Library menu access to Clock & Weather, To-Do List, Reading Summary, Reading Calendar, bookmarks, clippings, and Retrieve All Book Details.

### Reader and typography

- CrossPoint reader engine retained for EPUB, XTC/XTCH, TXT, and Markdown workflows.
- Direct CBZ reader with ComicInfo.xml metadata, cover/thumbnail caching,
  bounded archive extraction, Fit Width/Fit Page/Landscape/Zoom, page picker,
  RTL/LTR navigation, bookmarks, cache replay, and responsive read-ahead.
- Improved EPUB CSS handling, HTML tables/cells, images, metadata, and memory safety.
- EPUB formatting now includes optional paragraph indents, improved lists/tables and `<hr>` separators, lightweight strikethrough/redaction handling, and Reader Guide Dots. These changes stay in the existing parser/render path and do not replace the image pipeline.
- Large images are fitted to the display instead of producing empty squares where possible.
- XTC/XTCH cover and page rendering improvements.
- Optional **Stable Pages** mode uses a compact per-book `stable_pages.bin`
  map, can import CrossInk `META-INF/x-locations.json`, and keeps page numbers
  consistent across font/layout changes. Current Pages remains the default;
  stable-page preparation is streamed, bounded, cancellable, reusable, and
  releases its temporary memory when finished.
- Reader font size in points rather than only Small/Medium/Large presets.
- Point-based margin controls and line-spacing controls with fine percentage steps.
- UI scale controls for menus and reader controls; bookshelf geometry remains fixed.
- Reader dark mode.
- Multi-dictionary lookup with dictionary history and preferred-dictionary reuse; the selected dictionary may build its index on first use, while alternate dictionaries are searched only when their sidecar is already current so a miss never blocks on several index builds. Definition pages show the source dictionary, allow switching, and can search all prepared dictionaries with source-labelled results.
- Reader settings include a one-dictionary-at-a-time **Prepare Dictionary Indexes** screen so larger alternate dictionaries can be prepared before use, with percentage progress, Back-to-cancel, and resumable checkpoints; dictionaries with no installed set show setup guidance instead of a blank screen.
- Text clipping/highlighting: select a continuous word range (with held-button navigation), save clips, and review saved clips from the reader.
- Saved clippings are rendered back as continuous highlights with selectable black, dark-gray, light-gray, or white highlight backgrounds.
- Bookmark, clipping, and highlight lists can be opened from the reader, Recent/Finished actions, and the Library menu; entries support viewing, editing, and deletion.
- Reader shortcuts and existing CrossPoint input mappings remain available.
- CrossInk-inspired controls: short/long power actions, separate reader front-button remapping,
  side-button layout and long-press actions, plus Reader Options shortcuts while reading.
- Dictionary settings are available directly from the Reader settings tab as well as Text Settings.
- Dictionary font and dictionary font-size settings are available independently from reading typography.
- Reader Options can be opened while reading from the reader menu, mapped front button, long-press menu, or configured power-button action.
- Bluetooth HID/page-turner support is present in the codebase but remains experimental and is not considered stable for release yet.

#### Dictionary setup and use

Folio Nooir uses StarDict-format dictionaries stored on the SD card. A dictionary
folder must contain exactly one index stem and its matching data file:

```text
/dictionaries/<folder>/<stem>.idx
/dictionaries/<folder>/<stem>.dict     (or <stem>.dict.dz)
```

The hidden `/.dictionaries/<folder>/` root is also supported. Folders with no
data file, multiple `.idx` stems, or unsupported 64-bit index offsets are not
listed. The firmware creates a small `.qidx` sidecar next to the index; it is a
rebuildable cache and does not change the dictionary source files.

1. Copy a complete dictionary folder to `/dictionaries/` or `/.dictionaries/`.
2. Open **Settings > Reader > Dictionary Settings** and select the primary
   dictionary, dictionary font, and dictionary font size.
3. The primary dictionary may prepare its index automatically on its first
   lookup. This is a one-time SD-card scan; later lookups use the sidecar.
4. Prepare additional dictionaries ahead of time from **Settings > Reader >
   Prepare Dictionary Indexes**. Select a dictionary marked **Needs index** to
   see a percentage progress bar. Press **Back** to cancel; the partial index is
   saved as **Paused**, and selecting it again resumes from its checkpoint.
5. While viewing a definition, the source dictionary is shown below the
   headword. Open the dictionary action to switch to another prepared source or
   choose **Search all prepared dictionaries** for source-labelled combined
   results. Only current prepared sidecars participate in alternate/search-all
   lookups, so a missing word cannot trigger several long scans or freeze the
   reader.

If no valid dictionary folders are found, the index screen explains that a
dictionary must be added before indexes can be prepared. If a dictionary is
copied or replaced, rerun **Prepare Dictionary Indexes**; stale `.qidx` files
are rebuilt automatically.

Already using CrossPoint? The same complete StarDict dictionary folder can be
used with Folio Nooir. Nooir supports offline StarDict lookup while reading;
additional compatible dictionaries are available from
[CrossInk's dictionary downloads](https://inky.crossink.dev/#downloads).

### KOReader Progress Sync

Nooir supports KOReader-compatible progress sync, allowing reading progress to
continue between Nooir and KOReader on another device.

To use the public KOReader server, open **Settings → System → KOReader Sync**
and enter:

- Server URL: `https://sync.koreader.rocks`
- Your KOReader Sync username
- Your KOReader Sync password

Select **Authenticate**. While reading, open **Reader Menu → Sync Progress**
and choose **Apply Remote** to continue from the server or **Upload Local** to
send the current Nooir position. Use the same server where your KOReader
account was created; accounts are not shared between different sync servers.

The sync settings include an editable **Sync Device Name**, defaulting to
`Folio Nooir X4`. The existing device ID remains unchanged for compatibility.
CrossPoint sync receives the richer CrossPoint position data, while generic
KOReader servers receive standard KOReader fields only. On download, portable
XPath/percentage mapping is tried first, with the rich CrossPoint page and
paragraph position used as a fallback when needed.

### CBZ / Manga reading guide

CBZ support in Folio Nooir is currently **experimental**.

Nooir can open normal `.cbz` comic and manga files directly from the SD card. No conversion or Web UI preprocessing is required.

A CBZ is an archive containing comic page images. Because the XTEINK has limited RAM, Nooir processes pages in bounded chunks and uses SD-backed temporary/cache files where needed instead of loading an entire high-resolution page into memory.

The original `.cbz` file is not modified.

#### Quick start

1. Copy a `.cbz` file to any normal book folder on the SD card.
2. Open it from **Library**.
3. Start with **Fit Width** for normal manga reading.
4. Press **Confirm** to open the CBZ View Mode menu.
5. Use the Page Picker or bookmarks when you want to jump around the book.

Example folder layout:

```text
/Manga/
  ONE PIECE/
    Chapter 1.cbz
    Chapter 2.cbz
```

The `/Manga/` folder is only an example. CBZ files do not need to be stored in a special folder.

#### Library integration

CBZ books use the normal Folio Nooir bookshelf workflow.

Where available, Nooir can use `ComicInfo.xml` from inside the CBZ for book metadata.

CBZ books can also participate in:

- Library
- Recent
- Finished/status tracking
- Reading progress
- Reading statistics
- Cover/thumbnail caching
- Metadata retrieval
- Per-book page bookmarks

#### View modes

Press **Confirm** while reading a CBZ to open the **View Mode** menu.

##### Fit Width

The recommended default mode for normal manga reading.

The page is scaled to use the available reading width while preserving its aspect ratio.

```text
Left     Previous page
Right    Next page
Confirm  View Mode menu
```

##### Fit Page

Shows the complete comic page on screen.

This is useful for seeing the full page layout, although small manga text may naturally appear smaller.

```text
Left     Previous page
Right    Next page
Confirm  View Mode menu
```

##### Landscape

Uses the landscape reading width to enlarge the page and lets you scroll through the oversized page.

```text
Left     Scroll toward the top/start
Right    Scroll toward the bottom/end
Up       Previous comic page
Down     Next comic page
Confirm  View Mode menu
```

Scrolling stops at the page boundary instead of unexpectedly changing comic pages.

##### Zoom

Enlarges the current page and allows you to move around it.

```text
Left / Right / Up / Down  Pan around the page

Hold Left                 Previous page
Hold Right                Next page

Confirm                   View Mode menu
```

Short directional presses remain pan-only while Zoom mode is active. Panning stops at the page boundaries.

#### Page Picker

The **Page Picker** lets you jump directly to another comic page without paging through the whole book.

#### Reset View

Use **Reset View** to return the current CBZ page to its default pan/zoom position.

#### Manga reading direction

CBZ books support both normal **LTR** and manga-style **RTL** page navigation.

Changing the reading direction changes comic page navigation only. It does not modify the original CBZ file.

#### CBZ bookmarks

CBZ books support per-book page bookmarks.

You can save a comic page and later jump directly back to that bookmarked page.

#### Covers and ComicInfo.xml

When available, Nooir can read `ComicInfo.xml` metadata from inside the CBZ.

CBZ cover/thumbnail files are cached separately from temporary reader pages so normal reader cleanup does not remove the bookshelf cover.

#### CBZ caching and performance

Comic pages are image-heavy, while XTEINK devices have limited RAM.

To stay within those limits, Nooir uses a bounded rendering pipeline and SD-backed page caches instead of keeping a full decoded comic page in memory.

A first-time page load may therefore take several seconds because Nooir may need to:

1. Find the requested image inside the CBZ.
2. Extract the current page.
3. Decode the source image.
4. Prepare the device-friendly page/cache.
5. Refresh the e-ink display.

Simplified flow:

```text
CBZ archive
    ↓
Current page image
    ↓
Bounded image decode
    ↓
Device/page cache
    ↓
XTEINK display
```

Prepared/cached pages can be faster to display.

Where supported and safe, Nooir can prepare upcoming pages in advance while you read the current page, helping reduce the wait on later page turns.

This design intentionally favors memory safety over trying to keep entire high-resolution manga pages in RAM.

#### Current CBZ limitations

CBZ support is still being developed and tuned.

Current limitations may include:

- Very small manga text and fine line art may not yet render as clearly as expected.
- Large or high-resolution pages may take several seconds on their first decode.
- Landscape and Zoom require additional processing compared with normal Fit Width reading.
- Rendering quality can vary depending on the source image and compression used inside the CBZ.
- Physical e-ink refresh time still contributes to page-turn delay.

For now, **Fit Width** is the recommended starting mode for normal reading.

#### Reporting CBZ problems

If a particular CBZ behaves unexpectedly, useful information for a bug report includes:

- XTEINK model/revision
- CBZ page dimensions
- Image format inside the CBZ, if known
- A photo of the physical display
- Relevant serial logs, if available

### Reading statistics

- Persistent per-book reading time, session count, progress, status, and dates.
- Persistent page-turn counts for each book and recorded day, plus pages-per-minute pace.
- Current and best consecutive reading-day streaks.
- On-device book statistics from the long-press menu.
- Reading Summary from the home/Library menu.
- On-device reading calendar showing the last 30 recorded days.
- Web statistics cards and JSON export include pages, pace, streaks, and daily page counts.
- The on-device summary includes total time, sessions, average session, pages, pace, streaks, book states, today, and recent recorded days.
- Featured-book summary such as `Ongoing - 12% - 18 min - 22 sessions`.
- Finished, Reading, On Hold, and New state tracking.

### Web interface

When the device is connected to the same network, the built-in web interface provides:

- Folio Nooir-styled device dashboard.
- Bookshelf with covers and progress.
- Reading calendar and statistics dashboard at `/stats`.
- Per-book covers, time, sessions, pages, pace, dates, status, progress, and synopsis.
- Web editing for title, author, synopsis, status, progress, start date, and finish date.
- Reset-reading-data action and JSON statistics export.
- Clock/weather card with editable location coordinates, Celsius/Fahrenheit choice, last-sync status, cached conditions, and a one-shot Sync now action.
- On-device Clock & Weather status page from the home menu, including cached clock/date/weather information and a one-shot refresh button.
- To-Do List page at `/todo`, synchronized with the device list and supporting quick add, edit, complete, reorder, delete, and clear-completed actions. Saves are guarded against duplicates and refreshes pause while the user is editing.
- Web metadata editing for title, author, synopsis, status, progress, start date, and finish date without rewriting the original book file.
- The web statistics JSON includes daily page counts, current/best streaks, and total pages for external tools.
- File browsing, image preview, upload, download, rename, move, delete, and folder creation.
- Transfer-page optimization for EPUBs and opt-in progressive-CBZ-JPEG
  normalization, with guidance on when conversion is useful and when a normal
  transfer is sufficient.
- Existing CrossPoint settings, Wi-Fi, OPDS, font, and typography pages.
- Clock & Weather sync reports progress and prevents duplicate requests; a
  device-started sync may release Wi-Fi when it finishes, while web mode keeps
  the current session alive through temporary station-Wi-Fi loss.
- Network activities keep their existing behavior and are entered without an
  unconditional reboot; memory-heavy cleanup is performed when leaving the
  activity.

### Sleep and display

- Custom PNG/BMP sleep images.
- Random sleep images from `/.sleep/`.
- Transparent PNG page-overlay sleep mode that keeps the last reader page visible beneath the overlay, rendered with the full four-level grayscale pipeline.
- `Cover + Overlay`: use the current/recent book cover as the background and composite the transparent page overlay above it.
- `Reading Stats`, `Minimal Stats`, and `Clipping + Cover` sleep modes.
- To-Do List sleep mode with Unchecked, Completed, Random, and All task choices; the All mode uses a centered card up to 98% of the display height.
- Quick Resume and Resume Reader on Wake are separate controls: Quick Resume chooses whether the current page is retained while asleep, while Resume Reader on Wake chooses Reader versus Recent/Library after waking.
- Ghosting mitigation and clean refreshes when leaving books or entering sleep.
- Conservative X3/X4 display-driver detection, including newer X3 UC8279d probing with UC8253 fallback and the known X4 SSD1677 default path.

Some sleep-overlay, display-compatibility, and reader usability ideas were reviewed against the open-source [CrossInk](https://github.com/uxjulia/CrossInk) project and adapted where they fit Folio Nooir's CrossPoint base.

## Installation

1. Download a release from this repository's **Releases** page.
2. Keep a copy of the currently working firmware for recovery.
3. Open the CrossPoint web flasher and select the custom firmware option.
4. Choose the Folio Nooir `firmware.bin` and flash it to a supported older-model X4.

Test builds are provided without warranty. Flashing custom firmware is at your own risk.

### SD-card update

The SD-card firmware picker accepts a file named exactly `firmware.bin` in the SD-card root. The current reading position and book data are stored separately from the firmware image.

### Over-the-air updates

Folio Nooir checks releases from:

```text
https://github.com/toshio2011/folio-nooir/releases/latest
```

Each compatible GitHub release must contain an asset named exactly:

```text
firmware.bin
```

Use a numeric release tag such as `1.5.10`. Devices running an older build that still points to CrossPoint must be manually flashed once with a build containing the Folio Nooir OTA endpoint.

## Custom sleep images

Choose **Custom** in sleep-screen settings, then use either:

- `/sleep.png` or `/sleep.bmp` for one fixed image; or
- multiple `.png` and `.bmp` files inside `/.sleep/` for randomized sleep images.

If both root files exist, `/sleep.bmp` takes priority.

`Clipping + Cover` selects a random saved clipping and renders it in a quote
card over the clipping's own book cover when the device sleeps.

### Page overlay

Choose **Page Overlay** or **Cover + Overlay** and place PNG artwork in `/.sleep/` or `/sleep/`; one image is selected randomly for each sleep screen. Both modes preserve transparent artwork in grayscale; Cover + Overlay tries the next image when the random choice is opaque, then shows an opaque image only if no transparent artwork is available. A single `/sleep-overlay.png` (or `overlay.png`) is also supported as a fixed fallback. Transparent PNGs are recommended so the cover or reader page remains visible underneath.

## Building

Folio Nooir uses PlatformIO. From the repository root:

```powershell
python scripts/build_html.py
.\.venv\Scripts\pio.exe run -e default
```

The development firmware is written to:

```text
.pio/build/default/firmware.bin
```

For a release build, use the `gh_release` environment:

```powershell
.\.venv\Scripts\pio.exe run -e gh_release
```

## Credits and license

Folio Nooir is built on [CrossPoint Reader](https://github.com/crosspoint-reader/crosspoint-reader), with display and reader foundations from the CrossPoint contributors. It also acknowledges the open-source [CrossInk](https://github.com/uxjulia/CrossInk) project as a reference for compatible Xteink display, sleep-screen, and reader improvements.

Licensed under the MIT License.
