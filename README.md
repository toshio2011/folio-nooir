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
- Transparent PNG page-overlay sleep mode with ghosting mitigation
- Folio Nooir branding and boot logo
- Core reader, Wi-Fi transfer, sleep-cover and settings features retained

Bluetooth remote support is experimental and may be disabled or unstable in current test builds.

<img width="960" height="1280" alt="WhatsApp Image 2026-08-05 at 2 46 29 PM" src="https://github.com/user-attachments/assets/f8ef98bf-49bc-43e4-a6bd-1a774e7ecf38" />
<img width="765" height="1020" alt="WhatsApp Image 2026-08-05 at 2 46 28 PM" src="https://github.com/user-attachments/assets/fe269b6b-9f88-4e08-b9ac-41c41dcc247a" />
<img width="765" height="1020" alt="WhatsApp Image 2026-08-05 at 2 46 28 PM (3)" src="https://github.com/user-attachments/assets/10fe0c5d-7848-4845-9e1d-d071976ef0c5" />
<img width="765" height="1020" alt="WhatsApp Image 2026-08-05 at 2 46 28 PM (2)" src="https://github.com/user-attachments/assets/5455e05f-4c30-4390-a9d6-4c994957ec0f" />
<img width="765" height="1020" alt="WhatsApp Image 2026-08-05 at 2 46 28 PM (1)" src="https://github.com/user-attachments/assets/159aad88-3e7b-46a6-8492-307680439f27" />
<img width="765" height="1020" alt="WhatsApp Image 2026-08-05 at 2 46 27 PM" src="https://github.com/user-attachments/assets/2fc621c3-248a-41b7-8b67-e719c16fe163" />

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

### Page overlay

Select **Page Overlay** in the sleep-screen settings and place a transparent
PNG at `/sleep-overlay.png` on the SD card. The overlay is drawn over the last
reader page; transparent pixels leave the page visible. The folder alternatives
`/.sleep/overlay.png` and `/sleep/overlay.png` are also supported. If the named
overlay is absent, Folio Nooir selects a PNG randomly from `/.sleep/` or
`/sleep/`, so no additional setting is needed for rotating overlays.

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
