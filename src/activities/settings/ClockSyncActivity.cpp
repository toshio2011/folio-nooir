#include "ClockSyncActivity.h"

#include <GfxRenderer.h>
#include <HalClock.h>
#include <I18n.h>
#include <Logging.h>
#include <WiFi.h>

#include <cstdio>

#include "ClockWeatherSyncService.h"
#include "CrossPointSettings.h"
#include "MappedInputManager.h"
#include "WeatherStore.h"
#include "activities/network/WifiSelectionActivity.h"
#include "components/UITheme.h"
#include "fontIds.h"

void ClockSyncActivity::onEnter() {
  Activity::onEnter();
  state = SYNCING;
  syncedTime[0] = '\0';
  weatherLine[0] = '\0';

  // This is an explicit device action, so its Wi-Fi session is always
  // one-shot.  The Web UI uses ClockWeatherSyncService directly and keeps
  // its own server session alive instead.
  shouldTearDownWifiOnExit = true;

  if (WiFi.status() == WL_CONNECTED) {
    requestUpdate();
    return;
  }

  launchWifiSelection();
}

void ClockSyncActivity::onExit() {
  Activity::onExit();

  if (shouldTearDownWifiOnExit && WiFi.getMode() != WIFI_MODE_NULL) {
    WiFi.disconnect(false);
    WiFi.mode(WIFI_OFF);
  }
}

void ClockSyncActivity::launchWifiSelection() {
  LOG_INF("CLK", "Manual sync requested without WiFi, launching WiFi selection");
  // The explicit sync performs its own request after the Wi-Fi activity
  // returns; avoid doing a duplicate automatic sync while connecting.
  startActivityForResult(std::make_unique<WifiSelectionActivity>(renderer, mappedInput, true, false),
                         [this](const ActivityResult& result) { onWifiSelectionComplete(!result.isCancelled); });
}

void ClockSyncActivity::onWifiSelectionComplete(const bool connected) {
  if (!connected) {
    LOG_INF("CLK", "WiFi selection cancelled before manual clock sync");
    finish();
    return;
  }

  state = SYNCING;
  requestUpdate();
}

void ClockSyncActivity::runSync() {
  if (WiFi.status() != WL_CONNECTED) {
    LOG_INF("CLK", "Manual sync requested but WiFi is not connected after selection");
    state = NO_WIFI;
    requestUpdate();
    return;
  }

  const ClockWeatherSyncResult result = ClockWeatherSyncService::sync(true, syncWeather);

  // This device action is deliberately one-shot: release the radio as soon
  // as the request is complete rather than keeping Wi-Fi alive while the
  // result screen waits for Back.  The Web UI does not use this activity.
  if (shouldTearDownWifiOnExit) {
    WiFi.disconnect(false);
    WiFi.mode(WIFI_OFF);
    shouldTearDownWifiOnExit = false;
  }

  // Read the freshly synced time back for the user-facing confirmation.
  char buf[9];
  if (halClock.formatTime(buf, sizeof(buf), ClockWeatherSyncService::locationOffsetQuarterHours(),
                          SETTINGS.clockFormat == 1)) {
    snprintf(syncedTime, sizeof(syncedTime), "%s", buf);
  }
  if (result.weatherSynced) {
    const float temperature = WEATHER_STORE.temperatureTenths / 10.0f;
    snprintf(weatherLine, sizeof(weatherLine), "%s: %.1f %s (%s)", WEATHER_STORE.location, temperature,
             WEATHER_STORE.fahrenheit ? "F" : "C",
             ClockWeatherSyncService::weatherDescription(WEATHER_STORE.weatherCode));
  }

  // X4 has no hardware RTC, so a successful weather refresh is still a
  // successful Sync Clock & Weather action even though clockSynced is false.
  const bool clockSatisfied = result.clockSynced || (!halClock.isAvailable() && syncWeather);
  if (clockSatisfied && (!syncWeather || result.weatherSynced)) {
    state = SUCCESS;
  } else if (result.clockSynced) {
    state = PARTIAL;
  } else if (syncWeather && result.weatherSkippedNoLocation) {
    state = NO_LOCATION;
  } else {
    state = FAILED;
  }
  requestUpdate();
}

void ClockSyncActivity::loop() {
  if (state == SYNCING) {
    // First-tick: render the "Syncing..." screen, then perform the (blocking) sync.
    // requestUpdateAndWait below forces the render before we block on WiFi.
    requestUpdateAndWait();
    runSync();
    return;
  }

  int x = 0;
  int y = 0;
  if (mappedInput.wasPressed(MappedInputManager::Button::Back) || mappedInput.wasScreenTapped(x, y)) {
    finish();
  }
}

void ClockSyncActivity::render(RenderLock&&) {
  const auto& metrics = UITheme::getInstance().getMetrics();
  const auto pageWidth = renderer.getScreenWidth();
  const auto pageHeight = renderer.getScreenHeight();

  renderer.clearScreen();

  const char* header = syncWeather ? I18n::getInstance().get(StrId::STR_CLOCK_WEATHER_SYNC)
                                   : I18n::getInstance().get(StrId::STR_CLOCK_SYNC);
  const char* syncing = syncWeather ? I18n::getInstance().get(StrId::STR_CLOCK_WEATHER_SYNCING)
                                    : I18n::getInstance().get(StrId::STR_CLOCK_SYNCING);
  GUI.drawHeader(renderer, Rect{0, metrics.topPadding, pageWidth, metrics.headerHeight}, header);

  const int midY = pageHeight / 2;

  switch (state) {
    case SYNCING:
      renderer.drawCenteredText(UI_12_FONT_ID, midY, syncing);
      break;
    case SUCCESS: {
      const char* success = syncWeather ? I18n::getInstance().get(StrId::STR_CLOCK_WEATHER_SYNC_OK)
                                         : I18n::getInstance().get(StrId::STR_CLOCK_SYNC_OK);
      renderer.drawCenteredText(UI_12_FONT_ID, midY - 34, success, true, EpdFontFamily::BOLD);
      if (syncedTime[0] != '\0') {
        char line[32];
        snprintf(line, sizeof(line), "%s %s", tr(STR_CURRENT_TIME), syncedTime);
        renderer.drawCenteredText(UI_10_FONT_ID, midY - 4, line);
      }
      if (weatherLine[0] != '\0') {
        renderer.drawCenteredText(UI_10_FONT_ID, midY + 24, weatherLine);
      }
      break;
    }
    case PARTIAL:
      renderer.drawCenteredText(UI_12_FONT_ID, midY - 20, tr(STR_CLOCK_WEATHER_SYNC_PARTIAL), true,
                                EpdFontFamily::BOLD);
      if (syncedTime[0] != '\0') {
        char line[32];
        snprintf(line, sizeof(line), "%s %s", tr(STR_CURRENT_TIME), syncedTime);
        renderer.drawCenteredText(UI_10_FONT_ID, midY + 10, line);
      }
      break;
    case NO_LOCATION:
      renderer.drawCenteredText(UI_12_FONT_ID, midY - 20, tr(STR_CLOCK_WEATHER_NO_LOCATION), true,
                                EpdFontFamily::BOLD);
      renderer.drawCenteredText(UI_10_FONT_ID, midY + 10, "Set location in Web UI.");
      break;
    case NO_WIFI:
      renderer.drawCenteredText(UI_12_FONT_ID, midY - 20, tr(STR_CLOCK_SYNC_NO_WIFI), true, EpdFontFamily::BOLD);
      renderer.drawCenteredText(UI_10_FONT_ID, midY + 10, tr(STR_CLOCK_SYNC_NO_WIFI_HINT));
      break;
    case FAILED:
      renderer.drawCenteredText(UI_12_FONT_ID, midY - 20, tr(STR_CLOCK_SYNC_FAIL), true, EpdFontFamily::BOLD);
      renderer.drawCenteredText(UI_10_FONT_ID, midY + 10, tr(STR_CHECK_SERIAL_OUTPUT));
      break;
  }

  if (state != SYNCING) {
    const auto labels = mappedInput.mapLabels(tr(STR_BACK), "", "", "");
    GUI.drawButtonHints(renderer, labels.btn1, labels.btn2, labels.btn3, labels.btn4);
  }

  renderer.displayBuffer();
}
