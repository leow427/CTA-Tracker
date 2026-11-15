#pragma once
// Every tunable value from the spec, in one place (spec 6.3, 6.4, 6.6, 8, 10). Mirrored in the app's Config.swift.
#include <stdint.h>

namespace cta {

// Polling (spec 6.6)
constexpr uint32_t kDockPollSec = 30;
constexpr uint32_t kHandheldClosePollSec = 60;   // the 3 closest train stations
constexpr uint32_t kHandheldFarPollSec = 180;    // the other train stations
constexpr uint32_t kHandheldBusPollSec = 60;     // all bus stops, one batched call
constexpr uint32_t kAlertsPollSec = 300;
constexpr uint32_t kWeatherPollSec = 900;
constexpr uint32_t kSlowPollSec = 300;           // night hours or docked
constexpr uint32_t kGeoLookupSec = 86400;        // the dock's weather location from ipapi.co: at boot, then daily (7.5)

// Moods and staleness (spec 6.3, 6.4, 8.3)
constexpr int kHypedMaxMin = 3;
constexpr uint32_t kStaleSec = 300;
constexpr uint32_t kLedStaleSec = 120;

// Night hours default 1:00–5:00 (minutes after midnight; end is exclusive)
constexpr int kNightStartMin = 60;
constexpr int kNightEndMin = 300;

// Time (spec 6.6)
constexpr const char* kTz = "CST6CDT,M3.2.0,M11.1.0";

// BLE and the handheld (spec 6.3, 8, 10)
constexpr uint16_t kBleMaxPayload = 512;
constexpr uint8_t kNearbyPerTab = 5;   // stations and bus stops each (Leo, 2026-11-09: 10 was too slow)
constexpr uint8_t kHypedClosestStops = 3;
constexpr uint32_t kScreenDimSec = 15;
// Card caps for the handheld's nearest stops per tab (handheld plan, milestone 5; proposed spec values). Downtown is
// the worst case: State & Lake alone has about 60 bus route-directions, Clark/Lake 12 train line-directions.
constexpr int kMaxBusCards = 80;
constexpr int kMaxTrainCards = 80;

// Pairing over the network (ecosystem spec 4.3)
// An unpaired board stays in pairing mode until it pairs (Leo, 2026-11-14: no 5-minute window).
constexpr uint8_t kPairMaxWrong = 5;            // wrong codes in a row, then a cooldown and a new code
constexpr uint32_t kPairCooldownMs = 30 * 1000;

// Follow a train (ecosystem spec 5.7)
constexpr uint32_t kFollowEndedSecs = 60;   // Arrived or Lost shows this long, then the card falls back to the pin
constexpr int kFollowGraceMin = 2;          // a seen vehicle counts as arrived this long after its last minutes ran out
constexpr int kFollowUnseenCapMin = 3;      // a vehicle never on the card is lost after this many minutes of fresh polls

// The wearable's Bluetooth link (wearable BLE link plan, "Relay" and "Status")
constexpr uint32_t kRelayHoldSec = 90;       // "relaying" (own CTA polling off) this long after the last /relay tick
constexpr uint32_t kRelayTickSec = 30;       // the app's /relay tick
constexpr uint32_t kInfoHeartbeatSec = 30;   // an info notify at least this often while subscribed

}  // namespace cta
