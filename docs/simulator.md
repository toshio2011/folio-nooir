# Folio Nooir native simulators

Folio Nooir provides native desktop builds of the real firmware application.
They run the normal Nooir setup/loop, activity manager, UI, reader, and host
HAL through the pinned CrossPoint simulator dependency. No physical device or
firmware flashing is required.

## Supported profiles

| Device profile | PlatformIO environment | Portrait logical geometry |
| --- | --- | --- |
| X4 | `simulator_x4` | 480 × 800 |
| X3 | `simulator_x3` | 528 × 792 (792 × 528 panel) |

The X3 environment extends the X4 simulator configuration and selects the
upstream X3 board, display, orientation, button, RTC, battery metadata, and
tilt-capability profile. Only the X3 and X4 profiles are covered by this guide.

## Windows and WSL

The native build expects the Linux/macOS `sdl2-config` tool. On Windows, use
WSL2 (Ubuntu or another Debian-based distribution) and run the commands inside
the WSL terminal. Do not build from PowerShell or `cmd.exe`.

For good build performance, keep the repository inside the WSL/Linux
filesystem, for example `~/src/folio-nooir`, rather than under
`/mnt/c/...`. The latter works but can make PlatformIO builds and simulator
startup much slower.

## Install prerequisites

On Debian, Ubuntu, or Ubuntu in WSL:

```bash
sudo apt update
sudo apt install -y \
  build-essential curl git pkg-config \
  python3 python3-pip python3-venv \
  libsdl2-dev libssl-dev
```

`libsdl2-dev` provides `sdl2-config`. `libssl-dev` provides the OpenSSL/
`libcrypto` symbols used by the simulator's MD5 compatibility layer.

## Clone and set up

Clone with the FreeInk SDK submodule included:

```bash
mkdir -p ~/src
cd ~/src
git clone --recurse-submodules https://github.com/toshio2011/folio-nooir.git
cd folio-nooir
git submodule update --init --recursive
```

Create a project-local Python environment and install PlatformIO Core:

```bash
python3 -m venv .venv
source .venv/bin/activate
python -m pip install --upgrade pip platformio
pio --version
```

If the repository was already cloned, update it from the repository root and
make sure the submodule is present:

```bash
git pull --ff-only
git submodule update --init --recursive
source .venv/bin/activate
```

## Build and run

Run these commands from the repository root with the virtual environment
activated. The first build downloads the native PlatformIO platform and the
pinned simulator library.

### X4

```bash
pio run -e simulator_x4
pio run -e simulator_x4 -t run_simulator
```

### X3

```bash
pio run -e simulator_x3
pio run -e simulator_x3 -t run_simulator
```

The first command builds only. The `run_simulator` target builds if necessary
and launches the SDL2 window. Close the window to end the simulator.

## Virtual SD card

When launched from the repository root, the simulator maps `./fs_` to the
device-style SD-card root:

```text
fs_/books/       -> /books/
fs_/.crosspoint/ -> /.crosspoint/   (book and reader caches)
fs_/fonts/       -> /fonts/
fs_/sleep/       -> /sleep/
```

The directory is created automatically. Add books under `fs_/books/` before
launching:

```bash
mkdir -p fs_/books
cp /path/to/book.epub fs_/books/
```

EPUB, XTC, XTCH, TXT, and Markdown files can be tested through the normal
library/file-browser paths. Subfolders under `fs_/books/` are supported. Keep
the books and other runtime data out of Git.

If you want to use another SD-card directory, set the simulator's documented
override before running:

```bash
export CROSSPOINT_SIM_SD="$HOME/nooir-sim-sd"
pio run -e simulator_x4 -t run_simulator
```

## Keyboard controls

These are the controls implemented by the pinned simulator HAL for the X3 and
X4 profiles:

| Key | Simulated action |
| --- | --- |
| Up / Down | Side-button navigation; previous/next page in the reader |
| Left / Right | Front-button navigation |
| Enter / Return | Confirm or select |
| Escape | Back |
| P | Power-button action |
| S | Simulator sleep shortcut |

The X3-only simulator tilt shim adds:

| Key | Simulated X3 action |
| --- | --- |
| A | Left tilt |
| D | Right tilt |

To test A/D, open **Settings → Tilt Page Turn** on the X3 simulator and select
Normal or Inverted, then open a reader. Press and release A or D once. The key
injects a raw one-shot IMU impulse into the simulated `HalTiltSensor`; Nooir's
existing orientation, inversion, reader-only, threshold, and cooldown logic
decides whether to turn the page. Holding a key does not repeat the gesture.
Release it to return the simulated sensor to neutral. With Tilt Page Turn off,
or outside the reader, A/D do nothing.

## EPUB testing and caches

The first time a chapter is opened, an indexing message may appear while the
normal EPUB section/page cache is built. Later opens use the cache in
`fs_/.crosspoint/` and should be much faster. Reader font, margin, line-spacing,
CSS, and image-layout changes can invalidate that cache automatically.

If a test still shows an old layout, close the simulator and remove only the
generated cache directory; do not remove `fs_/books/`:

```bash
rm -rf fs_/.crosspoint
```

Then launch the same profile again and allow the EPUB to index. Stable Pages,
when selected, also keeps its per-book map in the book cache and may need to be
prepared again after the cache is cleared.

## Troubleshooting

### `sdl2-config: command not found`

Run inside WSL/Linux and install `libsdl2-dev`:

```bash
sudo apt install libsdl2-dev
command -v sdl2-config
```

### OpenSSL or `MD5_*` linker errors

Install the development package, then rebuild:

```bash
sudo apt install libssl-dev
pkg-config --libs openssl
pio run -e simulator_x4
```

### `pio: command not found`

Activate the project environment (`source .venv/bin/activate`) or install
PlatformIO into it with `python -m pip install platformio`.

### Builds or launches are very slow in WSL

Move the checkout from `/mnt/c/...` into the WSL filesystem under `~/src/`.
The simulator and PlatformIO access many small files, so this can make a
large difference.

### Books do not appear

Confirm that the simulator was launched from the repository root, that the
book is under `fs_/books/`, and that the file extension is supported. If using
`CROSSPOINT_SIM_SD`, verify that the exported directory contains its own
`books/` folder.

### A book still shows stale covers or pages

The virtual SD card keeps caches between runs. Stop the simulator, remove
`fs_/.crosspoint/`, and reopen the book to rebuild its metadata, image, and
section caches.

## Simulator limitations

The desktop build does not reproduce physical e-ink waveform timing, ghosting,
partial-refresh behavior, panel LUTs, battery drain, real heap/SD-card timing,
or every hardware peripheral. Network and sleep behavior are host simulations,
and the X3 A/D input is a synthetic tilt event rather than a physical IMU.

Always perform final display-driver, refresh, power, Bluetooth, storage, and
long-reading validation on the target X3 or X4 hardware.
