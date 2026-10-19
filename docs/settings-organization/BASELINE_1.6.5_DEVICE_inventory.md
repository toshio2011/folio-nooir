# 1.6.5 Device Settings Baseline

- Exact device/settings source snapshot: `3a66ef9b8b88c538e96ac763878fa899e72a53b1` (`3a66ef9b`).
- Historical Web exposure source: `d4a97653` (`d4a97653`), where `/settings` GET and `/api/settings` GET/POST are registered.
- Current Web routes remain disabled by Worker C commit `4eb9b811`; Clippings routes remain disabled. No route was re-enabled to make this inventory.
- The Interface Font row was available in this 1.6.5 baseline; the 1.6.6 RC hides the row, retains the saved value, and uses built-in UI fonts.
- This file records the protected 1.6.5 device source; no baseline files were changed.

## Counts

- Base descriptor records: 84
- Additional manually managed device settings: 17
- Dynamic dictionary selector descriptors: 1
- Device-only action records: 21
- Stable Web setting keys from the pre-deferral API: 83
- Intentionally hidden/deferred records: 21
- Dynamic options (installed fonts, dictionaries) are recorded as source-defined runtime sets, not guessed from an absent device SD card.
- Proposed group IDs are included in JSON on each record; group counts include action and deferred records.

## Web Settings stable IDs before the safety deferral

- Source-possible keys (including conditional touch/tilt entries): 83.
- X4 key set after the existing touch/tilt filters: 81.
`backShortToFileBrowser`, `carouselLayout`, `clockFormat`, `clockHasBeenSynced`, `clockUtcOffsetQ`, `coverProgressStyle`, `dictionaryFontFamily`, `dictionaryFontSize`, `embeddedStyle`, `extraParagraphSpacing`, `fadingFix`, `featuredShowAuthor`, `featuredShowCover`, `featuredShowFileType`, `featuredShowFinishedDate`, `featuredShowProgress`, `featuredShowReadingTime`, `featuredShowSessions`, `featuredShowStartedDate`, `featuredShowStatus`, `featuredShowSynopsis`, `finishedBookLayout`, `focusReadingEnabled`, `fontFamily`, `fontSize`, `forceParagraphIndents`, `frontButtonFollowOrientation`, `guideDots`, `hideBatteryPercentage`, `highlightColor`, `hyphenationEnabled`, `imageRendering`, `koDeviceName`, `koMatchMethod`, `koPassword`, `koSendMetadata`, `koServerUrl`, `koSyncBehavior`, `koUsername`, `lineSpacing`, `longPressButtonBehavior`, `longPressMenuFunction`, `longPwrBtn`, `moveFinishedToReadFolder`, `opdsDownloadFolder`, `opdsFilenameFormat`, `orientation`, `pageNumberMode`, `paragraphAlignment`, `pwrBtnFootnoteBack`, `quickResumeSleepScreen`, `readerDarkMode`, `recentBookLayout`, `refreshFrequency`, `removeReadBooksFromRecents`, `resumeReaderOnWake`, `screenMargin`, `shortPwrBtn`, `showHiddenFiles`, `sideButtonLayout`, `sideLongPressAction`, `sleepScreen`, `sleepScreenCoverFilter`, `sleepScreenCoverMode`, `sleepTimeoutMinutes`, `spineAppearanceMode`, `spineDecorationStyle`, `statusBarBattery`, `statusBarBookProgressPercentage`, `statusBarChapterPageCount`, `statusBarClock`, `statusBarPercentageFormat`, `statusBarProgressBar`, `statusBarProgressBarThickness`, `statusBarTitle`, `textAntiAliasing`, `todoSleepMode`, `uiScalePercent`, `uiTheme`, `weatherSyncEnabled`, `xtcStatusBarMode`

## Special decisions

- Flexible Sleep is absent from the 1.6.5 device baseline; its 1.6.6 persisted enum and composer settings are separately inventoried as hidden/deferred.
- Web Clippings/Notes is included as a deferred capability backed by the existing per-book ClipFile JSON. `/My Clippings.txt` remains unchanged.
- The current Web settings route is not a parity baseline: the user-directed comparison baseline is the pre-Worker-C exposed key set above.
- Full machine-readable option/range/default/source metadata is in `BASELINE_1.6.5_DEVICE_inventory.json`.
