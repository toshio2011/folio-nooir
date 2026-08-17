# Folio Nooir X4 simulator

Stage 1 adds the native X4 simulator foundation. It runs the existing Nooir
`setup()`/`loop()` and `ActivityManager` through the CrossPoint simulator's
SDL2 and host-HAL implementations; it does not contain a second UI.

## Host prerequisites

The pinned simulator currently supports macOS and Linux/WSL. Native Windows is
not supported by the upstream simulator because the build expects `sdl2-config`.
On Debian/Ubuntu/WSL, install:

```text
sudo apt update
sudo apt install libsdl2-dev libssl-dev curl
```

PlatformIO and Python 3 are also required. Keep the repository inside the WSL
filesystem when possible for faster builds.

## X4 build and run

From the repository root:

```text
pio run -e simulator_x4
pio run -e simulator_x4 -t run_simulator
```

The simulator uses the pinned CrossMux-compatible dependency in
`platformio.ini`. The first build downloads the native platform and simulator
library.

## Simulated SD card

The simulator maps these local paths to the device-style SD card:

```text
fs_/books/
fs_/.crosspoint/
fs_/fonts/
fs_/sleep/
```

The first boot can run with an empty `fs_/` directory. Add EPUB/XTCH files to
`fs_/books/` for reader testing. Runtime books, caches, screenshots, and logs
are ignored by Git.

## Keyboard controls

The simulator's X4 input mapping follows the CrossPoint simulator contract:

| Key | Device action |
| --- | --- |
| Up / Down | Navigate or previous/next page |
| Left / Right | Front-button navigation |
| Enter | Confirm/select |
| Escape | Back |
| P | Power/sleep |

SDL display timing, e-ink ghosting, panel LUT behavior, and real power rails
are not modeled by the desktop simulator. Those still require hardware tests.

## Scope of Stage 1

Only `simulator_x4` is provided. X3, X4 Pro, networking, CBZ, PDF, and reader
feature changes are intentionally deferred.
