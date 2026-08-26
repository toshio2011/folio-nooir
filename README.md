# Folio Nooir

Current development line: **v1.6.0 release candidate**.

Folio Nooir is an experimental, bookshelf-focused e-reader firmware for Xteink devices. It is a personal fork of [CrossPoint Reader](https://github.com/crosspoint-reader/crosspoint-reader), keeping the core reader, wireless transfer, sleep, and settings features while adding a Folio Nooir interface and reading tools.

## Hardware warning

**Folio Nooir has been physically tested only on the older Xteink X4 hardware revision.** That is the only device currently available to the maintainer.

The current display-driver direction follows the compatible X3/X4 work described by [CrossInk v1.5.0-rc-3](https://github.com/uxjulia/CrossInk/releases/tag/v1.5.0-rc-3), which reports fixes for all known X3/X4 display variants and support for the latest X4 battery latch. This should address the known panel/driver compatibility problems, but newer hardware still needs real-device validation with Folio Nooir. X3 should also work with the compatible driver path, but X3 testing is still pending. X4 Pro/S3 hardware is not supported by this build.

This does not repair physical screen damage, factory firmware locks, damaged cables, or other hardware faults. Keep a working recovery firmware before flashing. An incompatible panel or board can leave the display unusable and may require recovery through the SD-card firmware picker.

If CrossInk is already installed and working on your device, you can safely ignore this firmware. If CrossPoint is installed and working, use the recovery instructions and keep a known-good image before switching.

## Native Simulator

The repository includes native PlatformIO profiles for `simulator_x4` and
`simulator_x3`. They exercise the shared Nooir UI, reader, Carousel, and
library code without flashing a physical device. The X3 profile also includes
simulated tilt input. See [docs/simulator.md](docs/simulator.md) for setup,
build/run commands, controls, virtual SD-card data, and limitations.

## Features

### Folio Nooir bookshelf

- Folio Nooir boot logo and visual theme.
- Three bookshelf views: **Library**, **Recent**, and **Finished**.
- Library acts as a folder/file browser and loads metadata lazily as books are highlighted.
- Featured-book panel with cover, title, author, HTML synopsis, progress, status, reading minutes, and session count.
- Compact 4 x 2 cover grid with percentage progress ribbons.
- Independent **Carousel** theme with **3-Cover Carousel** and **5-Cover
  Carousel** layouts. The selected cover is dominant, side covers use the
  shared mirrored perspective treatment, navigation loops through the
  collection, and small collections never render duplicate books in one
  frame.
- Folio Recent and Finished independently support **3 Covers**, **4 x 2 Grid**,
  **3-Cover Carousel**, and **5-Cover Carousel** without changing the
  graphical Library UI.
- Carousel center covers can use an explicitly prepared 360px thumbnail and
  immediately fall back to the existing 220px thumbnail; side covers remain
  220px, and navigation never generates an HQ cover synchronously. Bounded
  source-cache reuse keeps visible covers stable during a frame.
- Featured cover sizing is derived from the active shelf geometry and uses the
  shelf-compatible cover source for consistent grayscale rendering.
- Cover cache warm-up with visible retrieval feedback, cache reuse, and invalid/blank-BMP recovery.
- Long-press actions for opening, status changes, progress reset, cache refresh, full synopsis, book statistics, and removing a book from the list without deleting the file.
- Automatic movement to Finished when a book reaches 100%.

### Reader and typography

- CrossPoint reader engine retained for EPUB, XTC/XTCH, TXT, and Markdown workflows.
- Direct CBZ reader with ComicInfo.xml metadata, bounded extraction, cover and
  thumbnail caching, Fit Width/Fit Page/Landscape/Zoom, page picker, RTL/LTR
  navigation, bookmarks, persistent cache replay, and X3/X4 read-ahead.
- Improved EPUB CSS handling, HTML tables/cells, images, metadata, and memory safety.
- Large images are fitted to the display instead of producing empty squares where possible.
- XTC/XTCH cover and page rendering improvements.
- Optional Stable Pages mode, bounded image/cache preparation, Reader Guide
  Dots, improved clipping/highlight restoration across reflow, and safe
  long-operation feedback for CBZ and EPUB loads.
- Reader font size in points rather than only Small/Medium/Large presets.
- Point-based margin and line-spacing controls.
- Reader dark mode.
- Multi-dictionary lookup with dictionary history and preferred-dictionary reuse.
- Multi-dictionary preparation and source-labelled definition results, with
  resumable dictionary indexes for larger alternate dictionaries.
- Text clipping and highlighting: select continuous text, save clips, review
  them, and render saved highlights with selectable monochrome backgrounds.
- Reader shortcuts and existing CrossPoint input mappings remain available.
- Bluetooth HID/page-turner support is present in the codebase but remains experimental and is not considered stable for release yet.

PDF and FB2 reader support are not implemented; the repository contains
feasibility notes only.

### Reading statistics

- The on-device Statistics screen provides **Overview**, **Calendar**, **Books**,
  and **Achievements** tabs.
- Persistent per-book reading time, session count, progress, status, and dates.
- Persistent daily pages, sessions, reading time, current/best streaks, book
  totals, and average-session information, with daily history bounded to 730
  days.
- Calendar month navigation and monochrome daily intensity; Books circular
  navigation with cached 220px covers; and twenty derived earned/locked
  achievements with progress bars.
- On-device book statistics from the long-press menu.
- Overall reading statistics from the Recent menu.
- Reading Stats and Minimal Stats sleep screens reuse existing cached covers;
  they do not generate or prepare covers while asleep. Minimal Stats makes the
  current book the visual centerpiece, while legacy full-screen Cover,
  Cover + Overlay, and Clipping + Cover paths retain their existing behavior.
- Featured-book summary such as `Ongoing - 12% - 18 min - 22 sessions`.
- Finished, Reading, On Hold, and New state tracking.

### Web interface

When the device is connected to the same network, the built-in web interface provides:

- Folio Nooir-styled device dashboard.
- Bookshelf with covers and progress.
- Reading calendar and statistics dashboard at `/stats`.
- Per-book covers, time, sessions, dates, status, and progress.
- Pages, pace, streaks, daily totals, and JSON statistics export.
- Web editing for title, author, synopsis, status, progress, start date, and finish date.
- Reset-reading-data action and JSON statistics export.
- File browsing, image preview, upload, download, rename, move, delete, and folder creation.
- Existing CrossPoint settings, Wi-Fi, Calibre wireless transfer, OPDS, font,
  and typography pages.

### Sleep and display

- Dark, Light, Blank, Custom, Cover, Quick Resume, Page Overlay, Cover +
  Overlay, Reading Stats, Minimal Stats, Clipping + Cover, and To-Do List sleep
  modes.
- Custom PNG/BMP sleep images.
- Random sleep images from `/.sleep/`.
- Transparent PNG page-overlay sleep mode that keeps the last reader page visible beneath the overlay.
- To-Do sleep can show Unchecked, Completed, Random, or All tasks; Cover +
  Overlay and Clipping + Cover preserve their separate full-screen cover
  composition.
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

Use a numeric release tag such as `1.5.1`. Devices running an older build that still points to CrossPoint must be manually flashed once with a build containing the Folio Nooir OTA endpoint.

## Custom sleep images

Choose **Custom** or **Cover + Custom** in sleep-screen settings, then use either:

- `/sleep.png` or `/sleep.bmp` for one fixed image; or
- multiple `.png` and `.bmp` files inside `/.sleep/` for randomized sleep images.

If both root files exist, `/sleep.bmp` takes priority.

### Page overlay

Choose **Page Overlay** and place a transparent PNG at `/sleep-overlay.png`. Folder alternatives `/.sleep/overlay.png` and `/sleep/overlay.png` are also supported. If the named overlay is absent, Folio Nooir selects a PNG randomly from `/.sleep/` or `/sleep/`.

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
