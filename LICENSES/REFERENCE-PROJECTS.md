# Upstream attribution and license inventory

This inventory covers the Folio Nooir v1.6.6 browser-emulator workstream. It separates the application base and bundled engine from projects consulted as references. It does not grant rights beyond each upstream license.

## Application and bundled engine

| Project | Use in this browser work | License and notices |
|---|---|---|
| [CrossPoint Reader](https://github.com/crosspoint-reader/crosspoint-reader) | Folio Nooir application and reader base; existing source keeps its upstream copyright headers. | MIT; see [`../LICENSE`](../LICENSE). The CrossPoint project also documents GPLv3 implications for its combined firmware executable due to wolfSSL; this web package contains no reader firmware. |
| [eink-qemu](https://github.com/serialx/eink-qemu/tree/d1b24a5fbc1bbae7ea31075460f63cd4c989c1dc) | Provides the browser/WASM emulator engine bundled in the generated package. Nooir's browser shell, branding, package scripts, and integration are project-side work. | GPL-2.0 with file-specific LGPL-2.1 and MIT components. The package retains upstream `LICENSE`, `COPYING`, `COPYING.LIB`, generated notices, component licenses, and a corresponding-source archive; see [`CORRESPONDING-SOURCE.md`](CORRESPONDING-SOURCE.md). |
| [Espressif esp-rom-elfs](https://github.com/espressif/esp-rom-elfs) | Supplies the official ESP32-C3 rev3 ROM ELF from release `20260528`; Nooir converts that licensed object to the flat ROM image required by eink-qemu. | Apache-2.0 for the ROM ELF object, with ROM third-party notices documented in [`ESP32C3-ROM-NOTICES.md`](ESP32C3-ROM-NOTICES.md). |
| [Emscripten](https://github.com/emscripten-core/emscripten) | Pinned SDK `6.0.12` compiles eink-qemu to WebAssembly and supplies the SDL2 browser port. | Emscripten's MIT / University of Illinois-NCSA dual license is included in the generated package; SDL2's notice is preserved with component licenses. |

## References only; no source or artwork copied into the web shell

| Project | How it informed the work | License / attribution |
|---|---|---|
| [CrossInk](https://github.com/uxjulia/CrossInk) | Reviewed for compatible Xteink display, sleep, input, and reader behavior. | MIT; the repository copyright notice names Dave Allie and contributors. |
| [CrossPoint Simulator](https://github.com/crosspoint-reader/crosspoint-simulator) | Reviewed as a desktop simulator regression framework; its code is not part of the browser package. | MIT; copyright Julia Nguyen. |
| [Reader Simulator](https://github.com/PewPewzxc/reader-simulator) | Reviewed as a browser/WASM feasibility reference; no code or artwork is copied. | MIT; VARAGH contributors. |
| [FreeInk SDK](https://github.com/Free-Ink/freeink-sdk) | Acknowledged as a hardware-support reference for related device porting; no SDK code is used by this browser work. | MIT and third-party notices belong to that separate SDK. |

The browser package includes Nooir's MIT license, the pinned engine and SDK notices, an exact corresponding-source archive, and hash-locked emulator support assets. The original eink-qemu ROM blobs, unneeded S3/Picco support binaries, and Nooir reader firmware are excluded. Links to reference-only projects are acknowledgements and do not imply that their source is bundled. Publication remains gated on a successful build, package audit and artifact review.
