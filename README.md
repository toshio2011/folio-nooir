# Folio Nooir

Current development version: **v1.5.6**.

## v1.5.6 changes

This branch starts the next non-BLE development cycle from the stable v1.5.5 baseline.

- **Search All Folders** now scans from the SD-card root, matches the complete relative path, skips non-book images and internal cache/sleep folders, and reports scanned entries/books found in a compact multiline progress popup.
- Search results retain their nested paths so opening a result goes to the correct folder. Empty searches show a short completion message instead of leaving a blank/stuck shelf.
- Library highlighting uses an existing EPUB metadata cache without reopening the EPUB or triggering automatic retrieval. Cached title, author, synopsis, and valid cover data are reused; **Refresh Book Cache** remains the explicit repair path for missing or stale data.
- Retrieve All now uses two stages: it persists lightweight EPUB `metadata.bin` data for every book, then queues only missing/invalid shelf thumbnails for a small, resumable cover pass. It never builds the full reader index for the whole library.
- Shortened the Text Settings dictionary tab label to **Dict.** so the Controls tab remains visible on the X3/X4 screen.
- Retrieve All adds a light panel maintenance refresh every 15 completed books to reduce long-run e-ink ghosting without rebuilding the shelf.
- Lightweight metadata writes use a temporary file and rename commit, so a reset or power loss cannot leave a half-written cache.
- Retrieve All identifies the current filename and phase (metadata or thumbnail preparation), prioritizes the highlighted book's missing cover, and keeps completed thumbnails when stopped so the next run resumes quickly.
- Active Retrieve All and recursive search keep the CPU at full speed and temporarily suppress the global sleep timer until the operation finishes.
- Valid metadata and thumbnail caches are skipped independently. A missing cover is prepared in the second phase without deleting metadata, progress, bookmarks, or clippings; unsupported or oversized covers remain safe filename/title fallbacks.
- Added one-shot **Sync Clock & Weather**: NTP/RTC time and cached current weather are refreshed together, automatic refresh runs at most once per RTC day (or once per Wi-Fi session on an X4 without an RTC) when Wi-Fi is already being connected, and a device-started sync powers Wi-Fi back off after completion. Location/unit and a Web UI **Sync now** action are available without keeping Wi-Fi permanently enabled.
- Added an on-device **Clock & Weather** status page to the Library/Recent menu. It reads the cache instantly and offers an explicit **Sync now** action without adding network work to normal shelf navigation.
- Added **Resume Reader on Wake**. Turn it off to return to the bookshelf after a normal sleep wake; Quick Resume still returns directly to the reader.
- Added a small right-aligned battery icon and percentage to the Folio Nooir Library, Recent, and Finished shelf headers. It reuses the existing 1.5-second battery cache and does not change shelf geometry or cover loading.
- Added a persistent **To-Do List** at `/.crosspoint/todo.json`, with on-device check/add/edit/delete/reorder actions, a Folio Nooir web editor, and selectable unchecked/completed/random/all sleep-screen modes.
- To-Do presentation now includes open/done counts, lightweight priority markers, priority-aware ordering, and a centered sleep card that fits the selected list within 98% of the screen.
- To-Do sleep cards follow the selected mode: Unchecked, Completed, Random, or All. The All mode displays every task, including checked `[x]` items, in a centered card that can use up to 98% of the display height.
- Quick Resume and wake routing are now independent settings. The selected sleep frame controls what is shown while asleep; Resume Reader on Wake controls whether waking opens the last reader or the bookshelf.
- Improved newer X3 display compatibility with a conservative UC8279d/UC8253 controller probe before SPI starts. The detected controller is cached, an explicit override is respected, and an inconclusive probe falls back to the original UC8253 path. The older X4 SSD1677 path remains the default unless an X4 controller probe is explicitly enabled for validated hardware.
- Web Transfer now yields regularly during sustained uploads/downloads so the Wi‑Fi and SD tasks keep running; the Bookshelf page no longer parses EPUBs or decodes missing covers while it is open.
- Added **Edit book metadata** beside EPUB/XTC/TXT/Markdown files in Transfer. Title, author, and synopsis edits are stored in a lightweight device-side override and are applied to Library/Recent without rewriting the book or disturbing reading progress, bookmarks, or clippings.

### Quick Resume and wake behavior

These two settings control different parts of sleep:

- **Quick Resume on + Resume Reader on Wake on:** the current page remains visible during sleep and waking returns quickly to the reader.
- **Quick Resume on + Resume Reader on Wake off:** the current page remains visible during sleep, but waking goes to Recent/Library.
- **Quick Resume off + Resume Reader on Wake on:** the configured normal sleep image is shown, then waking reopens the reader.
- **Quick Resume off + Resume Reader on Wake off:** the configured normal sleep image is shown, then waking goes to Recent/Library.

Quick Resume can be selected as the sleep screen itself, or enabled only for automatic inactivity timeout. Resume Reader on Wake only chooses the destination after wake; it does not keep Wi-Fi or Bluetooth running.

The 1.5.6 non-BLE release carries forward the stable 1.5.5 reader and bookshelf baseline, then adds the changes above and the following maintained features:

- Added lightweight page-turn statistics for EPUB, XTC/XTCH, and TXT readers. Counters are committed when leaving a book, so normal page turns do not add SD-card writes or slow rendering.
- Added per-book and daily pages turned, pages-per-minute pace, current reading streak, and best streak statistics on-device and in the web statistics dashboard.
- Extended the reading calendar and web JSON export with page counts and pace data while keeping older statistics files compatible.
- Improved Retrieve All Book Details with a streaming SD-card queue, valid-cache skipping, visible book/progress feedback, and a responsive **Stop for now** action for large libraries.
- Retrieve All and per-book retrieval now refresh only the affected shelf entry instead of rebuilding the entire bookshelf.
- Changed retrieval progress dialogs to a light-gray, black-text style to reduce black-popup ghosting on e-ink displays.
- Added filename-first Library search to the 1.5.5 baseline: current-folder filtering is immediate and does not open books or retrieve metadata.
- Kept the default build non-BLE and based on the stable 1.5.4 baseline; Bluetooth remains isolated and experimental.

## v1.5.4 changes

- EPUB image rendering now warms missing image caches before font prewarming, reducing failures caused by fragmented heap memory.
- Added a bounded TJpgDec fallback for valid baseline JPEGs that JPEGDEC cannot decode because of long AC Huffman tables. Normal JPEGs remain on the faster JPEGDEC path.
- Added safer lazy image extraction, invalid-cache cleanup, one-time source refresh, and failure memoization so a broken image cannot repeatedly stall page rendering.
- Problematic EPUB images are now cached in the same compact pixel-cache format after successful decoding, making later visits fast and avoiding repeated SD/decoder work.
- Improved XTC/XTCH streaming by retaining the rendered black-and-white plane in small framebuffer chunks, reducing full-page rereads while keeping low-memory fallback behavior.
- Reduced redundant Recent-book SD writes and invalidated the featured-cover snapshot correctly when the selected book changes, improving home-screen responsiveness.
- Updated the firmware version to **1.5.4**. The changes use the shared X3/X4 code path; real-device validation remains recommended on every hardware revision.

## v1.5.3 changes

- Refresh Book Cache now rereads the book cover, title, author, and synopsis used by the featured-book panel.
- Refreshing metadata preserves reading progress, status, bookmarks, and clippings; an intentionally removed synopsis can also be cleared.
- Metadata refresh uses the lightweight metadata-only path, processes one book at a time, and skips unnecessary writes to keep Recent and Library responsive.
- Manual refresh always rereads metadata even when an older thumbnail is still present, then regenerates the cover when needed.
- The refresh path uses the shared X3/X4 code path and adds no X4-only behavior; X3 remains supported pending additional hardware testing.
- Multi-dictionary lookup now shows the source dictionary, supports switching/history and searching all prepared dictionaries, and avoids blocking on unprepared alternate dictionaries.
- Dictionary indexes can be prepared one at a time with percentage progress, Back-to-cancel, and resumable checkpoints; stale or replaced dictionary sidecars are detected and rebuilt.
- Dictionary font and dictionary font-size settings remain independent from reading typography.
- Full synopsis now decodes HTML entities and preserves paragraph, heading, list, and line-break structure while paging.
- Long-press book actions can be opened repeatedly after returning from an action; menu transitions now close the popup before opening Settings or another activity.
- Clipping + Cover sleep cards now show only the saved clipping text and a compact `- Book title, page N` attribution.
- Reader control parity: front and side long-press actions both support OFF, chapter skip, font-size cycling, and orientation change; Menu and Power long-press pickers expose the shared reader actions (Reader Options, statistics, screenshot, sleep, bookmark, dictionary, dark mode, and KOReader Sync where supported).
- A new Controls tab in Reader Options lets these front, side, Menu, and Power actions be changed without leaving the book; Menu sleep uses the normal sleep-screen/deep-sleep path.
- UI Scale now applies to Library text, tabs, menus, book actions, synopsis, book/reading statistics, calendar, clipping/bookmark lists, and Reader Options while leaving reading-page typography, bookshelf cover geometry, and the 4 x 2 grid independent. Recent and Finished remain entirely on the fixed-font shelf path for faster navigation.
- Library search offers an instant current-folder filename filter or an explicit recursive Search All Folders mode. The recursive scan is processed in small batches, never parses EPUBs or retrieves covers, and can be cancelled with Back; the same menu entry clears the search.
- Recent/Finished startup no longer validates every thumbnail or retrieves partial caches in the background. Existing metadata is shown immediately with a title/filename fallback; first-time empty caches use a visible one-book-at-a-time warm-up, and refresh remains manual.

Folio Nooir is an experimental, bookshelf-focused e-reader firmware for Xteink devices. It is a personal fork of [CrossPoint Reader](https://github.com/crosspoint-reader/crosspoint-reader), keeping the core reader, wireless transfer, sleep, and settings features while adding a Folio Nooir interface and reading tools.

## Hardware warning

**Folio Nooir has been physically tested only on the older Xteink X4 hardware revision.** That is the only device currently available to the maintainer.

The display-driver path now covers both X3 and X4 through the shared hardware-detection layer. For newer X3 panels, the firmware probes for the UC8279d controller before SPI starts, caches a confirmed result, honors an explicit override, and falls back conservatively to the original UC8253 path when detection is inconclusive. X4 keeps its known SSD1677 path by default, with newer X4 battery-latch handling retained; the optional X4 controller probe is only for separately validated hardware. This improves compatibility but is not a guarantee for every production revision, so keep recovery firmware available and test any newer X3/X4 unit carefully. X4 Pro/S3 hardware is not supported by this build.

This does not repair physical screen damage, factory firmware locks, damaged cables, or other hardware faults. Keep a working recovery firmware before flashing. An incompatible panel or board can leave the display unusable and may require recovery through the SD-card firmware picker.

If CrossInk is already installed and working on your device, you can safely ignore this firmware. If CrossPoint is installed and working, use the recovery instructions and keep a known-good image before switching.

## Features

### CrossPoint functions retained

Folio Nooir is an interface and feature layer on top of CrossPoint rather than a replacement reader. The existing CrossPoint workflows remain available:

- EPUB, XTC/XTCH, TXT, Markdown, and PDF/file-browser workflows.
- EPUB chapter navigation, footnotes, bookmarks, go-to-percent, auto page turn, orientation control, screenshots, and custom fonts.
- Image preview from the file browser, plus the existing X3 tilt-page-turn path where supported.
- Wi-Fi setup, browser-based file transfer, and the built-in web server.
- OPDS browsing, KOReader Sync, and OTA update support.
- Sleep cover, battery/status screens, SD-card firmware update, and recovery tools.
- Existing input mappings, themes/settings storage, status-bar controls, localization, and device configuration.

### Folio Nooir bookshelf

- Folio Nooir boot logo and visual theme.
- Three bookshelf views: **Library**, **Recent**, and **Finished**.
- Library acts as a folder/file browser and loads metadata lazily as books are highlighted.
- Library search is available from the menu and performs fast, case-insensitive filename filtering in the current folder, with an optional recursive **Search All Folders** mode.
- Featured-book panel with cover, title, author, HTML synopsis, progress, status, reading minutes, and session count.
- Compact 4 x 2 cover grid with percentage progress ribbons.
- Cover cache warm-up with visible retrieval feedback, cache reuse, and invalid/blank-BMP recovery.
- **Retrieve All Book Details** from the Library menu, with streaming metadata progress, a resumable missing-thumbnail pass, selected-book priority, valid-cache skipping, and **Stop for now** support for large SD-card libraries.
- Long-press actions for opening, status changes, progress reset, cache refresh, full synopsis, book statistics, and removing a book from the list without deleting the file.
- Bookmark, clipping, and highlight managers are available from the home menu and book actions; entries can be reviewed, edited, or deleted, and selecting a bookmark opens its book at the saved location.
- Automatic movement to Finished when a book reaches 100%.

### Reader and typography

- CrossPoint reader engine retained for EPUB, XTC/XTCH, TXT, and Markdown workflows.
- Improved EPUB CSS handling, HTML tables/cells, images, metadata, and memory safety.
- Large images are fitted to the display instead of producing empty squares where possible.
- XTC/XTCH cover and page rendering improvements.
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
- Ghosting mitigation and clean refreshes when leaving books or entering sleep.
- Conservative X3/X4 display-driver detection for the supported C3 family.

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

Use a numeric release tag such as `1.5.5`. Devices running an older build that still points to CrossPoint must be manually flashed once with a build containing the Folio Nooir OTA endpoint.

## Custom sleep images

Choose **Custom** in sleep-screen settings, then use either:

- `/sleep.png` or `/sleep.bmp` for one fixed image; or
- multiple `.png` and `.bmp` files inside `/.sleep/` for randomized sleep images.

If both root files exist, `/sleep.bmp` takes priority.

`Clipping + Cover` selects a random saved clipping from the current/recent book
and renders it in a quote card over that book's cover when the device sleeps.

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
