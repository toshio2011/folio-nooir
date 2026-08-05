# Folio Nooir

Folio Nooir is experimental e-reader firmware with a bookshelf-focused interface for the Xteink X4.

> ## ⚠️ Hardware warning
>
> Folio Nooir is developed and tested only on the **older Xteink X4 hardware revision**, because that is the only physical device currently available to the maintainer.
>
> Newer X3 and X4 devices may use a different display panel. **Do not flash Folio Nooir unless you know your device is the older X4 model.** An incompatible build may leave the display unusable or make recovery difficult.
>
> If CrossInk is already installed and working on your device, you can safely ignore this firmware update.

## Features

- Folio Nooir bookshelf home screen
- Library, Recent and Finished sections
- Book covers, title, author, synopsis and reading progress
- Persistent Reading, On Hold and Finished book states
- Reading time, daily activity and finished-book statistics
- Long-press book actions:
  - Mark as Reading, On Hold or Finished
  - Reset reading progress
  - Refresh the book cache
  - Remove a book from Recent and Finished without deleting the ebook
- Improved EPUB styling, tables, images and metadata handling
- Typography controls using point sizes, margins and line spacing
- Web-based library and file-transfer tools
- BMP and PNG custom sleep images
- Folio Nooir branding and boot logo
- Core reader, Wi-Fi transfer, sleep-cover and settings features retained

Bluetooth remote support is experimental and may be disabled or unstable in current test builds.

## Install

1. Download the latest `.bin` file from this repository's **Releases** page.
2. Keep a copy of your currently working firmware for recovery.
3. Open the CrossPoint web flasher and select the custom firmware option.
4. Choose the Folio Nooir `.bin` file and flash it to a supported older-model X4.

Test builds are provided without warranty. Flashing custom firmware is at your own risk.

## Custom sleep images

Select **Custom** or **Cover + Custom** in the sleep-screen settings, then use either:

- `/sleep.png` or `/sleep.bmp` for one image; or
- multiple `.png` and `.bmp` files inside `/.sleep/` for randomized sleep images.

BMP takes priority if both `/sleep.bmp` and `/sleep.png` exist.

## Building

Folio Nooir uses PlatformIO:

```powershell
.\.venv\Scripts\pio.exe run -e default
```

The resulting firmware is written to:

```text
.pio/build/default/firmware.bin
```

## Credits

Folio Nooir is a personal fork of [CrossPoint Reader](https://github.com/crosspoint-reader/crosspoint-reader), with a redesigned library experience and additional reading features. Thanks to the CrossPoint contributors for the reader engine and hardware foundation.

Licensed under the MIT License.
