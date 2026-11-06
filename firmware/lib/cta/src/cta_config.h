#pragma once
// Every spec value, defined once (base spec 6.3, 6.4, 6.6, 7, 10). Mirrored by CTAKit Config.swift.
#include <stdint.h>

namespace cta {

// Polling (spec 6.6), seconds
constexpr uint32_t kDockPollSec = 30;
constexpr uint32_t kHandheldClosePollSec = 60;
constexpr uint32_t kHandheldFarPollSec = 180;
constexpr uint32_t kHandheldBusPollSec = 60;
constexpr uint32_t kAlertsPollSec = 300;
constexpr uint32_t kWeatherPollSec = 900;
constexpr uint32_t kSlowPollSec = 300;        // night hours or docked
constexpr uint32_t kGeoIpPollSec = 86400;     // dock weather location (spec 7.5)

// Moods and LEDs (spec 6.3, 6.4)
constexpr int kHypedMaxMin = 3;
constexpr uint32_t kStaleSec = 300;
constexpr uint32_t kLedStaleSec = 120;
constexpr int kNightStartMin = 60;   // 1:00
constexpr int kNightEndMin = 300;    // 5:00

constexpr const char* kTz = "CST6CDT,M3.2.0,M11.1.0";
constexpr uint16_t kBleMaxPayload = 512;
constexpr int kNearbyPerTab = 10;
constexpr int kHypedClosestStops = 3;
constexpr uint32_t kScreenDimSec = 15;
constexpr int kBusStopsPerCall = 10;
constexpr int kPetNameLen = 16;   // bytes, excluding NUL

// Device copy (spec 7.4)
constexpr const char* kCopyEmptyStop = "Tap to pick a stop";
constexpr const char* kCopyNoWifi = "Open the CTA app to set up Wi-Fi";
constexpr const char* kCopyNoKey = "No API key";
constexpr const char* kCopyUpdated = "Updated %d min ago";   // offline or stale arrivals (spec 7.4)

}  // namespace cta
