<img width="324" height="450" alt="WhatsApp Image 2026-08-07 at 4 32 39 PM" src="https://github.com/user-attachments/assets/cf0a9cd3-6783-417a-986c-0ec2fdf89ba4" />

# Folio Nooir

Current development release: **v1.5.2**.

Folio Nooir is an experimental, bookshelf-focused e-reader firmware for Xteink devices. It is a personal fork of [CrossPoint Reader](https://github.com/crosspoint-reader/crosspoint-reader), keeping the core reader, wireless transfer, sleep, and settings features while adding a Folio Nooir interface and reading tools.

## Hardware warning

**Folio Nooir has been physically tested only on the older Xteink X4 hardware revision.** That is the only device currently available to the maintainer.

The current display-driver direction follows the compatible X3/X4 work described by [CrossInk v1.5.0-rc-3](https://github.com/uxjulia/CrossInk/releases/tag/v1.5.0-rc-3), which reports fixes for all known X3/X4 display variants and support for the latest X4 battery latch. This should address the known panel/driver compatibility problems, but newer hardware still needs real-device validation with Folio Nooir. X3 should also work with the compatible driver path, but X3 testing is still pending. X4 Pro/S3 hardware is not supported by this build.

This does not repair physical screen damage, factory firmware locks, damaged cables, or other hardware faults. Keep a working recovery firmware before flashing. An incompatible panel or board can leave the display unusable and may require recovery through the SD-card firmware picker.

If CrossInk or CrossPoint is installed and working, use the recovery instructions and keep a known-good image before switching.

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
- Featured-book panel with cover, title, author, HTML synopsis, progress, status, reading minutes, and session count.
- Compact 4 x 2 cover grid with percentage progress ribbons.
- Cover cache warm-up with visible retrieval feedback, cache reuse, and invalid/blank-BMP recovery.
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
- Multi-dictionary lookup with dictionary history and preferred-dictionary reuse.
- Text clipping: select text, save clips, and review saved clips from the reader.
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
- Transparent PNG page-overlay sleep mode that keeps the last reader page visible beneath the overlay.
- Ghosting mitigation and clean refreshes when leaving books or entering sleep.
- Conservative X3/X4 display-driver detection for the supported C3 family.

Some sleep-overlay, display-compatibility, and reader usability ideas were reviewed against the open-source [CrossInk](https://github.com/uxjulia/CrossInk) project and adapted where they fit Folio Nooir's CrossPoint base.

### 🔄 KOReader Progress Sync

Nooir supports KOReader-compatible progress sync, allowing you to continue reading between Nooir and KOReader on another device.

Using the Public KOReader Server

If you already use KOReader Progress Sync, go to:

Settings → System → KOReader Sync

Enter:

- Sync Server URL: "https://sync.koreader.rocks"
- Username: Your KOReader Sync username
- Password: Your KOReader Sync password

Then select Authenticate.

«💡 Make sure you use the same server, username and password that you use in KOReader.»

Sync Your Reading Progress

While reading:

Reader Menu → Sync Progress

You can then choose:

- Apply Remote → Continue from the progress stored on the sync server.
- Upload Local → Send your current Nooir reading position to the sync server.

Important

If you previously used KOReader with "sync.koreader.rocks", make sure the Sync Server URL is entered explicitly.

Your account belongs to the server where it was created, so an account created on the public KOReader server is separate from an account on another KOReader-compatible sync server.

### 📖 Dictionary

Already using CrossPoint? You can use the same StarDict dictionary folder and files with Nooir. 📚

Nooir supports offline StarDict dictionaries for word lookup while reading.or you can download it from https://inky.crossink.dev/#downloads

Setup

1. Create a dictionary folder on your SD card:

/dictionaries/

You can also use the hidden folder:

/.dictionaries/

2. Put each dictionary inside its own folder:

/dictionaries/
└── English/
    ├── english.idx
    ├── english.dict.dz
    └── english.ifo

Required files:

- ".idx" — required and must be uncompressed
- ".dict" or ".dict.dz" — required
- ".ifo" — optional

«Keep only one dictionary per folder.»

3. On Nooir, go to:

Settings → Reader → Dictionary

Select the dictionary you want to use.

How to Use

While reading:

1. Open the Reader Menu and select Look Up.
2. A word on the page will be highlighted.
3. Use Left / Right to move between words.
4. Use Side Up / Down to move between lines.
5. Press Confirm to look up the selected word.
6. Press Back when you're done.

Quick Dictionary Access

You can also assign Dictionary to:

Settings → Controls → Long-press Menu → Dictionary

Then simply long-press Confirm while reading to start word lookup.

«💡 The first lookup may take a little longer while Nooir indexes the dictionary. After that, lookups should be much faster.»

Everything works offline once your dictionary is installed. 📚

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
