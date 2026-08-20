
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

Current release: **v1.5.10** (development).

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

This release's change list starts with the **Stable Pages** enhancement.
Earlier v1.5.8 work remains documented in the complete feature list below.

A stability and performance-focused update, with new simulator support and a few EPUB improvements.

### Performance & Library
- Faster **Retrieve All Book Details** for large libraries.
- Reduced duplicate metadata and thumbnail cache checks.
- Removed unnecessary fixed delays during bulk book-detail retrieval.
- Faster individual metadata retrieval after selection settles.
- Preserved lightweight Library scrolling — simply scrolling through books will **not** trigger metadata retrieval for every highlighted item.
- Added lightweight performance logging for metadata and thumbnail retrieval.
- Improved cache-hit handling so already-cached books can be skipped more efficiently.
- Kept the existing low-memory, SD-backed retrieval queue.

### EPUB Improvements
- Improved image fitting after margins and vertical spacing are calculated.
- Prevents oversized EPUB images from extending outside the available reading area.
- Better handling of tall/full-page images on different screen sizes.
- Existing image cache, lazy extraction and fallback decoding behaviour preserved.
- Section cache version updated so incompatible older layout caches rebuild safely.

### X3 Support & Simulator
- Added a dedicated **X3 desktop simulator profile**.
- Added X3-specific screen geometry and orientation handling.
- Added simulated X3 tilt page turning:
  - `A` = previous page / left tilt
  - `D` = next page / right tilt
- X3 simulator reuses the same Nooir UI, reader and library code used by the firmware.
- EPUB image-layout fixes validated against the X3 screen size.

### X4 Simulator
- Added/expanded the native **X4 desktop simulator foundation**.
- Nooir UI, Library, Recent, reader, EPUB rendering and virtual SD storage can now be tested on desktop.
- Added simulator compatibility handling for clock, PNG, String and host-specific dependencies.
- Simulator uses a virtual SD card through the `fs_` directory.

### Documentation
- Added simulator setup and usage documentation.
- Added X4 and X3 build/run instructions.
- Added virtual SD / books folder instructions.
- Added X3 simulated tilt controls.
- Documented simulator limitations compared with real hardware.

### Validation
Tested with:
- X4 simulator
- X3 simulator
- Real XTEink X4
- Large library with **452 books**
- Retrieve All Book Details and cancellation
- Rapid Library scrolling
- Library / Recent / Finished navigation
- EPUB text and image-heavy books
- Forward/back page navigation
- Reader exit and cache persistence
- Sleep/wake behaviour

### Notes
The simulator is intended for UI, navigation and rendering development. It does **not** fully reproduce real-device RAM limits, SD-card speed, e-ink refresh timing, battery behaviour or sleep hardware.

X3 support uses the shared X3/X4 code path, but device revisions can differ, so keeping a backup and a known recovery method is still recommended.


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
- **Clear Reading Cache** clears Recent entries, Book State records, reading statistics, and the Folio shelf snapshot while preserving covers, thumbnails, `metadata.bin`, `book.bin`, bookmarks, clippings, and highlights.

### Folio Nooir bookshelf

- Folio Nooir boot logo and visual theme.
- Direct CBZ/Comic Book support with ComicInfo metadata, cover/thumbnail
  caching, bounded page indexing, and Library/Recent/Finished integration.
- CBZ retrieval is metadata-first and Recent is updated only when a CBZ is
  actually opened for reading.
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
- Library menu access to Clock & Weather, To-Do List, Reading Statistics, bookmarks, clippings, and Retrieve All Book Details.

### Reader and typography

- CrossPoint reader engine retained for EPUB, XTC/XTCH, TXT, and Markdown workflows.
- CBZ reader controls include Fit Width, Fit Page, Landscape, Zoom, Reset View,
  a page picker, RTL/LTR navigation, per-book page bookmarks, and safe
  one-page lookahead on the validated hardware path.
- Improved EPUB CSS handling, HTML tables/cells, images, metadata, and memory safety.
- EPUB formatting now includes optional paragraph indents, improved lists/tables and `<hr>` separators, lightweight strikethrough/redaction handling, and Reader Guide Dots. These changes stay in the existing parser/render path and do not replace the image pipeline.
- Large images are fitted to the display instead of producing empty squares where possible.
- XTC/XTCH cover and page rendering improvements.
- Optional **Stable Pages** mode uses a compact per-book `stable_pages.bin` map,
  can import CrossInk `META-INF/x-locations.json`, and keeps page numbers
  consistent across font/layout changes. Current Pages remains the default;
  preparation is streamed, bounded, cancellable, reusable, and releases its
  temporary memory when finished.
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
XPath/percentage mapping is tried first, with rich CrossPoint page and paragraph
position used as a fallback when needed.

### Reading statistics

- Persistent per-book reading time, session count, progress, status, and dates.
- Persistent page-turn counts for each book and recorded day, plus pages-per-minute pace.
- Current and best consecutive reading-day streaks.
- On-device book statistics from the long-press menu.
- Overall reading statistics from the Recent menu.
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
- To-Do List page at `/todo`, synchronized with the device list and supporting quick add, edit, complete, reorder, delete, and clear-completed actions.
- Web metadata editing for title, author, synopsis, status, progress, start date, and finish date without rewriting the original book file.
- The web statistics JSON includes daily page counts, current/best streaks, and total pages for external tools.
- File browsing, image preview, upload, download, rename, move, delete, and folder creation.
- Existing CrossPoint settings, Wi-Fi, OPDS, font, and typography pages.
- Network activities keep their existing behavior and are entered without an unconditional reboot; memory-heavy cleanup is performed when leaving the activity.

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

### Quick Resume and wake behavior

These two settings control different parts of sleep:

- **Quick Resume on + Resume Reader on Wake on:** the current page remains visible during sleep and waking returns quickly to the reader.
- **Quick Resume on + Resume Reader on Wake off:** the current page remains visible during sleep, but waking goes to Recent/Library.
- **Quick Resume off + Resume Reader on Wake on:** the configured normal sleep image is shown, then waking reopens the reader.
- **Quick Resume off + Resume Reader on Wake off:** the configured normal sleep image is shown, then waking goes to Recent/Library.

Quick Resume can be selected as the sleep screen itself, or enabled only for automatic inactivity timeout. Resume Reader on Wake only chooses the destination after wake; it does not keep Wi-Fi or Bluetooth running.

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
