# eink-qemu browser bundle license and source inventory

This inventory covers the browser package produced from
[serialx/eink-qemu](https://github.com/serialx/eink-qemu/tree/d1b24a5fbc1bbae7ea31075460f63cd4c989c1dc)
at exact commit `d1b24a5fbc1bbae7ea31075460f63cd4c989c1dc`, using its pinned
WebAssembly build and browser packaging scripts. The Nooir wrapper is separate
project-side code. The package is approved for source review only after the
actual workflow artifact passes the hash and source-archive checks. The
distribution assessment and all asset hashes below supersede the earlier r5
review package inventory.

## Runtime and generated bundle components

| Component | Evidence in pinned build | License/notice in package | Source reference |
|---|---|---|---|
| QEMU and Espressif ESP32 machine code | `qemu-system-xtensa.wasm`, `qemu-system-riscv32.wasm`; root and file-level source headers | `licenses/eink-qemu/COPYING`, `COPYING.LIB`, `LICENSE`, upstream README | Exact eink-qemu commit above; its source tree contains the build scripts and source changes |
| Browser UI/runtime and bundled JavaScript dependencies | `eink-qemu/desktop` browser build, lockfile, generated `assets/*.js` | Generated upstream README plus scanned `desktop-npm` license texts | `eink-qemu/desktop/package.json` and `pnpm-lock.yaml` at the pinned commit |
| SDL2/Emscripten browser port | Emscripten browser build and `build-wasm-deps/em-cache/ports/sdl2` | SDL2 license text is included in the scanned `wasm-build-dependencies` notices | The Emscripten port version is resolved by the pinned build/toolchain; verify exact source input and any emitted runtime code before distribution |
| zlib, libffi, Pixman, GLib, PCRE2, libgcrypt, libgpg-error | `eink-qemu/wasm/build-deps.sh` and the resulting QEMU link targets | Their license texts are copied from the pinned build dependency sources into `licenses/eink-qemu-components/` | Versioned URLs and SHA-256 values are in the pinned `build-deps.sh`; GLib's PCRE2 fallback and generated patches are also recorded there |
| lwIP and Mbed TLS browser network adapter | `browser-net.wasm`, `browser-net.mjs`; `eink-qemu/wasm/build-browser-net.sh` | `engine/lwip.LICENSE.txt`, `engine/mbedtls.LICENSE.txt`, and scanned source notices | Exact source versions and build recipe are referenced by the pinned adapter build script |
| Berkeley SoftFloat, device-tree/libfdt, keycodemapdb | QEMU WebAssembly build graph and `subprojects/*.wrap` | SoftFloat and keycodemapdb notices are copied; DTC's GPL text, libfdt's BSD-2-Clause text, and `README.license` are copied under `licenses/eink-qemu-components/` | `subprojects/berkeley-softfloat-3.wrap`, `subprojects/dtc.wrap` (DTC `b6910bec11614980a21e46fbccc35934b671bd81`), and `subprojects/keycodemapdb.wrap` (keycodemapdb `f5772a62ec52591ff6870b7e8ef32482371f22c6`) |
| ESP32-C3 ROM, bootloader, partition and NVS support data | `engine/esp32c3-rom.bin` plus exactly three `engine/boot/*.bin` files | `licenses/ESP32C3-ROM-NOTICES.md`, `licenses/eink-qemu-boot/`, and scanned source notices | ROM generated from Espressif's licensed C3 rev3 ELF; boot assets traced in the pinned `eink-qemu/boot/README.md`; exact hashes below |
| UI fonts | Three bundled font files in `engine/assets/` | `engine/fonts.LICENSE.txt` | The upstream browser package documents the Latin Inter, Lora and Geist Mono subsets under OFL |
| Cross-origin isolation worker | `engine/coi-serviceworker.js` plus a root-scope Pages copy | `engine/coi-serviceworker.LICENSE.txt` | Upstream package identifies gzuidhof/coi-serviceworker commit `7b1d2a092d0d2dd2b7270b6f12f13605de26f214` |

The packager preserves the top-level eink-qemu license texts, upstream README,
boot-asset license texts, Emscripten license, generated notices and component
license texts from the pinned build and frontend dependencies. It also places
the corresponding-source archive next to the website package. The source map
identifies linked dependencies, build-only inputs, exact URLs and SHA-256
values. The package audit checks every shipped emulator `.bin` against an
explicit hash allowlist and rejects unknown firmware/debug binaries, key files
and common credential patterns.

## Corresponding-source availability

The exact engine commit, build scripts, selected Meson subproject revisions,
hash-pinned C/C++ dependency sources, browser-network sources, SDL2 port, and
locked frontend sources are co-hosted as
`sources/eink-qemu-d1b24a5f-corresponding-source.tar.gz` in the generated
package. The Nooir wrapper, ROM preparation script, build workflow and package
scripts are in this repository at the same revision. The full component map is
[`CORRESPONDING-SOURCE.md`](CORRESPONDING-SOURCE.md). This supplies users a
matching source download alongside the WebAssembly distribution; it does not
claim the source is bit-for-bit reproducible because the pinned network adapter
build generates a self-signed local emulator certificate.

The audited ROM source is Espressif's Apache-2.0-licensed ELF release, not the
unlicensed upstream QEMU ROM blobs. The Espressif C3 ROM third-party notices
and the precise object conversion are recorded in
[`ESP32C3-ROM-NOTICES.md`](ESP32C3-ROM-NOTICES.md). The package contains the
full Apache license text, upstream boot license texts, and CrossPoint MIT
partition notice. The NVS data is generated from the pinned GPL-2.0 engine
script/profile and is covered by the co-hosted corresponding source.

This assessment covers the pinned inputs and package policy. Public release is
still gated on the actual built package passing the workflow and package audit;
the GitHub Pages artifact must be checked before deployment.

## Exact emulator support binary inventory (allowed package inputs)

These are the only `.bin` files allowed in the website package and source
archive. None is Folio Nooir reader firmware.

| Packaged file | Bytes | SHA-256 | Source and review status |
|---|---:|---|---|
| `engine/esp32c3-rom.bin` | 393216 | `3a69ac75662367dc99fbd5b8ddf265515b6288e79e51e4c4168c597aced7b79d` | Reconstructed from the official Espressif Apache-2.0 `esp32c3_rev3_rom.elf`, release `20260528`; archive, ELF, copy-table and output hashes plus required notices are in `ESP32C3-ROM-NOTICES.md`. |
| `engine/boot/bootloader-esp32c3.bin` | 18688 | `46893df6619f0c7d7689fa309e76551e964e3d83d68c38ccb3e52ed8c3537226` | ESP-IDF second-stage bootloader; source ELF hash and conversion recipe are in `engine/boot/boot-provenance.md`; Apache-2.0 notice is included. |
| `engine/boot/nvs-crosspoint.bin` | 20480 | `67c80ffb02df062522216e33553f0691a9ddea06be794404365a66c90bc8e94f` | Generated by pinned eink-qemu `mknvs.py` from `profiles/crosspoint.json`; source is under the pinned engine's GPL-2.0 default terms. |
| `engine/boot/partitions.bin` | 3072 | `bd0f7954aca2ef7d925ee21aaa1f3dc8822d1d6ce5cbbd26a135e5886bfff6ce` | Generated from the CrossPoint Reader 1.6.5 `partitions.csv` at commit `93e98bb78702e29868a16a13b80c40e6b36ccdff`; MIT notice is included. |

The pinned upstream eink-qemu `pc-bios/esp32c3-rom.bin` and
`pc-bios/esp32s3_rev0_rom.bin` have no explicit per-file license in that
revision and are excluded. The S3 bootloader and Picco partition binary are
unused for verified Nooir X3/X4 and are excluded as well. This package does not
enable X4 Pro/X4 Classic and does not contain any reader firmware.
