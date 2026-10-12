#include "ClockLocationActivity.h"

#include <GfxRenderer.h>
#include <I18n.h>
#include <WiFi.h>

#include <algorithm>
#include <cstring>
#include <memory>

#include "ClockWeatherSyncService.h"
#include "WeatherStore.h"
#include "activities/network/WifiSelectionActivity.h"
#include "activities/util/KeyboardEntryActivity.h"
#include "components/UITheme.h"
#include "fontIds.h"

namespace {
std::string trimText(std::string value) {
  const auto first = value.find_first_not_of(" \t\r\n");
  if (first == std::string::npos) return {};
  const auto last = value.find_last_not_of(" \t\r\n");
  return value.substr(first, last - first + 1);
}
}  // namespace

void ClockLocationActivity::onEnter() {
  Activity::onEnter();
  ignoreInitialConfirmRelease = mappedInput.isPressed(MappedInputManager::Button::Confirm);
  ignoreInitialBackRelease = mappedInput.isPressed(MappedInputManager::Button::Back);
  query = WEATHER_STORE.location;
  status = "Enter a city or location name.";
  requestUpdate();
}

void ClockLocationActivity::onExit() {
  if (wifiSessionStarted && WiFi.getMode() != WIFI_MODE_NULL) {
    WiFi.disconnect(false);
    WiFi.mode(WIFI_OFF);
  }
  Activity::onExit();
}

void ClockLocationActivity::editLocation() {
  startActivityForResult(
      std::make_unique<KeyboardEntryActivity>(renderer, mappedInput, "Change location", query, 47),
      [this](const ActivityResult& result) {
        if (result.isCancelled || !std::holds_alternative<KeyboardResult>(result.data)) {
          requestUpdate(true);
          return;
        }
        query = trimText(std::get<KeyboardResult>(result.data).text);
        if (query.empty()) {
          state = State::ERROR;
          status = "Enter a city name to continue.";
          requestUpdate(true);
          return;
        }
        if (WiFi.status() == WL_CONNECTED) {
          state = State::RESOLVING;
          status = "Resolving location...";
          requestUpdate(true);
        } else {
          launchWifiSelection();
        }
      });
}

void ClockLocationActivity::launchWifiSelection() {
  state = State::RESOLVING;
  status = "Connect Wi-Fi to resolve location...";
  requestUpdate(true);
  startActivityForResult(std::make_unique<WifiSelectionActivity>(renderer, mappedInput, true, false),
                         [this](const ActivityResult& result) {
                           const auto* wifi = std::get_if<WifiResult>(&result.data);
                           if (result.isCancelled || !wifi || !wifi->connected) {
                             state = State::ERROR;
                             status = "Wi-Fi is required to resolve the location.";
                             requestUpdate(true);
                             return;
                           }
                           wifiSessionStarted = true;
                           state = State::RESOLVING;
                           status = "Resolving location...";
                           requestUpdate(true);
                         });
}

void ClockLocationActivity::resolveLocation() {
  ClockWeatherLocation resolved;
  if (!ClockWeatherSyncService::resolveLocation(query, resolved)) {
    state = State::ERROR;
    status = "Location not found. Try a city name.";
    requestUpdate(true);
    return;
  }

  strncpy(WEATHER_STORE.location, resolved.name.c_str(), sizeof(WEATHER_STORE.location) - 1);
  WEATHER_STORE.location[sizeof(WEATHER_STORE.location) - 1] = '\0';
  strncpy(WEATHER_STORE.latitude, resolved.latitude.c_str(), sizeof(WEATHER_STORE.latitude) - 1);
  WEATHER_STORE.latitude[sizeof(WEATHER_STORE.latitude) - 1] = '\0';
  strncpy(WEATHER_STORE.longitude, resolved.longitude.c_str(), sizeof(WEATHER_STORE.longitude) - 1);
  WEATHER_STORE.longitude[sizeof(WEATHER_STORE.longitude) - 1] = '\0';
  strncpy(WEATHER_STORE.timezone, resolved.timezone.c_str(), sizeof(WEATHER_STORE.timezone) - 1);
  WEATHER_STORE.timezone[sizeof(WEATHER_STORE.timezone) - 1] = '\0';
  // The weather request will provide the new location's current UTC offset.
  // Do not continue showing the previous city's offset while that request is
  // in progress.
  WEATHER_STORE.locationUtcOffsetSeconds = 0;
  WEATHER_STORE.hasLocationTimezone = 0;
  WEATHER_STORE.clearWeatherCache();
  WEATHER_STORE.lastClockSyncDateKey = 0;
  WEATHER_STORE.lastClockSyncEpoch = 0;
  WEATHER_STORE.saveToFile();
  state = State::SYNCING;
  status = "Syncing clock and weather...";
  requestUpdate(true);
}

void ClockLocationActivity::syncResolvedLocation() {
  const ClockWeatherSyncResult result = ClockWeatherSyncService::sync(true, true);
  if (!result.clockSynced && !result.weatherSynced) {
    state = State::ERROR;
    status = "Sync failed. Press Edit to try again.";
    requestUpdate(true);
    return;
  }
  // The parent Clock & Weather activity reloads the persisted store when this
  // activity returns, so no second clock/weather implementation is needed.
  finish();
}

void ClockLocationActivity::loop() {
  if (ignoreInitialConfirmRelease) {
    if (mappedInput.wasReleased(MappedInputManager::Button::Confirm)) ignoreInitialConfirmRelease = false;
    return;
  }
  if (ignoreInitialBackRelease) {
    if (mappedInput.wasReleased(MappedInputManager::Button::Back)) ignoreInitialBackRelease = false;
    return;
  }

  if (state == State::RESOLVING) {
    requestUpdateAndWait();
    resolveLocation();
    return;
  }
  if (state == State::SYNCING) {
    requestUpdateAndWait();
    syncResolvedLocation();
    return;
  }

  if (mappedInput.wasReleased(MappedInputManager::Button::Back)) {
    finish();
    return;
  }
  if (mappedInput.wasReleased(MappedInputManager::Button::Confirm)) {
    editLocation();
  }
}

void ClockLocationActivity::render(RenderLock&&) {
  const auto& metrics = UITheme::getInstance().getMetrics();
  const int width = renderer.getScreenWidth();
  renderer.clearScreen();
  GUI.drawHeader(renderer, Rect{0, metrics.topPadding, width, metrics.headerHeight}, "Change location");

  const int lineHeight = renderer.getLineHeight(UI_10_FONT_ID);
  const int centre = renderer.getScreenHeight() / 2;
  renderer.drawCenteredText(UI_12_FONT_ID, centre - lineHeight * 2, WEATHER_STORE.location, true,
                            EpdFontFamily::BOLD);
  renderer.drawCenteredText(UI_10_FONT_ID, centre - lineHeight / 2, status.c_str());
  if (state == State::EDIT || state == State::ERROR) {
    renderer.drawCenteredText(UI_10_FONT_ID, centre + lineHeight + 8, "Confirm to enter a city name");
  }

  const auto labels = mappedInput.mapLabels(tr(STR_BACK), (state == State::EDIT || state == State::ERROR) ? "Edit" : "",
                                            "", "");
  GUI.drawButtonHints(renderer, labels.btn1, labels.btn2, labels.btn3, labels.btn4);
  renderer.displayBuffer();
}
