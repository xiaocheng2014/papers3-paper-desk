#pragma once

#include <Arduino.h>
#include <M5Unified.h>

#include "AppState.h"
#include "LiteracyContent.h"
#include "WeatherService.h"

namespace paperdesk {

class ClockView {
 public:
  void begin();
  void setPortrait(bool portrait);
  void showConnecting(const String& ssid);
  void showProvisioning(const String& apSsid, const String& error = String());
  void showClock(time_t now, const String& ssid, const String& ip, bool ntpSynced,
                 int batteryPercent, const WeatherSnapshot& weather,
                 bool forceQuality = false);
  void showPortraitMenu(AppState::Mode selected);
  void showLiteracy(size_t index);
  void showBadge(const AppState& state);
  void showTodo(const AppState& state);
  void showTodoConfirmation(const AppState::Todo& item);
  void showWifiResetProgress(uint8_t percent);

 private:
  void beginFrame(bool quality);
  void endFrame();
  void drawHeader(const char* title);
  void drawBadgePhoto(int x, int y, bool available);
  void drawWeatherIcon(int centerX, int centerY, int weatherCode, bool isDay);
  void drawCompactWeatherIcon(int centerX, int centerY, int weatherCode, bool isDay);
  void drawTrendGraph(int x, int y, int width, int height, const char* title,
                      const float values[8], float minimum, float maximum);
  void drawMonthCalendar(const struct tm& localTime, int x, int y, int width, int height);
  int drawVerticalText(const String& text, int centerX, int topY, int step, int maxGlyphs);
  static const char* weekdayName(int weekday);
  static const char* weatherName(int weatherCode);

  uint32_t renderedMinutes_ = 0;
  uint8_t landscapeRotation_ = 1;
  uint8_t portraitRotation_ = 0;
};

}  // namespace paperdesk
