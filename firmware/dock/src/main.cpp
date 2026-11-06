// Dock firmware (dock plan): setup and loop wiring only. The UI and LEDs run here on core 1; the network
// task (poller) runs on core 0; BLE runs on NimBLE's host task.
#include <Arduino.h>
#include <mbedtls/platform.h>
#include <mbedtls/platform_util.h>

#include "ble_server.h"
#include "display.h"
#include "leds.h"
#include "line_colors.h"
#include "local_time.h"
#include "net.h"
#include "poller.h"
#include "state.h"
#include "store.h"
#include "ui.h"

static constexpr const char* kFirmware = "0.1.0";
static uint32_t sLastLeds = 0, sLastHeapLog = 0;
static bool sDisplayOk = false;
static char sLine[cta::kBleMaxPayload + 1];
static size_t sLineLen = 0;

// TLS buffers (~45 KB per connection) go to PSRAM; internal RAM stays free for Wi-Fi, BLE and DMA.
static void* tlsCalloc(size_t n, size_t size) {
    void* p = heap_caps_calloc(n, size, MALLOC_CAP_SPIRAM);
    return p ? p : calloc(n, size);
}

// Bring-up insurance (dock plan milestones 2 and 4): a line of `config` JSON on the serial port applies like a
// BLE write, e.g. {"stops":[{"type":"train","route":"Red","dir":"95th/Dan Ryan","id":"30255","name":"Belmont"}]}.
// Wi-Fi is never accepted here: passwords cross only the encrypted BLE link.
static void serialConfig() {
    while (Serial.available()) {
        char c = Serial.read();
        if (c != '\n' && c != '\r') {
            if (sLineLen < sizeof sLine - 1) sLine[sLineLen++] = c;
            continue;
        }
        if (!sLineLen) continue;
        sLine[sLineLen] = '\0';
        bool ok;
        {
            Lock l;
            static cta::DeviceConfig cfg;
            cfg = gState.cfg;
            ok = sLine[0] == '{' && sLineLen < sizeof sLine - 1 && cta::applyConfigJson(sLine, sLineLen, cfg);
            if (ok) {
                for (int i = 0; i < 3; i++)
                    if (memcmp(&cfg.stops[i], &gState.cfg.stops[i], sizeof cfg.stops[i]) != 0) pickChanged(i);
                gState.cfg = cfg;
                gState.cfgVersion++;
                gState.cfgDirty = true;
            }
        }
        Serial.println(ok ? "config: applied" : "config: rejected");
        sLineLen = 0;
    }
}

// One writer for the config in flash, so the newest pick always ends up there (spec 7.2).
static void saveIfDirty() {
    static cta::DeviceConfig cfg;
    {
        Lock l;
        if (!gState.cfgDirty) return;
        gState.cfgDirty = false;
        cfg = gState.cfg;
    }
    store::saveConfig(cfg);
}

void setup() {
    Serial.begin(115200);
    mbedtls_platform_set_calloc_free(tlsCalloc, free);
    stateBegin();
    store::begin();
    {
        Lock l;
        gState.cfg = store::loadConfig();
        char pass[64];
        gState.hasWifiConfig = store::loadWifi(gState.ssid, sizeof gState.ssid, pass, sizeof pass);
        mbedtls_platform_zeroize(pass, sizeof pass);
    }
    leds::begin();
    sDisplayOk = display::begin();
    if (sDisplayOk) ui::begin();
    else Serial.println("dock: display failed; running without a screen (LEDs, BLE and polling still run)");
    ble::begin(kFirmware);
    poller::begin();
}

void loop() {
    if (sDisplayOk) {
        display::tick();
        ui::tick();
    }
    serialConfig();
    saveIfDirty();
    uint32_t ms = millis();
    if (ms - sLastLeds >= 33) {
        sLastLeds = ms;
        uint32_t now = net::now();
        int start, end;
        {
            Lock l;
            start = gState.cfg.nightStart;
            end = gState.cfg.nightEnd;
        }
        bool night = net::timeValid() && cta::inNightHours(net::minuteOfDay(), start, end);
        leds::tick(now, night || !now);   // no clock yet: nothing is known to be fresh
    }
    if (ms - sLastHeapLog >= 5 * 60 * 1000 || !sLastHeapLog) {   // the soak test watches this (dock plan 9)
        sLastHeapLog = ms ? ms : 1;
        Serial.printf("heap: internal free %u (min %u, largest block %u), psram free %u\n",
                      (unsigned)heap_caps_get_free_size(MALLOC_CAP_INTERNAL),
                      (unsigned)heap_caps_get_minimum_free_size(MALLOC_CAP_INTERNAL),
                      (unsigned)heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL),
                      (unsigned)heap_caps_get_free_size(MALLOC_CAP_SPIRAM));
    }
    delay(5);
}
