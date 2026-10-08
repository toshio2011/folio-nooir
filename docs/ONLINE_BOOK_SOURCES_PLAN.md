# Folio Nooir — Future Online Book Sources Plan

**Status:** Future exploration only. Not part of the 1.6.4 stabilization/release scope.

## Goal

Give Nooir a lightweight on-device way to browse/search online book catalogs and download supported books directly to the SD card without turning the X3/X4 into a general-purpose web browser.

The preferred foundation is **OPDS** rather than adding a separate firmware client for every website/service.

## Why OPDS first

OPDS is a standard catalog/acquisition protocol for ebooks. A capable Nooir OPDS client can provide one reusable flow for many different catalogs and personal servers:

- browse catalogs/categories;
- search;
- title/author/description metadata;
- covers;
- format/file-size information when supplied;
- pagination;
- authenticated personal catalogs such as Calibre/Calibre-Web where supported;
- direct EPUB download to SD;
- choose destination folder;
- refresh the Nooir library/index after download;
- optionally open the newly downloaded book.

This should be treated as the generic **Online Books / Book Sources** layer rather than tying the UI to one provider.

## Suggested Nooir UI

Possible entry point:

`Home -> Online Books`

Suggested flow:

1. Select a configured source/catalog.
2. Browse or search.
3. Show a bounded page of results with cover, title, author, year/language/format/size when available.
4. Open lightweight details.
5. Download supported EPUB directly to a temporary SD file.
6. Atomically rename after successful completion.
7. Refresh library metadata/index.
8. Offer Open Now.

## Architecture rules

The X3/X4 should do as little internet-side processing as possible.

Prefer:

- standard OPDS feeds;
- compact bounded responses;
- paginated result sets;
- lazy cover loading, one/few at a time;
- streamed download directly to SD;
- no full-book buffering in RAM;
- no large retained JSON/XML document;
- bounded strings/results;
- deterministic temporary-file cleanup;
- atomic publication of completed downloads;
- no Reader-time network work.

Avoid:

- scraping arbitrary HTML directly on the device;
- browser-like parsing/rendering;
- large result lists;
- long-lived network buffers;
- background sync/polling while reading.

## Optional provider/relay adapters

If a useful catalog does not expose OPDS, prefer an **external lightweight adapter/relay** that presents compact OPDS-like/catalog results to Nooir instead of adding a dedicated website scraper to firmware.

Conceptually:

`Nooir -> OPDS / compact provider API -> adapter/relay -> upstream catalog`

This keeps changing web/API details off the ESP32-C3 and lets the firmware retain one bounded search/download UI.

CrossCover's LibGen integration is a useful architectural reference because it uses a lightweight relay rather than making the X3/X4 parse the full website. Do not copy it blindly; first evaluate what can be generalized into Nooir's provider abstraction.

Possible future investigation can include third-party catalog adapters, including services users are legally authorized to access. Prioritize lawful/open catalogs such as Standard Ebooks, Project Gutenberg, personal Calibre/Calibre-Web servers, and other OPDS-compatible libraries first.

## BLE / Wi-Fi lifecycle

Online catalog browsing/downloading is resource-sensitive network work.

Preferred lifecycle when BLE is enabled:

1. Enter Online Books.
2. Temporarily stop BLE if required by the existing network/resource policy.
3. Start Wi-Fi.
4. Browse/search/download.
5. Fully tear down network/catalog resources on exit.
6. Refresh library if needed.
7. Re-admit BLE through the normal wanted/pending lifecycle when memory floors pass.

When BLE is disabled, the feature must follow the normal BLE-less Nooir path with no BLE admission or memory-floor influence.

## Relationship to current OPDS support

Before adding new source-specific code, audit Nooir's existing OPDS implementation and improve it into a polished first-class Online Books experience where practical.

Questions for a later engineering round:

- What OPDS 1.x/2.0 behaviors are already supported?
- Are search templates supported?
- Multiple saved catalogs?
- Authentication?
- Pagination?
- Cover acquisition/cache behavior?
- Direct EPUB acquisition?
- Destination-folder selection?
- Library refresh/open-after-download?
- Memory usage during catalog parsing?
- Network teardown and failure recovery?

## Scope / release placement

Do **not** add this during 1.6.4 stabilization.

Potential later release sequence:

1. Audit and polish existing OPDS.
2. Build reusable Online Books / Book Sources UI around OPDS.
3. Add safe authentication/catalog management.
4. Add robust streamed SD downloads and automatic library refresh.
5. Only then evaluate optional compact provider/relay adapters for non-OPDS catalogs.

This is a candidate for 1.6.5/1.7 or later, after BLE/Web UI stability and the 1.6.4 release are complete.
