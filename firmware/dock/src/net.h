#pragma once
// The dock's data: Wi-Fi (the network saved in the store, else DOCK_WIFI_SSID / DOCK_WIFI_PASS from firmware/secrets.h;
// once it's up, the HTTP link starts, link.h),
// the clock, and every poll of spec 6.6 for the 3 picked stops (arrivals, vehicle positions for the LED rows and the
// map, alerts, weather at the location ipapi.co gives, spec 7.5), plus the picker's live bus lists (spec 7.2). It all
// runs on a task of its own on core 0, so a slow HTTPS fetch never stalls a frame; the loop copies the newest
// snapshot with read().
#include <stdint.h>

#include "dock_ui.h"
#include "picks.h"

namespace net {

// Starts the network task with these picks. Without Wi-Fi or keys in secrets.h it stays offline, and the snapshot
// says so (Data::wifiSetUp, Data::keys).
void start(const cta::PickedStop picks[cta::kDockStops]);
// New picks (the on-screen picker): a changed section's old data is dropped, its row rebuilt, and it's polled at once.
void setPicks(const cta::PickedStop picks[cta::kDockStops]);
// Night hours: everything every cta::kSlowPollSec (spec 6.6).
void setSlow(bool slow);
// A picker list to fetch: the routes, a route's directions, or its stops one way.
void request(dock::Request what, const char* route, const char* dir);
// The touch extras (ecosystem spec 5.10): one request each, made before the next poll; the answer comes in the
// snapshot (Data::ahead, Data::peek) under the same id. A newer ask replaces one not served yet.
void ahead(uint32_t id, int section, const char* vehicle);   // a section's arrival's stops ahead
void peek(uint32_t id, int section);                          // a section's stop the other way
// Copies the newest snapshot (or picker lists) if it changed since `version`, and updates it. False if unchanged.
bool read(dock::Data& out, uint32_t& version);
bool readLists(dock::PickerLists& out, uint32_t& version);
// Scans once and logs whether the dock's network is on the air (never its name).
void checkWifi();
// Diagnostic: the radio off (nothing polled) or back on.
void setWifiOff(bool off);
// Test: made-up arrivals and LED positions every 10 s instead of CTA (serial "k" toggles).
void setFake(bool on);
bool fake();
// Wi-Fi joined with an IP (any task).
bool wifiUp();
// Unix time once the clock is set, else 0.
uint32_t unixNow();
// CTA requests since boot, against the keys' daily limits.
uint32_t requests();

}  // namespace net
