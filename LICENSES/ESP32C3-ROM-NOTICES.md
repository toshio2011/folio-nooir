# ESP32-C3 emulator ROM notices

The porting branch has one reviewed C3 ROM image for use as an experimental
emulator support asset: `engine/esp32c3-rom.bin`. It is **not reader firmware**.
It is generated from Espressif's
`esp32c3_rev3_rom.elf` in the official
[esp-rom-elfs](https://github.com/espressif/esp-rom-elfs) release `20260528`.
Espressif states that the ROM ELF files may be used under Apache License 2.0,
while the mask-ROM implementation itself is not open source. The ROM-specific
redistribution review is complete on the condition that the exact pinned
Espressif and embedded-component notices below accompany the image. This does
not clear the complete emulator package: GPL corresponding source, other
component notices, and the package audit remain required before beta
publication. The experimental construction restores only startup copy-table
payloads and the `.rodata.interface` layout section from the licensed ELF.
Native and browser X3/X4 runs reached Nooir Home and completed EPUB navigation
with FAT32 SD cards. No bytes from eink-qemu's unlicensed stock ROM are used.

## Exact source and conversion

| Input/output | Size | SHA-256 |
|---|---:|---|
| `esp-rom-elfs-20260528.tar.gz` | 4,902,355 bytes | `caa463d3cbef2430a5a35847c1d9f2f152403b17a802050927ff60c8da54fe46` |
| `esp32c3_rev3_rom.elf` | — | `19ac22e08707df926fb0cf4c54795d4067b983fae8635f396ded173a6d78fc3c` |
| Default flat `esp32c3-rom.bin` | 387,784 bytes | `3336ae473a063be8dae6790bd45fee4506f2cf273aa36f3dfd9e0495ecb665cc` |
| Experimental copy-table-only image | 387,784 bytes | `2202e4f5c946fb2f8fc3d77e48f71ce29758b42913e7da868b955f0f6813fb72` |
| Experimental image with layout section and zero padding | 393,216 bytes | `3a69ac75662367dc99fbd5b8ddf265515b6288e79e51e4c4168c597aced7b79d` |

[`scripts/prepare_esp32c3_rom.py`](../scripts/prepare_esp32c3_rom.py)
pins the release archive, input ELF and expected output hashes, and refuses to
overwrite existing outputs. The flat image may be produced by
`riscv64-unknown-elf-objcopy -O binary`; the pinned Python/pyelftools path can
also reconstruct it from the file-backed allocated `PT_LOAD` sections. Both
paths are checked against the same flat-image length and hash. Passing
`--experimental-licensed-elf-startup`
reconstructs only the ELF sections matched to the ROM copy table plus the exact
`.rodata.interface` section mapped through its zero-length PT_LOAD anchor. It
checks the DROM alias, section flags, address ranges, symbol values, section
hashes and complete output hash, then writes a JSON manifest with the reviewed
source hashes, initialized ranges, and no-stock-ROM assertion. The verified
reconstruction used the pinned archive and matched the existing image and
manifest byte-for-byte. Any package must preserve the exact Espressif license
and the ROM-related third-party notices listed below; it does not redistribute
eink-qemu's unlicensed upstream ROM blobs.

Any generated site that redistributes this candidate must carry these offline notice files under
`licenses/esp32c3-rom/`:

| File | Pinned upstream source | Upstream Git blob SHA-1 |
|---|---|---|
| `esp-rom-elfs-README.md` | `espressif/esp-rom-elfs`, tag `20260528` | `0df304ef2d7c2ba4009c3e9931d98a951321dec2` |
| `Apache-2.0.txt` | `espressif/esp-rom-elfs`, tag `20260528` | `261eeb9e9f8b2b4b0d119366dda99c6fd7d35c64` |
| `esp-idf-COPYRIGHT.rst` | `espressif/esp-idf`, commit `d445e947156f276dfe0db5804e4edf6ae90fe6fb` | `d6f42432fec445e5f05971147a8a1de6d2cbf93e` |
| `Newlib-COPYING.NEWLIB` | `espressif/esp-idf`, commit `d445e947156f276dfe0db5804e4edf6ae90fe6fb` | `86827de4bcecbe78b0ae77d1e381588f9d4effc7` |
| `BSD-3-Clause.txt` | `spdx/license-list-data`, commit `31846481de7ea17d92f2f46fad76abe58b2e9bb0` | `ea890afbc70c94011b765722a95e2c8c7d4f1233` |
| `CC0-1.0.txt` | `spdx/license-list-data`, commit `31846481de7ea17d92f2f46fad76abe58b2e9bb0` | `0e259d42c996742e9e3cba14c677129b2c1b6311` |

The ESP-IDF notice includes the ROM component copyright attributions and the
complete MIT and TJpgDec terms reproduced there. The Newlib notice preserves
its individual copyright blocks and BSD terms. The Apache and BSD-3-Clause
files are included for the Espressif ROM object, Zephyr USB components and
Newlib notices. The BSD license text comes from SPDX's CC0 license-list data,
whose CC0 terms are included too. The Newlib and BSD-3 text copies remove only
trailing whitespace and a final empty line; no copyright, condition, or
disclaimer text is changed. The table records their original upstream blob
hashes, while the packager and package audit verify the whitespace-normalized
copies. All six offline files are required by the audit.

## ROM third-party notices

Espressif's official [ESP32-C3 ROM copyright and license page](https://docs.espressif.com/projects/esp-idf/en/latest/esp32c3/COPYRIGHT.html)
identifies third-party components present in the mask ROM. This generated ROM
notice retains those attributions and license references:

| Component | Notice/license identified by Espressif |
|---|---|
| Newlib | BSD; copyright by its respective parties; see `COPYING.NEWLIB` in the official source reference |
| Xtensa libhal | Tensilica, Inc.; MIT |
| TinyBasic Plus | Mike Field and Scott Lawrence; MIT |
| miniz | Rich Geldreich; public domain |
| TJpgDec | ChaN; permissive redistribution terms with source notice retention and no warranty |
| Zephyr DesignWare USB device driver | Intel; Apache-2.0 |
| Zephyr generic USB device driver | Bertrik Sikken and Intel; BSD-3-Clause |
| Zephyr USB descriptors functionality | PHYTEC and Intel; Apache-2.0 |
| Zephyr USB DFU class driver | Intel and PHYTEC; BSD-3-Clause |
| Zephyr USB CDC ACM class driver | Intel; Apache-2.0 |

The upstream pages remain linked from the About & Credits view and
corresponding-source index; the pinned copies above ensure their license terms
and notices are also available offline with the redistributed ROM image.

## Other emulator support images

The package includes only these additional, independently checked files:

* `boot/bootloader-esp32c3.bin`: ESP-IDF second-stage bootloader, Apache-2.0;
  its upstream boot README records the source ELF hash and conversion recipe.
* `boot/nvs-crosspoint.bin`: generated by the pinned eink-qemu `mknvs.py` from
  its `crosspoint.json` seed profile; those source files follow the pinned
  repository's GPL-2.0 default license and are included in the corresponding
  source archive.
* `boot/partitions.bin`: generated from the CrossPoint Reader 1.6.5 partition
  table, MIT; the exact source commit and MIT text are included.

The original eink-qemu `pc-bios` ROM blobs have no explicit per-file license in
the pinned revision and are not redistributed. The S3 ROM and S3 bootloader,
Picco partition image, all X3/X4 reader firmware, and all other `.bin` files
are excluded. The X4 Pro and X4 Classic remain disabled.
