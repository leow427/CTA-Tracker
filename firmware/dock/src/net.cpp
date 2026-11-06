#include "net.h"

#include <Arduino.h>
#include <WiFi.h>
#include <time.h>

#include "cta_config.h"

namespace net {

static char sSsid[33], sPass[64];
static constexpr uint32_t kFirstBackoffMs = 8000;   // long enough for a slow router to associate
static uint32_t sNextTry = 0, sBackoff = kFirstBackoffMs;
static bool sTimeStarted = false;

void setCredentials(const char* ssid, const char* pass) {
    strlcpy(sSsid, ssid, sizeof sSsid);
    strlcpy(sPass, pass, sizeof sPass);
    WiFi.disconnect();
    sNextTry = 0;
    sBackoff = kFirstBackoffMs;
}

void begin(const char* ssid, const char* pass) {
    WiFi.mode(WIFI_STA);
    WiFi.setSleep(true);   // modem sleep keeps BLE and Wi-Fi sharing the radio smoothly
    WiFi.setAutoReconnect(false);
    setCredentials(ssid, pass);
}

void poll() {
    if (!sSsid[0]) return;
    if (WiFi.status() == WL_CONNECTED) {
        sBackoff = kFirstBackoffMs;
        if (!sTimeStarted) {
            configTzTime(cta::kTz, "pool.ntp.org", "time.nist.gov");
            sTimeStarted = true;
        }
        return;
    }
    if ((int32_t)(millis() - sNextTry) < 0) return;
    WiFi.disconnect();
    WiFi.begin(sSsid, sPass);
    sNextTry = millis() + sBackoff;
    sBackoff = sBackoff * 2 > 60000 ? 60000 : sBackoff * 2;   // 8 s, 16 s ... up to a minute
}

bool up() { return WiFi.status() == WL_CONNECTED; }

bool timeValid() { return time(nullptr) > 1700000000; }

uint32_t now() { return timeValid() ? (uint32_t)time(nullptr) : 0; }

int minuteOfDay() {
    time_t t = time(nullptr);
    struct tm lt;
    localtime_r(&t, &lt);
    return lt.tm_hour * 60 + lt.tm_min;
}

}  // namespace net
