#pragma once
// The live build's data: Wi-Fi, NTP, and the CTA, Customer Alerts, and Open-Meteo polls (spec 6.6, 8.3), on a task
// of its own so a slow HTTPS fetch never stalls a frame. The loop copies the latest snapshot with read(). What's
// nearby comes from the `nearby` message (spec 8.3), kept in flash; until the app's BLE link exists, the laptop
// sends it over USB serial (tools/send_nearby.py).
#include <stddef.h>
#include <stdint.h>
#include <string.h>

#include "cta_config.h"
#include "cta_types.h"
#include "nearby_msg.h"

namespace live {

using cta::kMaxBusCards;     // one per route and direction across the nearby bus stops
using cta::kMaxTrainCards;   // one per line and direction across the nearby stations

struct Rec {
    cta::StopRecord rec;
    uint8_t stop;   // the stop's place in the nearby list's trains or buses, 0 = nearest
};

struct Snapshot {
    Rec trains[kMaxTrainCards];
    int nTrains = 0;
    Rec buses[kMaxBusCards];
    int nBuses = 0;
    bool loaded = false;       // every stop has been polled once
    bool located = false;      // a nearby list has arrived (spec 6.3: Sleepy until then)
    char close[cta::kHypedClosestStops][cta::Nearby::kIdLen];   // the 3 closest stops: only these may hype
    int nClose = 0;
    bool wifiUp = false;
    bool homeWifi = false;     // on the first saved network (spec 8.4); the prototype has one
    char status[40] = "";      // what the network side is doing when there's no data: for Trackie's reason
    bool hasWeather = false;
    int wmo = 0;
    float tempC = 0;
    uint32_t requests = 0;     // CTA requests since boot, against the keys' daily limits
};

// Empties s in place. A Snapshot is about 16 KB: never build one as a temporary on a task's stack.
inline void clear(Snapshot& s) { memset(&s, 0, sizeof(s)); }

// Starts the network task. Without Wi-Fi in secrets.h it only reports "No Wi-Fi set up".
void start();
// Docked or night hours: everything polls every cta::kSlowPollSec (spec 6.6).
void setSlow(bool slow);
// A new `nearby` message (JSON). Checked, saved to flash, and polled from the next fetch on, dropping the old
// stops' data. False if the message is malformed (the old list stays).
bool setNearby(const char* json, size_t len);
// A wake (spec 8.2): with no Wi-Fi, try to join now instead of waiting out the backoff.
void wake();
// Copies the newest snapshot into out if it changed since `version`, and updates `version`. False if unchanged.
bool read(Snapshot& out, uint32_t& version);
// Unix time once NTP has synced, else 0.
uint32_t unixNow();

}  // namespace live
