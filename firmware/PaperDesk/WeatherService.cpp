#include "WeatherService.h"

#include <HTTPClient.h>
#include <WiFiClientSecure.h>
#include <cstring>

#include "AppConfig.h"

namespace paperdesk {
namespace {

constexpr char kWeatherPreferences[] = "paperdesk_wx";
constexpr uint32_t kWeatherRefreshMs = 60UL * 60UL * 1000UL;
constexpr uint32_t kWeatherRetryMs = 5UL * 60UL * 1000UL;
constexpr uint32_t kLocationRetryMs = 30UL * 60UL * 1000UL;

}  // namespace

void WeatherService::begin() {
  if (!preferences_.begin(kWeatherPreferences, true)) return;
  snapshot_.city = preferences_.getString("city", "");
  snapshot_.resolvedCity = preferences_.getString("resolved", "");
  latitude_ = preferences_.getFloat("lat", 0.0f);
  longitude_ = preferences_.getFloat("lon", 0.0f);
  hasLocation_ = preferences_.getBool("located", false);
  locationSource_ = preferences_.getString("source", snapshot_.city.isEmpty() ? "" : "manual");
  preferences_.end();
  refreshRequested_ = !snapshot_.city.isEmpty();
}

void WeatherService::update(bool connected) {
  if (!connected) return;
  const uint32_t nowMs = millis();
  if (snapshot_.city.isEmpty()) {
    if (lastAttemptMs_ != 0 && nowMs - lastAttemptMs_ < kLocationRetryMs) return;
    lastAttemptMs_ = nowMs;
    if (!autoLocateFromIp()) return;
  }
  const uint32_t interval = snapshot_.valid ? kWeatherRefreshMs : kWeatherRetryMs;
  if (!refreshRequested_ && lastAttemptMs_ != 0 && nowMs - lastAttemptMs_ < interval) return;

  refreshRequested_ = false;
  lastAttemptMs_ = nowMs;
  snapshot_.error = "";
  if ((!hasLocation_ && !geocode()) || !fetchForecast()) {
    if (snapshot_.error.isEmpty()) snapshot_.error = F("天气更新失败");
    changed_ = true;
    Serial.printf("[weather] update failed: %s\n", snapshot_.error.c_str());
    return;
  }
  changed_ = true;
  Serial.printf("[weather] %s %.1f C, code=%d\n", snapshot_.resolvedCity.c_str(),
                snapshot_.temperature, snapshot_.weatherCode);
}

bool WeatherService::setCity(const String& cityValue) {
  String city = cityValue;
  city.trim();
  if (city.length() > 64) return false;
  snapshot_.city = city;
  snapshot_.resolvedCity = "";
  snapshot_.valid = false;
  snapshot_.error = "";
  hasLocation_ = false;
  locationSource_ = city.isEmpty() ? "" : "manual";
  latitude_ = 0.0f;
  longitude_ = 0.0f;
  saveLocation();
  refreshRequested_ = !city.isEmpty();
  changed_ = true;
  return true;
}

bool WeatherService::setCoordinates(float latitude, float longitude, const String& labelValue,
                                    const String& source) {
  if (!isfinite(latitude) || !isfinite(longitude) || latitude < -90.0f || latitude > 90.0f ||
      longitude < -180.0f || longitude > 180.0f) return false;

  String label = labelValue;
  label.trim();
  if (label.length() > 64) return false;
  if (label.isEmpty()) reverseGeocode(latitude, longitude, label);
  if (label.isEmpty()) label = F("当前位置");

  latitude_ = latitude;
  longitude_ = longitude;
  hasLocation_ = true;
  locationSource_ = source;
  snapshot_.city = label;
  snapshot_.resolvedCity = label;
  snapshot_.valid = false;
  snapshot_.error = "";
  saveLocation();
  refreshRequested_ = true;
  changed_ = true;
  return true;
}

bool WeatherService::autoLocateFromIp() {
  String body;
  snapshot_.error = "";
  if (!httpGet(F("https://ipwho.is/"), body)) {
    snapshot_.error = F("网络自动定位失败，请手动填写城市");
    changed_ = true;
    return false;
  }

  float latitude = 0.0f;
  float longitude = 0.0f;
  String city;
  if (!parseNumberAfter(body, F("\"latitude\":"), latitude) ||
      !parseNumberAfter(body, F("\"longitude\":"), longitude) ||
      !parseStringAfter(body, F("\"city\":"), city) || city.isEmpty()) {
    snapshot_.error = F("网络定位没有返回有效城市");
    changed_ = true;
    return false;
  }

  return setCoordinates(latitude, longitude, city, "ip");
}

void WeatherService::requestRefresh() {
  refreshRequested_ = !snapshot_.city.isEmpty();
}

bool WeatherService::consumeChanged() {
  const bool value = changed_;
  changed_ = false;
  return value;
}

bool WeatherService::geocode() {
  String body;
  const String url = String(F("https://geocoding-api.open-meteo.com/v1/search?count=1&language=zh&format=json&name=")) +
                     urlEncode(snapshot_.city);
  if (!httpGet(url, body)) return false;

  float latitude = 0.0f;
  float longitude = 0.0f;
  String resolved;
  const size_t results = body.indexOf(F("\"results\""));
  if (results == static_cast<size_t>(-1) ||
      !parseNumberAfter(body, F("\"latitude\":"), latitude, results) ||
      !parseNumberAfter(body, F("\"longitude\":"), longitude, results)) {
    snapshot_.error = F("未找到这个城市，请换用“城市,省份”重试");
    return false;
  }
  parseStringAfter(body, F("\"name\":"), resolved, results);
  latitude_ = latitude;
  longitude_ = longitude;
  hasLocation_ = true;
  snapshot_.resolvedCity = resolved.isEmpty() ? snapshot_.city : resolved;
  saveLocation();
  return true;
}

bool WeatherService::reverseGeocode(float latitude, float longitude, String& city) {
  String url = F("https://nominatim.openstreetmap.org/reverse?format=jsonv2&addressdetails=1&accept-language=zh-CN&zoom=10&lat=");
  url += String(latitude, 6);
  url += F("&lon=");
  url += String(longitude, 6);
  String body;
  if (!httpGet(url, body)) return false;
  const size_t address = body.indexOf(F("\"address\":"));
  if (address == static_cast<size_t>(-1)) return false;
  if (parseStringAfter(body, F("\"city\":"), city, address) && !city.isEmpty()) return true;
  if (parseStringAfter(body, F("\"town\":"), city, address) && !city.isEmpty()) return true;
  if (parseStringAfter(body, F("\"municipality\":"), city, address) && !city.isEmpty()) return true;
  if (parseStringAfter(body, F("\"county\":"), city, address) && !city.isEmpty()) return true;
  return parseStringAfter(body, F("\"state\":"), city, address) && !city.isEmpty();
}

bool WeatherService::fetchForecast() {
  String url = F("https://api.open-meteo.com/v1/forecast?latitude=");
  url += String(latitude_, 5);
  url += F("&longitude=");
  url += String(longitude_, 5);
  url += F("&current=temperature_2m,apparent_temperature,relative_humidity_2m,precipitation,wind_speed_10m,wind_direction_10m,weather_code,is_day");
  url += F("&hourly=temperature_2m,precipitation_probability,relative_humidity_2m,pressure_msl,uv_index,weather_code");
  url += F("&daily=temperature_2m_max,temperature_2m_min,sunrise,sunset&temperature_unit=celsius");
  url += F("&wind_speed_unit=kmh&timezone=auto&forecast_days=2");

  String body;
  if (!httpGet(url, body)) return false;
  const size_t current = body.indexOf(F("\"current\":"));
  float temperature = 0.0f;
  float apparent = 0.0f;
  float high = 0.0f;
  float low = 0.0f;
  float humidity = 0.0f;
  float precipitation = 0.0f;
  float windSpeed = 0.0f;
  float windDirection = 0.0f;
  int code = -1;
  int isDay = 1;
  String currentTime;
  const size_t hourly = body.indexOf(F("\"hourly\":"));
  const size_t daily = body.indexOf(F("\"daily\":"));
  if (current == static_cast<size_t>(-1) ||
      !parseNumberAfter(body, F("\"temperature_2m\":"), temperature, current) ||
      !parseNumberAfter(body, F("\"apparent_temperature\":"), apparent, current) ||
      !parseNumberAfter(body, F("\"relative_humidity_2m\":"), humidity, current) ||
      !parseNumberAfter(body, F("\"precipitation\":"), precipitation, current) ||
      !parseNumberAfter(body, F("\"wind_speed_10m\":"), windSpeed, current) ||
      !parseNumberAfter(body, F("\"wind_direction_10m\":"), windDirection, current) ||
      !parseIntAfter(body, F("\"weather_code\":"), code, current) ||
      hourly == static_cast<size_t>(-1) || daily == static_cast<size_t>(-1) ||
      !parseArrayNumber(body, F("\"temperature_2m_max\":"), 0, high, daily) ||
      !parseArrayNumber(body, F("\"temperature_2m_min\":"), 0, low, daily)) {
    snapshot_.error = F("天气服务返回的数据无法解析");
    return false;
  }
  parseIntAfter(body, F("\"is_day\":"), isDay, current);
  parseStringAfter(body, F("\"time\":"), currentTime, current);
  int currentHour = 0;
  const int timeSeparator = currentTime.indexOf('T');
  if (timeSeparator >= 0 && currentTime.length() >= static_cast<size_t>(timeSeparator + 3)) {
    currentHour = currentTime.substring(timeSeparator + 1, timeSeparator + 3).toInt();
  }
  snapshot_.temperature = temperature;
  snapshot_.apparentTemperature = apparent;
  snapshot_.humidity = humidity;
  snapshot_.precipitation = precipitation;
  snapshot_.windSpeed = windSpeed;
  snapshot_.windDirection = windDirection;
  snapshot_.todayMax = high;
  snapshot_.todayMin = low;
  snapshot_.weatherCode = code;
  snapshot_.isDay = isDay != 0;
  String sunrise;
  String sunset;
  if (parseArrayString(body, F("\"sunrise\":"), 0, sunrise, daily)) {
    const int separator = sunrise.indexOf('T');
    snapshot_.sunrise = separator >= 0 ? sunrise.substring(separator + 1) : sunrise;
  }
  if (parseArrayString(body, F("\"sunset\":"), 0, sunset, daily)) {
    const int separator = sunset.indexOf('T');
    snapshot_.sunset = separator >= 0 ? sunset.substring(separator + 1) : sunset;
  }
  for (int i = 0; i < 8; ++i) {
    const int sourceIndex = currentHour + i + 1;
    auto& item = snapshot_.hourly[i];
    item.hour = (currentHour + i + 1) % 24;
    float parsed = 0.0f;
    if (parseArrayNumber(body, F("\"temperature_2m\":"), sourceIndex, parsed, hourly))
      item.temperature = parsed;
    if (parseArrayNumber(body, F("\"precipitation_probability\":"), sourceIndex, parsed, hourly))
      item.precipitationProbability = parsed;
    if (parseArrayNumber(body, F("\"relative_humidity_2m\":"), sourceIndex, parsed, hourly))
      item.humidity = parsed;
    if (parseArrayNumber(body, F("\"pressure_msl\":"), sourceIndex, parsed, hourly))
      item.pressure = parsed;
    if (parseArrayNumber(body, F("\"uv_index\":"), sourceIndex, parsed, hourly))
      item.uvIndex = parsed;
    if (parseArrayNumber(body, F("\"weather_code\":"), sourceIndex, parsed, hourly))
      item.weatherCode = static_cast<int>(parsed);
  }
  snapshot_.valid = true;
  snapshot_.updatedAt = time(nullptr);
  snapshot_.error = "";
  return true;
}

bool WeatherService::httpGet(const String& url, String& response) {
  WiFiClientSecure client;
  client.setInsecure();
  HTTPClient http;
  http.setConnectTimeout(8000);
  http.setTimeout(10000);
  if (!http.begin(client, url)) {
    snapshot_.error = F("无法连接天气服务");
    return false;
  }
  http.addHeader(F("Accept"), F("application/json"));
  http.addHeader(F("User-Agent"),
                 F("PaperDesk-M5Stack/1.0 (github.com/xiaocheng2014/papers3-paper-desk)"));
  http.addHeader(F("Accept-Language"), F("zh-CN,zh;q=0.9,en;q=0.6"));
  const int status = http.GET();
  if (status != HTTP_CODE_OK) {
    snapshot_.error = String(F("天气服务 HTTP ")) + status;
    http.end();
    return false;
  }
  response = http.getString();
  http.end();
  return !response.isEmpty();
}

void WeatherService::saveLocation() {
  if (!preferences_.begin(kWeatherPreferences, false)) return;
  preferences_.putString("city", snapshot_.city);
  preferences_.putString("resolved", snapshot_.resolvedCity);
  preferences_.putFloat("lat", latitude_);
  preferences_.putFloat("lon", longitude_);
  preferences_.putBool("located", hasLocation_);
  preferences_.putString("source", locationSource_);
  preferences_.end();
}

String WeatherService::urlEncode(const String& value) {
  String encoded;
  char buffer[4];
  for (size_t i = 0; i < value.length(); ++i) {
    const uint8_t ch = static_cast<uint8_t>(value[i]);
    if ((ch >= 'a' && ch <= 'z') || (ch >= 'A' && ch <= 'Z') ||
        (ch >= '0' && ch <= '9') || ch == '-' || ch == '_' || ch == '.' || ch == '~') {
      encoded += static_cast<char>(ch);
    } else {
      snprintf(buffer, sizeof(buffer), "%%%02X", ch);
      encoded += buffer;
    }
  }
  return encoded;
}

bool WeatherService::parseNumberAfter(const String& json, const String& key, float& value,
                                      size_t from) {
  const int keyPos = json.indexOf(key, from);
  if (keyPos < 0) return false;
  const char* start = json.c_str() + keyPos + key.length();
  char* end = nullptr;
  value = strtof(start, &end);
  return end != start;
}

bool WeatherService::parseIntAfter(const String& json, const String& key, int& value,
                                   size_t from) {
  float parsed = 0.0f;
  if (!parseNumberAfter(json, key, parsed, from)) return false;
  value = static_cast<int>(parsed);
  return true;
}

bool WeatherService::parseStringAfter(const String& json, const String& key, String& value,
                                      size_t from) {
  const int keyPos = json.indexOf(key, from);
  if (keyPos < 0) return false;
  const int open = json.indexOf('"', keyPos + key.length());
  if (open < 0) return false;
  const int close = json.indexOf('"', open + 1);
  if (close < 0) return false;
  value = json.substring(open + 1, close);
  return true;
}

bool WeatherService::parseFirstArrayNumber(const String& json, const String& key, float& value) {
  return parseArrayNumber(json, key, 0, value);
}

bool WeatherService::parseArrayNumber(const String& json, const String& key, int index,
                                      float& value, size_t from) {
  const int keyPos = json.indexOf(key, from);
  if (keyPos < 0) return false;
  const int open = json.indexOf('[', keyPos + key.length());
  if (open < 0) return false;
  const char* start = json.c_str() + open + 1;
  for (int i = 0; i < index; ++i) {
    start = strchr(start, ',');
    if (!start) return false;
    ++start;
  }
  char* end = nullptr;
  value = strtof(start, &end);
  return end != start;
}

bool WeatherService::parseArrayString(const String& json, const String& key, int index,
                                      String& value, size_t from) {
  const int keyPos = json.indexOf(key, from);
  if (keyPos < 0) return false;
  int cursor = json.indexOf('[', keyPos + key.length());
  if (cursor < 0) return false;
  for (int i = 0; i <= index; ++i) {
    const int open = json.indexOf('"', cursor + 1);
    if (open < 0) return false;
    const int close = json.indexOf('"', open + 1);
    if (close < 0) return false;
    if (i == index) {
      value = json.substring(open + 1, close);
      return true;
    }
    cursor = close;
  }
  return false;
}

}  // namespace paperdesk
