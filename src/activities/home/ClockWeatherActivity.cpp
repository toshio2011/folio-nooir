#include "ClockWeatherActivity.h"

#include <HalClock.h>
#include <I18n.h>
#include <GfxRenderer.h>
#include <EpdFontFamily.h>

#include <algorithm>
#include <cstdio>
#include <ctime>
#include <memory>

#include "ClockWeatherSyncService.h"
#include "CrossPointSettings.h"
#include "WeatherStore.h"
#include "activities/settings/ClockSyncActivity.h"
#include "components/UITheme.h"
#include "fontIds.h"

namespace {
std::string dateText(const uint32_t key) {
  if (key == 0) return "Unavailable";
  char buf[16];
  snprintf(buf, sizeof(buf), "%04lu-%02lu-%02lu", static_cast<unsigned long>(key / 10000),
           static_cast<unsigned long>((key / 100) % 100), static_cast<unsigned long>(key % 100));
  return buf;
}

uint8_t displayOffsetQ() {
  if (!WEATHER_STORE.hasLocationTimezone) return SETTINGS.clockUtcOffsetQ;
  const int quarterHours = static_cast<int>((WEATHER_STORE.locationUtcOffsetSeconds +
                                              (WEATHER_STORE.locationUtcOffsetSeconds >= 0 ? 450 : -450)) /
                                             900);
  return static_cast<uint8_t>(std::max(0, std::min(104, quarterHours + 48)));
}

uint32_t displayDateKey() {
  if (!WEATHER_STORE.hasLocationTimezone) return halClock.getDateKey();
  const time_t now = time(nullptr);
  if (now <= 100000) return halClock.getDateKey();
  const time_t localNow = now + WEATHER_STORE.locationUtcOffsetSeconds;
  struct tm timeinfo;
  gmtime_r(&localNow, &timeinfo);
  return static_cast<uint32_t>(timeinfo.tm_year + 1900) * 10000UL +
         static_cast<uint32_t>(timeinfo.tm_mon + 1) * 100UL + static_cast<uint32_t>(timeinfo.tm_mday);
}
}  // namespace

void ClockWeatherActivity::onEnter() {
  Activity::onEnter();
  ignoreInitialConfirmRelease = mappedInput.isPressed(MappedInputManager::Button::Confirm);
  ignoreInitialBackRelease = mappedInput.isPressed(MappedInputManager::Button::Back);
  rebuildLines();
  requestUpdate();
}

void ClockWeatherActivity::rebuildLines() {
  renderer.setUiScaleTextEnabled(true);
  lines.clear();

  char time[16] = {};
  if (halClock.formatTime(time, sizeof(time), displayOffsetQ(), SETTINGS.clockFormat == 1)) {
    lines.emplace_back(std::string("Clock: ") + time);
    lines.emplace_back("Date: " + dateText(displayDateKey()));
  } else {
    lines.emplace_back("Clock: unavailable on this device");
    lines.emplace_back("Date: unavailable");
  }

  if (WEATHER_STORE.hasWeather) {
    char weather[80];
    snprintf(weather, sizeof(weather), "Weather: %.1f %s - %s",
             WEATHER_STORE.temperatureTenths / 10.0f, WEATHER_STORE.fahrenheit ? "F" : "C",
             ClockWeatherSyncService::weatherDescription(WEATHER_STORE.weatherCode));
    lines.emplace_back(weather);
    lines.emplace_back(std::string("Location: ") + WEATHER_STORE.location);
    lines.emplace_back("Last weather sync: " + dateText(WEATHER_STORE.lastWeatherSyncDateKey));
  } else {
    lines.emplace_back("Weather: no cached reading");
    lines.emplace_back(std::string("Location: ") + WEATHER_STORE.location);
    lines.emplace_back("Last weather sync: Never");
  }
  lines.emplace_back("Last clock sync: " + dateText(WEATHER_STORE.lastClockSyncDateKey));

  lines.emplace_back("Wi-Fi auto clock: " + std::string(SETTINGS.clockSyncEnabled ? "On" : "Off"));
  lines.emplace_back("Wi-Fi auto weather: " + std::string(SETTINGS.weatherSyncEnabled ? "On" : "Off"));
  lines.emplace_back("");
  lines.emplace_back("Select Sync now to connect once and refresh.");
}

void ClockWeatherActivity::syncNow() {
  startActivityForResult(std::make_unique<ClockSyncActivity>(renderer, mappedInput, true),
                         [this](const ActivityResult&) {
                           WEATHER_STORE.loadFromFile();
                           rebuildLines();
                           // ClockSyncActivity can finish on the same Back
                           // release that returned to this page. Do not let
                           // that release close Clock & Weather too.
                           suppressBackUntilRelease =
                               mappedInput.isPressed(MappedInputManager::Button::Back) ||
                               mappedInput.wasReleased(MappedInputManager::Button::Back);
                           requestUpdate();
                         });
}

void ClockWeatherActivity::loop() {
  if (ignoreInitialConfirmRelease) {
    if (mappedInput.wasReleased(MappedInputManager::Button::Confirm)) ignoreInitialConfirmRelease = false;
    return;
  }
  if (ignoreInitialBackRelease) {
    if (mappedInput.wasReleased(MappedInputManager::Button::Back)) ignoreInitialBackRelease = false;
    return;
  }
  if (suppressBackUntilRelease) {
    if (mappedInput.wasReleased(MappedInputManager::Button::Back) ||
        !mappedInput.isPressed(MappedInputManager::Button::Back)) {
      suppressBackUntilRelease = false;
    }
    return;
  }
  if (mappedInput.wasReleased(MappedInputManager::Button::Back)) {
    finish();
    return;
  }
  if (mappedInput.wasReleased(MappedInputManager::Button::Confirm)) {
    syncNow();
  }
}

void ClockWeatherActivity::render(RenderLock&&) {
  renderer.setUiScaleTextEnabled(true);
  renderer.clearScreen();
  const auto& metrics = UITheme::getInstance().getMetrics();
  const int side = metrics.contentSidePadding;
  const int width = renderer.getScreenWidth() - side * 2;
  GUI.drawHeader(renderer, Rect{0, metrics.topPadding, renderer.getScreenWidth(), metrics.headerHeight},
                 tr(STR_CLOCK_WEATHER));

  const int top = metrics.topPadding + metrics.headerHeight + metrics.verticalSpacing;
  const int bottom = renderer.getScreenHeight() - metrics.buttonHintsHeight - metrics.verticalSpacing;
  const int lineHeight = renderer.getLineHeight(UI_10_FONT_ID);
  const int cardGap = metrics.verticalSpacing;
  const int cardWidth = width;
  const int cardHeight = lineHeight * 3 + metrics.verticalSpacing * 2;
  const int cardY = top;
  const int weatherY = cardY + cardHeight + cardGap;

  // A pair of quiet, information-dense cards makes the page readable at a
  // glance without adding a network request or a heavy redraw.
  renderer.drawRect(side, cardY, cardWidth, cardHeight);
  renderer.drawRect(side, weatherY, cardWidth, cardHeight);
  renderer.drawText(SMALL_FONT_ID, side + 8, cardY + 6, "CLOCK", true, EpdFontFamily::BOLD);
  renderer.drawText(SMALL_FONT_ID, side + 8, weatherY + 6, "WEATHER", true, EpdFontFamily::BOLD);
  if (lines.size() >= 2) {
    const std::string clockLine = renderer.truncatedText(UI_10_FONT_ID, lines[0].c_str(), cardWidth - 16);
    const std::string dateLine = renderer.truncatedText(UI_10_FONT_ID, lines[1].c_str(), cardWidth - 16);
    renderer.drawText(UI_10_FONT_ID, side + 8, cardY + lineHeight + 7, clockLine.c_str(), true,
                      EpdFontFamily::BOLD);
    renderer.drawText(UI_10_FONT_ID, side + 8, cardY + lineHeight * 2 + 7, dateLine.c_str(), true);
  }
  if (lines.size() >= 3) {
    const std::string weatherLine = renderer.truncatedText(UI_10_FONT_ID, lines[2].c_str(), cardWidth - 16);
    renderer.drawText(UI_10_FONT_ID, side + 8, weatherY + lineHeight + 7, weatherLine.c_str(), true,
                      EpdFontFamily::BOLD);
    if (lines.size() >= 4) {
      const std::string locationLine = renderer.truncatedText(UI_10_FONT_ID, lines[3].c_str(), cardWidth - 16);
      renderer.drawText(UI_10_FONT_ID, side + 8, weatherY + lineHeight * 2 + 7, locationLine.c_str(), true);
    }
  }

  const int detailTop = weatherY + cardHeight + metrics.verticalSpacing;
  const int detailHeight = std::max(0, bottom - detailTop);
  const size_t visibleLines = static_cast<size_t>(std::max(1, detailHeight / lineHeight));
  // The cards already contain the first four lines; the lower area is for
  // sync freshness and the two opt-in automatic-sync switches.
  for (size_t i = 4; i < std::min(lines.size(), visibleLines + 4); ++i) {
    const std::string line = renderer.truncatedText(UI_10_FONT_ID, lines[i].c_str(), width);
    renderer.drawText(UI_10_FONT_ID, side, detailTop + static_cast<int>(i - 4) * lineHeight, line.c_str(), true);
  }

  const auto labels = mappedInput.mapLabels(tr(STR_BACK), tr(STR_SYNC_NOW), "", "");
  GUI.drawButtonHints(renderer, labels.btn1, labels.btn2, labels.btn3, labels.btn4);
  renderer.displayBuffer();
}
