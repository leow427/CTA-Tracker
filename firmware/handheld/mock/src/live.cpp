#include "live.h"

#include <Arduino.h>
#include <HTTPClient.h>
#include <NetworkClientSecure.h>
#include <Preferences.h>
#include <WiFi.h>
#include <esp_wifi.h>
#include <string.h>
#include <sys/time.h>
#include <time.h>

#include "cta_config.h"
#include "local_time.h"
#include "live_store.h"
#include "secrets.h"
#include "urls.h"
#ifdef POWER_LOG
#include "power_log.h"
#endif

#ifndef WIFI_SSID
#define WIFI_SSID ""
#endif
#ifndef WIFI_PASS
#define WIFI_PASS ""
#endif

// The ESP-IDF certificate bundle (Mozilla's roots) linked into the framework: TLS is always verified (spec 11).
extern const uint8_t kCaBundleStart[] asm("_binary_x509_crt_bundle_start");
extern const uint8_t kCaBundleEnd[] asm("_binary_x509_crt_bundle_end");

namespace live {

namespace {

constexpr int kClosePolled = 3;               // the 3 closest stations poll faster (spec 6.6)
constexpr uint32_t kRetrySec = 30;            // after a failed fetch
// Joining backs off from 15 s to 5 min while the network is out of reach (the wearable's usual state away from
// home), so the radio isn't scanning nonstop; a wake starts over.
constexpr uint32_t kWifiRetryMinMs = 15000, kWifiRetryMaxMs = 300000, kWifiJoinMs = 20000;   // a campus network took about 12 s to join

// Written only by the network task.
Store store;
uint32_t stationPolled[cta::kNearbyPerTab] = {};   // Unix time of the last attempt, 0 = never
uint32_t busPolled = 0, alertsPolled = 0, weatherPolled = 0;
uint32_t requests = 0;
char status[40] = "Starting";

// Shared with the loop.
SemaphoreHandle_t mu;
Snapshot* published;
Snapshot* building;
volatile uint32_t pubVersion = 0;
volatile bool slow = false;
volatile bool woke = false;
cta::Nearby pendingNearby;          // under mu: a list from setNearby() the network task hasn't taken yet
volatile bool nearbyPending = false;

bool ntpSynced() { return time(nullptr) > 1700000000; }

void setStatus(const char* s) { snprintf(status, sizeof(status), "%s", s); }

// One HTTPS connection, kept open between train requests. The train API's certificate is ECDSA, which this chip
// verifies in software: a fresh handshake there takes about 2.4 s, so reusing the connection is what makes ten
// stations load in seconds. Only one connection is ever open (each holds about 45 KB of internal RAM): a request to
// another host closes it first, and those requests (one a minute or rarer) close theirs when done.
struct Conn {
    NetworkClientSecure tls;
    HTTPClient http;
    char host[64] = "";
    uint32_t lastUsed = 0;
};
Conn* conn;
// Within one poll pass the due stations share the connection. Each pass's bus poll (another host) closes it, so
// the next pass's first station pays one handshake; this idle close only matters when polling stops.
constexpr uint32_t kIdleCloseMs = 90000;

void hostOf(const char* url, char* out, size_t cap) {
    const char* h = strstr(url, "://");
    h = h ? h + 3 : url;
    size_t n = strcspn(h, "/:?");
    if (n >= cap) n = cap - 1;
    memcpy(out, h, n);
    out[n] = 0;
}

void closeConn() {
    conn->tls.stop();
    conn->host[0] = 0;
}

// GETs url into body. Returns the HTTP status, or a negative HTTPClient error. Never logs the URL (it has keys).
// keep: leave the connection open for the next request to the same host.
int getOnce(const char* url, String& body, bool keep);
// kind, for the power log: t trains, b buses, a alerts, w weather.
int get(const char* url, String& body, char kind, bool keep = false) {
#ifdef POWER_LOG
    plog::reqStart(kind);
    const uint32_t t0 = millis();
    const int code = getOnce(url, body, keep);
    plog::reqEnd(kind, code, code == 200 ? body.length() : 0, millis() - t0);
    return code;
#else
    (void)kind;
    return getOnce(url, body, keep);
#endif
}

int getOnce(const char* url, String& body, bool keep) {
    char host[sizeof(conn->host)];
    hostOf(url, host, sizeof(host));
    if (strcmp(host, conn->host) != 0) {
        closeConn();
        snprintf(conn->host, sizeof(conn->host), "%s", host);
    }
    for (int attempt = 0;; ++attempt) {
        const bool reused = conn->tls.connected();
        conn->http.setReuse(true);
        if (!conn->http.begin(conn->tls, url)) return -1;
        const int code = conn->http.GET();
        if (code == 200) body = conn->http.getString();
        conn->http.end();   // keeps the connection open when the server allows it
        conn->lastUsed = millis();
        if (code == 200) {
            if (!keep) closeConn();
            return code;
        }
        closeConn();   // an error or an unread body: start clean next time
        snprintf(conn->host, sizeof(conn->host), "%s", host);
        if (code < 0 && reused && attempt == 0) continue;   // the server had closed the idle connection: retry fresh
        return code;
    }
}

// Sets the clock from an HTTPS response's Date header, for networks that block NTP (UDP 123), as some campus and
// guest networks do. Good to about a second, which is plenty for minute countdowns; NTP corrects it if it ever
// gets through. Uses the train API, whose connection the first poll then reuses.
bool clockFromHttp() {
    static const char* keys[] = {"Date"};
    closeConn();
    snprintf(conn->host, sizeof(conn->host), "lapi.transitchicago.com");
    conn->http.setReuse(true);
    if (!conn->http.begin(conn->tls, "https://lapi.transitchicago.com/")) return false;
    conn->http.collectHeaders(keys, 1);
    const int code = conn->http.GET();
    const String date = conn->http.header("Date");
    conn->http.end();
    closeConn();   // the body was never read
    int64_t t;
    if (code <= 0 || !cta::parseHttpDate(date.c_str(), t)) {
        Serial.printf("net: clock from HTTP failed (HTTP %d)\n", code);
        return false;
    }
    const timeval tv = {static_cast<time_t>(t), 0};
    settimeofday(&tv, nullptr);
    Serial.println("net: clock set from HTTP (NTP gave nothing)");
    return true;
}

void publish() {
    clear(*building);
    store.fill(*building);
    building->wifiUp = WiFi.status() == WL_CONNECTED;
    building->homeWifi = building->wifiUp && WiFi.SSID() == WIFI_SSID;
    snprintf(building->status, sizeof(building->status), "%s", status);
    building->requests = requests;
    xSemaphoreTake(mu, portMAX_DELAY);
    *published = *building;
    pubVersion = pubVersion + 1;
    xSemaphoreGive(mu);
}

// ---------- the polls (spec 6.6) ----------

// Slow mode never polls more often than normal (weather already runs every 15 min). Arrivals poll kSlowEarlySec
// before the 5 min mark, since a record goes stale at 5 min (kStaleSec): polling right on it would grey every card
// for the length of its fetch, or all of them if one fetch hangs.
constexpr uint32_t kSlowEarlySec = 30;
uint32_t every(uint32_t sec) { return slow && sec < cta::kSlowPollSec ? cta::kSlowPollSec : sec; }
uint32_t everyArrivals(uint32_t sec) { return slow && sec < cta::kSlowPollSec ? cta::kSlowPollSec - kSlowEarlySec : sec; }
bool due(uint32_t polled, uint32_t sec, uint32_t now) { return polled == 0 || now - polled >= every(sec); }
bool dueArrivals(uint32_t polled, uint32_t sec, uint32_t now) { return polled == 0 || now - polled >= everyArrivals(sec); }
// After a failure, try again in kRetrySec rather than a whole interval.
uint32_t retryStamp(uint32_t now, uint32_t sec) { return now - everyArrivals(sec) + kRetrySec; }

void pollStation(int st, uint32_t now) {
    static char url[256];
    const uint32_t sec = st < kClosePolled ? cta::kHandheldClosePollSec : cta::kHandheldFarPollSec;
    const char* id = store.nearby().trains[st];
    stationPolled[st] = now;
    if (!cta::trainArrivalsUrl(url, sizeof(url), CTA_TRAIN_KEY, id)) return;
    String body;
    const uint32_t t0 = millis();
    const int code = get(url, body, 't', true);
    ++requests;
    const int n = code == 200 ? store.applyTrains(st, body.c_str(), body.length(), now) : -1;
    Serial.printf("net: trains %s: HTTP %d, %d cards, %u ms\n", id, code, n, static_cast<unsigned>(millis() - t0));
    if (n < 0) stationPolled[st] = retryStamp(now, sec);
}

void pollBuses(uint32_t now) {
    static char url[512];
    busPolled = now;
    const cta::Nearby& nb = store.nearby();
    const char* ids[cta::kNearbyPerTab];
    for (int b = 0; b < nb.nBuses; ++b) ids[b] = nb.buses[b];
    if (!cta::busPredictionsUrl(url, sizeof(url), CTA_BUS_KEY, ids, nb.nBuses)) return;
    String body;
    const uint32_t t0 = millis();
    const int code = get(url, body, 'b');
    ++requests;
    const int n = code == 200 ? store.applyBuses(body.c_str(), body.length(), now) : -1;
    Serial.printf("net: buses, %d stops: HTTP %d, %d cards, %u ms\n", nb.nBuses, code, n,
                  static_cast<unsigned>(millis() - t0));
    if (n < 0) busPolled = retryStamp(now, cta::kHandheldBusPollSec);
}

void pollAlerts(uint32_t now) {
    static char url[768];   // 48 routes at up to 7 bytes and a comma, plus the base
    alertsPolled = now;
    const char* routes[Store::kMaxRoutes];
    const int nRoutes = store.routes(routes, Store::kMaxRoutes);
    // Unplanned disruptions only: elevator outages and planned bus-stop notes would mark nearly every route.
    if (!cta::alertsUrl(url, sizeof(url), routes, nRoutes, true)) return;
    String body;
    const int code = get(url, body, 'a');
    const int n = code == 200 ? store.applyAlerts(body.c_str(), body.length()) : -1;
    Serial.printf("net: alerts, %d routes: HTTP %d, %d alerted\n", nRoutes, code, n);
    if (n < 0) alertsPolled = retryStamp(now, cta::kAlertsPollSec);
}

void pollWeather(uint32_t now) {
    static char url[160];
    weatherPolled = now;
    if (!cta::weatherUrl(url, sizeof(url), store.nearby().lat, store.nearby().lon)) return;
    String body;
    const int code = get(url, body, 'w');
    const bool ok = code == 200 && store.applyWeather(body.c_str(), body.length());
    Serial.printf("net: weather: HTTP %d, %s\n", code, ok ? "ok" : "failed");
    if (!ok) weatherPolled = retryStamp(now, cta::kWeatherPollSec);
}

// One poll per call, the closest stops first, and every due station before the buses so the train API's
// connection is reused across them. False when nothing was due.
bool pollOne(uint32_t now) {
    const cta::Nearby& nb = store.nearby();
    for (int st = 0; st < nb.nTrains; ++st)
        if (dueArrivals(stationPolled[st], st < kClosePolled ? cta::kHandheldClosePollSec : cta::kHandheldFarPollSec, now)) {
            pollStation(st, now);
            return true;
        }
    if (nb.nBuses && dueArrivals(busPolled, cta::kHandheldBusPollSec, now)) { pollBuses(now); return true; }
    if (due(weatherPolled, cta::kWeatherPollSec, now)) { pollWeather(now); return true; }
    const char* any[1];
    if (due(alertsPolled, cta::kAlertsPollSec, now) && store.routes(any, 1)) { pollAlerts(now); return true; }
    return false;
}

#ifdef POWER_LOG
// The stress test's network burst (power analysis spec, section 3): test only. Fetches the next item in rotation
// (each station, the buses, the alerts, the weather) every 2 s, regardless of when it's due, at most 180 per run.
volatile bool burst = false;
volatile bool burstReset = false;
constexpr uint32_t kBurstGapMs = 2000, kBurstMax = 180;
uint32_t burstCount = 0, burstLast = 0;
int burstNext = 0;

void burstOne(uint32_t now) {
    const cta::Nearby& nb = store.nearby();
    const int n = nb.nTrains + 3;
    const int k = burstNext++ % n;
    if (k < nb.nTrains) pollStation(k, now);
    else if (k == nb.nTrains) { if (nb.nBuses) pollBuses(now); }
    else if (k == nb.nTrains + 1) { const char* any[1]; if (store.routes(any, 1)) pollAlerts(now); }
    else pollWeather(now);
}
#endif

// After a failed join: is the network in reach, and on what? The ESP32-S3 has 2.4 GHz only, and the most common
// failures are a 5 GHz-only network, a WPA3-only one, or a name that differs by a character. Logs only our SSID.
void reportScan() {
    const int n = WiFi.scanNetworks();
    for (int i = 0; i < n; ++i)
        if (WiFi.SSID(i) == WIFI_SSID) {
            Serial.printf("net: %s is in reach: channel %d, RSSI %d, auth %d\n", WIFI_SSID, WiFi.channel(i), WiFi.RSSI(i),
                          static_cast<int>(WiFi.encryptionType(i)));
            WiFi.scanDelete();
            return;
        }
    Serial.printf("net: %s not seen among %d networks on 2.4 GHz; check the name, and that the router offers 2.4 GHz\n",
                  WIFI_SSID, n);
    WiFi.scanDelete();
}

void netTask(void*) {
    if (!WIFI_SSID[0] || !CTA_TRAIN_KEY[0] || !CTA_BUS_KEY[0]) {
        setStatus(WIFI_SSID[0] ? "No API key" : "No Wi-Fi set up");
        Serial.printf("net: %s in firmware/secrets.h; staying offline\n", WIFI_SSID[0] ? "no CTA keys" : "no WIFI_SSID");
        publish();
        vTaskDelete(nullptr);
        return;
    }
    conn = new Conn();
    conn->tls.setCACertBundle(kCaBundleStart, kCaBundleEnd - kCaBundleStart);
    conn->tls.setHandshakeTimeout(12);   // seconds; the default is 120 s, long enough to miss a dropped Wi-Fi
    conn->http.setTimeout(12000);
    conn->http.setConnectTimeout(8000);
    WiFi.mode(WIFI_STA);
#ifdef WIFI_MAC
    {
        // A replacement MAC from secrets.h, for a router that has blocked the board's own (one did, 2026-11-09,
        // after a reboot loop: it ignored every join from the factory MAC and admitted a new one at once).
        uint8_t mac[6] = WIFI_MAC;
        Serial.printf("net: using the MAC from secrets.h%s\n", esp_wifi_set_mac(WIFI_IF_STA, mac) == ESP_OK ? "" : " (refused)");
    }
#endif
    // Why a join or a connection ended, in the driver's words (wifi_err_reason_t: 15 and 204 are a wrong password,
    // 201 the network out of reach, 8 left on purpose). Logged once per distinct reason in a row.
    WiFi.onEvent([](arduino_event_id_t, arduino_event_info_t info) {
        // Every reason, at most one line a second (the driver can repeat one many times a second).
        static uint32_t lastMs = 0;
        const uint8_t reason = info.wifi_sta_disconnected.reason;
        if (millis() - lastMs >= 1000) Serial.printf("net: disconnected, reason %u\n", reason);
        lastMs = millis();
    }, ARDUINO_EVENT_WIFI_STA_DISCONNECTED);
    WiFi.setAutoReconnect(false);   // the driver would rescan right after every miss; this loop backs off instead
    // NTP servers by address, not name: while SNTP's own DNS lookup is pending (as it stays on a network that
    // blocks it), an HTTPS request's hostByName() clears the DNS cache, which runs SNTP's callback outside the
    // TCP/IP lock and trips an lwIP assert (a reboot; seen 2026-11-09, core 3.3). These are time.google.com's and
    // time.cloudflare.com's published anycast addresses.
    configTzTime(cta::kTz, "216.239.35.0", "216.239.35.4", "162.159.200.1");
    uint32_t lastBegin = 0, retryMs = kWifiRetryMinMs;
    bool wasUp = false, joining = false, scanned = false;
    for (;;) {
        if (nearbyPending) {   // a new list: start over with its stops
            static cta::Nearby n;
            xSemaphoreTake(mu, portMAX_DELAY);
            n = pendingNearby;
            nearbyPending = false;
            xSemaphoreGive(mu);
            store.setNearby(n);
            memset(stationPolled, 0, sizeof(stationPolled));
            busPolled = alertsPolled = weatherPolled = 0;
            Serial.printf("net: nearby list: %d stations, %d bus stops\n", n.nTrains, n.nBuses);
            publish();
        }
        if (conn->host[0] && millis() - conn->lastUsed > kIdleCloseMs) closeConn();
        if (woke) {
            woke = false;
            if (!wasUp && !joining) { lastBegin = 0; retryMs = kWifiRetryMinMs; scanned = false; }
        }
        if (WiFi.status() != WL_CONNECTED) {
            if (wasUp) { Serial.println("net: Wi-Fi lost"); lastBegin = 0; retryMs = kWifiRetryMinMs; }
            wasUp = false;
            if (joining && millis() - lastBegin > kWifiJoinMs) {   // this attempt failed: radio off until the next
                joining = false;
                const int why = WiFi.status();
                WiFi.disconnect();
                Serial.printf("net: no %s (status %d); next try in %u s\n", WIFI_SSID, why, static_cast<unsigned>(retryMs / 1000));
                if (!scanned) {   // once per boot or wake: say whether the network is in reach at all
                    scanned = true;
                    reportScan();
                }
            }
            if (!joining && (lastBegin == 0 || millis() - lastBegin > retryMs)) {
                if (lastBegin != 0) retryMs = retryMs * 2 > kWifiRetryMaxMs ? kWifiRetryMaxMs : retryMs * 2;
                Serial.printf("net: connecting to %s\n", WIFI_SSID);
                WiFi.begin(WIFI_SSID, WIFI_PASS);
                lastBegin = millis();
                joining = true;
            }
            setStatus(joining ? "Connecting to Wi-Fi" : "No Wi-Fi");
            publish();
            vTaskDelay(pdMS_TO_TICKS(1000));
            continue;
        }
        joining = false;
        retryMs = kWifiRetryMinMs;
        if (!wasUp) {
            wasUp = true;
            Serial.printf("net: Wi-Fi up, %s, RSSI %d\n", WiFi.localIP().toString().c_str(), WiFi.RSSI());
            if (ntpSynced()) setStatus("");
            publish();   // back online now, not at the next due poll
        }
        if (!ntpSynced()) {
            // NTP first; after 10 s with nothing, the Date header, every 30 s until something works.
            static uint32_t waitingSince = 0, lastHttpTry = 0;
            if (!waitingSince) waitingSince = millis();
            if (millis() - waitingSince > 10000 && (!lastHttpTry || millis() - lastHttpTry > 30000)) {
                lastHttpTry = millis();
                if (clockFromHttp()) continue;
            }
            setStatus("Setting the clock");
            publish();
            vTaskDelay(pdMS_TO_TICKS(500));
            continue;
        }
        if (!store.located()) {   // spec 8.3: nothing to poll until the app (here, the laptop) sends a list
            if (strcmp(status, "Waiting for location") != 0) { setStatus("Waiting for location"); publish(); }
            vTaskDelay(pdMS_TO_TICKS(500));
            continue;
        }
        if (status[0]) { setStatus(""); publish(); }
#ifdef POWER_LOG
        if (burstReset) { burstReset = false; burstCount = 0; burstNext = 0; }
        if (burst && burstCount < kBurstMax) {
            if (millis() - burstLast >= kBurstGapMs) {
                burstLast = millis();
                ++burstCount;
                burstOne(static_cast<uint32_t>(time(nullptr)));
                publish();
            } else {
                vTaskDelay(pdMS_TO_TICKS(20));
            }
            continue;
        }
#endif
        if (pollOne(static_cast<uint32_t>(time(nullptr)))) {
            publish();
            vTaskDelay(pdMS_TO_TICKS(50));
        } else {
            vTaskDelay(pdMS_TO_TICKS(500));
        }
    }
}

}  // namespace

void start() {
    mu = xSemaphoreCreateMutex();
    published = static_cast<Snapshot*>(ps_malloc(sizeof(Snapshot)));
    building = static_cast<Snapshot*>(ps_malloc(sizeof(Snapshot)));
    if (!published || !building || !store.init()) { Serial.println("net: out of memory"); return; }
    clear(*published);
    // The last nearby list (spec 8.3: kept in flash).
    Preferences prefs;
    if (prefs.begin("trackie", true)) {
        const String saved = prefs.getString("nearby", "");
        prefs.end();
        cta::Nearby n;
        if (saved.length() && cta::parseNearby(saved.c_str(), saved.length(), n)) {
            store.setNearby(n);
            Serial.printf("net: saved nearby list: %d stations, %d bus stops\n", n.nTrains, n.nBuses);
        }
    }
    if (!store.located()) Serial.println("net: no nearby list yet; send one with tools/send_nearby.py");
    // Core 0, below the panel sender (2) and touch (3): TLS handshakes take CPU but never hold up a frame.
    xTaskCreatePinnedToCore(netTask, "net", 12288, nullptr, 1, nullptr, 0);
}

void setSlow(bool s) { slow = s; }

bool setNearby(const char* json, size_t len) {
    cta::Nearby n;
    if (!mu || !cta::parseNearby(json, len, n)) return false;
    if (!published) return false;   // the network task never started (no memory)
    Preferences prefs;
    if (!prefs.begin("trackie", false)) return false;
    const bool saved = prefs.putString("nearby", String(json, len)) > 0;
    prefs.end();
    if (!saved) return false;
    xSemaphoreTake(mu, portMAX_DELAY);
    pendingNearby = n;
    nearbyPending = true;
    xSemaphoreGive(mu);
    return true;
}

void wake() { woke = true; }

#ifdef POWER_LOG
void setBurst(bool on) { burst = on; }
void resetBurstCount() { burstReset = true; }
#endif

bool read(Snapshot& out, uint32_t& version) {
    if (!published || version == pubVersion) return false;
    xSemaphoreTake(mu, portMAX_DELAY);
    out = *published;
    version = pubVersion;
    xSemaphoreGive(mu);
    return true;
}

uint32_t unixNow() { return ntpSynced() ? static_cast<uint32_t>(time(nullptr)) : 0; }

}  // namespace live
