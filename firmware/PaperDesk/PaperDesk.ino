#include <M5Unified.h>
#include <WiFi.h>
#include <esp_system.h>
#include <esp_sntp.h>
#include <time.h>

#include "AppConfig.h"
#include "AppState.h"
#include "ClockView.h"
#include "WeatherService.h"
#include "WifiPortal.h"

namespace {

paperdesk::WifiPortal wifiPortal;
paperdesk::ClockView clockView;
paperdesk::AppState appState;
paperdesk::WeatherService weatherService;

paperdesk::WifiPortal::State lastWifiState = paperdesk::WifiPortal::State::kIdle;
uint32_t lastClockRefreshMs = 0;
uint32_t resetTouchStartedMs = 0;
uint8_t lastResetProgress = 0;
bool clockDrawn = false;
bool ntpConfigured = false;
bool ntpSynced = false;
bool viewDirty = true;
uint32_t pendingTodoId = 0;
size_t literacyIndex = 0;
size_t todoPage = 0;
bool todoHideCompleted = false;
bool todoClearConfirmation = false;
constexpr size_t kTodosPerPage = 10;

struct LockQuote {
  const char* line1;
  const char* line2;
  const char* author;
};

constexpr LockQuote kLockQuotes[] = {
    {"千里之行", "始于足下", "——《道德经》"},
    {"天行健", "君子以自强不息", "——《周易》"},
    {"锲而不舍", "金石可镂", "——《荀子》"},
    {"路漫漫其修远兮", "吾将上下而求索", "—— 屈原"},
    {"知不足而奋进", "望远山而前行", "—— 与君共勉"},
    {"心有所向", "日复一日必有精进", "—— 与君共勉"},
    {"不积跬步", "无以至千里", "——《荀子》"},
    {"博观而约取", "厚积而薄发", "—— 苏轼"},
    {"长风破浪会有时", "直挂云帆济沧海", "—— 李白"},
    {"纸上得来终觉浅", "绝知此事要躬行", "—— 陆游"},
};

enum class ScreenOrientation : uint8_t { kLandscape, kPortrait };
enum class PortraitPage : uint8_t { kMenu, kClock, kLiteracy, kBadge, kTodo, kSettings };
enum class SettingsConfirmation : uint8_t { kNone, kRestart, kPowerOff };

ScreenOrientation screenOrientation = ScreenOrientation::kLandscape;
ScreenOrientation orientationCandidate = ScreenOrientation::kLandscape;
PortraitPage portraitPage = PortraitPage::kMenu;
paperdesk::AppState::Mode lastPortraitMode = paperdesk::AppState::Mode::kBadge;
uint8_t orientationCandidateCount = 0;
uint32_t lastOrientationSampleMs = 0;
uint32_t gyroQuietStartedMs = 0;
uint32_t lastOrientationSwitchMs = 0;
float accumulatedRotationDegrees = 0.0f;
bool gyroRotationArmed = true;
bool firstOrientationLogged = false;
bool locked = true;
size_t lockQuoteIndex = 0;
uint32_t lastActivityMs = 0;
PortraitPage pageBeforeLock = PortraitPage::kMenu;
ScreenOrientation orientationBeforeLock = ScreenOrientation::kPortrait;
SettingsConfirmation settingsConfirmation = SettingsConfirmation::kNone;

void invalidateView();

void drawLockScreen() {
  const auto& quote = kLockQuotes[lockQuoteIndex % (sizeof(kLockQuotes) / sizeof(kLockQuotes[0]))];
  clockView.setPortrait(true);
  clockView.showLockScreen(quote.line1, quote.line2, quote.author);
  viewDirty = false;
  clockDrawn = false;
}

void enterLockScreen() {
  if (!locked) {
    pageBeforeLock = portraitPage;
    orientationBeforeLock = screenOrientation;
  }
  locked = true;
  pendingTodoId = 0;
  lockQuoteIndex = esp_random() % (sizeof(kLockQuotes) / sizeof(kLockQuotes[0]));
  screenOrientation = ScreenOrientation::kPortrait;
  drawLockScreen();
}

void unlockScreen() {
  locked = false;
  portraitPage = pageBeforeLock;
  screenOrientation = orientationBeforeLock;
  clockView.setPortrait(screenOrientation == ScreenOrientation::kPortrait);
  lastActivityMs = millis();
  invalidateView();
}

bool rtcHasUsableTime() {
  if (!M5.Rtc.isEnabled()) return false;
  const auto dateTime = M5.Rtc.getDateTime();
  return dateTime.date.year >= 2024 && dateTime.date.year <= 2099;
}

void configureTime() {
  setenv("TZ", paperdesk::kTimeZone, 1);
  tzset();
  if (rtcHasUsableTime()) {
    M5.Rtc.setSystemTimeFromRtc();
  }
}

void updateNtp() {
  if (!wifiPortal.connected()) return;
  if (!ntpConfigured) {
    configTzTime(paperdesk::kTimeZone, paperdesk::kNtpServer1,
                 paperdesk::kNtpServer2, paperdesk::kNtpServer3);
    ntpConfigured = true;
  }

  if (!ntpSynced && sntp_get_sync_status() == SNTP_SYNC_STATUS_COMPLETED) {
    const time_t now = time(nullptr);
    if (now > 1704067200) {
      M5.Rtc.setDateTime(gmtime(&now));
      ntpSynced = true;
      clockDrawn = false;
    }
  }
}

void renderForWifiState(bool force = false) {
  const auto state = wifiPortal.state();
  if (!force && state == lastWifiState) return;
  lastWifiState = state;
  // Networking is optional. A connection change updates status-bearing pages,
  // but never replaces the current local screen or blocks touch interaction.
  if (!locked && (portraitPage == PortraitPage::kClock ||
                  portraitPage == PortraitPage::kSettings)) {
    invalidateView();
  }
}

void updateLockTouch() {
  const auto touch = M5.Touch.getDetail();
  if (touch.isPressed()) lastActivityMs = millis();
  if (!touch.wasFlicked()) return;
  const int distanceX = touch.distanceX();
  const int startX = touch.x - distanceX;
  const int startY = touch.y - touch.distanceY();
  if (distanceX > 260 && abs(distanceX) > abs(touch.distanceY()) &&
      startX >= 35 && startX <= 155 && startY >= 735 && startY <= 890) {
    unlockScreen();
  }
}

void updateAutoLock() {
  if (locked) return;
  const uint16_t minutes = appState.settings().autoLockMinutes;
  if (minutes == 0) return;
  const uint32_t timeoutMs = static_cast<uint32_t>(minutes) * 60000UL;
  if (millis() - lastActivityMs >= timeoutMs) enterLockScreen();
}

void updateClock() {
  if (portraitPage != PortraitPage::kClock) return;
  const uint32_t nowMs = millis();
  if (clockDrawn && nowMs - lastClockRefreshMs < paperdesk::kClockRefreshMs) return;

  const time_t now = time(nullptr);
  const String networkName = wifiPortal.connected() ? wifiPortal.stationSsid() : String("离线");
  clockView.showClock(now, networkName, wifiPortal.stationIp(), ntpSynced,
                      M5.Power.getBatteryLevel(), weatherService.snapshot(), !clockDrawn);
  lastClockRefreshMs = nowMs - (nowMs % paperdesk::kClockRefreshMs);
  clockDrawn = true;
  viewDirty = false;
}

void invalidateView() {
  viewDirty = true;
  clockDrawn = false;
}

void applyOrientation(ScreenOrientation detected, const char* source) {
  if (detected == screenOrientation) return;
  screenOrientation = detected;
  lastOrientationSwitchMs = millis();
  // Discard the remainder of the movement that produced this transition.
  // A new gesture is accepted only after the device has become still again.
  gyroRotationArmed = false;
  gyroQuietStartedMs = 0;
  accumulatedRotationDegrees = 0.0f;
  orientationCandidate = detected;
  orientationCandidateCount = 6;
  pendingTodoId = 0;
  clockView.setPortrait(detected == ScreenOrientation::kPortrait);
  invalidateView();
  Serial.printf("[orientation] switched to %s via %s\n",
                detected == ScreenOrientation::kPortrait ? "portrait" : "landscape", source);
}

void updateOrientation() {
  if (!M5.Imu.isEnabled()) return;
  const uint32_t nowMs = millis();
  const uint32_t elapsedMs = nowMs - lastOrientationSampleMs;
  if (elapsedMs < 100) return;
  lastOrientationSampleMs = nowMs;
  if (!M5.Imu.update()) return;

  const auto imu = M5.Imu.getImuData();
  const float absX = fabsf(imu.accel.x);
  const float absY = fabsf(imu.accel.y);
  if (!firstOrientationLogged) {
    firstOrientationLogged = true;
    Serial.printf("[orientation] accel x=%.2f y=%.2f z=%.2f, gyro z=%.2f\n",
                  imu.accel.x, imu.accel.y, imu.accel.z, imu.gyro.z);
  }

  // Let the physical movement and the relatively slow e-paper refresh settle
  // before accepting another orientation transition.
  if (lastOrientationSwitchMs != 0 && nowMs - lastOrientationSwitchMs < 1200) return;

  // When PaperS3 is flat, gravity is on Z and cannot reveal its in-plane
  // orientation. Integrate a deliberate quarter-turn around Z instead.
  if (fabsf(imu.gyro.z) < 5.0f) {
    if (gyroQuietStartedMs == 0) gyroQuietStartedMs = nowMs;
    if (!gyroRotationArmed && nowMs - gyroQuietStartedMs >= 350) {
      gyroRotationArmed = true;
      accumulatedRotationDegrees = 0.0f;
    }
  } else {
    gyroQuietStartedMs = 0;
    if (gyroRotationArmed) {
      accumulatedRotationDegrees += imu.gyro.z * (elapsedMs / 1000.0f);
      if (fabsf(accumulatedRotationDegrees) >= 48.0f) {
        const ScreenOrientation toggled = screenOrientation == ScreenOrientation::kLandscape
                                              ? ScreenOrientation::kPortrait
                                              : ScreenOrientation::kLandscape;
        gyroRotationArmed = false;
        accumulatedRotationDegrees = 0.0f;
        applyOrientation(toggled, "gyro");
        return;
      }
    }
  }

  // Gravity-based orientation is reliable only when the device is standing.
  // While it is flat (Z close to 1 g), the gyro path above owns rotation.
  if (fabsf(imu.accel.z) > 0.72f || max(absX, absY) < 0.55f ||
      fabsf(absX - absY) < 0.18f) return;

  // PaperS3's BMI270 is mounted with its axes rotated relative to the native
  // display coordinates: Y-dominant gravity is physical landscape.
  const ScreenOrientation detected = absY > absX
                                         ? ScreenOrientation::kLandscape
                                         : ScreenOrientation::kPortrait;
  if (detected != orientationCandidate) {
    orientationCandidate = detected;
    orientationCandidateCount = 1;
    return;
  }
  if (orientationCandidateCount < 6) ++orientationCandidateCount;
  if (orientationCandidateCount < 6 || detected == screenOrientation) return;

  applyOrientation(detected, "gravity");
}

void updateApplicationView() {
  if (wifiPortal.consumeContentChanged()) {
    pendingTodoId = 0;
    todoClearConfirmation = false;
    lastPortraitMode = appState.mode();
    invalidateView();
  }
  if (!viewDirty) return;
  if (portraitPage == PortraitPage::kClock) {
    updateClock();
    return;
  }
  switch (portraitPage) {
    case PortraitPage::kMenu:
      clockView.showPortraitMenu(appState.mode());
      break;
    case PortraitPage::kClock:
      break;
    case PortraitPage::kLiteracy:
      clockView.showLiteracy(literacyIndex);
      break;
    case PortraitPage::kBadge:
      clockView.showBadge(appState);
      break;
    case PortraitPage::kTodo:
      {
        if (todoClearConfirmation) {
          const String detail = String(appState.todoCount()) + " 条待办将被永久删除";
          clockView.showDeviceConfirmation("清空全部待办？", detail.c_str());
        } else {
          const size_t visibleCount = appState.visibleTodoCount(todoHideCompleted);
          const size_t pageCount = max<size_t>(1, (visibleCount + kTodosPerPage - 1) /
                                                     kTodosPerPage);
          todoPage = min(todoPage, pageCount - 1);
          clockView.showTodo(appState, todoPage, todoHideCompleted);
        }
      }
      break;
    case PortraitPage::kSettings:
      if (settingsConfirmation == SettingsConfirmation::kRestart) {
        clockView.showDeviceConfirmation("确认重启？", "设备会自动重新启动");
      } else if (settingsConfirmation == SettingsConfirmation::kPowerOff) {
        clockView.showDeviceConfirmation("确认关机？", "再次开机请按机身侧键");
      } else {
        clockView.showDeviceSettings(appState.settings(), wifiPortal.stationIp(),
                                     wifiPortal.stationSsid(), wifiPortal.connected(),
                                     wifiPortal.provisioning(), wifiPortal.apSsid(),
                                     wifiPortal.savedNetworkCount());
      }
      break;
  }
  viewDirty = false;
}

void updateTouch() {
  const auto touch = M5.Touch.getDetail();
  if (touch.isPressed() || touch.wasClicked() || touch.wasFlicked()) lastActivityMs = millis();
  const bool inResetCorner = touch.isPressed() && touch.x < 150 && touch.y < 110;
  if (!inResetCorner) {
    if (resetTouchStartedMs != 0 && lastResetProgress != 0) invalidateView();
    resetTouchStartedMs = 0;
    lastResetProgress = 0;
  } else {
    if (resetTouchStartedMs == 0) resetTouchStartedMs = millis();
    const uint32_t heldMs = millis() - resetTouchStartedMs;
    const uint8_t progress = min<uint32_t>(100, heldMs * 100 / paperdesk::kWifiResetHoldMs);
    if (progress >= lastResetProgress + 10) {
      lastResetProgress = progress;
      clockView.showWifiResetProgress(progress);
    }
    if (heldMs >= paperdesk::kWifiResetHoldMs) {
      wifiPortal.clearCredentials();
      delay(200);
      ESP.restart();
    }
    return;
  }

  if (portraitPage != PortraitPage::kMenu && touch.wasFlicked() &&
      touch.distanceX() > 80 && abs(touch.distanceX()) > abs(touch.distanceY())) {
    pendingTodoId = 0;
    todoClearConfirmation = false;
    settingsConfirmation = SettingsConfirmation::kNone;
    portraitPage = PortraitPage::kMenu;
    screenOrientation = ScreenOrientation::kPortrait;
    clockView.setPortrait(true);
    invalidateView();
    return;
  }

  if (portraitPage == PortraitPage::kTodo && pendingTodoId == 0 &&
      !todoClearConfirmation && touch.wasFlicked() &&
      abs(touch.distanceY()) > 80 && abs(touch.distanceY()) > abs(touch.distanceX())) {
    const size_t visibleCount = appState.visibleTodoCount(todoHideCompleted);
    const size_t pageCount = max<size_t>(1, (visibleCount + kTodosPerPage - 1) /
                                               kTodosPerPage);
    if (touch.distanceY() < 0 && todoPage + 1 < pageCount) ++todoPage;
    else if (touch.distanceY() > 0 && todoPage > 0) --todoPage;
    invalidateView();
    return;
  }

  if (!touch.wasClicked()) return;

  if (portraitPage == PortraitPage::kMenu) {
    if (touch.x >= 420 && touch.y <= 120) {
      enterLockScreen();
    } else if (touch.x >= 55 && touch.x <= 485 && touch.y >= 140 && touch.y <= 255) {
      portraitPage = PortraitPage::kClock;
      screenOrientation = ScreenOrientation::kLandscape;
      clockView.setPortrait(false);
      invalidateView();
    } else if (touch.x >= 55 && touch.x <= 485 && touch.y >= 285 && touch.y <= 400) {
      portraitPage = PortraitPage::kLiteracy;
      screenOrientation = ScreenOrientation::kLandscape;
      clockView.setPortrait(false);
      invalidateView();
    } else if (touch.x >= 55 && touch.x <= 485 && touch.y >= 430 && touch.y <= 545) {
      appState.setMode("badge");
      lastPortraitMode = paperdesk::AppState::Mode::kBadge;
      portraitPage = PortraitPage::kBadge;
      invalidateView();
    } else if (touch.x >= 55 && touch.x <= 485 && touch.y >= 575 && touch.y <= 690) {
      appState.setMode("todo");
      lastPortraitMode = paperdesk::AppState::Mode::kTodo;
      portraitPage = PortraitPage::kTodo;
      todoPage = 0;
      todoClearConfirmation = false;
      invalidateView();
    } else if (touch.x >= 55 && touch.x <= 485 && touch.y >= 720 && touch.y <= 835) {
      portraitPage = PortraitPage::kSettings;
      settingsConfirmation = SettingsConfirmation::kNone;
      invalidateView();
    }
    return;
  }

  if (portraitPage == PortraitPage::kSettings) {
    if (settingsConfirmation != SettingsConfirmation::kNone) {
      if (touch.y >= 610 && touch.y <= 715 && touch.x >= 55 && touch.x <= 245) {
        settingsConfirmation = SettingsConfirmation::kNone;
        invalidateView();
      } else if (touch.y >= 610 && touch.y <= 715 && touch.x >= 295 && touch.x <= 485) {
        if (settingsConfirmation == SettingsConfirmation::kRestart) {
          delay(200);
          ESP.restart();
        } else {
          M5.Display.waitDisplay();
          delay(250);
          M5.Power.powerOff();
        }
      }
      return;
    }

    const auto& settings = appState.settings();
    if (touch.y >= 115 && touch.y <= 205) {
      appState.updateSettings(settings.wifiPowerSave == 0,
                              settings.webManagementEnabled != 0,
                              settings.autoLockMinutes);
      invalidateView();
    } else if (touch.y >= 220 && touch.y <= 310) {
      appState.updateSettings(settings.wifiPowerSave != 0,
                              settings.webManagementEnabled == 0,
                              settings.autoLockMinutes);
      invalidateView();
    } else if (touch.y >= 325 && touch.y <= 415) {
      constexpr uint16_t intervals[] = {0, 5, 15, 30, 60, 120};
      size_t next = 0;
      for (size_t i = 0; i < sizeof(intervals) / sizeof(intervals[0]); ++i) {
        if (intervals[i] == settings.autoLockMinutes) {
          next = (i + 1) % (sizeof(intervals) / sizeof(intervals[0]));
          break;
        }
      }
      appState.updateSettings(settings.wifiPowerSave != 0,
                              settings.webManagementEnabled != 0, intervals[next]);
      invalidateView();
    } else if (touch.y >= 440 && touch.y <= 545) {
      wifiPortal.toggleProvisioning();
      invalidateView();
    } else if (touch.y >= 575 && touch.y <= 663) {
      enterLockScreen();
    } else if (touch.y >= 695 && touch.y <= 783 && touch.x >= 45 && touch.x <= 255) {
      settingsConfirmation = SettingsConfirmation::kRestart;
      invalidateView();
    } else if (touch.y >= 695 && touch.y <= 783 && touch.x >= 285 && touch.x <= 495) {
      settingsConfirmation = SettingsConfirmation::kPowerOff;
      invalidateView();
    }
    return;
  }

  if (portraitPage == PortraitPage::kLiteracy) {
    if (touch.x < M5.Display.width() / 2) {
      literacyIndex = literacyIndex == 0 ? paperdesk::kLiteracyEntryCount - 1 : literacyIndex - 1;
    } else {
      literacyIndex = (literacyIndex + 1) % paperdesk::kLiteracyEntryCount;
    }
    invalidateView();
    return;
  }

  if (portraitPage != PortraitPage::kTodo) return;

  if (todoClearConfirmation) {
    if (touch.y >= 610 && touch.y <= 715 && touch.x >= 55 && touch.x <= 245) {
      todoClearConfirmation = false;
      invalidateView();
    } else if (touch.y >= 610 && touch.y <= 715 && touch.x >= 295 && touch.x <= 485) {
      appState.clearTodos();
      todoClearConfirmation = false;
      pendingTodoId = 0;
      todoPage = 0;
      invalidateView();
    }
    return;
  }

  if (pendingTodoId != 0) {
    if (touch.y >= 635 && touch.y <= 727 && touch.x >= 75 && touch.x <= 245) {
      pendingTodoId = 0;
      invalidateView();
    } else if (touch.y >= 635 && touch.y <= 727 && touch.x >= 295 && touch.x <= 465) {
      const int index = appState.findTodo(pendingTodoId);
      if (index >= 0) {
        appState.setTodoCompleted(pendingTodoId, !appState.todo(index).completed);
      }
      pendingTodoId = 0;
      invalidateView();
    }
    return;
  }

  if (touch.y >= 28 && touch.y <= 100 && touch.x >= 220 && touch.x <= 390) {
    todoHideCompleted = !todoHideCompleted;
    todoPage = 0;
    invalidateView();
    return;
  }
  if (touch.y >= 28 && touch.y <= 100 && touch.x >= 405 && touch.x <= 520) {
    if (appState.todoCount() > 0) {
      todoClearConfirmation = true;
      invalidateView();
    }
    return;
  }

  if (touch.x <= 90 && touch.y >= 130 && touch.y < 850) {
    const uint8_t row = (touch.y - 130) / 72;
    const size_t position = todoPage * kTodosPerPage + row;
    const int index = appState.todoIndexForVisiblePosition(position, todoHideCompleted);
    if (index >= 0 && row < kTodosPerPage) {
      pendingTodoId = appState.todo(static_cast<size_t>(index)).id;
      clockView.showTodoConfirmation(appState.todo(static_cast<size_t>(index)));
      viewDirty = false;
    }
  }
}

}  // namespace

void setup() {
  Serial.begin(115200);
  delay(150);
  Serial.println("\n[paperdesk] booting integrated firmware");

  auto config = M5.config();
  config.internal_spk = false;
  config.internal_mic = false;
  config.led_brightness = 0;
  M5.begin(config);
  M5.Speaker.end();

  clockView.begin();
  appState.begin();
  weatherService.begin();
  lastPortraitMode = appState.mode();
  configureTime();
  wifiPortal.attachState(&appState);
  wifiPortal.attachWeather(&weatherService);
  wifiPortal.begin();
  lockQuoteIndex = esp_random() % (sizeof(kLockQuotes) / sizeof(kLockQuotes[0]));
  lastActivityMs = millis();
  lastWifiState = wifiPortal.state();
  screenOrientation = ScreenOrientation::kPortrait;
  drawLockScreen();
  Serial.printf("[paperdesk] display initialized; IMU=%s; speaker and microphone disabled\n",
                M5.Imu.isEnabled() ? "ready" : "unavailable");
}

void loop() {
  M5.update();
  wifiPortal.update();
  if (wifiPortal.consumePowerOffRequest()) {
    M5.Display.waitDisplay();
    delay(250);
    M5.Power.powerOff();
    return;
  }
  if (wifiPortal.consumeLockRequest()) enterLockScreen();
  if (!locked && M5.BtnPWR.wasClicked()) enterLockScreen();
  updateNtp();
  renderForWifiState();
  weatherService.update(wifiPortal.connected());
  if (weatherService.consumeChanged()) invalidateView();
  if (locked) {
    updateLockTouch();
    delay(20);
    return;
  }
  updateApplicationView();
  updateClock();
  updateTouch();
  updateAutoLock();
  delay(20);
}
