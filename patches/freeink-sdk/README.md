# Nooir FreeInk SDK integration delta

The protected 20261120_04 image was compiled against the public FreeInk SDK commit
`958720659ea289ae325e83db20049d0ea844800d` plus the four-file Nooir delta in
`nooir-1.6.7_20261120_04.patch` (SHA-256:
`3d2e37ac22cf8727bd1e6aff6431617a3f60b071e972a2d01e7857ec048b191f`). The
pinned commit is reachable from the FreeInk repository's `nooir-1.5.10-tjpgd`
branch. The Nooir source snapshot's .gitmodules gitlink referred to
`7bd6aebe9b40198d7dddf455400cee9225430726`, which was not available from the
public remote at audit time.

After normalizing line endings, all 385 FreeInk files represented in the _04
source manifest matched the reachable 9587206 checkout except these four
semantic changes:

- `BleKeyboardHost.h` and `BleKeyboardHost.cpp`: descriptor-aware generic HID,
  touch input, report metadata, lifecycle and reconnect implementation used by
  Nooir's mapping and BLE behavior.
- `SDCardManager.h` and `SDCardManager.cpp`: bounded checked reads and storage
  operation correctness used by Nooir persistence paths.

The TJpgDec header difference was line-ending-only. The FreeInk SDK's MIT
LICENSE and NOTICE remain in the pinned submodule. These are app-integration
patches; no independent FreeInk SDK release is claimed.

A PlatformIO pre-build hook checks the exact submodule HEAD, patch digest and
modified path set, then applies the patch once. A clean checkout must initialize
submodules recursively before building. The submodule is expected to appear
modified in the local checkout after a build; do not commit those working-tree
changes into the parent repository. The parent commit carries the pinned gitlink
and patch source.