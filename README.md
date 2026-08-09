# Folio Nooir

Current development release: **v1.5.3**.

## v1.5.3 changes

- Refresh Book Cache now rereads the book cover, title, author, and synopsis used by the featured-book panel.
- Refreshing metadata preserves reading progress, status, bookmarks, and clippings; an intentionally removed synopsis can also be cleared.
- Metadata refresh uses the lightweight metadata-only path, processes one book at a time, and skips unnecessary writes to keep Recent and Library responsive.
- Manual refresh always rereads metadata even when an older thumbnail is still present, then regenerates the cover when needed.
- The refresh path uses the shared X3/X4 code path and adds no X4-only behavior; X3 remains supported pending additional hardware testing.
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

The current display-driver direction follows the compatible X3/X4 work described by [CrossInk v1.5.0-rc-3](https://github.com/uxjulia/CrossInk/releases/tag/v1.5.0-rc-3), which reports fixes for all known X3/X4 display variants and support for the latest X4 battery latch. This should address the known panel/driver compatibility problems, but newer hardware still needs real-device validation with Folio Nooir. X3 should also work with the compatible driver path, but X3 testing is still pending. X4 Pro/S3 hardware is not supported by this build.

This does not repair physical screen damage, factory firmware locks, damaged cables, or other hardware faults. Keep a working recovery firmware before flashing. An incompatible panel or board can leave the display unusable and may require recovery through the SD-card firmware picker.

If CrossInk is already installed and working on your device, you can safely ignore this firmware. If CrossPoint is installed and working, use the recovery instructions and keep a known-good image before switching.

## Features

### CrossPoint functions retained

Folio Nooir is an interface and feature layer on top of CrossPoint rather than a replacement reader. The existing CrossPoint workflows remain available:

- EPUB, XTC/XTCH, TXT, Markdown, and PDF/file-browser workflows.
- Wi-Fi setup, browser-based file transfer, and the built-in web server.
- OPDS browsing, KOReader Sync, and OTA update support.
- Sleep cover, battery/status screens, SD-card firmware update, and recovery tools.
- Existing input mappings, themes/settings storage, and device configuration.

### Folio Nooir bookshelf

- Folio Nooir boot logo and visual theme.
- Three bookshelf views: **Library**, **Recent**, and **Finished**.
- Library acts as a folder/file browser and loads metadata lazily as books are highlighted.
- Library search is available from the menu and performs fast, case-insensitive filename filtering in the current folder.
- Featured-book panel with cover, title, author, HTML synopsis, progress, status, reading minutes, and session count.
- Compact 4 x 2 cover grid with percentage progress ribbons.
- Cover cache warm-up with visible retrieval feedback, cache reuse, and invalid/blank-BMP recovery.
- Long-press actions for opening, status changes, progress reset, cache refresh, full synopsis, book statistics, and removing a book from the list without deleting the file.
- Bookmark and clipping managers are available from the home menu and book actions; selecting a bookmark opens its book and jumps to the saved location.
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
- Reader shortcuts and existing CrossPoint input mappings remain available.
- CrossInk-inspired controls: short/long power actions, separate reader front-button remapping,
  side-button layout and long-press actions, plus Reader Options shortcuts while reading.
- Dictionary settings are available directly from the Reader settings tab as well as Text Settings.
- Dictionary font and dictionary font-size settings are available independently from reading typography.
- Reader Options can be opened while reading from the reader menu, mapped front button, long-press menu, or configured power-button action.
- Bluetooth HID/page-turner support is present in the codebase but remains experimental and is not considered stable for release yet.

### Reading statistics

- Persistent per-book reading time, session count, progress, status, and dates.
- On-device book statistics from the long-press menu.
- Overall reading statistics from the Recent menu.
- On-device reading calendar showing the last 30 recorded days.
- Featured-book summary such as `Ongoing - 12% - 18 min - 22 sessions`.
- Finished, Reading, On Hold, and New state tracking.

### Web interface

When the device is connected to the same network, the built-in web interface provides:

- Folio Nooir-styled device dashboard.
- Bookshelf with covers and progress.
- Reading calendar and statistics dashboard at `/stats`.
- Per-book covers, time, sessions, dates, status, and progress.
- Web editing for title, author, synopsis, status, progress, start date, and finish date.
- Reset-reading-data action and JSON statistics export.
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

Use a numeric release tag such as `1.5.3`. Devices running an older build that still points to CrossPoint must be manually flashed once with a build containing the Folio Nooir OTA endpoint.

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
