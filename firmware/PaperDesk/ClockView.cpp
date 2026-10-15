#include "ClockView.h"

#include <FFat.h>
#include <WiFi.h>

#include "AppConfig.h"

namespace paperdesk {

void ClockView::begin() {
  if (M5.Display.width() < M5.Display.height()) {
    M5.Display.setRotation(M5.Display.getRotation() ^ 1);
  }
  landscapeRotation_ = M5.Display.getRotation();
  portraitRotation_ = landscapeRotation_ ^ 1;
  M5.Display.setTextColor(TFT_BLACK, TFT_WHITE);
  M5.Display.setEpdMode(m5gfx::epd_mode_t::epd_quality);
  M5.Display.fillScreen(TFT_WHITE);
  M5.Display.display();
  M5.Display.waitDisplay();
  M5.Display.setEpdMode(m5gfx::epd_mode_t::epd_fastest);
}

void ClockView::setPortrait(bool portrait) {
  const uint8_t target = portrait ? portraitRotation_ : landscapeRotation_;
  if (M5.Display.getRotation() == target) return;
  M5.Display.waitDisplay();
  M5.Display.setRotation(target);
  M5.Display.setEpdMode(m5gfx::epd_mode_t::epd_quality);
  M5.Display.fillScreen(TFT_WHITE);
  M5.Display.display();
  M5.Display.waitDisplay();
  M5.Display.setEpdMode(m5gfx::epd_mode_t::epd_fastest);
}

void ClockView::beginFrame(bool quality) {
  M5.Display.waitDisplay();
  M5.Display.setEpdMode(quality ? m5gfx::epd_mode_t::epd_quality : m5gfx::epd_mode_t::epd_fastest);
  M5.Display.startWrite();
  M5.Display.fillScreen(TFT_WHITE);
  M5.Display.setTextColor(TFT_BLACK, TFT_WHITE);
}

void ClockView::endFrame() {
  M5.Display.endWrite();
  M5.Display.display();
  M5.Display.setEpdMode(m5gfx::epd_mode_t::epd_fastest);
}

void ClockView::drawHeader(const char* title) {
  M5.Display.setFont(&fonts::FreeSansBold24pt7b);
  M5.Display.setTextDatum(textdatum_t::top_left);
  M5.Display.drawString(title, 36, 30);
  M5.Display.drawFastHLine(36, 92, M5.Display.width() - 72, TFT_BLACK);
}

void ClockView::showConnecting(const String& ssid) {
  beginFrame(true);
  drawHeader(kProductName);
  M5.Display.setFont(&fonts::FreeSansBold24pt7b);
  M5.Display.drawString("Connecting to Wi-Fi", 54, 154);
  M5.Display.setFont(&fonts::Font4);
  M5.Display.drawString(ssid, 56, 232);
  M5.Display.drawString("If this fails, setup mode will start automatically.", 56, 300);
  endFrame();
}

void ClockView::showProvisioning(const String& apSsid, const String& error) {
  beginFrame(true);
  drawHeader("Wi-Fi setup");
  M5.Display.setFont(&fonts::FreeSansBold24pt7b);
  M5.Display.drawString("1  Connect your phone to", 54, 132);
  M5.Display.setFont(&fonts::DejaVu72);
  M5.Display.drawString(apSsid, 54, 190);
  M5.Display.setFont(&fonts::FreeSansBold24pt7b);
  M5.Display.drawString("2  Open 192.168.4.1", 54, 302);
  M5.Display.setFont(&fonts::Font2);
  M5.Display.drawString(String("Wi-Fi password: ") + kAdminPassword, 58, 365);
  M5.Display.drawString("The setup page may open automatically.", 58, 405);
  if (!error.isEmpty()) {
    M5.Display.drawString("Last connection failed. Check the password and try again.", 58, 450);
  }
  endFrame();
}

void ClockView::showClock(time_t now, const String& ssid, const String& ip, bool ntpSynced,
                          int batteryPercent, const WeatherSnapshot& weather,
                          bool forceQuality) {
  struct tm localTime {};
  localtime_r(&now, &localTime);
  const uint32_t minuteKey = static_cast<uint32_t>(now / 60);
  const bool periodicQuality = renderedMinutes_ != 0 && minuteKey % kQualityRefreshMinutes == 0;
  renderedMinutes_ = minuteKey;
  beginFrame(forceQuality || periodicQuality);

  constexpr int screenWidth = 960;
  constexpr int margin = 14;
  constexpr int headerHeight = 34;
  constexpr int topBottom = 286;
  constexpr int hourlyBottom = 408;
  constexpr int contentRight = 946;
  const String city = weather.city.isEmpty()
                          ? String("未设置天气城市")
                          : (weather.resolvedCity.isEmpty() ? weather.city : weather.resolvedCity);

  // Status strip.
  M5.Display.setFont(&fonts::Font2);
  M5.Display.setTextDatum(textdatum_t::top_left);
  M5.Display.drawString("PaperDesk", 18, 9);
  M5.Display.setTextDatum(textdatum_t::top_center);
  M5.Display.setFont(&fonts::efontCN_16);
  M5.Display.drawString(city, screenWidth / 2, 6);
  M5.Display.setFont(&fonts::Font2);
  M5.Display.setTextDatum(textdatum_t::top_right);
  const int rssi = WiFi.RSSI();
  const int quality = constrain(2 * (rssi + 100), 0, 100);
  const String status = String("WiFi ") + quality + "%   BAT " +
                        (batteryPercent >= 0 ? String(batteryPercent) + "%" : String("--"));
  M5.Display.drawString(status, 942, 9);

  // Main panel geometry from PaperS3Weather.
  M5.Display.drawRect(margin, headerHeight, contentRight - margin, 492, TFT_BLACK);
  const int columns[] = {14, 232, 465, 697, 946};
  for (int i = 1; i < 4; ++i)
    M5.Display.drawFastVLine(columns[i], headerHeight, topBottom - headerHeight, TFT_BLACK);
  M5.Display.drawFastHLine(margin, 76, contentRight - margin, TFT_BLACK);
  M5.Display.drawFastHLine(margin, topBottom, contentRight - margin, TFT_BLACK);
  M5.Display.drawFastHLine(margin, hourlyBottom, contentRight - margin, TFT_BLACK);

  static const char* titles[] = {"当前", "日月", "风向", "时间"};
  M5.Display.setFont(&fonts::efontCN_24_b);
  M5.Display.setTextDatum(textdatum_t::middle_center);
  for (int i = 0; i < 4; ++i)
    M5.Display.drawString(titles[i], (columns[i] + columns[i + 1]) / 2, 55);

  // Current conditions.
  if (weather.valid) {
    M5.Display.setFont(&fonts::FreeSansBold24pt7b);
    M5.Display.setTextSize(2);
    M5.Display.drawString(String(static_cast<int>(roundf(weather.temperature))), 86, 124);
    M5.Display.setTextSize(1);
    M5.Display.setFont(&fonts::efontCN_24_b);
    M5.Display.drawString("°C", 168, 113);
    drawCompactWeatherIcon(174, 164, weather.weatherCode, weather.isDay);
    M5.Display.setFont(&fonts::efontCN_24);
    M5.Display.drawString(weatherName(weather.weatherCode), 123, 218);
    M5.Display.setFont(&fonts::Font4);
    const String range = String("L ") + static_cast<int>(roundf(weather.todayMin)) +
                         "°   H " + static_cast<int>(roundf(weather.todayMax)) + "°";
    M5.Display.drawString(range, 123, 263);
  } else {
    M5.Display.setFont(&fonts::efontCN_16);
    const String message = weather.city.isEmpty() ? "请在网页设置城市" : "正在获取天气";
    M5.Display.drawString(message, 123, 170);
  }

  // Sunrise, sunset and a compact lunar phase label.
  M5.Display.setFont(&fonts::efontCN_24);
  M5.Display.drawString(String("日出  ") + (weather.sunrise.isEmpty() ? "--:--" : weather.sunrise),
                        348, 116);
  M5.Display.drawString(String("日落  ") + (weather.sunset.isEmpty() ? "--:--" : weather.sunset),
                        348, 174);
  const int moonIndex = static_cast<int>(floor(fmod((now / 86400.0) + 5.0, 29.53) / 3.691));
  static const char* moonNames[] = {"新月", "眉月", "上弦", "盈凸", "满月", "亏凸", "下弦", "残月"};
  M5.Display.setFont(&fonts::efontCN_16);
  M5.Display.drawString(String("月相：") + moonNames[constrain(moonIndex, 0, 7)], 348, 235);

  // Wind compass.
  const int compassX = 581;
  const int compassY = 177;
  constexpr int compassRadius = 70;
  M5.Display.drawCircle(compassX, compassY, compassRadius, TFT_BLACK);
  M5.Display.drawCircle(compassX, compassY, 44, TFT_BLACK);
  M5.Display.setFont(&fonts::Font2);
  M5.Display.drawString("N", compassX, compassY - 82);
  M5.Display.drawString("S", compassX, compassY + 82);
  M5.Display.drawString("W", compassX - 82, compassY);
  M5.Display.drawString("E", compassX + 82, compassY);
  const float windRadians = (weather.windDirection - 90.0f) * PI / 180.0f;
  M5.Display.drawLine(compassX, compassY, compassX + cosf(windRadians) * 62,
                      compassY + sinf(windRadians) * 62, TFT_BLACK);
  M5.Display.setFont(&fonts::Font4);
  M5.Display.drawString(String(weather.windSpeed, 1), compassX, compassY - 8);
  M5.Display.setFont(&fonts::Font2);
  M5.Display.drawString("km/h", compassX, compassY + 17);

  // Device/date panel. This is the actual clock in the dashboard.
  char dateBuffer[20];
  char timeBuffer[12];
  snprintf(dateBuffer, sizeof(dateBuffer), "%04d-%02d-%02d", localTime.tm_year + 1900,
           localTime.tm_mon + 1, localTime.tm_mday);
  snprintf(timeBuffer, sizeof(timeBuffer), "%02d:%02d", localTime.tm_hour, localTime.tm_min);
  M5.Display.setFont(&fonts::Font4);
  M5.Display.drawString(dateBuffer, 822, 112);
  M5.Display.setFont(&fonts::FreeSansBold24pt7b);
  M5.Display.drawString(timeBuffer, 822, 154);
  M5.Display.setFont(&fonts::efontCN_16);
  M5.Display.drawString(ntpSynced ? "网络校时" : "RTC 时间", 822, 194);
  M5.Display.setFont(&fonts::Font4);
  M5.Display.drawString(String("湿度 ") + static_cast<int>(roundf(weather.humidity)) + "%", 822, 232);
  M5.Display.setFont(&fonts::Font2);
  M5.Display.drawString(ssid, 822, 263);

  // Eight-hour forecast row.
  const int hourlyWidth = (contentRight - margin) / 8;
  M5.Display.setFont(&fonts::Font2);
  for (int i = 0; i < 8; ++i) {
    const int left = margin + i * hourlyWidth;
    if (i) M5.Display.drawFastVLine(left, topBottom, hourlyBottom - topBottom, TFT_BLACK);
    const int center = left + hourlyWidth / 2;
    const auto& item = weather.hourly[i];
    char hour[8];
    snprintf(hour, sizeof(hour), "%02d:00", item.hour);
    M5.Display.drawString(hour, center, 302);
    M5.Display.drawString(String(static_cast<int>(roundf(item.temperature))) + "°C", center, 326);
    drawCompactWeatherIcon(center, 373, item.weatherCode,
                           item.hour >= 6 && item.hour < 18);
  }

  // Four trend graphs.
  float temp[8], rain[8], humidity[8], pressure[8];
  for (int i = 0; i < 8; ++i) {
    temp[i] = weather.hourly[i].temperature;
    rain[i] = weather.hourly[i].precipitationProbability;
    humidity[i] = weather.hourly[i].humidity;
    pressure[i] = weather.hourly[i].pressure;
  }
  constexpr int graphWidth = 233;
  drawTrendGraph(14, 408, graphWidth, 118, "温度 (C)", temp, NAN, NAN);
  drawTrendGraph(247, 408, graphWidth, 118, "降水 (%)", rain, 0, 100);
  drawTrendGraph(480, 408, graphWidth, 118, "湿度 (%)", humidity, 0, 100);
  drawTrendGraph(713, 408, graphWidth, 118, "气压 (hPa)", pressure, NAN, NAN);
  endFrame();
}

void ClockView::drawCompactWeatherIcon(int centerX, int centerY, int weatherCode, bool isDay) {
  const bool cloudy = weatherCode >= 1;
  const bool rain = (weatherCode >= 51 && weatherCode <= 67) ||
                    (weatherCode >= 80 && weatherCode <= 82) || weatherCode >= 95;
  const bool snow = weatherCode >= 71 && weatherCode <= 86;
  if (!cloudy) {
    M5.Display.drawCircle(centerX, centerY, 17, TFT_BLACK);
    for (int angle = 0; angle < 360; angle += 45) {
      const float radians = angle * PI / 180.0f;
      M5.Display.drawLine(centerX + cosf(radians) * 22, centerY + sinf(radians) * 22,
                          centerX + cosf(radians) * 29, centerY + sinf(radians) * 29, TFT_BLACK);
    }
    return;
  }
  if (weatherCode <= 2 && isDay) M5.Display.drawCircle(centerX + 16, centerY - 12, 15, TFT_BLACK);
  M5.Display.drawCircle(centerX - 17, centerY + 3, 13, TFT_BLACK);
  M5.Display.drawCircle(centerX + 2, centerY - 7, 19, TFT_BLACK);
  M5.Display.drawCircle(centerX + 21, centerY + 4, 12, TFT_BLACK);
  M5.Display.drawFastHLine(centerX - 18, centerY + 16, 40, TFT_BLACK);
  if (rain) {
    for (int i = -1; i <= 1; ++i)
      M5.Display.drawLine(centerX + i * 13, centerY + 22,
                          centerX + i * 13 - 5, centerY + 34, TFT_BLACK);
  } else if (snow) {
    for (int i = -1; i <= 1; ++i) {
      M5.Display.drawFastHLine(centerX + i * 14 - 4, centerY + 28, 9, TFT_BLACK);
      M5.Display.drawFastVLine(centerX + i * 14, centerY + 24, 9, TFT_BLACK);
    }
  }
}

void ClockView::drawTrendGraph(int x, int y, int width, int height, const char* title,
                               const float values[8], float minimum, float maximum) {
  M5.Display.drawRect(x, y, width, height, TFT_BLACK);
  M5.Display.setTextDatum(textdatum_t::top_center);
  M5.Display.setFont(&fonts::efontCN_16);
  M5.Display.drawString(title, x + width / 2, y + 4);
  const int plotX = x + 20;
  const int plotY = y + 34;
  const int plotWidth = width - 30;
  const int plotHeight = height - 44;
  if (isnan(minimum) || isnan(maximum)) {
    minimum = values[0];
    maximum = values[0];
    for (int i = 1; i < 8; ++i) {
      minimum = min(minimum, values[i]);
      maximum = max(maximum, values[i]);
    }
    if (maximum - minimum < 1.0f) {
      minimum -= 1.0f;
      maximum += 1.0f;
    } else {
      minimum -= (maximum - minimum) * 0.12f;
      maximum += (maximum - minimum) * 0.12f;
    }
  }
  M5.Display.drawRect(plotX, plotY, plotWidth, plotHeight, TFT_BLACK);
  int lastX = plotX;
  int lastY = plotY + plotHeight -
              static_cast<int>((values[0] - minimum) / (maximum - minimum) * plotHeight);
  for (int i = 1; i < 8; ++i) {
    const int pointX = plotX + i * plotWidth / 7;
    const int pointY = plotY + plotHeight -
                       static_cast<int>((values[i] - minimum) / (maximum - minimum) * plotHeight);
    M5.Display.drawLine(lastX, constrain(lastY, plotY, plotY + plotHeight), pointX,
                        constrain(pointY, plotY, plotY + plotHeight), TFT_BLACK);
    M5.Display.fillCircle(pointX, constrain(pointY, plotY, plotY + plotHeight), 2, TFT_BLACK);
    lastX = pointX;
    lastY = pointY;
  }
}

void ClockView::drawWeatherIcon(int centerX, int centerY, int weatherCode, bool isDay) {
  const bool thunder = weatherCode >= 95;
  const bool snow = weatherCode >= 71 && weatherCode <= 86;
  const bool rain = (weatherCode >= 51 && weatherCode <= 67) ||
                    (weatherCode >= 80 && weatherCode <= 82);
  const bool fog = weatherCode >= 45 && weatherCode <= 48;
  const bool cloud = weatherCode >= 1;

  if (!cloud || weatherCode <= 2) {
    const int sunX = centerX + 16;
    const int sunY = centerY - 34;
    if (isDay) {
      M5.Display.drawCircle(sunX, sunY, 27, TFT_BLACK);
      M5.Display.drawCircle(sunX, sunY, 28, TFT_BLACK);
      for (int angle = 0; angle < 360; angle += 45) {
        const float rad = angle * PI / 180.0f;
        M5.Display.drawLine(sunX + cosf(rad) * 37, sunY + sinf(rad) * 37,
                            sunX + cosf(rad) * 52, sunY + sinf(rad) * 52, TFT_BLACK);
      }
    } else {
      M5.Display.drawCircle(sunX, sunY, 30, TFT_BLACK);
      M5.Display.fillCircle(sunX + 13, sunY - 10, 27, TFT_WHITE);
    }
  }
  if (cloud) {
    const int y = centerY + 10;
    M5.Display.fillCircle(centerX - 47, y, 31, TFT_WHITE);
    M5.Display.fillCircle(centerX + 3, y - 28, 48, TFT_WHITE);
    M5.Display.fillCircle(centerX + 55, y, 34, TFT_WHITE);
    M5.Display.fillRect(centerX - 48, y, 104, 37, TFT_WHITE);
    M5.Display.drawCircle(centerX - 47, y, 31, TFT_BLACK);
    M5.Display.drawCircle(centerX + 3, y - 28, 48, TFT_BLACK);
    M5.Display.drawCircle(centerX + 55, y, 34, TFT_BLACK);
    M5.Display.drawFastHLine(centerX - 47, y + 31, 103, TFT_BLACK);
  }
  if (fog) {
    for (int i = 0; i < 3; ++i)
      M5.Display.drawFastHLine(centerX - 62, centerY + 60 + i * 17, 124, TFT_BLACK);
  } else if (rain || thunder) {
    for (int i = -1; i <= 1; ++i)
      M5.Display.drawLine(centerX + i * 40, centerY + 52,
                          centerX + i * 40 - 13, centerY + 83, TFT_BLACK);
    if (thunder) {
      M5.Display.fillTriangle(centerX + 4, centerY + 47, centerX - 18, centerY + 92,
                              centerX + 5, centerY + 83, TFT_BLACK);
      M5.Display.fillTriangle(centerX + 5, centerY + 83, centerX - 4, centerY + 111,
                              centerX + 24, centerY + 72, TFT_BLACK);
    }
  } else if (snow) {
    for (int i = -1; i <= 1; ++i) {
      const int x = centerX + i * 42;
      const int y = centerY + 72;
      M5.Display.drawFastHLine(x - 10, y, 21, TFT_BLACK);
      M5.Display.drawFastVLine(x, y - 10, 21, TFT_BLACK);
      M5.Display.drawLine(x - 8, y - 8, x + 8, y + 8, TFT_BLACK);
      M5.Display.drawLine(x + 8, y - 8, x - 8, y + 8, TFT_BLACK);
    }
  }
}

void ClockView::drawMonthCalendar(const struct tm& localTime, int x, int y, int width,
                                  int height) {
  constexpr int columns = 7;
  constexpr int rows = 6;
  constexpr int weekdayHeight = 48;
  const int cellWidth = width / columns;
  const int cellHeight = (height - weekdayHeight) / rows;
  const int gridWidth = cellWidth * columns;
  const int gridHeight = cellHeight * rows;
  const int gridTop = y + weekdayHeight;
  static const char* weekdays[] = {"日", "一", "二", "三", "四", "五", "六"};

  M5.Display.setFont(&fonts::efontCN_24_b);
  M5.Display.setTextDatum(textdatum_t::middle_center);
  for (int col = 0; col < columns; ++col) {
    M5.Display.drawString(weekdays[col], x + col * cellWidth + cellWidth / 2,
                          y + weekdayHeight / 2);
  }

  struct tm first = localTime;
  first.tm_mday = 1;
  first.tm_hour = 12;
  mktime(&first);
  const int firstWeekday = first.tm_wday;
  const int year = localTime.tm_year + 1900;
  const int month = localTime.tm_mon + 1;
  static const int monthDays[] = {31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31};
  int days = monthDays[month - 1];
  if (month == 2 && ((year % 4 == 0 && year % 100 != 0) || year % 400 == 0)) days = 29;

  M5.Display.setFont(&fonts::Font4);
  for (int day = 1; day <= days; ++day) {
    const int index = firstWeekday + day - 1;
    const int row = index / columns;
    const int col = index % columns;
    if (row >= rows) break;
    const int cellX = x + col * cellWidth;
    const int cellY = gridTop + row * cellHeight;
    const int centerX = cellX + cellWidth / 2;
    const int centerY = cellY + cellHeight / 2;
    if (day == localTime.tm_mday) {
      M5.Display.fillRect(cellX + 1, cellY + 1, cellWidth - 1, cellHeight - 1, TFT_BLACK);
      M5.Display.setTextColor(TFT_WHITE, TFT_BLACK);
      M5.Display.drawNumber(day, centerX, centerY);
      M5.Display.setTextColor(TFT_BLACK, TFT_WHITE);
    } else {
      M5.Display.drawNumber(day, centerX, centerY);
    }
  }
  for (int col = 0; col <= columns; ++col)
    M5.Display.drawFastVLine(x + col * cellWidth, gridTop, gridHeight, TFT_BLACK);
  for (int row = 0; row <= rows; ++row)
    M5.Display.drawFastHLine(x, gridTop + row * cellHeight, gridWidth, TFT_BLACK);
}

const char* ClockView::weatherName(int weatherCode) {
  if (weatherCode == 0) return "晴";
  if (weatherCode == 1) return "大部晴朗";
  if (weatherCode == 2) return "多云";
  if (weatherCode == 3) return "阴";
  if (weatherCode >= 45 && weatherCode <= 48) return "有雾";
  if (weatherCode >= 51 && weatherCode <= 57) return "毛毛雨";
  if (weatherCode >= 61 && weatherCode <= 67) return "有雨";
  if (weatherCode >= 71 && weatherCode <= 77) return "有雪";
  if (weatherCode >= 80 && weatherCode <= 82) return "阵雨";
  if (weatherCode >= 85 && weatherCode <= 86) return "阵雪";
  if (weatherCode >= 95) return "雷暴";
  return "未知天气";
}

void ClockView::drawBadgePhoto(int x, int y, bool available) {
  constexpr int size = AppState::kPhotoWidth;
  if (!available) {
    M5.Display.drawRect(x, y, size, size, TFT_BLACK);
    M5.Display.drawLine(x, y, x + size, y + size, TFT_BLACK);
    M5.Display.drawLine(x + size, y, x, y + size, TFT_BLACK);
    M5.Display.setFont(&fonts::efontCN_24);
    M5.Display.setTextDatum(textdatum_t::middle_center);
    M5.Display.drawString("请从网页上传照片", x + size / 2, y + size / 2);
    return;
  }
  File photo = FFat.open("/badge.gray", FILE_READ);
  if (!photo || photo.size() != AppState::kPhotoBytes) {
    if (photo) photo.close();
    drawBadgePhoto(x, y, false);
    return;
  }
  lgfx::grayscale_t row[size];
  for (int yy = 0; yy < size; ++yy) {
    if (photo.read(reinterpret_cast<uint8_t*>(row), size) != size) break;
    M5.Display.pushImage(x, y + yy, size, 1, row);
  }
  photo.close();
  M5.Display.drawRect(x - 1, y - 1, size + 2, size + 2, TFT_BLACK);
}

int ClockView::drawVerticalText(const String& text, int centerX, int topY, int step,
                                int maxGlyphs) {
  size_t offset = 0;
  int y = topY;
  int glyphs = 0;
  while (offset < text.length() && glyphs < maxGlyphs) {
    const uint8_t lead = static_cast<uint8_t>(text[offset]);
    size_t length = 1;
    if ((lead & 0xE0) == 0xC0) length = 2;
    else if ((lead & 0xF0) == 0xE0) length = 3;
    else if ((lead & 0xF8) == 0xF0) length = 4;
    if (offset + length > text.length()) length = 1;
    M5.Display.drawString(text.substring(offset, offset + length), centerX, y);
    offset += length;
    y += step;
    ++glyphs;
  }
  return y;
}

void ClockView::showPortraitMenu(AppState::Mode selected) {
  beginFrame(true);
  M5.Display.setTextColor(TFT_BLACK, TFT_WHITE);
  M5.Display.setFont(&fonts::efontCN_24_b);
  M5.Display.setTextDatum(textdatum_t::top_center);
  M5.Display.setTextSize(2);
  M5.Display.drawString("主菜单", 270, 28);
  M5.Display.setTextSize(1);
  M5.Display.setFont(&fonts::efontCN_16);
  M5.Display.drawRoundRect(435, 28, 78, 54, 12, TFT_BLACK);
  M5.Display.drawString("锁屏", 474, 45);
  M5.Display.setTextSize(1);
  M5.Display.setFont(&fonts::efontCN_24);
  M5.Display.drawString("点击卡片进入", 270, 100);

  M5.Display.drawRoundRect(55, 140, 430, 115, 18, TFT_BLACK);
  M5.Display.setFont(&fonts::efontCN_24_b);
  M5.Display.setTextSize(1);
  M5.Display.drawString("时钟", 270, 155);
  M5.Display.setTextSize(1);
  M5.Display.setFont(&fonts::efontCN_16);
  M5.Display.drawString("天气 · 时间 · 趋势", 270, 211);

  M5.Display.drawRoundRect(55, 285, 430, 115, 18, TFT_BLACK);
  M5.Display.setFont(&fonts::efontCN_24_b);
  M5.Display.drawString("识字", 270, 300);
  M5.Display.setTextSize(1);
  M5.Display.setFont(&fonts::efontCN_16);
  M5.Display.drawString("一年级上 · 100 字", 270, 356);

  const bool badgeSelected = selected == AppState::Mode::kBadge;
  M5.Display.drawRoundRect(55, 430, 430, 115, 18, TFT_BLACK);
  if (badgeSelected) M5.Display.drawRoundRect(61, 436, 418, 103, 14, TFT_BLACK);
  M5.Display.setFont(&fonts::efontCN_24_b);
  M5.Display.drawString("工牌", 270, 445);
  M5.Display.setTextSize(1);
  M5.Display.setFont(&fonts::efontCN_16);
  M5.Display.drawString("姓名 · 职务 · 可选照片", 270, 501);

  const bool todoSelected = selected == AppState::Mode::kTodo;
  M5.Display.drawRoundRect(55, 575, 430, 115, 18, TFT_BLACK);
  if (todoSelected) M5.Display.drawRoundRect(61, 581, 418, 103, 14, TFT_BLACK);
  M5.Display.setFont(&fonts::efontCN_24_b);
  M5.Display.drawString("Todo", 270, 590);
  M5.Display.setTextSize(1);
  M5.Display.setFont(&fonts::efontCN_16);
  M5.Display.drawString("查看并完成待办", 270, 646);

  M5.Display.drawRoundRect(55, 720, 430, 115, 18, TFT_BLACK);
  M5.Display.setFont(&fonts::efontCN_24_b);
  M5.Display.drawString("设备设置", 270, 735);
  M5.Display.setFont(&fonts::efontCN_16);
  M5.Display.drawString("省电 · Web · 锁屏与电源", 270, 791);
  endFrame();
}

void ClockView::showDeviceSettings(const AppState::DeviceSettings& settings, const String& ip,
                                   const String& wifiSsid, bool wifiConnected,
                                   bool provisioning, const String& apSsid,
                                   size_t savedNetworkCount) {
  beginFrame(true);
  M5.Display.setTextColor(TFT_BLACK, TFT_WHITE);
  M5.Display.setTextDatum(textdatum_t::top_center);
  M5.Display.setFont(&fonts::efontCN_24_b);
  M5.Display.setTextSize(2);
  M5.Display.drawString("设备设置", 270, 28);
  M5.Display.setTextSize(1);
  M5.Display.setFont(&fonts::efontCN_16);
  M5.Display.drawString(wifiConnected ? String("已连接：") + wifiSsid : "当前离线可正常使用",
                        270, 88);

  auto drawSetting = [](int y, const char* title, const String& detail, bool enabled) {
    M5.Display.drawRoundRect(45, y, 450, 90, 16, TFT_BLACK);
    M5.Display.setTextDatum(textdatum_t::top_left);
    M5.Display.setFont(&fonts::efontCN_24_b);
    M5.Display.drawString(title, 70, y + 10);
    M5.Display.setFont(&fonts::efontCN_16);
    M5.Display.drawString(detail, 70, y + 52);
    M5.Display.setTextDatum(textdatum_t::middle_center);
    M5.Display.drawRoundRect(386, y + 18, 82, 54, 27, TFT_BLACK);
    if (enabled) {
      M5.Display.fillCircle(441, y + 45, 20, TFT_BLACK);
      M5.Display.setTextColor(TFT_WHITE, TFT_BLACK);
      M5.Display.setFont(&fonts::Font2);
      M5.Display.drawString("ON", 441, y + 45);
      M5.Display.setTextColor(TFT_BLACK, TFT_WHITE);
    } else {
      M5.Display.drawCircle(413, y + 45, 20, TFT_BLACK);
      M5.Display.setFont(&fonts::Font2);
      M5.Display.drawString("OFF", 413, y + 45);
    }
  };

  drawSetting(115, "Wi-Fi 省电", "联网时降低待机功耗", settings.wifiPowerSave != 0);
  const String webDetail = settings.webManagementEnabled
                               ? (ip.isEmpty() ? String("联网后可访问管理后台") : String("管理地址：") + ip)
                               : String("HTTP 与 paperdesk.local 已关闭");
  drawSetting(220, "Web 管理", webDetail, settings.webManagementEnabled != 0);

  String autoLockDetail;
  if (settings.autoLockMinutes == 0) autoLockDetail = "不自动锁屏";
  else if (settings.autoLockMinutes < 60) autoLockDetail = String(settings.autoLockMinutes) + " 分钟无操作后锁屏";
  else autoLockDetail = String(settings.autoLockMinutes / 60) + " 小时无操作后锁屏";
  drawSetting(325, "自动锁屏", autoLockDetail, settings.autoLockMinutes != 0);

  M5.Display.setTextDatum(textdatum_t::top_center);
  M5.Display.setFont(&fonts::efontCN_24_b);
  M5.Display.drawRoundRect(45, 440, 450, 105, 16, TFT_BLACK);
  M5.Display.drawString(provisioning ? "关闭 Wi-Fi 配网" : "开启 Wi-Fi 配网", 270, 452);
  M5.Display.setFont(&fonts::efontCN_16);
  String provisioningDetail;
  if (provisioning) {
    provisioningDetail = apSsid + " · 密码 " + kAdminPassword;
  } else if (wifiConnected) {
    provisioningDetail = String("当前：") + wifiSsid + " · 已保存 " +
                         savedNetworkCount + " 个";
  } else {
    provisioningDetail = String("当前离线 · 已保存 ") + savedNetworkCount + " 个";
  }
  M5.Display.drawString(provisioningDetail, 270, 500);

  M5.Display.setTextDatum(textdatum_t::middle_center);
  M5.Display.setFont(&fonts::efontCN_24_b);
  M5.Display.drawRoundRect(45, 575, 450, 88, 16, TFT_BLACK);
  M5.Display.drawString("立即锁屏", 270, 619);
  M5.Display.drawRoundRect(45, 695, 210, 88, 16, TFT_BLACK);
  M5.Display.drawString("重启", 150, 739);
  M5.Display.drawRoundRect(285, 695, 210, 88, 16, TFT_BLACK);
  M5.Display.drawString("关机", 390, 739);
  endFrame();
}

void ClockView::showDeviceConfirmation(const char* title, const char* detail) {
  beginFrame(true);
  M5.Display.setTextColor(TFT_BLACK, TFT_WHITE);
  M5.Display.setTextDatum(textdatum_t::top_center);
  M5.Display.setFont(&fonts::efontCN_24_b);
  M5.Display.setTextSize(2);
  M5.Display.drawString(title, 270, 190);
  M5.Display.setTextSize(1);
  M5.Display.setFont(&fonts::efontCN_24);
  M5.Display.drawString(detail, 270, 340);
  M5.Display.drawRoundRect(55, 610, 190, 105, 16, TFT_BLACK);
  M5.Display.drawString("取消", 150, 645);
  M5.Display.fillRoundRect(295, 610, 190, 105, 16, TFT_BLACK);
  M5.Display.setTextColor(TFT_WHITE, TFT_BLACK);
  M5.Display.drawString("确认", 390, 645);
  endFrame();
}

void ClockView::showLockScreen(const char* line1, const char* line2, const char* author) {
  beginFrame(true);
  M5.Display.setTextColor(TFT_BLACK, TFT_WHITE);
  M5.Display.setTextDatum(textdatum_t::top_center);
  M5.Display.setFont(&fonts::FreeSerifItalic24pt7b);
  M5.Display.setTextSize(1);
  M5.Display.drawString("PaperDesk", 270, 48);
  M5.Display.drawFastHLine(70, 118, 400, TFT_BLACK);

  M5.Display.setFont(&fonts::efontCN_24_b);
  M5.Display.setTextSize(2);
  M5.Display.setTextDatum(textdatum_t::middle_center);
  M5.Display.drawString(line1, 270, 310);
  if (line2 && line2[0]) M5.Display.drawString(line2, 270, 390);
  M5.Display.setTextSize(1);
  M5.Display.setFont(&fonts::efontCN_24);
  M5.Display.drawString(author, 270, 490);

  constexpr int trackX = 48;
  constexpr int trackY = 760;
  constexpr int trackWidth = 444;
  constexpr int trackHeight = 104;
  M5.Display.drawRoundRect(trackX, trackY, trackWidth, trackHeight, 20, TFT_BLACK);
  M5.Display.fillRoundRect(trackX + 10, trackY + 10, 84, 84, 15, TFT_BLACK);
  M5.Display.setTextColor(TFT_WHITE, TFT_BLACK);
  M5.Display.setFont(&fonts::Font4);
  M5.Display.drawString(">>", trackX + 52, trackY + 52);
  M5.Display.setTextColor(TFT_BLACK, TFT_WHITE);
  M5.Display.setFont(&fonts::efontCN_24);
  M5.Display.drawString("向右滑动解锁", 315, trackY + 52);
  endFrame();
}

void ClockView::showLiteracy(size_t index) {
  if (kLiteracyEntryCount == 0) return;
  index %= kLiteracyEntryCount;
  const auto& entry = kLiteracyEntries[index];
  beginFrame(true);
  M5.Display.setTextColor(TFT_BLACK, TFT_WHITE);

  M5.Display.setTextDatum(textdatum_t::top_left);
  M5.Display.setFont(&fonts::efontCN_24_b);
  M5.Display.drawString("识字 · 一年级上", 24, 8);
  M5.Display.setTextDatum(textdatum_t::top_right);
  M5.Display.setFont(&fonts::Font4);
  M5.Display.drawString(String(index + 1) + " / " + kLiteracyEntryCount, 936, 10);
  for (int x = 24; x < 936; x += 10) M5.Display.drawFastHLine(x, 43, 5, TFT_BLACK);

  constexpr int gridX = 58;
  constexpr int gridY = 76;
  constexpr int gridSize = 372;
  M5.Display.drawRect(gridX, gridY, gridSize, gridSize, TFT_BLACK);
  M5.Display.drawRect(gridX + 1, gridY + 1, gridSize - 2, gridSize - 2, TFT_BLACK);
  for (int offset = 8; offset < gridSize; offset += 16) {
    M5.Display.drawFastHLine(gridX + offset, gridY + gridSize / 2, 8, TFT_BLACK);
    M5.Display.drawFastVLine(gridX + gridSize / 2, gridY + offset, 8, TFT_BLACK);
  }
  for (int offset = 0; offset < gridSize; offset += 18) {
    M5.Display.drawLine(gridX + offset, gridY + offset,
                        gridX + min(offset + 9, gridSize), gridY + min(offset + 9, gridSize),
                        TFT_BLACK);
    M5.Display.drawLine(gridX + gridSize - offset, gridY + offset,
                        gridX + gridSize - min(offset + 9, gridSize),
                        gridY + min(offset + 9, gridSize), TFT_BLACK);
  }
  M5.Display.setTextDatum(textdatum_t::middle_center);
  M5.Display.setFont(&fonts::efontCN_24_b);
  M5.Display.setTextSize(7);
  M5.Display.drawString(entry.character, gridX + gridSize / 2, gridY + gridSize / 2);
  M5.Display.setTextSize(1);

  M5.Display.setFont(&fonts::FreeSansBold24pt7b);
  M5.Display.setTextDatum(textdatum_t::top_center);
  M5.Display.setTextSize(2);
  M5.Display.drawString(entry.pinyin, 700, 65);
  M5.Display.setTextSize(1);

  M5.Display.setTextDatum(textdatum_t::top_left);
  M5.Display.setFont(&fonts::efontCN_24_b);
  M5.Display.drawString("组词", 500, 184);
  for (int x = 568; x < 920; x += 10) M5.Display.drawFastHLine(x, 201, 5, TFT_BLACK);
  M5.Display.setFont(&fonts::efontCN_24);
  M5.Display.setTextSize(2);
  M5.Display.drawString(entry.words, 515, 225);
  M5.Display.setTextSize(1);

  M5.Display.setFont(&fonts::efontCN_24_b);
  M5.Display.drawString("例句", 500, 332);
  for (int x = 568; x < 920; x += 10) M5.Display.drawFastHLine(x, 349, 5, TFT_BLACK);
  M5.Display.setFont(&fonts::efontCN_24);
  M5.Display.drawString(String("我会认“") + entry.character + "”这个字。", 515, 382);

  for (int x = 24; x < 936; x += 10) M5.Display.drawFastHLine(x, 478, 5, TFT_BLACK);
  M5.Display.setFont(&fonts::efontCN_24);
  M5.Display.setTextDatum(textdatum_t::middle_left);
  M5.Display.drawString("◀  上一字", 86, 511);
  M5.Display.setTextDatum(textdatum_t::middle_right);
  M5.Display.drawString("下一字  ▶", 874, 511);
  endFrame();
}

void ClockView::showBadge(const AppState& state) {
  beginFrame(true);
  const auto& badge = state.badge();
  M5.Display.setTextColor(TFT_BLACK, TFT_WHITE);
  M5.Display.setFont(&fonts::efontCN_24_b);

  if (badge.layout == 5) {
    // F: information table.
    M5.Display.drawRect(34, 52, 472, 796, TFT_BLACK);
    M5.Display.setTextDatum(textdatum_t::middle_center);
    M5.Display.setTextSize(1);
    M5.Display.drawString(badge.company, 270, 108);
    M5.Display.drawFastHLine(34, 164, 472, TFT_BLACK);
    M5.Display.setTextDatum(textdatum_t::top_left);
    M5.Display.setFont(&fonts::efontCN_16);
    M5.Display.drawString("姓名", 55, 182);
    M5.Display.setFont(&fonts::efontCN_24_b);
    M5.Display.setTextSize(2);
    M5.Display.drawString(badge.name, 55, 222);
    M5.Display.drawFastHLine(34, 342, 472, TFT_BLACK);
    M5.Display.setFont(&fonts::efontCN_16);
    M5.Display.setTextSize(1);
    M5.Display.drawString("职务", 55, 360);
    M5.Display.setFont(&fonts::efontCN_24_b);
    M5.Display.drawString(badge.role, 55, 400);
    M5.Display.drawFastHLine(34, 478, 472, TFT_BLACK);
    M5.Display.setFont(&fonts::efontCN_16);
    M5.Display.drawString("编号", 55, 496);
    M5.Display.setFont(&fonts::Font4);
    M5.Display.setTextSize(2);
    M5.Display.drawString(badge.number, 55, 535);
    M5.Display.drawFastHLine(34, 652, 472, TFT_BLACK);
    M5.Display.setFont(&fonts::efontCN_16);
    M5.Display.setTextSize(1);
    M5.Display.drawString("联系方式", 55, 670);
    M5.Display.setFont(&fonts::efontCN_24);
    M5.Display.drawString(badge.contact, 55, 716);
  } else if (badge.layout == 4) {
    // E: oversized name and strong horizontal rhythm.
    M5.Display.setTextDatum(textdatum_t::top_center);
    M5.Display.setTextSize(1);
    M5.Display.drawString(badge.company, 270, 78);
    M5.Display.fillRect(70, 146, 400, 8, TFT_BLACK);
    M5.Display.setTextSize(3);
    M5.Display.drawString(badge.name, 270, 235);
    M5.Display.fillRect(70, 442, 400, 8, TFT_BLACK);
    M5.Display.setTextSize(1);
    M5.Display.drawString(badge.role, 270, 500);
    M5.Display.drawFastHLine(110, 585, 320, TFT_BLACK);
    M5.Display.setFont(&fonts::efontCN_24);
    M5.Display.drawString(String("编号 ") + badge.number, 270, 630);
    M5.Display.drawString(badge.contact, 270, 700);
  } else if (badge.layout == 3) {
    // D: modern left rail.
    M5.Display.fillRect(0, 0, 104, 960, TFT_BLACK);
    M5.Display.setTextColor(TFT_WHITE, TFT_BLACK);
    M5.Display.setTextDatum(textdatum_t::middle_center);
    M5.Display.setFont(&fonts::efontCN_24_b);
    M5.Display.setTextSize(1);
    const int companyEnd = drawVerticalText(badge.company, 52, 92, 48, 14);
    const int lineTop = max(companyEnd + 18, 410);
    if (lineTop < 820) M5.Display.drawFastVLine(52, lineTop, 820 - lineTop, TFT_WHITE);
    M5.Display.setTextColor(TFT_BLACK, TFT_WHITE);
    M5.Display.setTextDatum(textdatum_t::top_left);
    M5.Display.setFont(&fonts::efontCN_24_b);
    M5.Display.setTextSize(2);
    M5.Display.drawString(badge.name, 142, 145);
    M5.Display.setTextSize(1);
    M5.Display.drawString(badge.role, 144, 275);
    M5.Display.fillRect(144, 370, 330, 6, TFT_BLACK);
    M5.Display.setFont(&fonts::efontCN_16);
    M5.Display.drawString("编号", 144, 430);
    M5.Display.setFont(&fonts::Font4);
    M5.Display.setTextSize(2);
    M5.Display.drawString(badge.number, 144, 472);
    M5.Display.setFont(&fonts::efontCN_16);
    M5.Display.setTextSize(1);
    M5.Display.drawString("联系方式", 144, 610);
    M5.Display.setFont(&fonts::efontCN_24);
    M5.Display.drawString(badge.contact, 144, 655);
    M5.Display.drawFastHLine(144, 748, 330, TFT_BLACK);
    M5.Display.setFont(&fonts::Font2);
    M5.Display.drawString("PAPERDESK / IDENTITY", 144, 785);
  } else if (badge.layout == 2) {
    // C: formal photo-free certificate.
    M5.Display.drawRect(34, 48, 472, 800, TFT_BLACK);
    M5.Display.drawRect(44, 58, 452, 780, TFT_BLACK);
    M5.Display.setTextDatum(textdatum_t::top_center);
    M5.Display.setTextSize(1);
    M5.Display.drawString(badge.company, 270, 125);
    M5.Display.drawFastHLine(215, 205, 110, TFT_BLACK);
    M5.Display.setTextSize(3);
    M5.Display.drawString(badge.name, 270, 270);
    M5.Display.setTextSize(1);
    M5.Display.drawString(badge.role, 270, 455);
    M5.Display.setFont(&fonts::efontCN_24);
    M5.Display.drawString(String("编号 ") + badge.number, 270, 555);
    M5.Display.drawString(badge.contact, 270, 625);
    M5.Display.drawFastHLine(230, 720, 80, TFT_BLACK);
  } else if (badge.layout == 1) {
    // B: asymmetric editorial layout.
    M5.Display.setTextDatum(textdatum_t::top_left);
    M5.Display.setTextSize(1);
    M5.Display.drawString(badge.company, 38, 60);
    M5.Display.drawFastHLine(40, 125, 220, TFT_BLACK);
    M5.Display.setTextSize(2);
    M5.Display.drawString(badge.name, 38, 215);
    M5.Display.setTextSize(1);
    M5.Display.drawString(badge.role, 40, 330);
    M5.Display.setFont(&fonts::efontCN_24);
    M5.Display.drawString(String("编号 ") + badge.number, 40, 460);
    M5.Display.drawString(badge.contact, 40, 525);
    drawBadgePhoto(280, 180, state.hasPhoto());
    M5.Display.drawFastHLine(40, 720, 460, TFT_BLACK);
  } else {
    // A: classic centered portrait.
    M5.Display.setTextDatum(textdatum_t::top_center);
    M5.Display.setTextSize(1);
    M5.Display.drawString(badge.company, 270, 48);
    M5.Display.drawFastHLine(120, 112, 300, TFT_BLACK);
    drawBadgePhoto(150, 150, state.hasPhoto());
    M5.Display.setTextSize(2);
    M5.Display.drawString(badge.name, 270, 425);
    M5.Display.setTextSize(1);
    M5.Display.drawString(badge.role, 270, 535);
    M5.Display.setFont(&fonts::efontCN_24);
    M5.Display.drawFastHLine(120, 615, 300, TFT_BLACK);
    M5.Display.drawString(String("编号 ") + badge.number, 270, 650);
    M5.Display.drawString(badge.contact, 270, 715);
  }
  endFrame();
}

void ClockView::showTodo(const AppState& state, size_t page, bool hideCompleted) {
  beginFrame(true);
  constexpr size_t kItemsPerPage = 10;
  const size_t visibleCount = state.visibleTodoCount(hideCompleted);
  const size_t pageCount = max<size_t>(1, (visibleCount + kItemsPerPage - 1) / kItemsPerPage);
  page = min(page, pageCount - 1);
  const size_t offset = page * kItemsPerPage;
  M5.Display.setTextDatum(textdatum_t::top_left);
  M5.Display.setFont(&fonts::efontCN_24_b);
  M5.Display.setTextSize(1);
  M5.Display.drawString("今日待办", 28, 26);
  M5.Display.setFont(&fonts::efontCN_16);
  const String count = hideCompleted
                           ? String(visibleCount) + " 未完成 / " + state.todoCount() + " 全部"
                           : String(state.todoCount()) + " 项待办";
  M5.Display.drawString(count, 28, 72);

  M5.Display.setTextDatum(textdatum_t::middle_center);
  M5.Display.drawRoundRect(220, 28, 170, 64, 12, TFT_BLACK);
  if (hideCompleted) {
    M5.Display.fillRoundRect(220, 28, 170, 64, 12, TFT_BLACK);
    M5.Display.setTextColor(TFT_WHITE, TFT_BLACK);
  }
  M5.Display.drawString(hideCompleted ? "隐藏完成：开" : "隐藏完成：关", 305, 60);
  M5.Display.setTextColor(TFT_BLACK, TFT_WHITE);
  M5.Display.drawRoundRect(405, 28, 110, 64, 12, TFT_BLACK);
  M5.Display.drawString("清空", 460, 60);
  M5.Display.drawFastHLine(25, 115, 490, TFT_BLACK);

  M5.Display.setTextDatum(textdatum_t::middle_left);
  M5.Display.setFont(&fonts::efontCN_24);
  const size_t visible = min(kItemsPerPage, visibleCount - min(offset, visibleCount));
  for (size_t i = 0; i < visible; ++i) {
    const int y = 130 + i * 72;
    const int sourceIndex = state.todoIndexForVisiblePosition(offset + i, hideCompleted);
    if (sourceIndex < 0) break;
    const auto& item = state.todo(static_cast<size_t>(sourceIndex));
    M5.Display.drawRect(32, y + 15, 36, 36, TFT_BLACK);
    if (item.completed) {
      M5.Display.drawLine(39, y + 33, 49, y + 43, TFT_BLACK);
      M5.Display.drawLine(49, y + 43, 64, y + 21, TFT_BLACK);
    }
    M5.Display.drawString(item.text, 92, y + 34);
    if (item.completed) M5.Display.drawFastHLine(90, y + 34, 410, TFT_BLACK);
    M5.Display.drawFastHLine(28, y + 70, 484, 0xBDF7);
  }
  if (visible == 0) {
    M5.Display.setTextDatum(textdatum_t::middle_center);
    M5.Display.drawString(state.todoCount() == 0 ? "暂无待办，请在网页中添加"
                                                : "全部完成，关闭过滤可查看",
                          270, 460);
  }
  if (pageCount > 1) {
    const String pageText = String("第 ") + (page + 1) + " / " + pageCount +
                            " 页  ·  上下滑动翻页";
    M5.Display.setTextDatum(textdatum_t::bottom_center);
    M5.Display.setFont(&fonts::efontCN_24);
    M5.Display.drawString(pageText, 270, 925);
  }
  endFrame();
}

void ClockView::showTodoConfirmation(const AppState::Todo& item) {
  beginFrame(true);
  M5.Display.drawRoundRect(35, 145, 470, 650, 18, TFT_BLACK);
  M5.Display.setFont(&fonts::efontCN_24_b);
  M5.Display.setTextDatum(textdatum_t::top_center);
  M5.Display.setTextSize(2);
  M5.Display.drawString(item.completed ? "恢复待办？" : "完成待办？", 270, 210);
  M5.Display.setTextSize(1);
  M5.Display.setFont(&fonts::efontCN_24);
  M5.Display.drawString(item.text, 270, 390);
  M5.Display.drawRoundRect(75, 635, 170, 92, 12, TFT_BLACK);
  M5.Display.drawRoundRect(295, 635, 170, 92, 12, TFT_BLACK);
  M5.Display.setTextDatum(textdatum_t::middle_center);
  M5.Display.drawString("取消", 160, 681);
  M5.Display.fillRoundRect(295, 635, 170, 92, 12, TFT_BLACK);
  M5.Display.setTextColor(TFT_WHITE, TFT_BLACK);
  M5.Display.drawString("确认", 380, 681);
  endFrame();
}

void ClockView::showLoading(const char* message) {
  M5.Display.waitDisplay();
  M5.Display.setEpdMode(m5gfx::epd_mode_t::epd_fastest);
  M5.Display.startWrite();
  const int boxWidth = min<int>(420, static_cast<int>(M5.Display.width()) - 48);
  const int boxHeight = 150;
  const int x = (M5.Display.width() - boxWidth) / 2;
  const int y = (M5.Display.height() - boxHeight) / 2;
  M5.Display.fillRoundRect(x, y, boxWidth, boxHeight, 18, TFT_WHITE);
  M5.Display.drawRoundRect(x, y, boxWidth, boxHeight, 18, TFT_BLACK);
  M5.Display.setTextColor(TFT_BLACK, TFT_WHITE);
  M5.Display.setTextDatum(textdatum_t::middle_center);
  M5.Display.setFont(&fonts::efontCN_24_b);
  M5.Display.drawString("处理中…", M5.Display.width() / 2, y + 52);
  M5.Display.setFont(&fonts::efontCN_16);
  M5.Display.drawString(message, M5.Display.width() / 2, y + 105);
  M5.Display.endWrite();
  M5.Display.display();
  M5.Display.waitDisplay();
}

void ClockView::showPoweredOff() {
  beginFrame(true);
  M5.Display.setTextColor(TFT_BLACK, TFT_WHITE);
  M5.Display.setTextDatum(textdatum_t::middle_center);
  M5.Display.setFont(&fonts::efontCN_24_b);
  M5.Display.setTextSize(2);
  M5.Display.drawString("已关机", M5.Display.width() / 2, M5.Display.height() / 2 - 55);
  M5.Display.setTextSize(1);
  M5.Display.setFont(&fonts::efontCN_24);
  M5.Display.drawString("按机身侧键重新开机", M5.Display.width() / 2,
                        M5.Display.height() / 2 + 55);
  endFrame();
  M5.Display.waitDisplay();
}

void ClockView::showWifiResetProgress(uint8_t percent) {
  const int x = 36;
  const int y = 18;
  const int width = 220;
  M5.Display.fillRect(x, y, width, 18, TFT_WHITE);
  M5.Display.drawRect(x, y, width, 18, TFT_BLACK);
  M5.Display.fillRect(x + 2, y + 2, (width - 4) * percent / 100, 14, TFT_BLACK);
  M5.Display.display();
}

const char* ClockView::weekdayName(int weekday) {
  static constexpr const char* kWeekdays[] = {
      "Sunday", "Monday", "Tuesday", "Wednesday", "Thursday", "Friday", "Saturday"};
  return weekday >= 0 && weekday < 7 ? kWeekdays[weekday] : "";
}

}  // namespace paperdesk
