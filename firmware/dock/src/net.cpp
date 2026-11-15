#include "net.h"

#include <Arduino.h>
#include <HTTPClient.h>
#include <NetworkClientSecure.h>
#include <WiFi.h>
#include <esp_heap_caps.h>
#include <esp_mac.h>
#include <lwip/dns.h>
#include <lwip/ip_addr.h>
#include <lwip/tcpip.h>
#include <mbedtls/platform.h>
#include <mbedtls/ssl.h>
#include <string.h>
#include <sys/time.h>
#include <new>
#include <time.h>

#include "alerts_weather.h"
#include "approach.h"
#include "bus_parse.h"
#include "cta_config.h"
#include "local_time.h"
#ifdef PERF_LOG
#include "perf_log.h"
#endif
#include "schedule.h"
#include "secrets.h"
#include "store.h"
#include "train_parse.h"
#include "urls.h"

#ifndef CTA_TRAIN_KEY
#define CTA_TRAIN_KEY ""
#endif
#ifndef CTA_BUS_KEY
#define CTA_BUS_KEY ""
#endif
// The dock's own network (a WPA2-Personal one; the handheld's WIFI_* settings are its own). Never logged.
#ifndef DOCK_WIFI_SSID
#define DOCK_WIFI_SSID ""
#endif
#ifndef DOCK_WIFI_PASS
#define DOCK_WIFI_PASS ""
#endif

// The ESP-IDF certificate bundle (Mozilla's roots) linked into the framework: TLS is always verified (spec 11).
extern const uint8_t kCaBundleStart[] asm("_binary_x509_crt_bundle_start");
extern const uint8_t kCaBundleEnd[] asm("_binary_x509_crt_bundle_end");

namespace net {

namespace {

constexpr uint32_t kRetrySec = 30;   // after a failed fetch
constexpr uint32_t kWifiJoinMs = 20000;   // a try that hasn't joined by then has failed
constexpr int kRows = 4;             // bus patterns through one stop (short turns, branches)
constexpr int kMaxTrains = 64;
// getpredictions answers for every route at a stop (a downtown stop serves dozens), and getvehicles for the picked
// routes' whole fleets: both kept in PSRAM, sized so the picked route is never crowded out.
constexpr int kMaxBusRecs = cta::kMaxBusCards, kMaxBuses = 256;
cta::StopRecord* busRecs;      // kMaxBusRecs, PSRAM
cta::BusVehicle* busVehicles;  // kMaxBuses, PSRAM

// ---------- shared with the loop (under mu) ----------
SemaphoreHandle_t mu;
dock::Data* published;
dock::PickerLists* pubLists;
volatile uint32_t pubVersion = 0, listsVersion = 0;
volatile bool slow = false;
cta::PickedStop pendingPicks[cta::kDockStops];
volatile bool picksPending = false;
struct Req { dock::Request what; char route[8]; char dir[40]; };
Req pendingReq;
volatile bool reqPending = false;
volatile uint32_t nRequests = 0;
volatile bool joinFailed = false;
volatile bool scanWanted = false;   // the serial "w" check
volatile bool wifiOff = false;      // the diagnostic "Wi-Fi off" mode   // a disconnect event since the last WiFi.begin

// ---------- the network task's own ----------
dock::Data* building;
dock::PickerLists* lists;
struct Sec {
    cta::PickedStop pick;
    cta::ApproachRow rows[kRows];
    int nRows = 0;
    bool rowsReady = false;          // trains at once; buses once getpatterns has answered
    uint32_t arrPolled = 0, posPolled = 0, patTried = 0;
    bool alerted = false;
};
Sec secs[cta::kDockStops];
uint32_t busPredPolled = 0, alertsPolled = 0, weatherPolled = 0, geoTried = 0;
bool hasGeo = false, geoBooted = false;   // geoBooted: the lookup at boot has been tried (spec 7.5)
float geoLat = 0, geoLon = 0;
uint32_t geoWhen = 0;

bool clockSet() { return time(nullptr) > 1700000000; }
uint32_t nowUnix() { return static_cast<uint32_t>(time(nullptr)); }
void setStatus(const char* s) { snprintf(building->status, sizeof(building->status), "%s", s); }
bool hasPick(const Sec& s) { return s.pick.id[0] != 0; }
bool isBus(const Sec& s) { return s.pick.type == cta::Kind::Bus; }

void publish() {
    building->wifiUp = WiFi.status() == WL_CONNECTED;
    xSemaphoreTake(mu, portMAX_DELAY);
    *published = *building;
    pubVersion = pubVersion + 1;
    xSemaphoreGive(mu);
}
void publishLists() {
    xSemaphoreTake(mu, portMAX_DELAY);
    *pubLists = *lists;
    listsVersion = listsVersion + 1;
    xSemaphoreGive(mu);
}

// SNTP with the servers by address: a pending by-name lookup can trip an lwIP assert when an HTTPS request's lookup
// clears the DNS cache (the handheld, 2026-11-09). time.google.com's and time.cloudflare.com's anycast addresses.
void startSntp() { configTzTime(cta::kTz, "216.239.35.0", "216.239.35.4", "162.159.200.1"); }

// ---------- DNS (the handheld's live.cpp) ----------
// Arduino's hostByName() waits out lwIP's retries and then reports success with 0.0.0.0, so a connection to nowhere
// follows. This waits for lwIP's own answer and fails the request at once without one.
constexpr uint32_t kDnsWaitMs = 25000;
SemaphoreHandle_t dnsDone;
volatile uint32_t dnsWant = 0, dnsGot = 0, dnsAddr = 0;
char dnsName[64];

void dnsFound(const char*, const ip_addr_t* ip, void* arg) {   // tcpip thread
    const uint32_t id = static_cast<uint32_t>(reinterpret_cast<uintptr_t>(arg));
    if (id != dnsWant) return;
    dnsAddr = ip && IP_IS_V4(ip) ? ip_2_ip4(ip)->addr : 0;
    dnsGot = id;
    xSemaphoreGive(dnsDone);
}
void dnsStart(void* arg) {   // tcpip thread
    ip_addr_t ip;
    const err_t e = dns_gethostbyname_addrtype(dnsName, &ip, dnsFound, arg, LWIP_DNS_ADDRTYPE_IPV4);
    if (e != ERR_INPROGRESS) dnsFound(nullptr, e == ERR_OK ? &ip : nullptr, arg);
}
bool resolve(const char* host, IPAddress& out) {
    if (out.fromString(host)) return true;
    while (xSemaphoreTake(dnsDone, 0) == pdTRUE) {}
    static uint32_t lastId = 0;
    if (++lastId == 0) ++lastId;
    const uint32_t id = lastId;
    snprintf(dnsName, sizeof(dnsName), "%s", host);
    dnsWant = id;
    bool ok = false;
    const uint32_t t0 = millis();
    if (tcpip_callback(dnsStart, reinterpret_cast<void*>(static_cast<uintptr_t>(id))) == ERR_OK) {
        for (;;) {
            const uint32_t spent = millis() - t0;
            if (spent >= kDnsWaitMs || xSemaphoreTake(dnsDone, pdMS_TO_TICKS(kDnsWaitMs - spent)) != pdTRUE) break;
            if (dnsGot != id) continue;
            ok = dnsAddr != 0;
            if (ok) out = IPAddress(static_cast<uint32_t>(dnsAddr));
            break;
        }
    }
    dnsWant = 0;
    return ok;
}

// mbedTLS's memory: this core's build puts all of it in internal RAM (about 45 KB with a connection open), where the
// display's second band buffer (display.cpp) now lives too. Its big blocks (the 16 KB record buffer coming in, the
// 4 KB one going out) go to PSRAM instead, by the rule every other malloc on this board follows
// (CONFIG_SPIRAM_MALLOC_ALWAYSINTERNAL: 4 KB and up in PSRAM); the small ones stay internal. Either heap if the
// other is out.
constexpr size_t kTlsPsramFrom = 4096;
void* tlsCalloc(size_t n, size_t size) {
    if (size && n > SIZE_MAX / size) return nullptr;
    const uint32_t first = n * size >= kTlsPsramFrom ? MALLOC_CAP_SPIRAM : MALLOC_CAP_INTERNAL;
    const uint32_t second = first == MALLOC_CAP_SPIRAM ? MALLOC_CAP_INTERNAL : MALLOC_CAP_SPIRAM;
    if (void* p = heap_caps_calloc(n, size, first | MALLOC_CAP_8BIT)) return p;
    return heap_caps_calloc(n, size, second | MALLOC_CAP_8BIT);
}

// ---------- one HTTPS connection, kept open between requests to the same host ----------
// The train API's certificate chain is ECDSA, checked in software: a full handshake there takes about 2 s, so the
// train requests of a poll pass share one connection. Only one is open at a time (about 45 KB of internal RAM).
//
// A connection that has to be opened again resumes the host's last TLS session when the server agrees (the handheld's
// live.cpp, 2026-11-10): the handshake then skips the certificate checks and the key exchange, and takes about a round
// trip instead of seconds of core 0's time. The train API's connection is reopened whenever a request to another host
// closed it (every poll pass, and every train request of the stress test's network burst). A server that declines just
// runs a full handshake. Certificates are checked in full on every handshake that doesn't resume, and a session is only
// ever offered back to the host it came from (dock performance analysis, round 3).
class Tls : public NetworkClientSecure {
public:
    mbedtls_ssl_context* ssl() { return &sslclient->ssl_ctx; }   // NetworkClientSecure keeps it protected
    void stop() override {
        NetworkClientSecure::stop();
        _stillinPlainStart = false;
        // arduino-esp32 3.2.0 (pinned, platformio.ini) zeroes the whole TLS context on stop, the certificate bundle's
        // hook included, so every later handshake failed verification ("useRootCABundle is set, but ... was not
        // called"). 3.3 keeps the hook. Attach it again; the bundle itself stays set.
        attach_ssl_certificate_bundle(sslclient.get(), true);
    }
};
struct Conn {
    Tls tls;
    HTTPClient http;
    char host[64] = "";
    uint32_t lastUsed = 0;
    int64_t date = 0;   // the last response's Date header, Unix time
};
Conn* conn;
constexpr uint32_t kIdleCloseMs = 60000;
constexpr int kMaxBody = 256 * 1024;   // the largest response read without a length (PSRAM)

// The last session with each host, serialized in PSRAM: it holds the session's master secret, so it never leaves RAM
// and is never logged.
struct Session {
    char host[48];
    uint8_t* data;   // kSessionMax bytes in PSRAM, allocated on first use
    size_t len;      // 0: none
};
constexpr int kSessions = 4;           // one per host: trains, alerts, weather, location (buses' don't fit, below)
constexpr size_t kSessionMax = 4096;   // a session with the server's certificate: about 1.2-1.3 KB for those hosts
constexpr uint32_t kQuickFailMs = 3000;   // a resuming handshake that fails faster was refused, not lost (openConn)
Session* sessions;   // kSessions, zeroed, in PSRAM (start()): none of it takes internal RAM; null, no resuming
int nextSession = 0;

Session* sessionFor(const char* host, bool add) {
    if (!sessions) return nullptr;
    for (int k = 0; k < kSessions; ++k)
        if (sessions[k].data && strcmp(sessions[k].host, host) == 0) return &sessions[k];
    if (!add) return nullptr;
    Session& s = sessions[nextSession++ % kSessions];
    if (!s.data) s.data = static_cast<uint8_t*>(heap_caps_malloc(kSessionMax, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
    if (!s.data) return nullptr;
    snprintf(s.host, sizeof(s.host), "%s", host);
    s.len = 0;
    return &s;
}

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

// The response body, read into a String reserved to its full size up front and never grown past it: arduino-esp32
// 3.2.0's String keeps the old length in 16 bits while it grows (WString.cpp, changeBuffer), so a body that grew past
// 64 KB came out mangled (route 22's patterns are 70 KB, sent chunked). Appends within the reserve never regrow it.
class BodySink : public Stream {   // write-only: HTTPClient::writeToStream takes a Stream
public:
    BodySink(String& out, size_t cap) : out_(out), cap_(cap) {}
    int available() override { return 0; }
    int read() override { return -1; }
    int peek() override { return -1; }
    size_t write(uint8_t c) override { return write(&c, 1); }
    size_t write(const uint8_t* p, size_t n) override {
        if (out_.length() + n >= cap_) {
            over = true;
            return 0;
        }
        out_.concat(reinterpret_cast<const char*>(p), n);
        return n;
    }
    bool over = false;

private:
    String& out_;
    size_t cap_;
};

bool readBody(String& out) {
    const int size = conn->http.getSize();   // -1 when sent without a length (chunked)
    const size_t cap = size > 0 ? static_cast<size_t>(size) + 1 : kMaxBody;
    out = String();
    if (!out.reserve(cap)) return false;
    BodySink sink(out, cap);
    const int n = conn->http.writeToStream(&sink);
    if (n >= 0 && !sink.over) return true;
    Serial.printf("net: response body not read (%d%s)\n", n, sink.over ? ", over the size limit" : "");
    out = String();
    return false;
}

enum class Open : uint8_t { Ok, Failed, FailedResuming };

// Opens conn to host:443: DNS, TCP, then the TLS handshake, resuming the host's last session if there is one. The
// timeouts are the ones connect() had (TCP, then the handshake). kind is for the perf log, as in fetch().
Open openConn(const char* host, char kind) {
    IPAddress ip;
    if (!resolve(host, ip)) return Open::Failed;
    conn->tls.setPlainStart();   // the TCP connection and the TLS setup now, the handshake below
    if (!conn->tls.connect(ip, 443, host, nullptr, nullptr, nullptr)) return Open::Failed;
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
    // Keep the session, straight from the connection (no copy in internal RAM); a resumed one may have brought a new
    // ticket. A session too big for its slot isn't kept: the bus API's tickets are about 4 KB, so its handshakes stay
    // full ones (RSA, about 0.5 s). A resumed session keeps its first handshake's start time; a full handshake starts
    // a new one.
    const mbedtls_ssl_session* now = conn->tls.ssl()->MBEDTLS_PRIVATE(session);
    const bool resumed = now && offered && now->MBEDTLS_PRIVATE(start) == offeredStart;
    size_t n = 0;
    if (now && mbedtls_ssl_session_save(now, nullptr, 0, &n) == MBEDTLS_ERR_SSL_BUFFER_TOO_SMALL && n <= kSessionMax) {
        if (!s) s = sessionFor(host, true);
        if (s) s->len = mbedtls_ssl_session_save(now, s->data, kSessionMax, &n) == 0 ? n : 0;
    } else if (s) {
        s->len = 0;   // the old one is no use now
    }
#ifdef PERF_LOG
    perf::event("tls,%c,%s", kind, resumed ? "resumed" : "full");
#else
    (void)kind;
    (void)resumed;
#endif
    return Open::Ok;
}

// GETs url into body (unless null). Returns the HTTP status or a negative HTTPClient error. Never logs the URL (it
// carries keys). keep: leave the connection open for the next request to the same host. kind is for the perf log.
int fetchNow(const char* url, String* body, bool keep, char kind) {
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
        if (!reused) {
            const Open o = openConn(host, kind);
            if (o != Open::Ok) {
                closeConn();
                snprintf(conn->host, sizeof(conn->host), "%s", host);
                if (o == Open::FailedResuming && !fullRetried) {   // once more, with a full handshake
                    fullRetried = true;
                    continue;
                }
                return HTTPC_ERROR_CONNECTION_REFUSED;
            }
        }
        int code = conn->http.GET();
        if (code == 200 && body && !readBody(*body)) code = HTTPC_ERROR_TOO_LESS_RAM;
        int64_t t;
        if (code > 0 && cta::parseHttpDate(conn->http.header("Date").c_str(), t)) conn->date = t;
        conn->http.end();
        conn->lastUsed = millis();
        if (code == 200 && body) {
            if (!keep) closeConn();
            return code;
        }
        closeConn();
        snprintf(conn->host, sizeof(conn->host), "%s", host);
        if (code < 0 && reused && attempt == 0) continue;   // the server had closed the idle connection
        return code;
    }
}

#ifdef PERF_LOG
// The request's kind for the perf log, from its host (never the URL itself: it carries keys).
char kindOf(const char* url, bool hasBody) {
    if (strstr(url, "lapi.")) return hasBody ? 't' : 'c';
    if (strstr(url, "bustracker")) return 'b';
    if (strstr(url, "open-meteo")) return 'w';
    if (strstr(url, "ipapi")) return 'g';
    return 'a';
}
#endif
int fetch(const char* url, String* body, bool keep) {
#ifdef PERF_LOG
    const char kind = kindOf(url, body != nullptr);
    perf::reqStart(kind);
    const uint32_t t0 = millis();
    const int code = fetchNow(url, body, keep, kind);
    perf::reqEnd(kind, code, body ? body->length() : 0, millis() - t0);
    return code;
#else
    return fetchNow(url, body, keep, 0);
#endif
}

int get(const char* url, String& body, bool keep = false, bool cta = true) {
    if (cta) nRequests = nRequests + 1;
    return fetch(url, &body, keep);
}

// The clock from a response's Date header, for networks that block NTP.
bool clockFromDate() {
    if (!conn->date) return false;
    const timeval tv = {static_cast<time_t>(conn->date), 0};
    settimeofday(&tv, nullptr);
    Serial.println("net: clock set from HTTP (no NTP answer)");
    return true;
}

// The serial "w" check: is the dock's network on the air? Prints only whether it's seen and on what channel, signal
// and security, plus the configured name's length and whether it has stray spaces: never a network name or password.
void reportScan() {
    const char* want = DOCK_WIFI_SSID;
    const size_t len = strlen(want);
    const bool spaces = len && (want[0] == ' ' || want[len - 1] == ' ' || want[len - 1] == '\t' || want[len - 1] == '\r');
    Serial.printf("wifi check: configured name is %u bytes%s\n", static_cast<unsigned>(len), spaces ? ", with a leading or trailing space" : "");
    const int n = WiFi.scanNetworks();
    int seen = 0, nearMiss = 0;
    for (int i = 0; i < n; ++i) {
        const String ssid = WiFi.SSID(i);
        if (ssid == want) {
            ++seen;
            Serial.printf("wifi check: seen on channel %d, RSSI %d, auth %d\n", WiFi.channel(i), WiFi.RSSI(i),
                          static_cast<int>(WiFi.encryptionType(i)));
        } else if (ssid.equalsIgnoreCase(want) || (ssid.length() && ssid.indexOf(want) >= 0) ||
                   (len && String(want).indexOf(ssid) >= 0 && ssid.length() >= 3)) {
            ++nearMiss;   // differs only in case, or one contains the other
        }
    }
    Serial.printf("wifi check: %d networks on 2.4 GHz; the dock's %s%s\n", n, seen ? "is there" : "is NOT seen",
                  nearMiss ? " (but a near match is: check upper/lower case and extra characters)" : "");
    WiFi.scanDelete();
}

// ---------- picks ----------

void clearSection(int i) {
    dock::Section& s = building->sec[i];
    s.pick = secs[i].pick;
    memset(&s.rec, 0, sizeof(s.rec));
    s.mask = 0;
    s.posTs = 0;
    memset(s.zone, 0, sizeof(s.zone));
    building->busPathN[i] = 0;
}

void applyPicks(const cta::PickedStop* picks) {
    for (int i = 0; i < cta::kDockStops; ++i) {
        const cta::PickedStop& p = picks[i];
        Sec& s = secs[i];
        const bool same = memcmp(&p, &s.pick, sizeof(p)) == 0;
        if (same && s.pick.id[0]) continue;
        s = Sec();
        s.pick = p;
        clearSection(i);
        if (!p.id[0]) continue;
        if (p.type == cta::Kind::Train) {
            s.nRows = cta::trainRowFor(p.route, p.id, s.rows[0]) ? 1 : 0;
            s.rowsReady = true;
            if (s.nRows)
                for (int k = 0; k < 7; ++k) building->sec[i].zone[k] = {s.rows[0].lat[k], s.rows[0].lon[k]};
            else Serial.printf("net: section %d: platform %s isn't in the train tables; no LED row\n", i + 1, p.id);
        }
    }
    busPredPolled = 0;
    alertsPolled = 0;
}

// New picks from the loop, if any: applied to the sections. True if there were some.
bool takePendingPicks() {
    if (!picksPending) return false;
    static cta::PickedStop p[cta::kDockStops];
    xSemaphoreTake(mu, portMAX_DELAY);
    memcpy(p, pendingPicks, sizeof(p));
    picksPending = false;
    xSemaphoreGive(mu);
    applyPicks(p);
    return true;
}

// ---------- the polls (spec 6.6) ----------

uint32_t every(uint32_t sec) { return slow && sec < cta::kSlowPollSec ? cta::kSlowPollSec : sec; }
uint32_t retryStamp(uint32_t now, uint32_t sec) { return now - every(sec) + kRetrySec; }
bool due(uint32_t polled, uint32_t sec, uint32_t now) { return cta::pollDue(polled, sec, now, slow); }

void pollPatterns(int i, uint32_t now) {
    Sec& s = secs[i];
    s.patTried = now ? now : 1;
    static char url[256];
    if (!cta::busPatternsUrl(url, sizeof(url), CTA_BUS_KEY, s.pick.route)) return;
    String body;
    const int code = get(url, body);
    const int n = code == 200 ? cta::buildBusRows(body.c_str(), body.length(), s.pick.id, s.rows, kRows) : -1;
    Serial.printf("net: patterns for route %s: HTTP %d, %d rows\n", s.pick.route, code, n);
    if (n < 0) return;
    s.nRows = n;
    s.rowsReady = true;
    if (n) {
        for (int k = 0; k < 7; ++k) building->sec[i].zone[k] = {s.rows[0].lat[k], s.rows[0].lon[k]};
        const int m = cta::busPatternPath(body.c_str(), body.length(), s.rows[0].pid, building->busPath[i], dock::kMaxPath);
        building->busPathN[i] = static_cast<uint16_t>(m > 0 ? m : 0);
    }
}

void pollTrainArrivals(int i, uint32_t now) {
    Sec& s = secs[i];
    s.arrPolled = now;
    static char url[256];
    // rt= and the parser's filter: Diversey's and Belmont's Loop-bound platforms serve Brown and Purple alike.
    if (!cta::trainArrivalsUrl(url, sizeof(url), CTA_TRAIN_KEY, s.pick.id, s.pick.route)) return;
    String body;
    const uint32_t t0 = millis();
    const int code = get(url, body, true);
    cta::StopRecord r;
    memset(&r, 0, sizeof(r));
    snprintf(r.id, sizeof(r.id), "%s", s.pick.id);
    snprintf(r.route, sizeof(r.route), "%s", s.pick.route);
    snprintf(r.name, sizeof(r.name), "%s", s.pick.name);
    snprintf(r.dir, sizeof(r.dir), "%.24s", s.pick.dir);
    const bool ok = code == 200 && cta::parseTrainArrivals(body.c_str(), body.length(), now, r, s.pick.route);
    Serial.printf("net: trains %s: HTTP %d, %d arrivals, %u ms\n", s.pick.id, code, ok ? r.count : -1,
                  static_cast<unsigned>(millis() - t0));
    if (!ok) { s.arrPolled = retryStamp(now, cta::kDockPollSec); return; }
    r.alert = s.alerted;
    building->sec[i].rec = r;
}

// One ttpositions call per line serves every section on it.
void pollTrainPositions(int i, uint32_t now) {
    static char url[256];
    static cta::TrainPos pos[kMaxTrains];
    const char* route = secs[i].pick.route;
    for (Sec& s : secs)
        if (hasPick(s) && !isBus(s) && strcmp(s.pick.route, route) == 0) s.posPolled = now;
    if (!cta::trainPositionsUrl(url, sizeof(url), CTA_TRAIN_KEY, route)) return;
    String body;
    const int code = get(url, body, true);
    const int n = code == 200 ? cta::parseTrainPositions(body.c_str(), body.length(), pos, kMaxTrains) : -1;
    Serial.printf("net: positions %s: HTTP %d, %d trains\n", route, code, n);
    for (int j = 0; j < cta::kDockStops; ++j) {
        Sec& s = secs[j];
        if (!hasPick(s) || isBus(s) || strcmp(s.pick.route, route) != 0) continue;
        if (n < 0) { s.posPolled = retryStamp(now, cta::kDockPollSec); continue; }
        building->sec[j].mask = s.nRows ? cta::litMaskTrains(s.rows[0], pos, n) : 0;
        building->sec[j].posTs = now;
    }
}

// All picked bus stops in one getpredictions call, each section taking its own route and direction (spec 6.2).
void pollBusArrivals(uint32_t now) {
    busPredPolled = now;
    const char* ids[cta::kDockStops];
    int n = 0;
    for (const Sec& s : secs) {
        if (!hasPick(s) || !isBus(s)) continue;
        bool dup = false;
        for (int k = 0; k < n; ++k) dup |= strcmp(ids[k], s.pick.id) == 0;
        if (!dup) ids[n++] = s.pick.id;
    }
    static char url[256];
    if (!n || !cta::busPredictionsUrl(url, sizeof(url), CTA_BUS_KEY, ids, n)) return;
    String body;
    const int code = get(url, body);
    cta::StopRecord* recs = busRecs;
    const int got = code == 200 ? cta::parseBusPredictions(body.c_str(), body.length(), now, nullptr, recs, kMaxBusRecs) : -1;
    Serial.printf("net: buses, %d stops: HTTP %d, %d records\n", n, code, got);
    if (got < 0) { busPredPolled = retryStamp(now, cta::kDockPollSec); return; }
    for (int i = 0; i < cta::kDockStops; ++i) {
        const Sec& s = secs[i];
        if (!hasPick(s) || !isBus(s)) continue;
        cta::StopRecord r;
        memset(&r, 0, sizeof(r));
        r.type = cta::Kind::Bus;
        snprintf(r.id, sizeof(r.id), "%s", s.pick.id);
        snprintf(r.route, sizeof(r.route), "%s", s.pick.route);
        snprintf(r.dir, sizeof(r.dir), "%.24s", s.pick.dir);
        snprintf(r.name, sizeof(r.name), "%s", s.pick.name);
        r.ts = now;   // no record for it: no buses predicted right now
        for (int k = 0; k < got; ++k)
            if (strcmp(recs[k].id, s.pick.id) == 0 && strcmp(recs[k].route, s.pick.route) == 0 &&
                (!s.pick.dir[0] || strncmp(recs[k].dir, s.pick.dir, cta::kNameLen) == 0)) {
                r = recs[k];
                break;
            }
        r.alert = s.alerted;
        building->sec[i].rec = r;
    }
}

void pollBusPositions(uint32_t now) {
    const char* routes[cta::kDockStops];
    int n = 0;
    for (Sec& s : secs) {
        if (!hasPick(s) || !isBus(s)) continue;
        s.posPolled = now;
        bool dup = false;
        for (int k = 0; k < n; ++k) dup |= strcmp(routes[k], s.pick.route) == 0;
        if (!dup) routes[n++] = s.pick.route;
    }
    static char url[256];
    cta::BusVehicle* v = busVehicles;
    if (!n || !cta::busVehiclesUrl(url, sizeof(url), CTA_BUS_KEY, routes, n)) return;
    String body;
    const int code = get(url, body);
    const int got = code == 200 ? cta::parseBusVehicles(body.c_str(), body.length(), v, kMaxBuses) : -1;
    Serial.printf("net: bus positions, %d routes: HTTP %d, %d buses\n", n, code, got);
    for (int i = 0; i < cta::kDockStops; ++i) {
        Sec& s = secs[i];
        if (!hasPick(s) || !isBus(s)) continue;
        if (got < 0) { s.posPolled = retryStamp(now, cta::kDockPollSec); continue; }
        building->sec[i].mask = cta::litMaskBuses(s.rows, s.nRows, v, got);
        building->sec[i].posTs = now;
    }
}

void pollAlerts(uint32_t now) {
    alertsPolled = now;
    const char* routes[cta::kDockStops];
    int n = 0;
    for (const Sec& s : secs) {
        if (!hasPick(s)) continue;
        bool dup = false;
        for (int k = 0; k < n; ++k) dup |= strcmp(routes[k], s.pick.route) == 0;
        if (!dup) routes[n++] = s.pick.route;
    }
    static char url[256];
    if (!n || !cta::alertsUrl(url, sizeof(url), routes, n, true)) return;
    String body;
    const int code = get(url, body, false, false);
    bool alerted[cta::kDockStops] = {};
    const int got = code == 200 ? cta::parseAlertRoutes(body.c_str(), body.length(), routes, n, alerted) : -1;
    Serial.printf("net: alerts, %d routes: HTTP %d, %d alerted\n", n, code, got);
    if (got < 0) { alertsPolled = retryStamp(now, cta::kAlertsPollSec); return; }
    for (int i = 0; i < cta::kDockStops; ++i) {
        Sec& s = secs[i];
        s.alerted = false;
        for (int k = 0; k < n && hasPick(s); ++k)
            if (strcmp(routes[k], s.pick.route) == 0) s.alerted = alerted[k];
        building->sec[i].rec.alert = s.alerted;
    }
}

// The dock's approximate location from its public IP (spec 7.5), at boot and once a day, kept in flash. The
// coordinates are never logged.
void pollGeo(uint32_t now) {
    geoTried = now;
    static char url[64];
    if (!cta::geoIpUrl(url, sizeof(url))) return;
    String body;
    const int code = get(url, body, false, false);
    double lat, lon;
    const bool ok = code == 200 && cta::parseGeoIp(body.c_str(), body.length(), lat, lon);
    Serial.printf("net: location lookup: HTTP %d, %s\n", code, ok ? "ok" : hasGeo ? "failed, keeping the saved one" : "failed");
    geoBooted = true;
    if (!ok) return;   // keep the saved location; cta::geoLookupDue tries again in kGeoRetrySec
    const bool worth = cta::geoWorthSaving(hasGeo, geoLat, geoLon, static_cast<float>(lat), static_cast<float>(lon));
    hasGeo = true;
    geoLat = static_cast<float>(lat);
    geoLon = static_cast<float>(lon);
    geoWhen = now;
    // Written only when it moved over 1 km: no needless flash writes.
    if (worth) store::saveGeo(geoLat, geoLon, geoWhen);
    weatherPolled = 0;
}

void pollWeather(uint32_t now) {
    weatherPolled = now;
    static char url[160];
    if (!cta::weatherUrl(url, sizeof(url), geoLat, geoLon)) return;
    String body;
    const int code = get(url, body, false, false);
    int wmo;
    float t;
    const bool ok = code == 200 && cta::parseWeather(body.c_str(), body.length(), wmo, t);
    Serial.printf("net: weather: HTTP %d, %s\n", code, ok ? "ok" : "failed");
    if (!ok) { weatherPolled = retryStamp(now, cta::kWeatherPollSec); return; }
    building->hasWeather = true;
    building->wmo = wmo;
    building->tempC = t;
}

// One poll per call: bus patterns first (a new pick's LED row), then the train requests together (one connection),
// then the buses, the weather, and the alerts. False when nothing was due.
bool pollOne(uint32_t now) {
    for (int i = 0; i < cta::kDockStops; ++i) {
        Sec& s = secs[i];
        if (hasPick(s) && isBus(s) && !s.rowsReady && (s.patTried == 0 || now - s.patTried >= kRetrySec)) {
            pollPatterns(i, now);
            return true;
        }
    }
    for (int i = 0; i < cta::kDockStops; ++i)
        if (hasPick(secs[i]) && !isBus(secs[i]) && due(secs[i].arrPolled, cta::kDockPollSec, now)) { pollTrainArrivals(i, now); return true; }
    for (int i = 0; i < cta::kDockStops; ++i)
        if (hasPick(secs[i]) && !isBus(secs[i]) && due(secs[i].posPolled, cta::kDockPollSec, now)) { pollTrainPositions(i, now); return true; }
    bool anyBus = false, busPosDue = false;
    for (const Sec& s : secs)
        if (hasPick(s) && isBus(s)) { anyBus = true; busPosDue |= s.rowsReady && due(s.posPolled, cta::kDockPollSec, now); }
    if (anyBus && due(busPredPolled, cta::kDockPollSec, now)) { pollBusArrivals(now); return true; }
    if (busPosDue) { pollBusPositions(now); return true; }
    // At boot whatever is saved, then once a day (spec 7.5).
    if (cta::geoLookupDue(geoBooted, hasGeo ? geoWhen : 0, geoTried, now)) {
        pollGeo(now);
        return true;
    }
    if (hasGeo && due(weatherPolled, cta::kWeatherPollSec, now)) { pollWeather(now); return true; }
    bool any = false;
    for (const Sec& s : secs) any |= hasPick(s);
    if (any && due(alertsPolled, cta::kAlertsPollSec, now)) { pollAlerts(now); return true; }
    return false;
}

#ifdef PERF_LOG
// The network burst (stress phases 5 and 6): marks the next thing in rotation as never polled, so pollOne fetches it
// now. Trains (arrivals, positions), buses (arrivals, positions), weather, alerts.
void burstNext() {
    static int k = 0;
    for (int tries = 0; tries < 16; ++tries, ++k) {
        const int slot = k % 16;
        if (slot < 6) {
            Sec& s = secs[slot / 2];
            if (!hasPick(s)) continue;
            if (isBus(s)) {
                if (slot % 2 == 0) busPredPolled = 0;
                else if (s.rowsReady) s.posPolled = 0;
                else continue;
            } else {
                if (slot % 2 == 0) s.arrPolled = 0;
                else s.posPolled = 0;
            }
        } else if (slot == 6) {
            if (!hasGeo) continue;
            weatherPolled = 0;
        } else if (slot == 7) {
            alertsPolled = 0;
        } else {
            continue;
        }
        ++k;
        return;
    }
}
#endif

// ---------- the picker's lists ----------
void serveRequest(const Req& r) {
    static char url[256];
    String body;
    int code = -1, n = -1;
    switch (r.what) {
        case dock::Request::Routes:
            lists->routesState = dock::Load::Loading;
            publishLists();
            if (cta::busRoutesUrl(url, sizeof(url), CTA_BUS_KEY)) code = get(url, body);
            n = code == 200 ? cta::parseBusRoutes(body.c_str(), body.length(), lists->routes, dock::PickerLists::kRoutes) : -1;
            lists->nRoutes = n > 0 ? n : 0;
            lists->routesState = n >= 0 ? dock::Load::Ready : dock::Load::Failed;
            break;
        case dock::Request::Dirs:
            lists->dirsState = dock::Load::Loading;
            publishLists();
            if (cta::busDirectionsUrl(url, sizeof(url), CTA_BUS_KEY, r.route)) code = get(url, body);
            n = code == 200 ? cta::parseBusDirections(body.c_str(), body.length(), lists->dirs, dock::PickerLists::kDirs) : -1;
            lists->nDirs = n > 0 ? n : 0;
            snprintf(lists->dirsRoute, sizeof(lists->dirsRoute), "%s", r.route);
            lists->dirsState = n >= 0 ? dock::Load::Ready : dock::Load::Failed;
            break;
        case dock::Request::Stops:
            lists->stopsState = dock::Load::Loading;
            publishLists();
            if (cta::busStopsUrl(url, sizeof(url), CTA_BUS_KEY, r.route, r.dir)) code = get(url, body);
            n = code == 200 ? cta::parseBusStops(body.c_str(), body.length(), lists->stops, dock::PickerLists::kStops) : -1;
            lists->nStops = n > 0 ? n : 0;
            snprintf(lists->stopsRoute, sizeof(lists->stopsRoute), "%s", r.route);
            snprintf(lists->stopsDir, sizeof(lists->stopsDir), "%s", r.dir);
            lists->stopsState = n >= 0 ? dock::Load::Ready : dock::Load::Failed;
            break;
        case dock::Request::None: return;
    }
    Serial.printf("net: picker list %d: HTTP %d, %d entries\n", static_cast<int>(r.what), code, n);
    publishLists();
}

void netTask(void*) {
    conn = new Conn();
    conn->tls.setCACertBundle(kCaBundleStart, kCaBundleEnd - kCaBundleStart);
    conn->tls.setHandshakeTimeout(12);
    conn->tls.setConnectionTimeout(8000);
    conn->tls.setTimeout(12000);
    conn->http.setTimeout(12000);
    conn->http.setConnectTimeout(8000);
    static const char* dateHeader[] = {"Date"};
    conn->http.collectHeaders(dateHeader, 1);
    // Before mode(), which sets up the driver's storage: keep the network in RAM, not flash, so a join doesn't write
    // flash (a flash write stalls the panel's refills).
    WiFi.persistent(false);
    WiFi.mode(WIFI_STA);
    WiFi.onEvent([](arduino_event_id_t, arduino_event_info_t info) {
        static uint32_t lastMs = 0;
        joinFailed = true;   // ends a join attempt at once (netTask)
        if (millis() - lastMs >= 1000) Serial.printf("net: Wi-Fi disconnected, reason %u\n", info.wifi_sta_disconnected.reason);
        lastMs = millis();
    }, ARDUINO_EVENT_WIFI_STA_DISCONNECTED);
    WiFi.setAutoReconnect(false);   // this loop backs off instead of the driver rescanning nonstop
    startSntp();
    uint32_t lastBegin = 0, lastEnd = 0, joinFailures = 0, clockWait = 0;   // tries failed in a row: cta::wifiRetryDelayMs
    bool wasUp = false, joining = false;
    for (;;) {
        if (takePendingPicks()) publish();
        if (conn->host[0] && millis() - conn->lastUsed > kIdleCloseMs) closeConn();
        if (scanWanted) {
            scanWanted = false;
            reportScan();
        }
        if (wifiOff) {   // diagnostic: the radio off, nothing polled, until it's turned back on
            static bool off = false;
            if (!off) {
                off = true;
                closeConn();
                WiFi.disconnect(true);
                WiFi.mode(WIFI_OFF);
                setStatus("Wi-Fi off (diagnostic)");
                publish();
                Serial.println("net: Wi-Fi off (diagnostic)");
            }
            vTaskDelay(pdMS_TO_TICKS(200));
            if (wifiOff) continue;
            off = false;
            WiFi.mode(WIFI_STA);
            joinFailures = 0;
            joining = false;
            Serial.println("net: Wi-Fi back on");
        }
        if (WiFi.status() != WL_CONNECTED) {
            if (wasUp) { Serial.println("net: Wi-Fi lost"); joinFailures = 0; closeConn(); }
            wasUp = false;
            if (joining && (joinFailed || millis() - lastBegin > kWifiJoinMs)) {   // this try failed
                joining = false;
                ++joinFailures;
                lastEnd = millis();
                Serial.printf("net: couldn't join the dock's Wi-Fi (status %d); next try in %u s\n", static_cast<int>(WiFi.status()),
                              static_cast<unsigned>(cta::wifiRetryDelayMs(joinFailures) / 1000));
                WiFi.disconnect();
            }
            if (!joining && (joinFailures == 0 || millis() - lastEnd >= cta::wifiRetryDelayMs(joinFailures))) {
                Serial.println("net: joining the dock's Wi-Fi");
                joinFailed = false;
                WiFi.begin(DOCK_WIFI_SSID, DOCK_WIFI_PASS);
                lastBegin = millis();
                joining = true;
            }
            setStatus(joining ? "Connecting to Wi-Fi" : "No Wi-Fi");
            publish();
            vTaskDelay(pdMS_TO_TICKS(500));
            continue;
        }
        joining = false;
        joinFailures = 0;
        if (!wasUp) {
            wasUp = true;
            Serial.printf("net: Wi-Fi up, RSSI %d\n", WiFi.RSSI());
            if (!clockSet()) startSntp();   // its first request went out before there was a link
            clockWait = millis();
            publish();
        }
        if (reqPending) {   // the picker waits on a person: before any poll
            Req r;
            xSemaphoreTake(mu, portMAX_DELAY);
            r = pendingReq;
            reqPending = false;
            xSemaphoreGive(mu);
            serveRequest(r);
            continue;
        }
        if (!clockSet()) {
            // NTP, or after 10 s the train API's Date header, every 30 s until something works.
            static uint32_t lastHttpTry = 0;
            setStatus("Setting the clock");
            publish();
            if (millis() - clockWait > 10000 && (!lastHttpTry || millis() - lastHttpTry > 30000)) {
                lastHttpTry = millis();
                fetch("https://lapi.transitchicago.com/", nullptr, false);
                clockFromDate();
            }
            vTaskDelay(pdMS_TO_TICKS(500));
            continue;
        }
        if (building->status[0]) { setStatus(""); publish(); }
#ifdef PERF_LOG
        if (perf::burstDue()) burstNext();
#endif
        if (pollOne(nowUnix())) {
            takePendingPicks();   // picks made during that fetch: never publish the old ones over them
            publish();
            vTaskDelay(pdMS_TO_TICKS(50));
        } else {
            vTaskDelay(pdMS_TO_TICKS(250));
        }
    }
}

}  // namespace

void start(const cta::PickedStop picks[cta::kDockStops]) {
    mbedtls_platform_set_calloc_free(tlsCalloc, heap_caps_free);   // before any TLS: blocks are freed where they were made
    mu = xSemaphoreCreateMutex();
    dnsDone = xSemaphoreCreateBinary();
    published = new (ps_malloc(sizeof(dock::Data))) dock::Data();
    building = new (ps_malloc(sizeof(dock::Data))) dock::Data();
    pubLists = new (ps_malloc(sizeof(dock::PickerLists))) dock::PickerLists();
    lists = new (ps_malloc(sizeof(dock::PickerLists))) dock::PickerLists();
    busRecs = static_cast<cta::StopRecord*>(ps_malloc(sizeof(cta::StopRecord) * kMaxBusRecs));
    busVehicles = static_cast<cta::BusVehicle*>(ps_malloc(sizeof(cta::BusVehicle) * kMaxBuses));
    sessions = static_cast<Session*>(ps_calloc(kSessions, sizeof(Session)));   // none: every handshake a full one
    if (!published || !building || !pubLists || !lists || !busRecs || !busVehicles) {
        Serial.println("net: out of memory");
        return;
    }
    building->keys = CTA_TRAIN_KEY[0] && CTA_BUS_KEY[0];
    building->wifiSetUp = DOCK_WIFI_SSID[0] != 0;
    uint8_t mac[6];
    esp_read_mac(mac, ESP_MAC_WIFI_STA);   // from eFuse: Wi-Fi isn't started yet
    snprintf(building->name, sizeof(building->name), "CTA-Dock-%02X%02X", mac[4], mac[5]);
    hasGeo = store::loadGeo(geoLat, geoLon, geoWhen);
    applyPicks(picks);
    if (!building->keys || !building->wifiSetUp) {
        Serial.printf("net: %s in firmware/secrets.h; staying offline\n", building->keys ? "no DOCK_WIFI_SSID" : "no CTA keys");
        setStatus(building->keys ? "No Wi-Fi set up" : "No API key");
        publish();
        return;
    }
    setStatus("Starting");
    publish();
    // Core 0, below the touch task: TLS handshakes take CPU but never hold up a frame (the loop runs on core 1).
    xTaskCreatePinnedToCore(netTask, "net", 12288, nullptr, 1, nullptr, 0);
}

void setPicks(const cta::PickedStop picks[cta::kDockStops]) {
    if (!mu) return;
    xSemaphoreTake(mu, portMAX_DELAY);
    memcpy(pendingPicks, picks, sizeof(pendingPicks));
    picksPending = true;
    xSemaphoreGive(mu);
}

void setSlow(bool s) { slow = s; }

void request(dock::Request what, const char* route, const char* dir) {
    if (!mu) return;
    xSemaphoreTake(mu, portMAX_DELAY);
    pendingReq.what = what;
    snprintf(pendingReq.route, sizeof(pendingReq.route), "%s", route ? route : "");
    snprintf(pendingReq.dir, sizeof(pendingReq.dir), "%s", dir ? dir : "");
    reqPending = true;
    xSemaphoreGive(mu);
}

bool read(dock::Data& out, uint32_t& version) {
    if (!published || version == pubVersion) return false;
    xSemaphoreTake(mu, portMAX_DELAY);
    out = *published;
    version = pubVersion;
    xSemaphoreGive(mu);
    return true;
}

bool readLists(dock::PickerLists& out, uint32_t& version) {
    if (!pubLists || version == listsVersion) return false;
    xSemaphoreTake(mu, portMAX_DELAY);
    out = *pubLists;
    version = listsVersion;
    xSemaphoreGive(mu);
    return true;
}

void checkWifi() { scanWanted = true; }
void setWifiOff(bool off) { wifiOff = off; }

uint32_t unixNow() { return clockSet() ? nowUnix() : 0; }
uint32_t requests() { return nRequests; }

}  // namespace net
