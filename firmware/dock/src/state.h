#pragma once
// The one state object (dock plan: DockState). The network task writes it and the UI task reads it, both
// under lock(); BLE callbacks only change cfg under the lock and bump cfgVersion.
#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>

#include "cta_types.h"
#include "device_config.h"

struct DockState {
    cta::DeviceConfig cfg;
    uint32_t cfgVersion = 0;     // bumped on every config change (UI, BLE); the poller rebuilds rows on change
    uint8_t stopsChanged = 0;    // bit per section: poll it now
    bool cfgDirty = false;       // cfg changed; the loop task saves it to flash (one writer, newest wins)

    cta::StopRecord recs[3];     // ts == 0: never fetched
    uint8_t masks[3] = {};       // lit LEDs per row
    uint32_t maskTs[3] = {};     // Unix time the positions behind each mask were fetched

    bool hasWifiConfig = false;
    bool wifiUp = false;
    char ssid[33] = "";
    bool timeValid = false;
    bool keyMissing = false;
    bool ctaError = false;       // the last arrivals fetch failed (bad key, CTA down)
    uint32_t lastUpdate = 0;     // Unix time of the last good arrivals fetch

    bool hasWeather = false;
    int wmo = 0;
    float tempC = 0;

    bool wifiChanged = false;    // BLE wrote new credentials; the network task saves them and rejoins
    char pendingPass[64] = "";   // held only until the network task saves it, then wiped
    uint32_t passkey = 0;        // shown full-screen while non-zero (BLE pairing)
};

extern DockState gState;

// A recursive lock around gState, held only for copies and small updates (never across a fetch).
struct Lock {
    Lock();
    ~Lock();
};
void stateBegin();
// With the lock held: a section's pick changed. Forgets its old data so nothing of the previous stop shows
// under the new name, asks for an immediate poll, and marks the config for saving.
void pickChanged(int section);
