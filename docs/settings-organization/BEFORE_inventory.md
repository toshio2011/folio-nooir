# 1.6.6 Settings Organization — Frozen Inventory

- Exact device/settings source snapshot: `ce86b9f8c85d774af297ef11e6f98c4bee9e0729` (`ce86b9f8`).
- Historical Web exposure source: `d4a97653` (`d4a97653`), where `/settings` GET and `/api/settings` GET/POST are registered.
- Current Web routes remain disabled by Worker C commit `4eb9b811`; Clippings routes remain disabled. No route was re-enabled to make this inventory.
- Custom Interface Font application is deferred for the 1.6.6 RC; this frozen descriptor inventory retains the row and persisted field for parity comparison.
- This frozen descriptor inventory is compared with the protected 1.6.5 baseline and current Settings tabs/sections.

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
