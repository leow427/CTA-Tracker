#include "live.h"

#include <Arduino.h>
#include <HTTPClient.h>
#include <NetworkClientSecure.h>
#include <Preferences.h>
#include <WiFi.h>
#include <esp_wifi.h>
#include <lwip/dns.h>
#include <lwip/ip_addr.h>
#include <lwip/tcpip.h>
#include <mbedtls/ssl.h>
#include <string.h>
#include <sys/time.h>
#include <time.h>

#include "cpu_clock.h"
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
constexpr uint32_t kSilentGapMs = 15000;      // after a request whose DNS lookup got no answer (netTask)
constexpr uint32_t kOldConnectMs = 8000;      // what the old code then spent connecting to 0.0.0.0
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

// SNTP, with the servers by address (see netTask). Called again when Wi-Fi comes up without the clock: SNTP sends its
// first request when started (here, before Wi-Fi is up, so it goes nowhere) and lwIP waits 15 s before the next.
void startSntp() { configTzTime(cta::kTz, "216.239.35.0", "216.239.35.4", "162.159.200.1"); }

// ---------- DNS ----------
//
// Arduino's hostByName() waits out lwIP's own retries (7 s for each DNS server, so 14 s for two) and then returns
// lwIP's error code, which NetworkClientSecure takes for success: it goes on to spend its whole connect timeout
// (8 s) connecting to 0.0.0.0. 14 + 8 s is the 22 s each failed request took after a fresh boot on a network that
// passed no traffic for its first few minutes (power log, 2026-11-09). This lookup waits for lwIP's own answer just
// as long (so it takes every answer the old one did), and a failed lookup fails the request at once. The CPU stays
// slow while it waits (cpuclk::Lazy starts at the handshake).
constexpr uint32_t kDnsWaitMs = 25000;    // a backstop: lwIP always answers, after 7 s per server (3 at most)
constexpr uint32_t kDnsSilentMs = 5000;   // a failed lookup this slow had no answer at all
uint32_t dnsLastMs = 0;                   // how long the last lookup took
SemaphoreHandle_t dnsDone;       // given from the tcpip thread once the answer below is in
volatile uint32_t dnsWant = 0;   // the lookup the network task is waiting for; 0 = none
volatile uint32_t dnsGot = 0;    // the lookup dnsAddr belongs to
volatile uint32_t dnsAddr = 0;   // IPv4 address, network order; 0 = no answer
char dnsName[64];

void dnsFound(const char*, const ip_addr_t* ip, void* arg) {   // tcpip thread
    const uint32_t id = static_cast<uint32_t>(reinterpret_cast<uintptr_t>(arg));
    if (id != dnsWant) return;   // a lookup the network task gave up on
    dnsAddr = ip && IP_IS_V4(ip) ? ip_2_ip4(ip)->addr : 0;
    dnsGot = id;
    xSemaphoreGive(dnsDone);
}

void dnsStart(void* arg) {   // tcpip thread
    ip_addr_t ip;
    const err_t e = dns_gethostbyname_addrtype(dnsName, &ip, dnsFound, arg, LWIP_DNS_ADDRTYPE_IPV4);
    if (e != ERR_INPROGRESS) dnsFound(nullptr, e == ERR_OK ? &ip : nullptr, arg);   // cached, or failed outright
}

enum class Dns : uint8_t { Ok, NoAnswer, Failed };

// Failed: a negative answer (or no lookup); NoAnswer: lwIP ran out of retries with no answer, as on a network passing
// no traffic.
Dns resolve(const char* host, IPAddress& out) {
    if (out.fromString(host)) return Dns::Ok;
    while (xSemaphoreTake(dnsDone, 0) == pdTRUE) {}   // a late answer to a lookup given up on
    static uint32_t lastId = 0;
    if (++lastId == 0) ++lastId;   // 0 means none
    const uint32_t id = lastId;
    snprintf(dnsName, sizeof(dnsName), "%s", host);
    dnsWant = id;
    Dns r = Dns::Failed;
    const uint32_t t0 = millis();
    if (tcpip_callback(dnsStart, reinterpret_cast<void*>(static_cast<uintptr_t>(id))) == ERR_OK) {
        r = Dns::NoAnswer;
        for (;;) {
            const uint32_t spent = millis() - t0;
            if (spent >= kDnsWaitMs || xSemaphoreTake(dnsDone, pdMS_TO_TICKS(kDnsWaitMs - spent)) != pdTRUE) break;
            if (dnsGot != id) continue;   // an answer to an earlier lookup, given just before this one began
            // No address after lwIP's own retries ran out is silence; a server's "no such name" arrives within a
            // round trip or two.
            r = dnsAddr ? Dns::Ok : millis() - t0 >= kDnsSilentMs ? Dns::NoAnswer : Dns::Failed;
            if (r == Dns::Ok) out = IPAddress(static_cast<uint32_t>(dnsAddr));
            break;
        }
    }
    dnsWant = 0;
    dnsLastMs = millis() - t0;
    return r;
}

// ---------- the connection ----------

// One HTTPS connection, kept open between train requests. The train API's certificate chain is ECDSA, which this
// chip checks in software: a full TLS handshake there takes about 2.2 s, so reusing the connection is what makes
// the stations load in seconds. Only one connection is ever open (each holds about 45 KB of internal RAM): a request
// to another host closes it first, and those requests (one a minute or rarer) close theirs when done.
//
// A connection that has to be opened again resumes the host's last TLS session when the server agrees (the train
// API does, every minute, when the bus poll has closed its connection): the handshake then skips the certificate
// checks and the key exchange, and takes about one round trip instead of seconds of CPU and radio time. A server
// that declines just runs a full handshake. Certificates are checked in full on every handshake that doesn't
// resume, and a session is only ever offered back to the host it came from.
class Tls : public NetworkClientSecure {   // NetworkClientSecure keeps these protected
public:
    mbedtls_ssl_context* ssl() { return &sslclient->ssl_ctx; }
    // stop() leaves a failed connection's plain-start flag set, and HTTPClient's connected() check would then poll
    // the closed socket as a plain one.
    void stop() override {
        NetworkClientSecure::stop();
        _stillinPlainStart = false;
    }
};

struct Conn {
    Tls tls;
    HTTPClient http;
    char host[64] = "";
    uint32_t lastUsed = 0;
    int64_t date = 0;   // the last response's Date header (Unix time), 0 without one
};
Conn* conn;
// Within one poll pass the due stations share the connection. Each pass's bus poll (another host) closes it, so
// the next pass's first station opens it again (resuming its session); this idle close only matters when polling
// stops.
constexpr uint32_t kIdleCloseMs = 90000;

// The last session with each host, serialized in PSRAM: it holds the session's master secret, so it never leaves
// RAM and is never logged.
struct Session {
    char host[48];
    uint8_t* data;   // kSessionMax bytes in PSRAM, allocated on first use
    size_t len;      // 0: none
};
constexpr int kSessions = 4;           // one per host: trains, alerts, weather (buses' don't fit, below)
constexpr size_t kSessionMax = 4096;   // a session with the server's certificate: about 1.2-1.3 KB for those three
constexpr uint32_t kQuickFailMs = 3000;   // a resuming handshake that fails faster was refused, not lost (openConn)
Session sessions[kSessions] = {};
int nextSession = 0;

Session* sessionFor(const char* host, bool add) {
    for (Session& s : sessions)
        if (s.data && strcmp(s.host, host) == 0) return &s;
    if (!add) return nullptr;
    Session& s = sessions[nextSession++ % kSessions];
    if (!s.data) s.data = static_cast<uint8_t*>(heap_caps_malloc(kSessionMax, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
    if (!s.data) return nullptr;
    snprintf(s.host, sizeof(s.host), "%s", host);
    s.len = 0;
    return &s;
}

// The last request's DNS lookup got no answer at all (the network passing no traffic): netTask rests.
bool lastSilent = false;

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

enum class Open : uint8_t { Ok, Silent, Unreachable, Failed, FailedResuming };

// Opens conn to host:443: DNS, TCP, then the TLS handshake (resuming the host's last session if there is one). The
// lookup and the TCP connection only wait on the network, so full speed (fast) starts at the handshake. kind is for
// the power log, as in get().
Open openConn(const char* host, cpuclk::Lazy& fast, char kind) {
    IPAddress ip;
    const Dns d = resolve(host, ip);
    if (d != Dns::Ok) {
        Serial.printf("net: %s for %s\n", d == Dns::NoAnswer ? "no DNS answer" : "DNS lookup failed", host);
        return d == Dns::NoAnswer ? Open::Silent : Open::Unreachable;
    }
    conn->tls.setPlainStart();   // the TCP connection and the TLS setup now, the handshake below
    if (!conn->tls.connect(ip, 443, host, nullptr, nullptr, nullptr)) return Open::Unreachable;
    fast.on();
    Session* s = sessionFor(host, false);
    bool offered = false;
    mbedtls_time_t offeredStart = 0;
    if (s && s->len) {
        mbedtls_ssl_session saved;
        mbedtls_ssl_session_init(&saved);
        if (mbedtls_ssl_session_load(&saved, s->data, s->len) == 0 && mbedtls_ssl_set_session(conn->tls.ssl(), &saved) == 0) {
            offered = true;
            offeredStart = saved.MBEDTLS_PRIVATE(start);
        }
        mbedtls_ssl_session_free(&saved);
    }
    const uint32_t t0 = millis();
    if (!conn->tls.startTLS()) {   // the handshake failed (and the client stopped)
        if (s) s->len = 0;         // perhaps over the offered session: the next try starts fresh
        // A server that refuses the session answers at once; a handshake that timed out (lost replies) would only
        // time out again, so it isn't tried twice.
        return offered && millis() - t0 < kQuickFailMs ? Open::FailedResuming : Open::Failed;
    }
    // Keep the session, straight from the connection (no copy in internal RAM); a resumed one may have brought a
    // new ticket. A session too big for its slot isn't kept: the bus API's tickets are about 4 KB, so its
    // handshakes stay full ones (RSA, about 0.5 s). A resumed session keeps its first handshake's start time; a
    // full handshake starts a new one.
    const mbedtls_ssl_session* now = conn->tls.ssl()->MBEDTLS_PRIVATE(session);
    const bool resumed = now && offered && now->MBEDTLS_PRIVATE(start) == offeredStart;
    size_t n = 0;
    if (now && mbedtls_ssl_session_save(now, nullptr, 0, &n) == MBEDTLS_ERR_SSL_BUFFER_TOO_SMALL && n <= kSessionMax) {
        if (!s) s = sessionFor(host, true);
        if (s) s->len = mbedtls_ssl_session_save(now, s->data, kSessionMax, &n) == 0 ? n : 0;
    } else if (s) {
        s->len = 0;   // the old one is no use now
    }
#ifdef POWER_LOG
    plog::event("tls,%c,%s", kind, resumed ? "resumed" : "full");
#else
    (void)kind;
    (void)resumed;
#endif
    return Open::Ok;
}

// GETs url, into body unless it's null, and notes the response's Date header (conn->date). Returns the HTTP status,
// or a negative HTTPClient error. Never logs the URL (it has keys). keep: leave the connection open for the next
// request to the same host. Full speed (fast) from the handshake, or from the request on a kept-open connection.
int fetch(const char* url, String* body, bool keep, cpuclk::Lazy& fast, char kind) {
    lastSilent = false;
    conn->date = 0;
    char host[sizeof(conn->host)];
    hostOf(url, host, sizeof(host));
    if (strcmp(host, conn->host) != 0) {
        closeConn();
        snprintf(conn->host, sizeof(conn->host), "%s", host);
    }
    bool fullRetried = false;
    for (int attempt = 0;; ++attempt) {
        // begin() first: it drops a connected client whose host differs from the last request's, so it has to run
        // before openConn() makes a new connection. GET() then finds the connection open and sends on it.
        conn->http.setReuse(true);
        if (!conn->http.begin(conn->tls, url)) return -1;
        const bool reused = conn->tls.connected();
        if (reused) {
            fast.on();
        } else {
            const Open o = openConn(host, fast, kind);
            if (o != Open::Ok) {
                closeConn();
                snprintf(conn->host, sizeof(conn->host), "%s", host);
                if (o == Open::FailedResuming && !fullRetried) {   // once more, with a full handshake
                    fullRetried = true;
                    continue;
                }
                lastSilent = o == Open::Silent;
                return HTTPC_ERROR_CONNECTION_REFUSED;   // what HTTPClient's own connect returned
            }
        }
        const int code = conn->http.GET();
        if (code == 200 && body) *body = conn->http.getString();
        int64_t t;
        if (code > 0 && cta::parseHttpDate(conn->http.header("Date").c_str(), t)) conn->date = t;
        conn->http.end();   // keeps the connection open when the server allows it
        conn->lastUsed = millis();
        if (code == 200 && body) {
            if (!keep) closeConn();
            return code;
        }
        closeConn();   // an error or an unread body: start clean next time
        snprintf(conn->host, sizeof(conn->host), "%s", host);
        if (code < 0 && reused && attempt == 0) continue;   // the server had closed the idle connection: retry fresh
        return code;
    }
}

// kind, for the power log: t trains, b buses, a alerts, w weather.
int get(const char* url, String& body, char kind, cpuclk::Lazy& fast, bool keep = false) {
#ifdef POWER_LOG
    plog::reqStart(kind);
    const uint32_t t0 = millis();
    const int code = fetch(url, &body, keep, fast, kind);
    plog::reqEnd(kind, code, code == 200 ? body.length() : 0, millis() - t0);
    return code;
#else
    return fetch(url, &body, keep, fast, kind);
#endif
}

// Sets the clock from the last response's Date header, for networks that block NTP (UDP 123), as some campus and
// guest networks do. Good to about a second, which is plenty for minute countdowns; NTP corrects it if it ever
// gets through.
bool clockFromDate(int code) {
    if (code <= 0 || !conn->date) {
        Serial.printf("net: clock from HTTP failed (HTTP %d)\n", code);
        return false;
    }
    const timeval tv = {static_cast<time_t>(conn->date), 0};
    settimeofday(&tv, nullptr);
    Serial.println("net: clock set from HTTP (NTP gave nothing)");
    return true;
}

// The clock from a bare request to the train API, for when there's no station to poll (no nearby list yet).
bool clockFromHttp() {
    cpuclk::Lazy fast;   // a TLS handshake: full speed, as every request
    const int code = fetch("https://lapi.transitchicago.com/", nullptr, false, fast, 'c');
    return clockFromDate(code);
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

// Every poll's request and parse run at full speed (cpu_clock.h): a full TLS handshake checks the train API's ECDSA
// certificates in software, about 2.2 s at 240 MHz and roughly three times that at 80, with the radio on. The DNS
// lookup and the TCP connection before it only wait on the network, so they don't hold it (get()).
//
// now 0: the clock isn't set yet. Then this response's Date header sets it (clockFromDate) unless NTP answers first,
// so the first station loads as soon as Wi-Fi is up instead of after the clock; a response that can't set it is
// dropped, and netTask tries again.
void pollStation(int st, uint32_t now) {
    cpuclk::Lazy fast;
    static char url[256];
    const uint32_t sec = st < kClosePolled ? cta::kHandheldClosePollSec : cta::kHandheldFarPollSec;
    const char* id = store.nearby().trains[st];
    if (now) stationPolled[st] = now;
    if (!cta::trainArrivalsUrl(url, sizeof(url), CTA_TRAIN_KEY, id)) return;
    String body;
    const uint32_t t0 = millis();
    const int code = get(url, body, 't', fast, true);
    ++requests;
    if (!now) {
        if (!ntpSynced() && !clockFromDate(code)) return;
        now = static_cast<uint32_t>(time(nullptr));
        stationPolled[st] = now;
    }
    const int n = code == 200 ? store.applyTrains(st, body.c_str(), body.length(), now) : -1;
    Serial.printf("net: trains %s: HTTP %d, %d cards, %u ms\n", id, code, n, static_cast<unsigned>(millis() - t0));
    if (n < 0) stationPolled[st] = retryStamp(now, sec);
}

void pollBuses(uint32_t now) {
    cpuclk::Lazy fast;
    static char url[512];
    busPolled = now;
    const cta::Nearby& nb = store.nearby();
    const char* ids[cta::kNearbyPerTab];
    for (int b = 0; b < nb.nBuses; ++b) ids[b] = nb.buses[b];
    if (!cta::busPredictionsUrl(url, sizeof(url), CTA_BUS_KEY, ids, nb.nBuses)) return;
    String body;
    const uint32_t t0 = millis();
    const int code = get(url, body, 'b', fast);
    ++requests;
    const int n = code == 200 ? store.applyBuses(body.c_str(), body.length(), now) : -1;
    Serial.printf("net: buses, %d stops: HTTP %d, %d cards, %u ms\n", nb.nBuses, code, n,
                  static_cast<unsigned>(millis() - t0));
    if (n < 0) busPolled = retryStamp(now, cta::kHandheldBusPollSec);
}

void pollAlerts(uint32_t now) {
    cpuclk::Lazy fast;
    static char url[768];   // 48 routes at up to 7 bytes and a comma, plus the base
    alertsPolled = now;
    const char* routes[Store::kMaxRoutes];
    const int nRoutes = store.routes(routes, Store::kMaxRoutes);
    // Unplanned disruptions only: elevator outages and planned bus-stop notes would mark nearly every route.
    if (!cta::alertsUrl(url, sizeof(url), routes, nRoutes, true)) return;
    String body;
    const int code = get(url, body, 'a', fast);
    const int n = code == 200 ? store.applyAlerts(body.c_str(), body.length()) : -1;
    Serial.printf("net: alerts, %d routes: HTTP %d, %d alerted\n", nRoutes, code, n);
    if (n < 0) alertsPolled = retryStamp(now, cta::kAlertsPollSec);
}

void pollWeather(uint32_t now) {
    cpuclk::Lazy fast;
    static char url[160];
    weatherPolled = now;
    if (!cta::weatherUrl(url, sizeof(url), store.nearby().lat, store.nearby().lon)) return;
    String body;
    const int code = get(url, body, 'w', fast);
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
    const cpuclk::Fast fast;
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
    // The timeouts HTTPClient used to set when it connected: 8 s for the TCP connection (and the socket's reads and
    // writes), 12 s for a response.
    conn->tls.setConnectionTimeout(8000);
    conn->tls.setTimeout(12000);
    conn->http.setTimeout(12000);
    conn->http.setConnectTimeout(8000);   // were HTTPClient ever to connect by itself (it then sets the client's too)
    static const char* dateHeader[] = {"Date"};
    conn->http.collectHeaders(dateHeader, 1);
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
    startSntp();
    uint32_t lastBegin = 0, retryMs = kWifiRetryMinMs;
    bool wasUp = false, joining = false, scanned = false;
    // A DNS lookup that gets no answer at all means the network isn't passing traffic: polling rests until
    // kSilentGapMs after that request began, but never past the moment the old code's request would have ended (the
    // lookup, then 8 s connecting to 0.0.0.0), so nothing is tried later than before; in between the radio is idle
    // and the CPU slow. Any other failure (a negative DNS answer, no TCP connection, an HTTP error) goes straight on,
    // as before; each stop keeps its own retry time (kRetrySec).
    uint32_t restUntil = 0;
    // A join holds full speed until it ends either way, as requests do: the WPA handshake's key derivation is CPU work.
    auto setJoining = [&joining](bool j) {
        if (j == joining) return;
        joining = j;
        if (j) cpuclk::hold();
        else cpuclk::release();
    };
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
            restUntil = 0;
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
                setJoining(false);
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
                setJoining(true);
                WiFi.begin(WIFI_SSID, WIFI_PASS);
                lastBegin = millis();
            }
            setStatus(joining ? "Connecting to Wi-Fi" : "No Wi-Fi");
            publish();
            vTaskDelay(pdMS_TO_TICKS(1000));
            continue;
        }
        setJoining(false);
        retryMs = kWifiRetryMinMs;
        if (!wasUp) {
            wasUp = true;
            Serial.printf("net: Wi-Fi up, %s, RSSI %d\n", WiFi.localIP().toString().c_str(), WiFi.RSSI());
            if (ntpSynced()) setStatus("");
            else startSntp();   // its first request went out before there was a link
            restUntil = 0;
            publish();   // back online now, not at the next due poll
        }
        if (!ntpSynced()) {
            // NTP, or the train API's Date header, every 30 s until something works. With a station to poll, its
            // request goes at once and its Date sets the clock (pollStation), so the stops load without waiting for
            // the clock and without a request of its own; with none yet, a bare request after 10 s without NTP.
            static uint32_t waitingSince = 0, lastHttpTry = 0;
            if (!waitingSince) waitingSince = millis();
            if (strcmp(status, "Setting the clock") != 0) { setStatus("Setting the clock"); publish(); }
            const bool station = store.located() && store.nearby().nTrains > 0;
            if ((station || millis() - waitingSince > 10000) && (!lastHttpTry || millis() - lastHttpTry > 30000)) {
                lastHttpTry = millis();
                if (station) {
                    pollStation(0, 0);
                    publish();
                    if (ntpSynced()) continue;
                } else if (clockFromHttp()) {
                    continue;
                }
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
        if (restUntil && static_cast<int32_t>(millis() - restUntil) < 0) {
            vTaskDelay(pdMS_TO_TICKS(250));
            continue;
        }
        restUntil = 0;
        const uint32_t pollMs = millis();
        if (pollOne(static_cast<uint32_t>(time(nullptr)))) {
            if (lastSilent) restUntil = pollMs + (dnsLastMs + kOldConnectMs < kSilentGapMs ? dnsLastMs + kOldConnectMs : kSilentGapMs);
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
    dnsDone = xSemaphoreCreateBinary();
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
