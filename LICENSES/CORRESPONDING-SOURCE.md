# Browser emulator corresponding source and build inputs

This index describes the sources for the browser package built from the
isolated Folio Nooir v1.6.6 baseline. A generated package contains a
co-hosted archive at
`sources/eink-qemu-d1b24a5f-corresponding-source.tar.gz`. It includes the exact
pinned eink-qemu source tree, the resolved Meson source subprojects, the
downloaded C/C++ source archives used by the pinned scripts, the Emscripten
SDL2 port source, locked frontend package sources, and the four hash-verified
emulator support images. The source archive omits all unlicensed upstream ROM
blobs and reader firmware. The Nooir shell, ROM preparation script, package
scripts, workflow and license inventories are included in the public Nooir
repository at the commit that builds the site.

## Main engine and build scripts

* Project: [serialx/eink-qemu](https://github.com/serialx/eink-qemu)
* Exact source revision: [`d1b24a5fbc1bbae7ea31075460f63cd4c989c1dc`](https://github.com/serialx/eink-qemu/tree/d1b24a5fbc1bbae7ea31075460f63cd4c989c1dc)
* Root GPL-2.0, LGPL-2.1 and per-file terms are retained under
  `licenses/eink-qemu/`; the pinned source's build, browser packaging, boot
  asset and test scripts are preserved in the source archive.
* The upstream build applies one documented GLib compatibility edit in
  `eink-qemu/wasm/build-deps.sh` for Emscripten's compiler ID. The pinned
  script contains the exact patch operation; the source archive includes that
  original script and the pristine, hash-pinned GLib source archive.
* The full pinned QEMU tree includes its top-level `configure` script and
  `docs/devel/build-system.rst` build/install documentation. The package audit
  requires both, as well as the `wasm/build.sh` and `wasm/build-deps.sh` scripts.
* QEMU's resolved Meson sources are included from the build's
  `build-wasm-32limit/subprojects` directory. The relevant upstream wrap files
  are in the pinned repository and identify exact source revisions, including
  Berkeley SoftFloat `b64af41c3276f97f0e181920400ee056b9c88037`, DTC
  `b6910bec11614980a21e46fbccc35934b671bd81`, and keycodemapdb
  `f5772a62ec52591ff6870b7e8ef32482371f22c6`.
* Meson does not materialize the SoftFloat checkout in every build mode. The
  package workflow therefore reads the pinned `berkeley-softfloat-3.wrap`,
  verifies its exact GitLab URL and revision, fetches that revision, checks out
  it detached, verifies `HEAD`, and includes the source and its `COPYING.txt`
  under `meson-subprojects/berkeley-softfloat-3/`. The archive also records
  that source revision in `NOOIR-PROVENANCE.txt`. The package builder follows
  Meson source symlinks only within the pinned engine checkout when collecting
  DTC and keycodemapdb license texts.

## Downloaded dependency source archives

The pinned build scripts verify each archive with SHA-256 before building.
The corresponding-source archive retains the downloaded archives (and the
GLib Meson fallback package cache when that fallback is used).

| Component | Exact source input | SHA-256 | License notice in generated package |
|---|---|---|---|
| zlib | [1.3.1 source archive](https://zlib.net/fossils/zlib-1.3.1.tar.gz) | `9a93b2b7dfdac77ceba5a558a580e74667dd6fede4585b91eefb60f03b72df23` | `licenses/eink-qemu-components/wasm-build-dependencies/zlib/` |
| libffi | [v3.5.2 source archive](https://codeload.github.com/libffi/libffi/tar.gz/refs/tags/v3.5.2) | `dd19253d3007f366319a51d248a40c9e5fcace4498cbea990b566291844e4e30` | `.../libffi/` |
| Pixman | [0.44.2 source archive](https://cairographics.org/releases/pixman-0.44.2.tar.gz) | `6349061ce1a338ab6952b92194d1b0377472244208d47ff25bef86fc71973466` | `.../pixman/` |
| GLib | [2.84.0 source archive](https://download.gnome.org/sources/glib/2.84/glib-2.84.0.tar.xz) | `f8823600cb85425e2815cfad82ea20fdaa538482ab74e7293d58b3f64a5aff6a` | `.../glib/` |
| libgpg-error | [1.50 source archive](https://gnupg.org/ftp/gcrypt/libgpg-error/libgpg-error-1.50.tar.bz2) | `69405349e0a633e444a28c5b35ce8f14484684518a508dc48a089992fe93e20a` | `.../libgpg-error/` |
| libgcrypt | [1.11.0 source archive](https://gnupg.org/ftp/gcrypt/libgcrypt/libgcrypt-1.11.0.tar.bz2) | `09120c9867ce7f2081d6aaa1775386b98c2f2f246135761aae47d81f58685b9c` | `.../libgcrypt/` |
| lwIP | [STABLE-2_2_1_RELEASE source archive](https://github.com/lwip-tcpip/lwip/archive/refs/tags/STABLE-2_2_1_RELEASE.tar.gz) | `ce0b7461c0ad9602c376f0bf07c5eb7253b48c7bf66f011c6bf3e2a96731c539` | `.../browser-net/lwip-STABLE-2_2_1_RELEASE/` |
| Mbed TLS | [mbedtls-3.6.7 source archive](https://github.com/Mbed-TLS/mbedtls/releases/download/mbedtls-3.6.7/mbedtls-3.6.7.tar.bz2) | `a7e8bcbec0e6f761b4af24f25677626b35f762f68eef79c08677a363212d11f6` | `.../browser-net/mbedtls-3.6.7/` |

The exact Meson source archive URLs, hashes and wrap revisions are also
retained in the engine source. Build-only dependencies are not automatically
treated as linked browser code; the generated license directory preserves the
notices discovered in the pinned build inputs.

## Emscripten and browser frontend

* Emscripten SDK is pinned to `6.0.12`; its dual MIT / University of
  Illinois-NCSA license is copied to `licenses/emscripten/LICENSE`. LLVM's
  license text is included when present in the SDK. The build toolchain itself
  is not distributed.
* The SDL2 Emscripten port is taken from that SDK's pinned port cache; its
  source tree and notice are included in the corresponding-source archive and
  generated package.
* The browser frontend uses the pinned `eink-qemu/desktop/package.json` and
  `pnpm-lock.yaml`; exact npm package sources are retained in the source
  archive under `frontend-package-sources/`. The build uses Node.js 26 and
  pnpm 12.5.1.
* The separate browser network module uses the pinned `browser-net` source and
  the lwIP/Mbed TLS inputs listed above. Its self-signed local key is generated
  inside the emulator build for the emulated network only; no PEM key file is
  included in the published package or corresponding-source archive.

## ROM and boot-support source

The C3 ROM comes from Espressif's Apache-2.0 `esp-rom-elfs` release `20260528`.
The exact archive, ELF and generated binary hashes, conversion command and
third-party ROM notices are in
[`ESP32C3-ROM-NOTICES.md`](ESP32C3-ROM-NOTICES.md). The generated site also
ships pinned copies of Espressif's ROM ELF README and Apache license, the
ESP-IDF ROM copyright notice, Newlib's complete notice file, and the BSD-3
license text (with the CC0 notice for its SPDX license-list source). The
Newlib and BSD-3 files remove trailing whitespace only; their original
upstream blob hashes and normalized copy hashes are checked. Their
exact source commits and file hashes are recorded in that
notice. A machine-readable ROM manifest is included beside these notices and
copied into the corresponding-source archive. The generator is included in the
Nooir repository. The C3
second-stage bootloader source and conversion,
CrossPoint partition table source/MIT notice, and NVS generator/profile are
documented in `licenses/eink-qemu-boot/README.md` and the pinned engine source.

No `esp32c3-rom.bin` or `esp32s3_rev0_rom.bin` from the pinned eink-qemu
`pc-bios` directory is redistributed. No S3 or Picco support files and no
reader firmware are included.
