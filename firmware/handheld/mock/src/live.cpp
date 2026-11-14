#include "live.h"

#include <Arduino.h>
#include <HTTPClient.h>
#include <NetworkClientSecure.h>
#include <Preferences.h>
#include <WiFi.h>
#include <esp_heap_caps.h>
#include <esp_mac.h>
#include <esp_wifi.h>
#include <lwip/dns.h>
#include <lwip/ip_addr.h>
#include <lwip/tcpip.h>
#include <mbedtls/platform.h>
#include <mbedtls/ssl.h>
#include <string.h>
#include <sys/time.h>
#include <time.h>

#include <new>

#include "cpu_clock.h"
#include "cta_config.h"
#include "link.h"
#include "local_time.h"
#include "live_store.h"
#include "wifi_list.h"
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
// WPA2-Enterprise (eduroam): the account's username; WIFI_PASS is then its password. Unset: a WPA2-Personal network.
#ifndef WIFI_EAP_USER
#define WIFI_EAP_USER ""
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

// Written only by the network task. In PSRAM (up before constructors run): internal RAM is for Wi-Fi and TLS.
Store& store = *new (heap_caps_malloc(sizeof(Store), MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT)) Store();
uint32_t stationPolled[cta::kNearbyPerTab] = {};   // Unix time of the last attempt, 0 = never
uint32_t busPolled = 0, alertsPolled = 0, weatherPolled = 0, pinPolled = 0;
uint32_t requests = 0;
char status[40] = "Starting";

// The pinned stop, if it's of kind k.
const cta::PickedStop* pinOf(cta::Kind k) {
    const cta::PickedStop* p = store.pin();
    return p && p->type == k ? p : nullptr;
}

// Shared with the loop.
SemaphoreHandle_t mu;
Snapshot* published;
Snapshot* building;
volatile uint32_t pubVersion = 0;
volatile bool slow = false;
volatile bool woke = false;
cta::Nearby pendingNearby;          // under mu: a list from setNearby() the network task hasn't taken yet
volatile bool nearbyPending = false;
// Under mu: a pin or follow the network task hasn't taken yet (setPin, setFollow), in PSRAM. pin/follow: +1 set,
// -1 clear, 0 nothing new; a follow is taken after the pin.
struct PendingLink {
    int8_t pin, follow;
    cta::PickedStop pinStop;
    cta::FollowMsg followMsg;
};
PendingLink* pendingLink;

// The saved Wi-Fi networks (ecosystem spec 5.1, F1), slot 0 home, from NVS ("wifi"), else secrets.h's network as
// slot 0. In PSRAM.
cta::WifiList* nets;
bool netsFromSecrets = false;   // slot 0 is secrets.h's network (it may be WPA2-Enterprise)
char hostLabel[16];             // "cta-pet-xxxx", for mDNS

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
// the stations load in seconds. Only one connection is ever open (each holds about 45 KB, its big buffers in PSRAM): a request
// to another host closes it first, and those requests (one a minute or rarer) close theirs when done.
//
// A connection that has to be opened again resumes the host's last TLS session when the server agrees (the train
// API does, every minute, when the bus poll has closed its connection): the handshake then skips the certificate
// checks and the key exchange, and takes about one round trip instead of seconds of CPU and radio time. A server
// that declines just runs a full handshake. Certificates are checked in full on every handshake that doesn't
// resume, and a session is only ever offered back to the host it came from.
// mbedTLS's memory (the dock's net.cpp does the same): this core's build puts all of it in internal RAM (about 33 KB
// of buffers with a connection open). Its big blocks (the 16 KB record buffer coming in, the 4 KB one going out) go to
// PSRAM instead, by the rule every other malloc here follows (4 KB and up in PSRAM); the small ones stay internal.
// Either heap if the other is out.
constexpr size_t kTlsPsramFrom = 4096;
void* tlsCalloc(size_t n, size_t size) {
    if (size && n > SIZE_MAX / size) return nullptr;
    const uint32_t first = n * size >= kTlsPsramFrom ? MALLOC_CAP_SPIRAM : MALLOC_CAP_INTERNAL;
    const uint32_t second = first == MALLOC_CAP_SPIRAM ? MALLOC_CAP_INTERNAL : MALLOC_CAP_SPIRAM;
    if (void* p = heap_caps_calloc(n, size, first | MALLOC_CAP_8BIT)) return p;
    return heap_caps_calloc(n, size, second | MALLOC_CAP_8BIT);
}

class Tls : public NetworkClientSecure {   // NetworkClientSecure keeps these protected
public:
    mbedtls_ssl_context* ssl() { return &sslclient->ssl_ctx; }
    // stop() leaves a failed connection's plain-start flag set, and HTTPClient's connected() check would then poll
    // the closed socket as a plain one. On core 3.2 it also forgets the CA bundle (3.3 keeps it), and every full
    // handshake after that would fail for want of a trusted root.
    void stop() override {
        NetworkClientSecure::stop();
        _stillinPlainStart = false;
        attach_ssl_certificate_bundle(sslclient.get(), true);
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

// Takes a pin or follow from the app (setPin, setFollow) into the store; true if there was one. Every publish()
// takes them first, so a snapshot built mid-poll never brings back a pin the loop has just cleared.
bool takeLink() {
    if (!pendingLink->pin && !pendingLink->follow) return false;
    xSemaphoreTake(mu, portMAX_DELAY);
    const PendingLink p = *pendingLink;
    pendingLink->pin = pendingLink->follow = 0;
    xSemaphoreGive(mu);
    char was[sizeof(cta::PickedStop::id)];
    snprintf(was, sizeof(was), "%s", store.pin() ? store.pin()->id : "");
    if (p.pin) store.setPin(p.pin > 0 ? &p.pinStop : nullptr);
    if (p.follow) store.setFollow(p.follow > 0 ? &p.followMsg : nullptr);
    const char* id = store.pin() ? store.pin()->id : "";
    if (strcmp(was, id) != 0) {   // a new stop polls at once; the same one keeps its schedule
        pinPolled = 0;
        if (pinOf(cta::Kind::Bus)) busPolled = 0;
    }
    Serial.printf("net: pinned %s, following %s\n", id[0] ? id : "nothing",
                  store.pin() && p.follow > 0 ? p.followMsg.vehicle : p.follow ? "nothing" : "as before");
    return true;
}

void publish() {
    takeLink();
    if (ntpSynced()) store.updateFollow(static_cast<uint32_t>(time(nullptr)));   // after every poll (spec 5.7)
    clear(*building);
    store.fill(*building);
    building->wifiUp = WiFi.status() == WL_CONNECTED;
    building->homeWifi = building->wifiUp && nets->nets[0].ssid[0] && WiFi.SSID() == nets->nets[0].ssid;
    snprintf(building->status, sizeof(building->status), "%s", status);
    building->requests = requests;
    xSemaphoreTake(mu, portMAX_DELAY);
    // A clear that arrived while this snapshot was being built (setPin/setFollow cleared the published one already).
    if (pendingLink->pin < 0) building->hasPin = building->hasFollow = false;
    if (pendingLink->follow < 0) building->hasFollow = false;
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
// A listed station polls at the close rate when it's one of the 3 closest or the pinned stop (ecosystem spec 5.6).
uint32_t stationSec(int st) {
    const cta::PickedStop* p = pinOf(cta::Kind::Train);
    const bool pinned = p && strcmp(p->id, store.nearby().trains[st]) == 0;
    return st < kClosePolled || pinned ? cta::kHandheldClosePollSec : cta::kHandheldFarPollSec;
}

void pollStation(int st, uint32_t now) {
    cpuclk::Lazy fast;
    static char url[256];
    const uint32_t sec = stationSec(st);
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
    const char* ids[cta::kNearbyPerTab + 1];   // the list's stops and a pinned one outside it: Bus Tracker takes 10
    int n = 0;
    for (int b = 0; b < nb.nBuses; ++b) ids[n++] = nb.buses[b];
    if (const cta::PickedStop* p = pinOf(cta::Kind::Bus)) {
        bool listed = false;
        for (int b = 0; b < nb.nBuses; ++b) listed = listed || strcmp(nb.buses[b], p->id) == 0;
        if (!listed) ids[n++] = p->id;
    }
    if (!cta::busPredictionsUrl(url, sizeof(url), CTA_BUS_KEY, ids, n)) return;
    String body;
    const uint32_t t0 = millis();
    const int code = get(url, body, 'b', fast);
    ++requests;
    const int cards = code == 200 ? store.applyBuses(body.c_str(), body.length(), now) : -1;
    Serial.printf("net: buses, %d stops: HTTP %d, %d cards, %u ms\n", n, code, cards,
                  static_cast<unsigned>(millis() - t0));
    if (cards < 0) busPolled = retryStamp(now, cta::kHandheldBusPollSec);
}

// The pinned stop (ecosystem spec 5.6) at the close stations' rate. A bus pin rides in the bus poll (all at that
// rate, pollBuses); a train pin at a listed station is that station's poll (stationSec). Only a train pin outside the
// list polls on its own, here.
static_assert(cta::kHandheldBusPollSec <= cta::kHandheldClosePollSec, "bus stops poll at the close rate");
bool pinOwnPoll() {
    const cta::PickedStop* p = pinOf(cta::Kind::Train);
    if (!p) return false;
    const cta::Nearby& nb = store.nearby();
    for (int i = 0; i < nb.nTrains; ++i)
        if (strcmp(nb.trains[i], p->id) == 0) return false;
    return true;
}

void pollPin(uint32_t now) {
    cpuclk::Lazy fast;
    constexpr size_t kUrlCap = 256;
    static char* url = static_cast<char*>(heap_caps_malloc(kUrlCap, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));   // not internal
    const cta::PickedStop& p = *store.pin();
    pinPolled = now;
    if (!url || !cta::trainArrivalsUrl(url, kUrlCap, CTA_TRAIN_KEY, p.id)) return;
    String body;
    const uint32_t t0 = millis();
    const int code = get(url, body, 't', fast, true);
    ++requests;
    const int n = code == 200 ? store.applyPin(body.c_str(), body.length(), now) : -1;
    Serial.printf("net: pinned %s: HTTP %d, %d cards, %u ms\n", p.id, code, n, static_cast<unsigned>(millis() - t0));
    if (n < 0) pinPolled = retryStamp(now, cta::kHandheldClosePollSec);
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
        if (dueArrivals(stationPolled[st], stationSec(st), now)) {
            pollStation(st, now);
            return true;
        }
    if (pinOwnPoll() && dueArrivals(pinPolled, cta::kHandheldClosePollSec, now)) { pollPin(now); return true; }
    if ((nb.nBuses || pinOf(cta::Kind::Bus)) && dueArrivals(busPolled, cta::kHandheldBusPollSec, now)) { pollBuses(now); return true; }
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
void reportScan(const char* ssid) {
    const cpuclk::Fast fast;
    const int n = WiFi.scanNetworks();
    for (int i = 0; i < n; ++i)
        if (WiFi.SSID(i) == ssid) {
            Serial.printf("net: %s is in reach: channel %d, RSSI %d, auth %d\n", ssid, WiFi.channel(i), WiFi.RSSI(i),
                          static_cast<int>(WiFi.encryptionType(i)));
            WiFi.scanDelete();
            return;
        }
    Serial.printf("net: %s not seen among %d networks on 2.4 GHz; check the name, and that the router offers 2.4 GHz\n",
                  ssid, n);
    WiFi.scanDelete();
}

// The saved network to join (F1): the strongest in range, skipping the slots in skip (failed or just dropped), or any
// in range if every one in range is skipped; -1 if none is. With one saved network there's no scan: it's tried as
// before (a hidden network never shows in a scan, and scanning costs radio time away from home).
int pickNet(uint8_t skip) {
    int only = -1, saved = 0;
    for (int s = 0; s < cta::kWifiSlots; ++s)
        if (nets->nets[s].ssid[0]) { only = s; ++saved; }
    if (saved <= 1) return only;
    const cpuclk::Fast fast;
    constexpr int kMaxSeen = 24;
    using Name = char[cta::kSsidLen + 1];
    static Name* names = static_cast<Name*>(heap_caps_malloc(sizeof(Name) * kMaxSeen, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
    if (!names) return only;
    cta::SeenNet seen[kMaxSeen];
    const int found = WiFi.scanNetworks();
    int n = 0;
    for (int i = 0; i < found && n < kMaxSeen; ++i) {
        snprintf(names[n], sizeof(names[n]), "%s", WiFi.SSID(i).c_str());
        seen[n] = {names[n], static_cast<int8_t>(WiFi.RSSI(i))};
        ++n;
    }
    WiFi.scanDelete();
    int slot = cta::strongestSaved(*nets, seen, n, skip);
    if (slot < 0 && skip) slot = cta::strongestSaved(*nets, seen, n, 0);
    Serial.printf("net: %d networks in reach; joining slot %d\n", n, slot);
    return slot;
}

void netTask(void*) {
    bool anyNet = false;
    for (const cta::SavedNet& s : nets->nets) anyNet = anyNet || s.ssid[0];
    if (!anyNet || !CTA_TRAIN_KEY[0] || !CTA_BUS_KEY[0]) {
        setStatus(anyNet ? "No API key" : "No Wi-Fi set up");
        Serial.printf("net: %s; staying offline\n", anyNet ? "no CTA keys in firmware/secrets.h" : "no Wi-Fi saved");
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
    // 201 the network out of reach, 8 left on purpose); the loop below logs it. On ESP-IDF's event loop, not
    // WiFi.onEvent: core 3.2's list of Arduino handlers has no lock, and adding one while its task runs the start
    // event that mode() just posted could read a freed list. The handler only notes the reason: it runs on the 2 KB
    // system event task, too small for printf.
    static volatile uint8_t discReason = 0;
    static volatile uint32_t discCount = 0;
    esp_event_handler_register(WIFI_EVENT, WIFI_EVENT_STA_DISCONNECTED, [](void*, esp_event_base_t, int32_t, void* data) {
        discReason = static_cast<wifi_event_sta_disconnected_t*>(data)->reason;
        discCount = discCount + 1;
    }, nullptr);
    uint32_t discShown = 0;
    WiFi.setAutoReconnect(false);   // the driver would rescan right after every miss; this loop backs off instead
    // NTP servers by address, not name: while SNTP's own DNS lookup is pending (as it stays on a network that
    // blocks it), an HTTPS request's hostByName() clears the DNS cache, which runs SNTP's callback outside the
    // TCP/IP lock and trips an lwIP assert (a reboot; seen 2026-11-09, core 3.3). These are time.google.com's and
    // time.cloudflare.com's published anycast addresses.
    startSntp();
    uint32_t lastBegin = 0, retryMs = kWifiRetryMinMs;
    bool wasUp = false, joining = false, scanned = false;
    int slot = -1;      // the saved network being joined or joined
    uint8_t skip = 0;   // saved slots to pass over on the next pick: ones that failed or dropped since the last join
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
        // The latest disconnect reason, at most once a pass (about a second while joining; the driver can repeat one
        // many times a second).
        if (discCount != discShown) {
            discShown = discCount;
            Serial.printf("net: disconnected, reason %u\n", discReason);
        }
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
        if (takeLink()) publish();   // a pin or follow from the app: poll its stop from now on
        if (conn->host[0] && millis() - conn->lastUsed > kIdleCloseMs) closeConn();
        if (woke) {
            woke = false;
            if (!wasUp && !joining) { lastBegin = 0; retryMs = kWifiRetryMinMs; scanned = false; }
        }
        if (WiFi.status() != WL_CONNECTED) {
            if (wasUp) {   // F1: rescan and move to the next strongest saved network
                Serial.println("net: Wi-Fi lost");
                esp_wifi_disconnect();   // stops core 3.2's own reconnect try, which would fail pickNet's scan
                lastBegin = 0;
                retryMs = kWifiRetryMinMs;
                skip = slot >= 0 ? static_cast<uint8_t>(1u << slot) : 0;
            }
            wasUp = false;
            if (joining && millis() - lastBegin > kWifiJoinMs) {   // this attempt failed: radio off until the next
                setJoining(false);
                const int why = WiFi.status();
                esp_wifi_disconnect();   // core 3.2's WiFi.disconnect() skips this until associated, leaving a join running
                Serial.printf("net: no %s (status %d); next try in %u s\n", nets->nets[slot].ssid, why,
                              static_cast<unsigned>(retryMs / 1000));
                skip |= static_cast<uint8_t>(1u << slot);
                if (!scanned) {   // once per boot or wake: say whether the network is in reach at all
                    scanned = true;
                    reportScan(nets->nets[slot].ssid);
                }
            }
            if (!joining && (lastBegin == 0 || millis() - lastBegin > retryMs)) {
                if (lastBegin != 0) retryMs = retryMs * 2 > kWifiRetryMaxMs ? kWifiRetryMaxMs : retryMs * 2;
                lastBegin = millis();
                slot = pickNet(skip);
                if (slot < 0) {
                    Serial.printf("net: no saved network in reach; next try in %u s\n", static_cast<unsigned>(retryMs / 1000));
                    skip = 0;
                } else {
                    const cta::SavedNet& net = nets->nets[slot];
                    Serial.printf("net: connecting to %s\n", net.ssid);
                    setJoining(true);
                    if (slot == 0 && netsFromSecrets && WIFI_EAP_USER[0])
                        WiFi.begin(net.ssid, WPA2_AUTH_PEAP, WIFI_EAP_USER, WIFI_EAP_USER, net.pass);
                    else
                        WiFi.begin(net.ssid, net.pass);
                    lastBegin = millis();
                }
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
            skip = 0;
            Serial.printf("net: Wi-Fi up, %s, RSSI %d\n", WiFi.localIP().toString().c_str(), WiFi.RSSI());
            // The HTTP link, the first time only (it outlives reconnects): before Wi-Fi has an IP, httpd and mDNS
            // assert in lwIP and boot-loop the board.
            lan::start(hostLabel);
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
    mbedtls_platform_set_calloc_free(tlsCalloc, heap_caps_free);   // before any TLS: blocks are freed where they were made
    mu = xSemaphoreCreateMutex();
    dnsDone = xSemaphoreCreateBinary();
    published = static_cast<Snapshot*>(ps_malloc(sizeof(Snapshot)));
    building = static_cast<Snapshot*>(ps_malloc(sizeof(Snapshot)));
    pendingLink = static_cast<PendingLink*>(ps_calloc(1, sizeof(PendingLink)));
    nets = static_cast<cta::WifiList*>(ps_calloc(1, sizeof(cta::WifiList)));
    if (!published || !building || !pendingLink || !nets || !store.init()) { Serial.println("net: out of memory"); return; }
    clear(*published);
    uint8_t mac[6];
    esp_read_mac(mac, ESP_MAC_WIFI_STA);   // the factory MAC, as in GET /info's id and the BLE name
    snprintf(hostLabel, sizeof(hostLabel), "cta-pet-%02x%02x", mac[4], mac[5]);
    // The last nearby list (spec 8.3: kept in flash), and the saved networks (F1; written by BLE setup).
    Preferences prefs;
    if (prefs.begin("trackie", true)) {
        const String saved = prefs.getString("nearby", "");
        if (prefs.getBytesLength("wifi") == sizeof(cta::WifiList)) prefs.getBytes("wifi", nets, sizeof(cta::WifiList));
        prefs.end();
        cta::Nearby n;
        if (saved.length() && cta::parseNearby(saved.c_str(), saved.length(), n)) {
            store.setNearby(n);
            Serial.printf("net: saved nearby list: %d stations, %d bus stops\n", n.nTrains, n.nBuses);
        }
    }
    if (!store.located()) Serial.println("net: no nearby list yet; send one with tools/send_nearby.py");
    bool anyNet = false;
    for (cta::SavedNet& s : nets->nets) {
        s.ssid[cta::kSsidLen] = s.pass[cta::kPassLen] = 0;   // whatever flash held
        anyNet = anyNet || s.ssid[0];
    }
    if (!anyNet && WIFI_SSID[0]) {   // the build-time fallback (spec 5.1)
        snprintf(nets->nets[0].ssid, sizeof(nets->nets[0].ssid), "%s", WIFI_SSID);
        snprintf(nets->nets[0].pass, sizeof(nets->nets[0].pass), "%s", WIFI_PASS);
        netsFromSecrets = true;
    }
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

void setPin(const cta::PickedStop* stop) {
    if (!pendingLink || !published) return;
    xSemaphoreTake(mu, portMAX_DELAY);
    pendingLink->pin = stop ? 1 : -1;
    if (stop) pendingLink->pinStop = *stop;
    // The newer word wins, as in the store: a follow not yet taken ends unless this pins its own stop.
    if (pendingLink->follow > 0 && !(stop && cta::samePick(*stop, pendingLink->followMsg.stop))) pendingLink->follow = 0;
    if (!stop) {   // gone from the next read() at once, not on the network task's next pass
        published->hasPin = published->hasFollow = false;
        pubVersion = pubVersion + 1;
    }
    xSemaphoreGive(mu);
}

void setFollow(const cta::FollowMsg* f) {
    if (!pendingLink || !published) return;
    xSemaphoreTake(mu, portMAX_DELAY);
    pendingLink->follow = f ? 1 : -1;
    if (f) pendingLink->followMsg = *f;
    if (!f) {
        published->hasFollow = false;
        pubVersion = pubVersion + 1;
    }
    xSemaphoreGive(mu);
}

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
