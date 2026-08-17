# Folio Nooir desktop simulators

The native simulators run the existing Nooir `setup()`/`loop()` and
`ActivityManager` through the CrossPoint simulator's SDL2 and host-HAL
implementations; they do not contain a second UI.

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

## Build and run

From the repository root:

```text
pio run -e simulator_x4
pio run -e simulator_x4 -t run_simulator

pio run -e simulator_x3
pio run -e simulator_x3 -t run_simulator
```

`simulator_x4` uses the default 800x480 X4 profile. `simulator_x3` reuses the
same foundation and selects the upstream X3 profile, including its 792x528
geometry, orientation, buttons, RTC, and tilt-capability metadata. The
simulator uses the pinned CrossMux-compatible dependency in `platformio.ini`.
The first build downloads the native platform and simulator library.

## Simulated SD card

The simulator maps these local paths to the device-style SD card:

```text
fs_/books/
fs_/.crosspoint/
fs_/fonts/
fs_/sleep/
```

The first boot can run with an empty `fs_/` directory. Add EPUB/XTCH files to
`fs_/books/` for reader testing, then rebuild/run without copying them into
the repository. Runtime books, caches, screenshots, and logs are ignored by
Git.

## Keyboard controls

The simulator's X4/X3 input mapping follows the CrossPoint simulator contract:

| Key | Device action |
| --- | --- |
| Up / Down | Navigate or previous/next page |
| Left / Right | Front-button navigation |
| Enter | Confirm/select |
| Escape | Back |
| P | Power/sleep |
| A | X3 tilt left (previous-page gesture) |
| D | X3 tilt right (next-page gesture) |

The X3 host profile reports the tilt-sensor capability. The pinned host HAL has
no physical IMU, so the X3-only simulator shim supplies a raw one-shot gyro
impulse from the keyboard: press `A` for left and `D` for right. The event goes
through the simulated `HalTiltSensor`; Nooir still applies the selected
orientation, normal/inverted tilt mode, reader-only setting, and
one-gesture-per-keypress behavior before deciding whether to turn a page.
Release the key to return the simulated sensor to neutral. Holding a key does
not repeat the gesture. No real UC8253/UC8279 waveform timing or e-ink
ghosting is modeled.

SDL display timing, e-ink ghosting, panel LUT behavior, and real power rails
are not modeled by the desktop simulator. Those still require hardware tests.

## Scope

`simulator_x4` and `simulator_x3` are provided. X4 Pro, networking, CBZ, PDF,
and reader feature changes are intentionally deferred.
