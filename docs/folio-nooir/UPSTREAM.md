# Folio Nooir upstream policy

Folio Nooir is a downstream distribution of CrossPoint Reader. It follows
CrossPoint's beta line while keeping product-specific changes isolated enough to
review, test, and carry forward.

## Remotes and branches

- `upstream`: `https://github.com/crosspoint-reader/crosspoint-reader.git`
- `upstream/develop`: the CrossPoint beta baseline
- `upstream/master`: the CrossPoint stable baseline
- `upstream/feat-bluetooth`: Bluetooth work under active upstream development
- `codex/folio-nooir`: the local Folio Nooir integration branch

An `origin` remote will be added when the Folio Nooir GitHub repository exists.

## Update lanes

Folio Nooir publishes three lanes:

1. **Stable** follows a tested CrossPoint release tag.
2. **Beta** follows a tested snapshot of `upstream/develop`.
3. **Experimental** adds unfinished upstream feature branches, including
   Bluetooth, on top of the current beta snapshot.

The device must never update directly from an arbitrary CrossPoint branch.
Folio Nooir first builds and tests the upstream snapshot, then publishes a
Folio Nooir image with an explicit upstream commit recorded in its version
metadata.

## Integration rules

- Preserve upstream files and behavior unless a Folio Nooir requirement needs a
  change.
- Put the bookshelf UI, branding, statistics, and remote-specific policy in
  Folio Nooir-owned modules where possible.
- Integrate upstream Bluetooth work as a reviewed commit series; do not copy a
  compiled firmware image or maintain a second BLE stack.
- Keep branding changes separate from behavioral changes.
- Never edit generated i18n or HTML headers.
- Build the combined X3/X4 target after every upstream integration.
- Require physical X4 verification for BLE, sleep/wake, display refresh, and
  Wi-Fi coexistence before promoting a build.
- Require physical X3 beta feedback before promoting an X3 build to stable.

## Sync procedure

```powershell
git fetch upstream --tags --prune
git switch codex/folio-nooir
git merge --no-ff upstream/develop
git submodule update --init --recursive
.\.venv\Scripts\pio.exe run -e default
```

If the merge conflicts with Folio Nooir code, resolve the smallest possible
surface and keep the upstream behavior unless a documented Folio Nooir
requirement intentionally differs.

## Bluetooth integration

CrossPoint's current `feat-bluetooth` branch diverges from `develop`. Folio
Nooir must not merge it wholesale without review because it also contains
rendering, font, TLS, and heap-management changes. Integration starts with the
BLE HID host and settings commits, followed by only the memory and lifecycle
fixes they require. Each step must compile before the next step is applied.

## Release identity

Every Folio Nooir build should expose:

- Folio Nooir version
- target device profile
- CrossPoint upstream commit
- Folio Nooir commit
- release lane

This makes device logs and bug reports reproducible even when following fast
moving CrossPoint beta snapshots.
