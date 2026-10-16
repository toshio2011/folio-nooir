# Settings organization experiment — BEFORE inventory

- Exact device/settings source snapshot: `0d1c87933bd5fc5421ba33989991e141e0cd4adc` (`ce86b9f8`).
- Historical Web exposure source: `d4a97653` (`d4a97653`), where `/settings` GET and `/api/settings` GET/POST are registered.
- At the historical snapshot, Worker C had disabled the Web Settings routes (`4eb9b811`). The current 1.6.6 integration restores Web Settings through the bounded grouped API (`908453f3`/`7296bbc8`); Web Clippings routes remain disabled. This BEFORE inventory does not use the current API as its historical exposure baseline.
- At the `ce86b9f8` snapshot, Mini Pack application was deferred at runtime but the Interface Font row remained in the device inventory. The integrated 1.6.6 behavior keeps the row reachable and re-enables staged, lazy Mini Pack loading; this is not an intentional setting deferral.
- Home worker A's separate commit `33ed7e30` renamed persisted theme value 5 to Folio Nooir Lite and preserved stored layout fields. The current integration exposes Recent/Finished layouts in both Folio themes; the old Carousel field remains stored and migrates to those selectors. This inventory remains frozen at `ce86b9f8` as a BEFORE snapshot.

## Counts

- Base descriptor records: 89
- Additional manually managed device settings: 17
- Dynamic dictionary selector descriptors: 1
- Device-only action records: 23
- Stable Web setting keys from the pre-deferral API: 84
- Intentionally hidden/deferred records: 27
- Dynamic options (installed fonts, dictionaries) are recorded as source-defined runtime sets, not guessed from an absent device SD card.
- Proposed group IDs are included in JSON on each record; group counts include action and deferred records.

## Web Settings stable IDs before the safety deferral

- Source-possible keys (including conditional touch/tilt entries): 84.
- X4 key set after the existing touch/tilt filters: 82.
`backShortToFileBrowser`, `carouselLayout`, `clockFormat`, `clockHasBeenSynced`, `clockUtcOffsetQ`, `coverProgressStyle`, `dictionaryFontFamily`, `dictionaryFontSize`, `embeddedStyle`, `extraParagraphSpacing`, `fadingFix`, `featuredShowAuthor`, `featuredShowCover`, `featuredShowFileType`, `featuredShowFinishedDate`, `featuredShowProgress`, `featuredShowReadingTime`, `featuredShowSessions`, `featuredShowStartedDate`, `featuredShowStatus`, `featuredShowSynopsis`, `finishedBookLayout`, `focusReadingEnabled`, `fontFamily`, `fontSize`, `forceParagraphIndents`, `frontButtonFollowOrientation`, `guideDots`, `hideBatteryPercentage`, `highlightColor`, `hyphenationEnabled`, `imageRendering`, `koDeviceName`, `koMatchMethod`, `koPassword`, `koSendMetadata`, `koServerUrl`, `koSyncBehavior`, `koUsername`, `lineSpacing`, `longPressButtonBehavior`, `longPressMenuFunction`, `longPwrBtn`, `moveFinishedToReadFolder`, `opdsDownloadFolder`, `opdsFilenameFormat`, `orientation`, `pageNumberMode`, `paragraphAlignment`, `pwrBtnFootnoteBack`, `quickResumeSleepScreen`, `randomSleepOrder`, `readerDarkMode`, `recentBookLayout`, `refreshFrequency`, `removeReadBooksFromRecents`, `resumeReaderOnWake`, `screenMargin`, `shortPwrBtn`, `showHiddenFiles`, `sideButtonLayout`, `sideLongPressAction`, `sleepScreen`, `sleepScreenCoverFilter`, `sleepScreenCoverMode`, `sleepTimeoutMinutes`, `spineAppearanceMode`, `spineDecorationStyle`, `statusBarBattery`, `statusBarBookProgressPercentage`, `statusBarChapterPageCount`, `statusBarClock`, `statusBarPercentageFormat`, `statusBarProgressBar`, `statusBarProgressBarThickness`, `statusBarTitle`, `textAntiAliasing`, `todoSleepMode`, `uiScalePercent`, `uiTheme`, `weatherSyncEnabled`, `xtcStatusBarMode`

## Special decisions

- Flexible Sleep enum value 17 and its three persisted composer settings are preserved in the inventory as intentionally hidden/deferred.
- Web Clippings/Notes is included as a deferred capability backed by the existing per-book ClipFile JSON. `/My Clippings.txt` remains unchanged.
- The current Web settings route is not a parity baseline: the user-directed comparison baseline is the pre-Worker-C exposed key set above.
- Full machine-readable option/range/default/source metadata is in `BEFORE_inventory.json`.
