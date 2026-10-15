#include "WifiPortal.h"

#include <ESPmDNS.h>
#include <esp_mac.h>

#include <algorithm>
#include <cstring>

#include "AppConfig.h"
#include "AppState.h"
#include "ManagementPage.h"
#include "WeatherService.h"

namespace paperdesk {
namespace {

constexpr byte kDnsPort = 53;
constexpr uint32_t kOfflineRetryIntervalMs = 5UL * 60UL * 1000UL;
const IPAddress kPortalIp(192, 168, 4, 1);
const IPAddress kPortalGateway(192, 168, 4, 1);
const IPAddress kPortalMask(255, 255, 255, 0);

const char kSetupPage[] PROGMEM = R"HTML(
<!doctype html>
<html lang="zh-CN">
<head>
  <meta charset="utf-8">
  <meta name="viewport" content="width=device-width,initial-scale=1">
  <title>PaperDesk</title>
  <style>
    :root{color-scheme:light;--ink:#171717;--paper:#f3f0e8;--line:#c8c1b2;--accent:#242424}
    *{box-sizing:border-box} body{margin:0;background:var(--paper);color:var(--ink);font-family:system-ui,-apple-system,sans-serif}
    main{max-width:620px;margin:auto;padding:28px 18px 48px} h1{font-family:Georgia,serif;font-size:44px;margin:0 0 6px}
    .sub{margin:0 0 24px;color:#555}.card{background:#fffdf7;border:1px solid var(--line);border-radius:16px;padding:20px;margin:14px 0;box-shadow:0 8px 24px #0000000c}
    label{display:block;font-weight:650;margin:14px 0 6px}select,input,button{width:100%;font:inherit;border-radius:10px;padding:13px;border:1px solid #aaa;background:white}
    button{margin-top:18px;background:var(--accent);color:white;border-color:var(--accent);font-weight:700;cursor:pointer}button.secondary{background:white;color:#333}
    .status{padding:12px;border-left:4px solid #555;background:#f4f4f4;white-space:pre-wrap}.ok{border-color:#16803c;background:#eef8f0}.bad{border-color:#a42323;background:#fff0f0}
    .modes{display:grid;grid-template-columns:repeat(3,1fr);gap:8px}.mode{padding:14px 6px;text-align:center;border:1px solid var(--line);border-radius:10px;color:#777}
    small{color:#666}@media(max-width:480px){h1{font-size:36px}.modes{grid-template-columns:1fr}}
  </style>
</head>
<body><main>
  <h1>PaperDesk</h1><p class="sub">水墨屏桌面信息终端</p>
  <div id="status" class="status">正在读取设备状态…</div>
  <section id="provision" class="card" hidden>
    <h2>连接家庭 Wi-Fi</h2>
    <label for="ssid">网络</label><select id="ssid"><option>正在扫描…</option></select>
    <label for="password">密码</label><input id="password" type="password" maxlength="63" autocomplete="current-password" placeholder="开放网络可留空">
    <button id="connect">保存并连接</button><small>连接成功后加入已保存网络，不会删除其他 Wi-Fi。</small>
  </section>
  <section id="dashboard" class="card" hidden>
    <h2>设备管理</h2><div class="modes"><div class="mode">时钟<br><small>当前版本</small></div><div class="mode">工牌<br><small>下一里程碑</small></div><div class="mode">Todo<br><small>下一里程碑</small></div></div>
    <button id="reset" class="secondary">清除 Wi-Fi 并重新配网</button>
  </section>
</main>
<script>
const statusBox=document.querySelector('#status'),provision=document.querySelector('#provision'),dashboard=document.querySelector('#dashboard');
const escapeText=v=>String(v??'');
async function loadNetworks(){try{const r=await fetch('/api/networks');const d=await r.json();const s=document.querySelector('#ssid');s.textContent='';d.networks.forEach(n=>{const o=document.createElement('option');o.value=n.ssid;o.textContent=`${n.ssid} (${n.rssi} dBm)${n.open?' · 开放':''}`;s.appendChild(o)});if(!d.networks.length){const o=document.createElement('option');o.textContent='未发现网络，请稍后刷新';s.appendChild(o)}}catch(e){statusBox.textContent='扫描失败：'+e.message}}
async function update(){try{const r=await fetch('/api/status',{cache:'no-store'}),d=await r.json();const connected=d.connected;statusBox.className='status '+(connected?'ok':(d.error?'bad':''));statusBox.textContent=connected?`已连接 ${escapeText(d.ssid)}\n管理地址：http://${escapeText(d.ip)}/`:`状态：${escapeText(d.state)}${d.apSsid?'\n热点：'+escapeText(d.apSsid):''}${d.error?'\n'+escapeText(d.error):''}`;provision.hidden=connected;dashboard.hidden=!connected;if(!connected&&!window.scanned){window.scanned=true;loadNetworks()}}catch(e){statusBox.textContent='等待设备连接…'} }
document.querySelector('#connect').onclick=async()=>{const ssid=document.querySelector('#ssid').value,password=document.querySelector('#password').value;statusBox.className='status';statusBox.textContent='正在连接，请稍候…';const body=new URLSearchParams({ssid,password});const r=await fetch('/api/connect',{method:'POST',headers:{'Content-Type':'application/x-www-form-urlencoded'},body});if(!r.ok){statusBox.className='status bad';statusBox.textContent=await r.text()}else{setTimeout(update,1000)}};
document.querySelector('#reset').onclick=async()=>{if(confirm('确定清除 Wi-Fi 设置并重启配网？')){await fetch('/api/reset-wifi',{method:'POST'});statusBox.textContent='设备正在重启…'}};
update();setInterval(update,1500);
</script></body></html>
)HTML";

}  // namespace

void WifiPortal::begin() {
  WiFi.persistent(false);
  WiFi.setAutoReconnect(true);
  // Association and captive-portal setup are kept fully awake. Power saving
  // is enabled only after the station has obtained an address.
  WiFi.setSleep(false);
  WiFi.setHostname(kHostname);
  configureRoutes();
  const char* protectedHeaders[] = {"X-PaperDesk-Key"};
  server_.collectHeaders(protectedHeaders, 1);

  if (loadCredentials()) {
    startSavedConnection();
  } else {
    startPortal();
  }
  // NetworkServer needs the ESP-IDF TCP/IP stack created by WiFi.mode().
  server_.begin();
  serverRunning_ = true;
}

void WifiPortal::configureRoutes() {
  if (routesConfigured_) return;
  routesConfigured_ = true;

  server_.on("/", HTTP_GET, [this] { handleRoot(); });
  server_.on("/api/status", HTTP_GET, [this] { handleStatus(); });
  server_.on("/api/networks", HTTP_GET, [this] { handleNetworks(); });
  server_.on("/api/connect", HTTP_POST, [this] { handleConnect(); });
  server_.on("/api/reset-wifi", HTTP_POST, [this] { handleResetWifi(); });
  server_.on("/api/login", HTTP_POST, [this] { handleLogin(); });
  server_.on("/api/app", HTTP_GET, [this] { handleApp(); });
  server_.on("/api/mode", HTTP_POST, [this] { handleMode(); });
  server_.on("/api/badge", HTTP_POST, [this] { handleBadge(); });
  server_.on("/api/weather", HTTP_POST, [this] { handleWeather(); });
  server_.on("/api/weather/location", HTTP_POST, [this] { handleWeatherLocation(); });
  server_.on("/api/weather/auto-location", HTTP_POST,
             [this] { handleWeatherAutoLocation(); });
  server_.on("/api/device/lock", HTTP_POST, [this] { handleDeviceLock(); });
  server_.on("/api/device/restart", HTTP_POST, [this] { handleDeviceRestart(); });
  server_.on("/api/device/power-off", HTTP_POST, [this] { handleDevicePowerOff(); });
  server_.on("/api/todos", HTTP_GET, [this] { handleTodos(); });
  server_.on("/api/todos/add", HTTP_POST, [this] { handleTodoAdd(); });
  server_.on("/api/todos/delete", HTTP_POST, [this] { handleTodoDelete(); });
  server_.on("/api/todos/move", HTTP_POST, [this] { handleTodoMove(); });
  server_.on("/api/todos/toggle", HTTP_POST, [this] { handleTodoToggle(); });
  server_.on("/api/todo-templates", HTTP_GET, [this] { handleTodoTemplates(); });
  server_.on("/api/todo-templates/save", HTTP_POST, [this] { handleTodoTemplateSave(); });
  server_.on("/api/todo-templates/delete", HTTP_POST, [this] { handleTodoTemplateDelete(); });
  server_.on("/api/todos/import", HTTP_POST, [this] { handleTodoTemplateImport(); });
  server_.on("/api/badge/photo", HTTP_POST, [this] { handlePhotoComplete(); },
             [this] { handlePhotoChunk(); });

  server_.on("/generate_204", HTTP_ANY, [this] { handleRoot(); });
  server_.on("/gen_204", HTTP_ANY, [this] { handleRoot(); });
  server_.on("/hotspot-detect.html", HTTP_ANY, [this] { handleRoot(); });
  server_.on("/library/test/success.html", HTTP_ANY, [this] { handleRoot(); });
  server_.on("/ncsi.txt", HTTP_ANY, [this] { handleRoot(); });
  server_.on("/connecttest.txt", HTTP_ANY, [this] { handleRoot(); });
  server_.onNotFound([this] { handleNotFound(); });
}

bool WifiPortal::loadCredentials() {
  credentials_.clear();
  if (!preferences_.begin(kPreferencesNamespace, true)) return false;
  const size_t blobSize = preferences_.getBytesLength("wifi_list");
  if (blobSize > 0 && blobSize % sizeof(WifiCredential) == 0 &&
      blobSize <= sizeof(WifiCredential) * 32) {
    credentials_.resize(blobSize / sizeof(WifiCredential));
    if (preferences_.getBytes("wifi_list", credentials_.data(), blobSize) != blobSize) {
      credentials_.clear();
    }
  }
  const String legacySsid = preferences_.getString("ssid", "");
  const String legacyPassword = preferences_.getString("password", "");
  preferences_.end();

  // Upgrade the old one-network format without making the user configure it again.
  if (credentials_.empty() && !legacySsid.isEmpty()) {
    saveCredentials(legacySsid, legacyPassword);
  }
  return !credentials_.empty();
}

void WifiPortal::saveCredentials(const String& ssid, const String& password) {
  WifiCredential credential{};
  strlcpy(credential.ssid, ssid.c_str(), sizeof(credential.ssid));
  strlcpy(credential.password, password.c_str(), sizeof(credential.password));
  const auto found = std::find_if(credentials_.begin(), credentials_.end(),
                                  [&ssid](const WifiCredential& item) {
                                    return ssid == item.ssid;
                                  });
  if (found != credentials_.end()) credentials_.erase(found);
  credentials_.insert(credentials_.begin(), credential);
  credentialIndex_ = 0;
  persistCredentials();
}

bool WifiPortal::persistCredentials() {
  if (!preferences_.begin(kPreferencesNamespace, false)) return false;
  const size_t byteCount = credentials_.size() * sizeof(WifiCredential);
  const bool ok = byteCount == 0
                      ? preferences_.remove("wifi_list")
                      : preferences_.putBytes("wifi_list", credentials_.data(), byteCount) ==
                            byteCount;
  if (ok) {
    preferences_.remove("ssid");
    preferences_.remove("password");
  }
  preferences_.end();
  return ok;
}

void WifiPortal::startSavedConnection() {
  if (credentials_.empty()) {
    state_ = State::kIdle;
    lastError_ = "当前离线；可在设备设置中开启 Wi-Fi 配网。";
    return;
  }
  if (portalActive_) {
    dnsServer_.stop();
    WiFi.softAPdisconnect(true);
  }
  portalActive_ = false;
  pendingFromPortal_ = false;
  lastError_ = "";
  credentialIndex_ = 0;
  connectSavedCredential(credentialIndex_);
}

void WifiPortal::connectSavedCredential(size_t index) {
  if (index >= credentials_.size()) return;
  credentialIndex_ = index;
  state_ = State::kConnecting;
  connectionStartedMs_ = millis();
  WiFi.mode(WIFI_STA);
  WiFi.setSleep(false);
  appliedWifiPowerSave_ = 0;
  Serial.printf("[wifi] trying saved network %u/%u: %s\n",
                static_cast<unsigned>(index + 1),
                static_cast<unsigned>(credentials_.size()), credentials_[index].ssid);
  WiFi.begin(credentials_[index].ssid, credentials_[index].password);
}

void WifiPortal::startConnection(const String& ssid, const String& password, bool fromPortal) {
  pendingSsid_ = ssid;
  pendingPassword_ = password;
  pendingFromPortal_ = fromPortal;
  lastError_ = "";
  state_ = State::kConnecting;
  connectionStartedMs_ = millis();

  WiFi.disconnect(false, false);
  delay(100);
  if (portalActive_) WiFi.mode(WIFI_AP_STA);
  else WiFi.mode(WIFI_STA);
  WiFi.setSleep(false);
  appliedWifiPowerSave_ = 0;
  WiFi.begin(ssid.c_str(), password.c_str());
}

void WifiPortal::startPortal(const String& reason) {
  lastError_ = reason;
  state_ = reason.isEmpty() ? State::kProvisioning : State::kConnectionFailed;
  portalActive_ = true;
  pendingFromPortal_ = false;
  pendingSsid_ = "";
  pendingPassword_ = "";

  WiFi.disconnect(false, false);
  delay(100);
  WiFi.mode(WIFI_AP_STA);
  WiFi.setSleep(false);
  appliedWifiPowerSave_ = 0;
  WiFi.softAPConfig(kPortalIp, kPortalGateway, kPortalMask);
  apSsid_ = makeApSsid();
  WiFi.softAP(apSsid_.c_str(), kAdminPassword);
  dnsServer_.start(kDnsPort, "*", kPortalIp);
  Serial.printf("[wifi] provisioning AP ready: %s, http://%s/\n",
                apSsid_.c_str(), kPortalIp.toString().c_str());
}

void WifiPortal::startProvisioning() {
  startPortal();
  syncNetworkServices();
}

String WifiPortal::stationSsid() const {
  if (connected()) return WiFi.SSID();
  if (!pendingSsid_.isEmpty()) return pendingSsid_;
  if (credentialIndex_ < credentials_.size()) return String(credentials_[credentialIndex_].ssid);
  return String();
}

void WifiPortal::finishConnection() {
  state_ = State::kConnected;
  connectedMs_ = millis();
  lastError_ = "";
  if (pendingFromPortal_) {
    saveCredentials(pendingSsid_, pendingPassword_);
  } else if (credentialIndex_ < credentials_.size() && credentialIndex_ != 0) {
    const String ssid(credentials_[credentialIndex_].ssid);
    const String password(credentials_[credentialIndex_].password);
    saveCredentials(ssid, password);
  }
  pendingFromPortal_ = false;
  pendingSsid_ = "";
  pendingPassword_ = "";
  syncNetworkServices();
  Serial.printf("[wifi] connected: %s, IP=%s, web=%s\n",
                WiFi.SSID().c_str(), WiFi.localIP().toString().c_str(),
                serverRunning_ ? "enabled" : "disabled");
}

void WifiPortal::update() {
  if (serverRunning_) server_.handleClient();
  if (portalActive_) dnsServer_.processNextRequest();

  if (restartScheduled_ && static_cast<int32_t>(millis() - restartAtMs_) >= 0) {
    if (clearCredentialsOnRestart_) clearCredentials();
    ESP.restart();
  }

  if (state_ == State::kConnecting) {
    if (WiFi.status() == WL_CONNECTED) {
      finishConnection();
    } else if (millis() - connectionStartedMs_ >= kWifiConnectTimeoutMs) {
      if (pendingFromPortal_) {
        startPortal("连接失败，请检查 Wi-Fi 名称和密码。已有网络未被删除。");
      } else if (credentialIndex_ + 1 < credentials_.size()) {
        WiFi.disconnect(false, false);
        delay(50);
        connectSavedCredential(credentialIndex_ + 1);
      } else {
        WiFi.disconnect(false, false);
        state_ = State::kIdle;
        offlineRetryAtMs_ = millis() + kOfflineRetryIntervalMs;
        lastError_ = "附近没有已保存的 Wi-Fi，当前离线使用。";
        Serial.println("[wifi] no saved network available; continuing offline");
      }
    }
  } else if (state_ == State::kConnected) {
    if (portalActive_ && millis() - connectedMs_ >= kPortalShutdownDelayMs) {
      dnsServer_.stop();
      WiFi.softAPdisconnect(true);
      portalActive_ = false;
      WiFi.mode(WIFI_STA);
      appliedWifiPowerSave_ = 2;
    }

    if (WiFi.status() != WL_CONNECTED) {
      startSavedConnection();
    }
  } else if (state_ == State::kIdle && !portalActive_ && !credentials_.empty() &&
             static_cast<int32_t>(millis() - offlineRetryAtMs_) >= 0) {
    startSavedConnection();
  }
  syncNetworkServices();
}

void WifiPortal::syncNetworkServices() {
  const bool connectedNow = state_ == State::kConnected && WiFi.status() == WL_CONNECTED;
  const bool webEnabled = !appState_ || appState_->settings().webManagementEnabled != 0;
  // Provisioning and connection diagnostics always need the local server. Once
  // online, the user's on-device switch owns whether HTTP and mDNS keep running.
  const bool shouldRunServer = portalActive_ || (connectedNow && webEnabled);

  if (shouldRunServer && !serverRunning_) {
    server_.begin();
    serverRunning_ = true;
    Serial.println("[web] management server enabled");
  } else if (!shouldRunServer && serverRunning_) {
    if (mdnsRunning_) {
      MDNS.end();
      mdnsRunning_ = false;
    }
    server_.stop();
    serverRunning_ = false;
    Serial.println("[web] management server disabled");
  }

  const bool shouldRunMdns = connectedNow && webEnabled && serverRunning_ && !portalActive_;
  if (shouldRunMdns && !mdnsRunning_) {
    mdnsRunning_ = MDNS.begin(kHostname);
  } else if (!shouldRunMdns && mdnsRunning_) {
    MDNS.end();
    mdnsRunning_ = false;
  }

  if (connectedNow) {
    const uint8_t desiredPowerSave =
        appState_ && appState_->settings().wifiPowerSave == 0 ? 0 : 1;
    if (desiredPowerSave != appliedWifiPowerSave_) {
      WiFi.setSleep(desiredPowerSave != 0);
      appliedWifiPowerSave_ = desiredPowerSave;
    }
  }
}

void WifiPortal::clearCredentials() {
  if (preferences_.begin(kPreferencesNamespace, false)) {
    preferences_.clear();
    preferences_.end();
  }
  credentials_.clear();
  credentialIndex_ = 0;
  pendingSsid_ = "";
  pendingPassword_ = "";
  WiFi.disconnect(true, true);
}

void WifiPortal::scheduleRestart() {
  restartScheduled_ = true;
  restartAtMs_ = millis() + 1000;
}

void WifiPortal::handleRoot() {
  server_.sendHeader("Cache-Control", "no-store, no-cache, must-revalidate");
  server_.send_P(200, "text/html; charset=utf-8", kManagementPage);
}

void WifiPortal::handleStatus() {
  String body;
  body.reserve(256);
  body += F("{\"state\":\"");
  body += jsonEscape(stateName());
  body += F("\",\"connected\":");
  body += connected() ? F("true") : F("false");
  body += F(",\"ssid\":\"");
  body += jsonEscape(stationSsid());
  body += F("\",\"ip\":\"");
  body += jsonEscape(stationIp());
  body += F("\",\"apSsid\":\"");
  body += jsonEscape(portalActive_ ? apSsid_ : String());
  body += F("\",\"savedNetworks\":");
  body += credentials_.size();
  body += F(",\"error\":\"");
  body += jsonEscape(lastError_);
  body += F("\"}");
  server_.send(200, "application/json; charset=utf-8", body);
}

void WifiPortal::handleNetworks() {
  const int count = WiFi.scanNetworks(false, true);
  String body = F("{\"networks\":[");
  String seen;
  for (int i = 0; i < count; ++i) {
    const String ssid = WiFi.SSID(i);
    if (ssid.isEmpty() || seen.indexOf(String('\n') + ssid + '\n') >= 0) continue;
    seen += String('\n') + ssid + '\n';
    if (body.charAt(body.length() - 1) != '[') body += ',';
    body += F("{\"ssid\":\"");
    body += jsonEscape(ssid);
    body += F("\",\"rssi\":");
    body += WiFi.RSSI(i);
    body += F(",\"open\":");
    body += WiFi.encryptionType(i) == WIFI_AUTH_OPEN ? F("true") : F("false");
    body += '}';
  }
  WiFi.scanDelete();
  body += F("]}");
  server_.send(200, "application/json; charset=utf-8", body);
}

void WifiPortal::handleConnect() {
  const String ssid = server_.arg("ssid");
  const String password = server_.arg("password");
  if (ssid.isEmpty() || ssid.length() > 32 || password.length() > 63) {
    server_.send(400, "text/plain; charset=utf-8", "Wi-Fi 名称或密码长度无效。");
    return;
  }
  server_.send(202, "application/json", "{\"accepted\":true}");
  startConnection(ssid, password, true);
}

void WifiPortal::handleResetWifi() {
  if (!requireAuthorized()) return;
  server_.send(202, "application/json", "{\"restarting\":true}");
  clearCredentialsOnRestart_ = true;
  scheduleRestart();
}

void WifiPortal::handleLogin() {
  if (server_.arg("password") != kAdminPassword) {
    server_.send(401, "text/plain; charset=utf-8", "密码错误。");
    return;
  }
  server_.send(200, "application/json", "{\"ok\":true}");
}

void WifiPortal::handleApp() {
  if (!requireAuthorized()) return;
  if (!appState_) {
    server_.send(503, "application/json", "{\"error\":\"state_unavailable\"}");
    return;
  }
  String body;
  body.reserve(1800);
  body += F("{\"mode\":\"");
  body += AppState::modeName(appState_->mode());
  body += F("\",\"badge\":{\"name\":\"");
  body += jsonEscape(appState_->badge().name);
  body += F("\",\"role\":\"");
  body += jsonEscape(appState_->badge().role);
  body += F("\",\"contact\":\"");
  body += jsonEscape(appState_->badge().contact);
  body += F("\",\"company\":\"");
  body += jsonEscape(appState_->badge().company);
  body += F("\",\"number\":\"");
  body += jsonEscape(appState_->badge().number);
  body += F("\",\"layout\":");
  body += appState_->badge().layout;
  body += F(",\"hasPhoto\":");
  body += appState_->hasPhoto() ? F("true") : F("false");
  body += F("},\"todoRevision\":");
  body += appState_->todoRevision();
  body += F(",\"settings\":{\"wifiPowerSave\":");
  body += appState_->settings().wifiPowerSave ? F("true") : F("false");
  body += F(",\"webManagementEnabled\":");
  body += appState_->settings().webManagementEnabled ? F("true") : F("false");
  body += F(",\"autoLockMinutes\":");
  body += appState_->settings().autoLockMinutes;
  body += F("}");
  body += F(",\"weather\":{");
  if (weatherService_) {
    const auto& weather = weatherService_->snapshot();
    body += F("\"city\":\"");
    body += jsonEscape(weather.city);
    body += F("\",\"resolvedCity\":\"");
    body += jsonEscape(weather.resolvedCity);
    body += F("\",\"valid\":");
    body += weather.valid ? F("true") : F("false");
    body += F(",\"locationSource\":\"");
    body += jsonEscape(weatherService_->locationSource());
    body += F("\",\"latitude\":");
    body += String(weatherService_->latitude(), 6);
    body += F(",\"longitude\":");
    body += String(weatherService_->longitude(), 6);
    body += F(",\"error\":\"");
    body += jsonEscape(weather.error);
    body += F("\"");
  } else {
    body += F("\"city\":\"\",\"resolvedCity\":\"\",\"valid\":false,\"error\":\"\"");
  }
  body += F("}}");
  server_.send(200, "application/json; charset=utf-8", body);
}

void WifiPortal::handleMode() {
  if (!requireAuthorized()) return;
  if (!appState_ || !appState_->setMode(server_.arg("mode"))) {
    server_.send(400, "text/plain; charset=utf-8", "模式无效。");
    return;
  }
  contentChanged_ = true;
  server_.send(200, "application/json", "{\"ok\":true}");
}

void WifiPortal::handleBadge() {
  if (!requireAuthorized()) return;
  if (!appState_ || !appState_->updateBadge(server_.arg("name"), server_.arg("role"),
                                             server_.arg("contact"), server_.arg("company"),
                                             server_.arg("number"), server_.arg("layout").toInt())) {
    server_.send(400, "text/plain; charset=utf-8", "工牌字段不完整或过长。");
    return;
  }
  contentChanged_ = true;
  server_.send(200, "application/json", "{\"ok\":true}");
}

void WifiPortal::handleWeather() {
  if (!requireAuthorized()) return;
  if (!weatherService_ || !weatherService_->setCity(server_.arg("city"))) {
    server_.send(400, "text/plain; charset=utf-8", "城市名称过长。");
    return;
  }
  contentChanged_ = true;
  server_.send(200, "application/json", "{\"ok\":true}");
}

void WifiPortal::handleWeatherLocation() {
  if (!requireAuthorized()) return;
  if (!weatherService_ || !server_.hasArg("latitude") || !server_.hasArg("longitude")) {
    server_.send(400, "text/plain; charset=utf-8", "缺少定位坐标。");
    return;
  }
  const String latitudeText = server_.arg("latitude");
  const String longitudeText = server_.arg("longitude");
  char* latitudeEnd = nullptr;
  char* longitudeEnd = nullptr;
  const float latitude = strtof(latitudeText.c_str(), &latitudeEnd);
  const float longitude = strtof(longitudeText.c_str(), &longitudeEnd);
  if (latitudeEnd == latitudeText.c_str() || *latitudeEnd != '\0' ||
      longitudeEnd == longitudeText.c_str() || *longitudeEnd != '\0' ||
      !weatherService_->setCoordinates(latitude, longitude, server_.arg("label"), "phone")) {
    server_.send(400, "text/plain; charset=utf-8", "定位坐标无效。");
    return;
  }
  contentChanged_ = true;
  String body = F("{\"ok\":true,\"city\":\"");
  body += jsonEscape(weatherService_->snapshot().resolvedCity);
  body += F("\"}");
  server_.send(200, "application/json; charset=utf-8", body);
}

void WifiPortal::handleWeatherAutoLocation() {
  if (!requireAuthorized()) return;
  if (!weatherService_ || !weatherService_->autoLocateFromIp()) {
    const String error = weatherService_ ? weatherService_->snapshot().error
                                         : String(F("天气服务不可用"));
    server_.send(502, "text/plain; charset=utf-8", error);
    return;
  }
  contentChanged_ = true;
  String body = F("{\"ok\":true,\"city\":\"");
  body += jsonEscape(weatherService_->snapshot().resolvedCity);
  body += F("\"}");
  server_.send(200, "application/json; charset=utf-8", body);
}

void WifiPortal::handleDeviceLock() {
  if (!requireAuthorized()) return;
  lockRequested_ = true;
  server_.send(200, "application/json", "{\"ok\":true}");
}

void WifiPortal::handleDeviceRestart() {
  if (!requireAuthorized()) return;
  server_.send(202, "application/json", "{\"restarting\":true}");
  scheduleRestart();
}

void WifiPortal::handleDevicePowerOff() {
  if (!requireAuthorized()) return;
  powerOffRequested_ = true;
  server_.send(202, "application/json", "{\"poweringOff\":true}");
}

void WifiPortal::handleTodos() {
  if (!requireAuthorized()) return;
  if (!appState_) {
    server_.send(503, "application/json", "{\"error\":\"state_unavailable\"}");
    return;
  }
  const size_t offset = strtoul(server_.arg("offset").c_str(), nullptr, 10);
  constexpr size_t kPageSize = 20;
  size_t returned = 0;
  bool hasMore = false;
  String body = F("{\"todos\":");
  body += appState_->todosJsonPage(offset, kPageSize, returned, hasMore);
  body += F(",\"total\":");
  body += appState_->todoCount();
  body += F(",\"nextOffset\":");
  if (hasMore) body += offset + returned;
  else body += F("null");
  body += '}';
  server_.send(200, "application/json; charset=utf-8", body);
}

void WifiPortal::handleTodoAdd() {
  if (!requireAuthorized()) return;
  if (!appState_ || !appState_->addTodo(server_.arg("text"))) {
    server_.send(400, "text/plain; charset=utf-8", "待办为空、内容过长，或设备存储失败。");
    return;
  }
  contentChanged_ = true;
  server_.send(200, "application/json", "{\"ok\":true}");
}

void WifiPortal::handleTodoDelete() {
  if (!requireAuthorized()) return;
  const uint32_t id = strtoul(server_.arg("id").c_str(), nullptr, 10);
  if (!appState_ || !appState_->deleteTodo(id)) {
    server_.send(404, "text/plain; charset=utf-8", "未找到待办。");
    return;
  }
  contentChanged_ = true;
  server_.send(200, "application/json", "{\"ok\":true}");
}

void WifiPortal::handleTodoMove() {
  if (!requireAuthorized()) return;
  const uint32_t id = strtoul(server_.arg("id").c_str(), nullptr, 10);
  const int direction = server_.arg("direction").toInt();
  if (!appState_ || direction == 0 || !appState_->moveTodo(id, direction)) {
    server_.send(400, "text/plain; charset=utf-8", "无法移动该待办。");
    return;
  }
  contentChanged_ = true;
  server_.send(200, "application/json", "{\"ok\":true}");
}

void WifiPortal::handleTodoToggle() {
  if (!requireAuthorized()) return;
  const uint32_t id = strtoul(server_.arg("id").c_str(), nullptr, 10);
  if (!appState_) {
    server_.send(503, "text/plain", "state unavailable");
    return;
  }
  const int index = appState_->findTodo(id);
  if (index < 0 || !appState_->setTodoCompleted(id, !appState_->todo(index).completed)) {
    server_.send(404, "text/plain; charset=utf-8", "未找到待办。");
    return;
  }
  contentChanged_ = true;
  server_.send(200, "application/json", "{\"ok\":true}");
}

void WifiPortal::handleTodoTemplates() {
  if (!requireAuthorized()) return;
  if (!appState_) {
    server_.send(503, "application/json", "{\"error\":\"state_unavailable\"}");
    return;
  }
  size_t offset = strtoul(server_.arg("offset").c_str(), nullptr, 10);
  constexpr size_t kPageSize = 5;
  size_t returned = 0;
  bool hasMore = false;
  String body = F("{\"templates\":");
  body += appState_->todoTemplatesJsonPage(offset, kPageSize, returned, hasMore);
  body += F(",\"nextOffset\":");
  if (hasMore) body += offset + returned;
  else body += F("null");
  body += '}';
  server_.send(200, "application/json; charset=utf-8", body);
}

void WifiPortal::handleTodoTemplateSave() {
  if (!requireAuthorized()) return;
  const uint32_t id = strtoul(server_.arg("id").c_str(), nullptr, 10);
  if (!appState_ || !appState_->saveTodoTemplate(id, server_.arg("name"),
                                                  server_.arg("items"))) {
    server_.send(400, "text/plain; charset=utf-8",
                 "模板名称或内容无效；请确保至少有一条待办，且每条内容不要过长。");
    return;
  }
  server_.send(200, "application/json", "{\"ok\":true}");
}

void WifiPortal::handleTodoTemplateDelete() {
  if (!requireAuthorized()) return;
  const uint32_t id = strtoul(server_.arg("id").c_str(), nullptr, 10);
  if (!appState_ || !appState_->deleteTodoTemplate(id)) {
    server_.send(404, "text/plain; charset=utf-8", "未找到模板。");
    return;
  }
  server_.send(200, "application/json", "{\"ok\":true}");
}

void WifiPortal::handleTodoTemplateImport() {
  if (!requireAuthorized()) return;
  if (!appState_) {
    server_.send(503, "text/plain; charset=utf-8", "状态不可用。");
    return;
  }
  const int imported = appState_->importTodoTemplates(server_.arg("ids"));
  if (imported < 0) {
    server_.send(400, "text/plain; charset=utf-8", "请选择有效的模板。");
    return;
  }
  contentChanged_ = true;
  String body = F("{\"ok\":true,\"imported\":");
  body += imported;
  body += '}';
  server_.send(200, "application/json", body);
}

void WifiPortal::handlePhotoChunk() {
  HTTPUpload& upload = server_.upload();
  switch (upload.status) {
    case UPLOAD_FILE_START:
      photoUploadBytes_ = 0;
      photoUploadError_ = "";
      // WebServer exposes collected custom headers reliably only when the
      // request-complete handler runs. Write to a temporary file here, then
      // authenticate and commit it in handlePhotoComplete().
      photoUploadOk_ = appState_ && appState_->beginPhotoWrite();
      if (!photoUploadOk_) photoUploadError_ = F("无法打开设备临时文件");
      Serial.printf("[photo] start: field=%s file=%s\n", upload.name.c_str(),
                    upload.filename.c_str());
      break;
    case UPLOAD_FILE_WRITE:
      if (photoUploadOk_) {
        const size_t offset = upload.totalSize;
        if (offset == photoUploadBytes_) {
          photoUploadOk_ = appState_->appendPhoto(upload.buf, upload.currentSize);
          if (photoUploadOk_) {
            photoUploadBytes_ += upload.currentSize;
          } else {
            photoUploadError_ = F("写入设备存储失败");
          }
        } else if (offset + upload.currentSize > photoUploadBytes_) {
          // Reject gaps or overlapping, non-identical chunks. Some versions of
          // ESP32 WebServer invoke the upload callback twice for a chunk; an
          // exact repeat is harmless and is intentionally ignored here.
          photoUploadOk_ = false;
          photoUploadError_ = F("上传分块偏移异常：解析器偏移 ");
          photoUploadError_ += offset;
          photoUploadError_ += F("，设备偏移 ");
          photoUploadError_ += photoUploadBytes_;
        }
      }
      break;
    case UPLOAD_FILE_END:
      if (!photoUploadOk_ || photoUploadBytes_ != AppState::kPhotoBytes) {
        if (photoUploadError_.isEmpty()) photoUploadError_ = F("上传数据长度不完整");
        if (appState_) appState_->abortPhotoWrite();
        photoUploadOk_ = false;
      }
      Serial.printf("[photo] end: accepted=%u, parser=%u, ok=%d\n",
                    static_cast<unsigned>(photoUploadBytes_),
                    static_cast<unsigned>(upload.totalSize), photoUploadOk_);
      break;
    case UPLOAD_FILE_ABORTED:
      if (appState_) appState_->abortPhotoWrite();
      photoUploadOk_ = false;
      break;
  }
}

void WifiPortal::handlePhotoComplete() {
  if (!isAuthorized()) {
    if (appState_) appState_->abortPhotoWrite();
    photoUploadOk_ = false;
    requireAuthorized();
    return;
  }
  if (photoUploadOk_) {
    photoUploadOk_ = appState_ && appState_->finishPhotoWrite();
    if (!photoUploadOk_) photoUploadError_ = F("提交照片文件失败");
  }
  if (!photoUploadOk_) {
    if (appState_) appState_->abortPhotoWrite();
    String error = F("照片上传失败（");
    error += photoUploadError_.isEmpty() ? F("未知原因") : photoUploadError_;
    error += F("）：设备收到 ");
    error += photoUploadBytes_;
    error += F(" 字节，应为 240×240 = ");
    error += AppState::kPhotoBytes;
    error += F(" 字节。");
    server_.send(400, "text/plain; charset=utf-8", error);
    return;
  }
  contentChanged_ = true;
  server_.send(200, "application/json", "{\"ok\":true}");
}

bool WifiPortal::isAuthorized() const {
  return server_.header("X-PaperDesk-Key") == kAdminPassword;
}

bool WifiPortal::requireAuthorized() {
  if (isAuthorized()) return true;
  server_.send(401, "application/json", "{\"error\":\"unauthorized\"}");
  return false;
}

void WifiPortal::handleNotFound() {
  if (portalActive_) {
    server_.sendHeader("Location", String("http://") + kPortalIp.toString() + '/', true);
    server_.send(302, "text/plain", "");
    return;
  }
  server_.send(404, "application/json", "{\"error\":\"not_found\"}");
}

String WifiPortal::makeApSsid() const {
  uint8_t mac[6] = {};
  esp_efuse_mac_get_default(mac);
  char value[16];
  snprintf(value, sizeof(value), "eink-%02X%02X", mac[4], mac[5]);
  return String(value);
}

String WifiPortal::stateName() const {
  switch (state_) {
    case State::kIdle: return "idle";
    case State::kConnecting: return "connecting";
    case State::kProvisioning: return "provisioning";
    case State::kConnected: return "connected";
    case State::kConnectionFailed: return "connection_failed";
  }
  return "unknown";
}

String WifiPortal::jsonEscape(const String& value) {
  String escaped;
  escaped.reserve(value.length() + 8);
  for (size_t i = 0; i < value.length(); ++i) {
    const char ch = value.charAt(i);
    switch (ch) {
      case '\\': escaped += F("\\\\"); break;
      case '"': escaped += F("\\\""); break;
      case '\n': escaped += F("\\n"); break;
      case '\r': escaped += F("\\r"); break;
      case '\t': escaped += F("\\t"); break;
      default:
        if (static_cast<uint8_t>(ch) >= 0x20) escaped += ch;
        break;
    }
  }
  return escaped;
}

}  // namespace paperdesk
