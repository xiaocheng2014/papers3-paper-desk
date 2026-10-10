#include "WifiPortal.h"

#include <ESPmDNS.h>
#include <esp_mac.h>

#include "AppConfig.h"
#include "AppState.h"
#include "ManagementPage.h"
#include "WeatherService.h"

namespace paperdesk {
namespace {

constexpr byte kDnsPort = 53;
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
    <button id="connect">保存并连接</button><small>凭据仅保存在当前设备中，连接成功后才会写入。</small>
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
  server_.on("/api/todos/add", HTTP_POST, [this] { handleTodoAdd(); });
  server_.on("/api/todos/delete", HTTP_POST, [this] { handleTodoDelete(); });
  server_.on("/api/todos/move", HTTP_POST, [this] { handleTodoMove(); });
  server_.on("/api/todos/toggle", HTTP_POST, [this] { handleTodoToggle(); });
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
  if (!preferences_.begin(kPreferencesNamespace, true)) return false;
  savedSsid_ = preferences_.getString("ssid", "");
  savedPassword_ = preferences_.getString("password", "");
  preferences_.end();
  return !savedSsid_.isEmpty();
}

void WifiPortal::saveCredentials(const String& ssid, const String& password) {
  if (!preferences_.begin(kPreferencesNamespace, false)) return;
  preferences_.putString("ssid", ssid);
  preferences_.putString("password", password);
  preferences_.end();
  savedSsid_ = ssid;
  savedPassword_ = password;
}

void WifiPortal::startSavedConnection() {
  portalActive_ = false;
  pendingFromPortal_ = false;
  lastError_ = "";
  state_ = State::kConnecting;
  connectionStartedMs_ = millis();
  WiFi.mode(WIFI_STA);
  Serial.printf("[wifi] connecting saved network: %s\n", savedSsid_.c_str());
  WiFi.begin(savedSsid_.c_str(), savedPassword_.c_str());
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
  WiFi.softAPConfig(kPortalIp, kPortalGateway, kPortalMask);
  apSsid_ = makeApSsid();
  WiFi.softAP(apSsid_.c_str(), kAdminPassword);
  dnsServer_.start(kDnsPort, "*", kPortalIp);
  Serial.printf("[wifi] provisioning AP ready: %s, http://%s/\n",
                apSsid_.c_str(), kPortalIp.toString().c_str());
}

void WifiPortal::finishConnection() {
  state_ = State::kConnected;
  connectedMs_ = millis();
  lastError_ = "";
  if (pendingFromPortal_) {
    saveCredentials(pendingSsid_, pendingPassword_);
  }
  pendingFromPortal_ = false;
  pendingSsid_ = "";
  pendingPassword_ = "";
  MDNS.end();
  MDNS.begin(kHostname);
  Serial.printf("[wifi] connected: %s, http://%s/\n",
                WiFi.SSID().c_str(), WiFi.localIP().toString().c_str());
}

void WifiPortal::update() {
  server_.handleClient();
  if (portalActive_) dnsServer_.processNextRequest();

  if (restartScheduled_ && static_cast<int32_t>(millis() - restartAtMs_) >= 0) {
    if (clearCredentialsOnRestart_) clearCredentials();
    ESP.restart();
  }

  if (state_ == State::kConnecting) {
    if (WiFi.status() == WL_CONNECTED) {
      finishConnection();
    } else if (millis() - connectionStartedMs_ >= kWifiConnectTimeoutMs) {
      startPortal("连接失败，请检查 Wi-Fi 名称和密码。");
    }
  } else if (state_ == State::kConnected) {
    if (portalActive_ && millis() - connectedMs_ >= kPortalShutdownDelayMs) {
      dnsServer_.stop();
      WiFi.softAPdisconnect(true);
      portalActive_ = false;
      WiFi.mode(WIFI_STA);
    }

    if (WiFi.status() != WL_CONNECTED) {
      state_ = State::kConnecting;
      connectionStartedMs_ = millis();
      pendingFromPortal_ = false;
      WiFi.reconnect();
    }
  }
}

void WifiPortal::clearCredentials() {
  if (preferences_.begin(kPreferencesNamespace, false)) {
    preferences_.clear();
    preferences_.end();
  }
  savedSsid_ = "";
  savedPassword_ = "";
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
  body += F("\",\"error\":\"");
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
  body.reserve(8000);
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
  body += F("},\"todos\":[");
  for (uint8_t i = 0; i < appState_->todoCount(); ++i) {
    if (i) body += ',';
    const auto& item = appState_->todo(i);
    body += F("{\"id\":");
    body += item.id;
    body += F(",\"text\":\"");
    body += jsonEscape(item.text);
    body += F("\",\"completed\":");
    body += item.completed ? F("true") : F("false");
    body += '}';
  }
  body += F("],\"templates\":[");
  for (uint8_t i = 0; i < appState_->todoTemplateCount(); ++i) {
    if (i) body += ',';
    const auto& item = appState_->todoTemplate(i);
    body += F("{\"id\":");
    body += item.id;
    body += F(",\"name\":\"");
    body += jsonEscape(item.name);
    body += F("\",\"items\":[");
    for (uint8_t j = 0; j < item.itemCount; ++j) {
      if (j) body += ',';
      body += '"';
      body += jsonEscape(item.items[j]);
      body += '"';
    }
    body += F("]}");
  }
  body += F("],\"weather\":{");
  if (weatherService_) {
    const auto& weather = weatherService_->snapshot();
    body += F("\"city\":\"");
    body += jsonEscape(weather.city);
    body += F("\",\"resolvedCity\":\"");
    body += jsonEscape(weather.resolvedCity);
    body += F("\",\"valid\":");
    body += weather.valid ? F("true") : F("false");
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

void WifiPortal::handleTodoAdd() {
  if (!requireAuthorized()) return;
  if (!appState_ || !appState_->addTodo(server_.arg("text"))) {
    server_.send(400, "text/plain; charset=utf-8", "待办为空、过长，或已达到 12 条上限。");
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

void WifiPortal::handleTodoTemplateSave() {
  if (!requireAuthorized()) return;
  const uint32_t id = strtoul(server_.arg("id").c_str(), nullptr, 10);
  if (!appState_ || !appState_->saveTodoTemplate(id, server_.arg("name"),
                                                  server_.arg("items"))) {
    server_.send(400, "text/plain; charset=utf-8",
                 "模板名称或内容无效。最多 6 个模板，每个模板 8 条，每条 96 个字符。");
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
  if (imported == -2) {
    server_.send(400, "text/plain; charset=utf-8", "导入后会超过 12 条 Todo，请先删除部分待办。");
    return;
  }
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
