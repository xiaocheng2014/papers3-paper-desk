#pragma once

#include <Arduino.h>
#include <DNSServer.h>
#include <Preferences.h>
#include <WebServer.h>
#include <WiFi.h>

namespace paperdesk {

class AppState;
class WeatherService;

class WifiPortal {
 public:
  enum class State {
    kIdle,
    kConnecting,
    kProvisioning,
    kConnected,
    kConnectionFailed,
  };

  void begin();
  void update();
  void clearCredentials();
  void attachState(AppState* state) { appState_ = state; }
  void attachWeather(WeatherService* weather) { weatherService_ = weather; }
  bool consumeContentChanged() {
    const bool changed = contentChanged_;
    contentChanged_ = false;
    return changed;
  }

  State state() const { return state_; }
  bool connected() const { return state_ == State::kConnected && WiFi.status() == WL_CONNECTED; }
  bool provisioning() const { return portalActive_; }
  const String& apSsid() const { return apSsid_; }
  String stationSsid() const {
    if (connected()) return WiFi.SSID();
    if (!pendingSsid_.isEmpty()) return pendingSsid_;
    return savedSsid_;
  }
  String stationIp() const { return connected() ? WiFi.localIP().toString() : String(); }
  const String& lastError() const { return lastError_; }

 private:
  void configureRoutes();
  void startSavedConnection();
  void startConnection(const String& ssid, const String& password, bool fromPortal);
  void startPortal(const String& reason = String());
  void finishConnection();
  void saveCredentials(const String& ssid, const String& password);
  bool loadCredentials();
  void scheduleRestart();

  void handleRoot();
  void handleStatus();
  void handleNetworks();
  void handleConnect();
  void handleResetWifi();
  void handleLogin();
  void handleApp();
  void handleMode();
  void handleBadge();
  void handleWeather();
  void handleTodoAdd();
  void handleTodoDelete();
  void handleTodoMove();
  void handleTodoToggle();
  void handleTodoTemplates();
  void handleTodoTemplateSave();
  void handleTodoTemplateDelete();
  void handleTodoTemplateImport();
  void handlePhotoComplete();
  void handlePhotoChunk();
  void handleNotFound();

  bool isAuthorized() const;
  bool requireAuthorized();

  String makeApSsid() const;
  String stateName() const;
  static String jsonEscape(const String& value);

  DNSServer dnsServer_;
  WebServer server_{80};
  Preferences preferences_;
  AppState* appState_ = nullptr;
  WeatherService* weatherService_ = nullptr;
  State state_ = State::kIdle;

  String savedSsid_;
  String savedPassword_;
  String pendingSsid_;
  String pendingPassword_;
  String apSsid_;
  String lastError_;

  uint32_t connectionStartedMs_ = 0;
  uint32_t connectedMs_ = 0;
  uint32_t restartAtMs_ = 0;
  bool pendingFromPortal_ = false;
  bool portalActive_ = false;
  bool routesConfigured_ = false;
  bool restartScheduled_ = false;
  bool clearCredentialsOnRestart_ = false;
  bool contentChanged_ = false;
  bool photoUploadOk_ = false;
  size_t photoUploadBytes_ = 0;
  String photoUploadError_;
};

}  // namespace paperdesk
