#pragma once

#include <Arduino.h>

namespace paperdesk {

inline constexpr char kProductName[] = "PaperDesk";
inline constexpr char kHostname[] = "paperdesk";
inline constexpr char kPreferencesNamespace[] = "paperdesk";
inline constexpr char kAdminPassword[] = "159357hui";
inline constexpr char kTimeZone[] = "CST-8";
inline constexpr char kNtpServer1[] = "ntp.aliyun.com";
inline constexpr char kNtpServer2[] = "ntp1.aliyun.com";
inline constexpr char kNtpServer3[] = "pool.ntp.org";

inline constexpr uint32_t kWifiConnectTimeoutMs = 20000;
inline constexpr uint32_t kPortalShutdownDelayMs = 8000;
inline constexpr uint32_t kClockRefreshMs = 60000;
inline constexpr uint32_t kWifiResetHoldMs = 5000;
inline constexpr uint16_t kQualityRefreshMinutes = 30;

}  // namespace paperdesk
