#pragma once

#include <Arduino.h>
#include <Preferences.h>

namespace paperdesk {

struct WeatherSnapshot {
  struct Hourly {
    int hour = 0;
    float temperature = 0.0f;
    float precipitationProbability = 0.0f;
    float humidity = 0.0f;
    float pressure = 0.0f;
    float uvIndex = 0.0f;
    int weatherCode = -1;
  };

  String city;
  String resolvedCity;
  float temperature = 0.0f;
  float apparentTemperature = 0.0f;
  float humidity = 0.0f;
  float precipitation = 0.0f;
  float windSpeed = 0.0f;
  float windDirection = 0.0f;
  float todayMin = 0.0f;
  float todayMax = 0.0f;
  String sunrise;
  String sunset;
  int weatherCode = -1;
  bool isDay = true;
  bool valid = false;
  time_t updatedAt = 0;
  String error;
  Hourly hourly[8];
};

class WeatherService {
 public:
  void begin();
  void update(bool connected);
  bool setCity(const String& city);
  void requestRefresh();
  bool consumeChanged();

  const WeatherSnapshot& snapshot() const { return snapshot_; }

 private:
  bool geocode();
  bool fetchForecast();
  bool httpGet(const String& url, String& response);
  void saveLocation();
  static String urlEncode(const String& value);
  static bool parseNumberAfter(const String& json, const String& key, float& value,
                               size_t from = 0);
  static bool parseIntAfter(const String& json, const String& key, int& value,
                            size_t from = 0);
  static bool parseStringAfter(const String& json, const String& key, String& value,
                               size_t from = 0);
  static bool parseFirstArrayNumber(const String& json, const String& key, float& value);
  static bool parseArrayNumber(const String& json, const String& key, int index, float& value,
                               size_t from = 0);
  static bool parseArrayString(const String& json, const String& key, int index, String& value,
                               size_t from = 0);

  Preferences preferences_;
  WeatherSnapshot snapshot_;
  float latitude_ = 0.0f;
  float longitude_ = 0.0f;
  bool hasLocation_ = false;
  bool refreshRequested_ = true;
  bool changed_ = false;
  uint32_t lastAttemptMs_ = 0;
};

}  // namespace paperdesk
