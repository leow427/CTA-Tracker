#include "net.h"
#include "serial_out.h"

#include <esp_crt_bundle.h>
#include <esp_event.h>
#include <esp_heap_caps.h>
#include <esp_http_client.h>
#include <esp_log.h>
#include <esp_mac.h>
#include <esp_netif.h>
#include <esp_sntp.h>
#include <esp_timer.h>
#include <esp_wifi.h>
#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>
#include <freertos/task.h>
#include <lwip/dns.h>
#include <lwip/ip_addr.h>
#include <lwip/sockets.h>
#include <lwip/tcpip.h>
#include <mbedtls/ssl.h>
#include <stdio.h>
#include <string.h>
#include <strings.h>
#include <sys/time.h>
#include <new>
#include <time.h>

#include "ahead.h"
#include "alerts_weather.h"
#include "approach.h"
#include "bus_parse.h"
#include "clock_ms.h"
#include "cta_config.h"
#include "link.h"
#include "local_time.h"
#ifdef PERF_LOG
#include "perf_log.h"
#endif
#include "schedule.h"
#include "secrets.h"
#include "store.h"
#include "train_parse.h"
#include "urls.h"
#include "wifi_list.h"

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
struct Extra { uint32_t id; int section; char vehicle[cta::kVehicleLen + 1]; };   // the touch extras (spec 5.10)
Extra pendingAhead, pendingPeek;
volatile bool aheadPending = false, peekPending = false;
volatile uint32_t nRequests = 0;
volatile bool joinFailed = false;
volatile bool scanWanted = false;   // the serial "w" check
volatile bool wifiOff = false;      // the diagnostic "Wi-Fi off" mode
// Serial "k": made-up arrivals and positions, nothing fetched (LED board tests; real data at boot since 2026-11-20).
volatile bool fakeData = false;
volatile uint8_t discReason = 0;    // the last disconnect's reason, and how many there have been (the event handler)
volatile uint32_t discCount = 0;
volatile bool gotIp = false;        // Wi-Fi joined with an IP (the event handler)
volatile bool lostIp = false;       // the lease ran out while joined: netTask rejoins
cta::SavedNet wifiNet = {};         // the network to join, from secrets.h
char hostLabel[16];                 // "cta-dock-xxxx", for mDNS

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
    bool hasOpp = false;             // a bus stop's closest stop the other way, from getpatterns (spec 5.10)
    cta::PickedStop opp;
    cta::ApproachRow oppRows[kRows]; // the other way's LED rows, for a peek (trains: the other platform's)
    int nOppRows = 0;
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

int wifiRssi() {
    wifi_ap_record_t ap;
    return esp_wifi_sta_get_ap_info(&ap) == ESP_OK ? ap.rssi : 0;
}

void publish() {
    building->wifiUp = gotIp;
    building->rssi = static_cast<int8_t>(gotIp ? wifiRssi() : 0);   // Settings' bars
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
void startSntp() {
    if (esp_sntp_enabled()) esp_sntp_stop();
    esp_sntp_setoperatingmode(ESP_SNTP_OPMODE_POLL);
    esp_sntp_setservername(0, "216.239.35.0");
    esp_sntp_setservername(1, "216.239.35.4");
    esp_sntp_setservername(2, "162.159.200.1");
    esp_sntp_init();
}

// ---------- Wi-Fi (esp_wifi; the event handlers only set flags, this task logs) ----------
esp_netif_t* sta = nullptr;

void onWifiEvent(void*, esp_event_base_t base, int32_t id, void* d) {   // the default event loop's task
    if (base == WIFI_EVENT && id == WIFI_EVENT_STA_DISCONNECTED) {
        gotIp = false;
        discReason = static_cast<wifi_event_sta_disconnected_t*>(d)->reason;
        discCount = discCount + 1;
        joinFailed = true;   // ends a join attempt at once (netTask)
    } else if (base == IP_EVENT && id == IP_EVENT_STA_LOST_IP) {
        gotIp = false;
        lostIp = true;
    } else if (base == IP_EVENT && id == IP_EVENT_STA_GOT_IP) {
        gotIp = true;
    }
}

bool wifiInit() {
    if (esp_netif_init() != ESP_OK) return false;
    const esp_err_t loop = esp_event_loop_create_default();
    if (loop != ESP_OK && loop != ESP_ERR_INVALID_STATE) return false;
    sta = esp_netif_create_default_wifi_sta();
    esp_netif_set_hostname(sta, hostLabel);
    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    if (esp_wifi_init(&cfg) != ESP_OK) return false;
    // The network in RAM, not flash, so a join never writes flash (a flash write stalls the panel's refills).
    esp_wifi_set_storage(WIFI_STORAGE_RAM);
    esp_event_handler_register(WIFI_EVENT, WIFI_EVENT_STA_DISCONNECTED, onWifiEvent, nullptr);
    esp_event_handler_register(IP_EVENT, IP_EVENT_STA_GOT_IP, onWifiEvent, nullptr);
    esp_event_handler_register(IP_EVENT, IP_EVENT_STA_LOST_IP, onWifiEvent, nullptr);
    return esp_wifi_set_mode(WIFI_MODE_STA) == ESP_OK && esp_wifi_start() == ESP_OK;
}

// A join, as Arduino's WiFi.begin set it up: a fast scan, the strongest AP of that name, WPA2 or better, PMF if
// offered. The driver never reconnects on its own: this task backs off instead (cta::wifiRetryDelayMs).
void wifiJoin() {
    wifi_config_t c = {};
    // All 32 / 64 bytes, unterminated when full, as the driver takes them (a 64-character PSK is the raw hex key).
    memcpy(c.sta.ssid, wifiNet.ssid, strnlen(wifiNet.ssid, sizeof(c.sta.ssid)));
    memcpy(c.sta.password, wifiNet.pass, strnlen(wifiNet.pass, sizeof(c.sta.password)));
    c.sta.scan_method = WIFI_FAST_SCAN;
    c.sta.sort_method = WIFI_CONNECT_AP_BY_SIGNAL;
    c.sta.threshold.rssi = -127;
    if (wifiNet.pass[0]) c.sta.threshold.authmode = WIFI_AUTH_WPA2_PSK;
    c.sta.pmf_cfg.capable = true;
    esp_wifi_set_config(WIFI_IF_STA, &c);
    esp_wifi_connect();
}

// ---------- DNS (the handheld's live.cpp) ----------
// Waits for lwIP's own answer and fails the request at once without one (Arduino's hostByName() used to report
// success with 0.0.0.0 after lwIP's retries). The answer stays in lwIP's cache, so esp_http_client's own lookup
// right after is instant.
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
bool resolve(const char* host) {
    ip4_addr_t lit;
    if (ip4addr_aton(host, &lit)) return true;
    while (xSemaphoreTake(dnsDone, 0) == pdTRUE) {}
    static uint32_t lastId = 0;
    if (++lastId == 0) ++lastId;
    const uint32_t id = lastId;
    snprintf(dnsName, sizeof(dnsName), "%s", host);
    dnsWant = id;
    bool ok = false;
    const uint32_t t0 = nowMs();
    if (tcpip_callback(dnsStart, reinterpret_cast<void*>(static_cast<uintptr_t>(id))) == ERR_OK) {
        for (;;) {
            const uint32_t spent = nowMs() - t0;
            if (spent >= kDnsWaitMs || xSemaphoreTake(dnsDone, pdMS_TO_TICKS(kDnsWaitMs - spent)) != pdTRUE) break;
            if (dnsGot != id) continue;
            ok = dnsAddr != 0;
            break;
        }
    }
    dnsWant = 0;
    return ok;
}

// ---------- HTTPS: one esp_http_client per host ----------
// Each host keeps its client, and with it its TLS session ticket (save_client_session): a reopened connection resumes
// the session when the server agrees, skipping the certificate checks and the key exchange (the train API's ECDSA
// chain costs about 2 s of core 0 in software). A server that declines just runs a full handshake, verified against
// the certificate bundle. Only one connection is open at a time: opening one closes any other host's (the ticket
// stays). The train requests of a poll pass share one kept-alive connection. mbedTLS allocates in PSRAM
// (CONFIG_MBEDTLS_EXTERNAL_MEM_ALLOC), so none of this needs the old tlsCalloc.
constexpr int kHosts = 6;              // trains, buses, alerts, weather, location, and one spare
constexpr uint32_t kIdleCloseMs = 60000;
constexpr int kMaxBody = 256 * 1024;   // the largest response read (PSRAM)
// The client's own buffers, over CONFIG_SPIRAM_MALLOC_ALWAYSINTERNAL so they go to PSRAM; 8 KB reads also mean fewer
// parser passes per body than the default 512 B.
constexpr int kRxBuf = 8192, kTxBuf = 4608;
constexpr int kTimeoutMs = 12000;

struct Host {
    char name[48];
    esp_http_client_handle_t h;
    bool open;           // a kept-alive connection
    bool ticket;         // it has connected, so it may offer a session ticket on the next connect
    uint32_t lastUsed;
};
Host* hosts;    // kHosts, PSRAM, zeroed
char* body;     // kMaxBody + 1, PSRAM: the last response's body
int bodyLen = 0;
int64_t lastDate = 0;       // the last response's Date header, Unix time (0 = none)
int64_t connectedUs = 0;    // when the last connect finished (0 = no new connection)
volatile bool certChecked = false;   // the last handshake checked the server's certificates: a full one, not resumed

// The bundle's own check, counted: esp_http_client doesn't say whether a handshake resumed, and only a full one
// checks certificates. esp_crt_verify_callback is the bundle's (esp_crt_bundle.c), not in its header.
extern "C" int esp_crt_verify_callback(void* buf, mbedtls_x509_crt* crt, int depth, uint32_t* flags);
int countedVerify(void* buf, mbedtls_x509_crt* crt, int depth, uint32_t* flags) {
    certChecked = true;
    return esp_crt_verify_callback(buf, crt, depth, flags);
}
esp_err_t attachBundle(void* conf) {
    // esp-tls calls this between the TCP connect and the handshake. Arduino set TCP_NODELAY on its TLS sockets here;
    // without it, Nagle holds each handshake flight for an ACK (about +300 ms a connection). Only one is open at a time.
    for (int fd = LWIP_SOCKET_OFFSET; fd < LWIP_SOCKET_OFFSET + CONFIG_LWIP_MAX_SOCKETS; ++fd) {
        sockaddr_in peer;
        socklen_t n = sizeof(peer);
        const int one = 1;
        if (getpeername(fd, reinterpret_cast<sockaddr*>(&peer), &n) == 0 && peer.sin_port == htons(443))
            setsockopt(fd, IPPROTO_TCP, TCP_NODELAY, &one, sizeof(one));
    }
    const esp_err_t e = esp_crt_bundle_attach(conf);
    if (e == ESP_OK) mbedtls_ssl_conf_verify(static_cast<mbedtls_ssl_config*>(conf), countedVerify, nullptr);
    return e;
}

esp_err_t onHttpEvent(esp_http_client_event_t* e) {   // this task, inside the client's calls
    if (e->event_id == HTTP_EVENT_ON_HEADER && strcasecmp(e->header_key, "Date") == 0) {
        int64_t t;
        if (cta::parseHttpDate(e->header_value, t)) lastDate = t;
    } else if (e->event_id == HTTP_EVENT_ON_CONNECTED) {
        connectedUs = esp_timer_get_time();
    }
    return ESP_OK;
}

void hostOf(const char* url, char* out, size_t cap) {
    const char* h = strstr(url, "://");
    h = h ? h + 3 : url;
    size_t n = strcspn(h, "/:?");
    if (n >= cap) n = cap - 1;
    memcpy(out, h, n);
    out[n] = 0;
}

void closeHost(Host& s) {
    if (s.h && s.open) esp_http_client_close(s.h);
    s.open = false;
}
void closeAll() {
    for (int k = 0; k < kHosts; ++k) closeHost(hosts[k]);
}

// Closes the host's client and frees it, its session ticket with it.
void dropHost(Host& s) {
    if (!s.h) return;
    closeHost(s);
    esp_http_client_cleanup(s.h);
    s.h = nullptr;
    s.ticket = false;
}

// The host's client, made on first use; with every slot taken, the least recently used one makes way.
Host* hostFor(const char* name, const char* url) {
    Host* lru = &hosts[0];
    Host* gap = nullptr;    // dropHost leaves gaps: every slot is checked for the host first
    for (int k = 0; k < kHosts; ++k) {
        Host& s = hosts[k];
        if (s.h && strcmp(s.name, name) == 0) return &s;
        if (!s.h && !gap) gap = &s;
        if (s.lastUsed < lru->lastUsed) lru = &s;
    }
    if (gap) lru = gap;
    dropHost(*lru);
    esp_http_client_config_t c = {};
    c.url = url;
    c.event_handler = onHttpEvent;
    c.crt_bundle_attach = attachBundle;   // TLS always verified (spec 11)
    c.timeout_ms = kTimeoutMs;
    c.buffer_size = kRxBuf;
    c.buffer_size_tx = kTxBuf;
    c.save_client_session = true;
    c.disable_auto_redirect = true;   // as HTTPClient: redirects aren't followed
    lru->h = esp_http_client_init(&c);
    if (!lru->h) return nullptr;
    snprintf(lru->name, sizeof(lru->name), "%s", name);
    lru->open = false;
    lru->ticket = false;
    lru->lastUsed = nowMs();
    return lru;
}

// The whole body into `body`; false if it failed, was cut short, or is over kMaxBody.
bool readBody(esp_http_client_handle_t h, int64_t len) {
    bodyLen = 0;
    body[0] = 0;
    if (len > kMaxBody) {
        uprintf("net: response body not read (%lld bytes, over the size limit)\n", static_cast<long long>(len));
        return false;
    }
    for (;;) {
        const int n = esp_http_client_read(h, body + bodyLen, kMaxBody - bodyLen);
        if (n < 0) break;
        bodyLen += n;
        if (n == 0 || bodyLen >= kMaxBody) break;
    }
    body[bodyLen] = 0;
    if (esp_http_client_is_complete_data_received(h)) return true;
    uprintf("net: response body not read (%d bytes%s)\n", bodyLen, bodyLen >= kMaxBody ? ", over the size limit" : "");
    bodyLen = 0;
    return false;
}

constexpr int64_t kQuickFailUs = 3000000;   // a resuming handshake that fails faster was refused, not lost

// HTTPClient's error numbers, which the perf log has always printed.
constexpr int kErrConnect = -1, kErrSend = -2, kErrLowRam = -8, kErrReadTimeout = -11;

// GETs url (into `body` if wantBody). Returns the HTTP status or a negative HTTPClient-style error. Never logs the
// URL (it carries keys). keep: leave the connection open for the next request to the same host. kind is for the perf
// log.
int fetchNow(const char* url, bool wantBody, bool keep, char kind) {
    lastDate = 0;
    bodyLen = 0;
    char name[sizeof(Host::name)];
    hostOf(url, name, sizeof(name));
    for (int k = 0; k < kHosts; ++k)
        if (hosts[k].open && strcmp(hosts[k].name, name) != 0) closeHost(hosts[k]);
    Host* s = hostFor(name, url);
    if (!s) return kErrLowRam;
    bool fullRetried = false;
    for (int attempt = 0;; ++attempt) {
        const bool reused = s->open;
        if (!reused && !resolve(name)) { uprintf("net: no address for %s (DNS)\n", name); return kErrConnect; }
        int code;
        connectedUs = 0;
        certChecked = false;
        const int64_t t0 = esp_timer_get_time();
        const esp_err_t err = esp_http_client_set_url(s->h, url) == ESP_OK ? esp_http_client_open(s->h, 0) : ESP_FAIL;
#ifdef PERF_LOG
        if (connectedUs) perf::event("tls,%c,%s", kind, certChecked ? "full" : "resumed");
#else
        (void)kind;
        (void)t0;
#endif
        if (connectedUs) s->ticket = true;
        if (err == ESP_ERR_HTTP_CONNECT && s->ticket && !fullRetried && esp_timer_get_time() - t0 < kQuickFailUs) {
            // The server refused the offered session: once more with a fresh client (no ticket), a full handshake.
            // A handshake that timed out (lost replies) would only time out again, so it isn't tried twice.
            fullRetried = true;
            dropHost(*s);
            s = hostFor(name, url);
            if (!s) return kErrLowRam;
            continue;
        }
        bool unread = false;   // headers in, body not read to its end
        if (err != ESP_OK) {
            uprintf("net: %s: connection failed, %s after %d ms\n", name, esp_err_to_name(err),
                    static_cast<int>((esp_timer_get_time() - t0) / 1000));
            code = err == ESP_ERR_HTTP_CONNECT ? kErrConnect : kErrSend;
        } else {
            const int64_t len = esp_http_client_fetch_headers(s->h);
            if (len < 0) code = kErrReadTimeout;
            else {
                code = esp_http_client_get_status_code(s->h);
                unread = !(code == 200 && wantBody);
                if (!unread && !readBody(s->h, len)) code = kErrLowRam, unread = true;
            }
        }
        s->lastUsed = nowMs();
        s->open = true;
        if (code == 200 && wantBody && keep) return code;
        // Body bytes that came in with the headers stay in the client and would lead its next response (close and
        // open keep them; only reading them, another host, or cleanup clears them), so an unread body costs the
        // client, its ticket with it.
        if (unread) dropHost(*s);
        else closeHost(*s);
        if (code < 0 && reused && attempt == 0) {   // the server had closed the idle connection
            if (!s->h && !(s = hostFor(name, url))) return kErrLowRam;
            continue;
        }
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
int fetch(const char* url, bool wantBody, bool keep) {
#ifdef PERF_LOG
    const char kind = kindOf(url, wantBody);
    perf::reqStart(kind);
    const uint32_t t0 = nowMs();
    const int code = fetchNow(url, wantBody, keep, kind);
    perf::reqEnd(kind, code, wantBody ? bodyLen : 0, nowMs() - t0);
    return code;
#else
    return fetchNow(url, wantBody, keep, 0);
#endif
}

// GETs url into `body` / `bodyLen`.
int get(const char* url, bool keep = false, bool cta = true) {
    if (cta) nRequests = nRequests + 1;
    return fetch(url, true, keep);
}

// The clock from a response's Date header, for networks that block NTP.
bool clockFromDate() {
    if (!lastDate) return false;
    const timeval tv = {static_cast<time_t>(lastDate), 0};
    settimeofday(&tv, nullptr);
    uprintf("net: clock set from HTTP (no NTP answer)\n");
    return true;
}

// The serial "w" check: is the dock's network on the air? Prints only whether it's seen and on what channel, signal
// and security, plus the configured name's length and whether it has stray spaces: never a network name or password.
void reportScan() {
    const char* want = wifiNet.ssid;
    const size_t len = strlen(want);
    const bool spaces = len && (want[0] == ' ' || want[len - 1] == ' ' || want[len - 1] == '\t' || want[len - 1] == '\r');
    uprintf("wifi check: configured name is %u bytes%s\n", static_cast<unsigned>(len), spaces ? ", with a leading or trailing space" : "");
    uint16_t n = 0;
    wifi_ap_record_t* aps = nullptr;
    if (esp_wifi_scan_start(nullptr, true) == ESP_OK && esp_wifi_scan_get_ap_num(&n) == ESP_OK && n) {
        aps = static_cast<wifi_ap_record_t*>(heap_caps_malloc(n * sizeof(wifi_ap_record_t), MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
        if (!aps || esp_wifi_scan_get_ap_records(&n, aps) != ESP_OK) n = 0;
    }
    int seen = 0, nearMiss = 0;
    for (int i = 0; i < n; ++i) {
        const char* ssid = reinterpret_cast<const char*>(aps[i].ssid);
        const size_t sl = strlen(ssid);
        if (strcmp(ssid, want) == 0) {
            ++seen;
            uprintf("wifi check: seen on channel %d, RSSI %d, auth %d\n", aps[i].primary, aps[i].rssi, static_cast<int>(aps[i].authmode));
        } else if (strcasecmp(ssid, want) == 0 || (sl && strstr(ssid, want)) || (len && sl >= 3 && strstr(want, ssid))) {
            ++nearMiss;   // differs only in case, or one contains the other
        }
    }
    uprintf("wifi check: %d networks on 2.4 GHz; the dock's %s%s\n", n, seen ? "is there" : "is NOT seen",
           nearMiss ? " (but a near match is: check upper/lower case and extra characters)" : "");
    heap_caps_free(aps);
    esp_wifi_clear_ap_list();
}

// Serial "k": every picked section gets 3 live arrivals and two vehicles walking down its LED row, every 10 s.
void fakeSections(uint32_t now) {
    static uint8_t step = 0;
    ++step;
    if (!clockSet()) {   // no network: a daytime clock, so the LED rows count as fresh (SNTP corrects it later)
        timeval tv = {1795111200, 0};
        settimeofday(&tv, nullptr);
        now = nowUnix();
    }
    for (int i = 0; i < cta::kDockStops; ++i) {
        cta::PickedStop& p = building->sec[i].pick;
        if (!p.id[0]) {   // an empty section gets a bus, so every LED row has something on it
            p = cta::PickedStop{};
            p.type = cta::Kind::Bus;
            snprintf(p.route, sizeof(p.route), "22");
            snprintf(p.dir, sizeof(p.dir), "Southbound");
            snprintf(p.id, sizeof(p.id), "1828");
            snprintf(p.name, sizeof(p.name), "Clark & Belmont");
        }
        cta::StopRecord r;
        memset(&r, 0, sizeof(r));
        r.type = p.type;
        snprintf(r.id, sizeof(r.id), "%s", p.id);
        snprintf(r.route, sizeof(r.route), "%s", p.route);
        snprintf(r.dir, sizeof(r.dir), "%.24s", p.dir);
        snprintf(r.name, sizeof(r.name), "%s", p.name);
        r.count = 3;
        for (int k = 0; k < 3; ++k) {
            cta::Arrival& a = r.arrivals[k];
            a.min = static_cast<int16_t>((k * 6 + i * 2 + 12 - step % 12) % 20);   // counts down, wraps
            snprintf(a.dest, sizeof(a.dest), "Test");
            a.live = true;
            snprintf(a.vehicle, sizeof(a.vehicle), "9%d%d", i, k);
        }
        r.ts = now;
        building->sec[i].rec = r;
        building->sec[i].mask = static_cast<uint8_t>((1u << ((step + i * 2) % 6)) | (1u << ((step + i * 2 + 3) % 6)));
        building->sec[i].posTs = now;
    }
}

// ---------- picks ----------

void clearSection(int i) {
    dock::Section& s = building->sec[i];
    s.pick = secs[i].pick;
    memset(&s.rec, 0, sizeof(s.rec));
    s.mask = s.peekMask = 0;
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
            else uprintf("net: section %d: platform %s isn't in the train tables; no LED row\n", i + 1, p.id);
            cta::PickedStop o;   // servePeek's platform the other way
            if (cta::oppositeTrainPlatform(p.route, p.id, p.dir, o)) s.nOppRows = cta::trainRowFor(o.route, o.id, s.oppRows[0]) ? 1 : 0;
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
// Retries count from when the failure ended, not from the fetch's start: a host that fails slowly (15–37 s) would
// otherwise be due again at once, and failing bus patterns (pollOne's first pick) could starve everything else.
uint32_t retryStamp(uint32_t sec) { return nowUnix() - every(sec) + kRetrySec; }
bool due(uint32_t polled, uint32_t sec, uint32_t now) { return cta::pollDue(polled, sec, now, slow); }

void pollPatterns(int i, uint32_t now) {
    Sec& s = secs[i];
    s.patTried = now ? now : 1;
    static char url[256];
    if (!cta::busPatternsUrl(url, sizeof(url), CTA_BUS_KEY, s.pick.route)) return;
    const int code = get(url);
    const int n = code == 200 ? cta::buildBusRows(body, bodyLen, s.pick.id, s.rows, kRows) : -1;
    uprintf("net: patterns for route %s: HTTP %d, %d rows\n", s.pick.route, code, n);
    if (n < 0) {
        // One bus host: every bus section still without rows backs off together, counted from the failure's end, so
        // their turns can't starve the train polls.
        const uint32_t t = nowUnix();
        for (Sec& o : secs)
            if (hasPick(o) && isBus(o) && !o.rowsReady) o.patTried = t ? t : 1;
        return;
    }
    s.nRows = n;
    s.rowsReady = true;
    s.opp = s.pick;
    s.hasOpp = cta::oppositeBusStop(body, bodyLen, s.pick.route, s.pick.id, s.pick.dir, s.opp);
    if (s.hasOpp) {   // getpatterns has both directions: the peek's rows come from the same reply
        const int m = cta::buildBusRows(body, bodyLen, s.opp.id, s.oppRows, kRows);
        s.nOppRows = m > 0 ? m : 0;
    }
    if (n) {
        for (int k = 0; k < 7; ++k) building->sec[i].zone[k] = {s.rows[0].lat[k], s.rows[0].lon[k]};
        const int m = cta::busPatternPath(body, bodyLen, s.rows[0].pid, building->busPath[i], dock::kMaxPath);
        building->busPathN[i] = static_cast<uint16_t>(m > 0 ? m : 0);
    }
}

void pollTrainArrivals(int i, uint32_t now) {
    Sec& s = secs[i];
    s.arrPolled = now;
    static char url[256];
    // rt= and the parser's filter: Diversey's and Belmont's Loop-bound platforms serve Brown and Purple alike.
    if (!cta::trainArrivalsUrl(url, sizeof(url), CTA_TRAIN_KEY, s.pick.id, s.pick.route)) return;
    const uint32_t t0 = nowMs();
    const int code = get(url, true);
    cta::StopRecord r;
    memset(&r, 0, sizeof(r));
    snprintf(r.id, sizeof(r.id), "%s", s.pick.id);
    snprintf(r.route, sizeof(r.route), "%s", s.pick.route);
    snprintf(r.name, sizeof(r.name), "%s", s.pick.name);
    snprintf(r.dir, sizeof(r.dir), "%.24s", s.pick.dir);
    const bool ok = code == 200 && cta::parseTrainArrivals(body, bodyLen, now, r, s.pick.route);
    uprintf("net: trains %s: HTTP %d, %d arrivals, %u ms\n", s.pick.id, code, ok ? r.count : -1,
                  static_cast<unsigned>(nowMs() - t0));
    if (!ok) { s.arrPolled = retryStamp(cta::kDockPollSec); return; }
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
    const int code = get(url, true);
    const int n = code == 200 ? cta::parseTrainPositions(body, bodyLen, pos, kMaxTrains) : -1;
    uprintf("net: positions %s: HTTP %d, %d trains\n", route, code, n);
    for (int j = 0; j < cta::kDockStops; ++j) {
        Sec& s = secs[j];
        if (!hasPick(s) || isBus(s) || strcmp(s.pick.route, route) != 0) continue;
        if (n < 0) { s.posPolled = retryStamp(cta::kDockPollSec); continue; }
        building->sec[j].mask = s.nRows ? cta::litMaskTrains(s.rows[0], pos, n) : 0;
        building->sec[j].peekMask = s.nOppRows ? cta::litMaskTrains(s.oppRows[0], pos, n) : 0;
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
    const int code = get(url);
    cta::StopRecord* recs = busRecs;
    const int got = code == 200 ? cta::parseBusPredictions(body, bodyLen, now, nullptr, recs, kMaxBusRecs) : -1;
    uprintf("net: buses, %d stops: HTTP %d, %d records\n", n, code, got);
    if (got < 0) { busPredPolled = retryStamp(cta::kDockPollSec); return; }
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
    const int code = get(url);
    const int got = code == 200 ? cta::parseBusVehicles(body, bodyLen, v, kMaxBuses) : -1;
    uprintf("net: bus positions, %d routes: HTTP %d, %d buses\n", n, code, got);
    for (int i = 0; i < cta::kDockStops; ++i) {
        Sec& s = secs[i];
        if (!hasPick(s) || !isBus(s)) continue;
        if (got < 0) { s.posPolled = retryStamp(cta::kDockPollSec); continue; }
        building->sec[i].mask = cta::litMaskBuses(s.rows, s.nRows, v, got);
        building->sec[i].peekMask = cta::litMaskBuses(s.oppRows, s.nOppRows, v, got);
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
    const int code = get(url, false, false);
    bool alerted[cta::kDockStops] = {};
    const int got = code == 200 ? cta::parseAlertRoutes(body, bodyLen, routes, n, alerted) : -1;
    uprintf("net: alerts, %d routes: HTTP %d, %d alerted\n", n, code, got);
    if (got < 0) { alertsPolled = retryStamp(cta::kAlertsPollSec); return; }
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
    const int code = get(url, false, false);
    double lat, lon;
    const bool ok = code == 200 && cta::parseGeoIp(body, bodyLen, lat, lon);
    uprintf("net: location lookup: HTTP %d, %s\n", code, ok ? "ok" : hasGeo ? "failed, keeping the saved one" : "failed");
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
    const int code = get(url, false, false);
    int wmo;
    float t;
    const bool ok = code == 200 && cta::parseWeather(body, bodyLen, wmo, t);
    uprintf("net: weather: HTTP %d, %s\n", code, ok ? "ok" : "failed");
    if (!ok) { weatherPolled = retryStamp(cta::kWeatherPollSec); return; }
    building->hasWeather = true;
    building->wmo = wmo;
    building->tempC = t;
}

// One poll per call: bus patterns first (a new pick's LED row), then the train requests together (one connection),
// then the buses, the weather, and the alerts. False when nothing was due.
bool pollOne(uint32_t now) {
    static int patNext = 0;   // rotates, so a section whose patterns always fail can't keep the others from a try
    for (int k = 0; k < cta::kDockStops; ++k) {
        const int i = (patNext + k) % cta::kDockStops;
        Sec& s = secs[i];
        if (hasPick(s) && isBus(s) && !s.rowsReady && (s.patTried == 0 || now - s.patTried >= kRetrySec)) {
            pollPatterns(i, now);
            patNext = (i + 1) % cta::kDockStops;
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
    int code = -1, n = -1;
    switch (r.what) {
        case dock::Request::Routes:
            lists->routesState = dock::Load::Loading;
            publishLists();
            if (cta::busRoutesUrl(url, sizeof(url), CTA_BUS_KEY)) code = get(url);
            n = code == 200 ? cta::parseBusRoutes(body, bodyLen, lists->routes, dock::PickerLists::kRoutes) : -1;
            lists->nRoutes = n > 0 ? n : 0;
            lists->routesState = n >= 0 ? dock::Load::Ready : dock::Load::Failed;
            break;
        case dock::Request::Dirs:
            lists->dirsState = dock::Load::Loading;
            publishLists();
            if (cta::busDirectionsUrl(url, sizeof(url), CTA_BUS_KEY, r.route)) code = get(url);
            n = code == 200 ? cta::parseBusDirections(body, bodyLen, lists->dirs, dock::PickerLists::kDirs) : -1;
            lists->nDirs = n > 0 ? n : 0;
            snprintf(lists->dirsRoute, sizeof(lists->dirsRoute), "%s", r.route);
            lists->dirsState = n >= 0 ? dock::Load::Ready : dock::Load::Failed;
            break;
        case dock::Request::Stops:
            lists->stopsState = dock::Load::Loading;
            publishLists();
            if (cta::busStopsUrl(url, sizeof(url), CTA_BUS_KEY, r.route, r.dir)) code = get(url);
            n = code == 200 ? cta::parseBusStops(body, bodyLen, lists->stops, dock::PickerLists::kStops) : -1;
            lists->nStops = n > 0 ? n : 0;
            snprintf(lists->stopsRoute, sizeof(lists->stopsRoute), "%s", r.route);
            snprintf(lists->stopsDir, sizeof(lists->stopsDir), "%s", r.dir);
            lists->stopsState = n >= 0 ? dock::Load::Ready : dock::Load::Failed;
            break;
        case dock::Request::None: return;
    }
    uprintf("net: picker list %d: HTTP %d, %d entries\n", static_cast<int>(r.what), code, n);
    publishLists();
}

// ---------- the touch extras (ecosystem spec 5.10): one request each, on this task's one connection ----------
void serveAhead(const Extra& r, uint32_t now) {
    dock::Ahead& ah = building->ahead;
    ah = dock::Ahead();
    ah.id = r.id;
    ah.state = dock::Load::Loading;
    publish();
    const Sec& s = secs[r.section];
    const bool bus = isBus(s);
    static char url[256];
    int code = -1, n = -1;
    if (hasPick(s) && (bus ? cta::busAheadUrl(url, sizeof(url), CTA_BUS_KEY, r.vehicle)
                           : cta::trainFollowUrl(url, sizeof(url), CTA_TRAIN_KEY, r.vehicle)))
        code = get(url);
    if (code == 200)
        n = bus ? cta::parseBusAhead(body, bodyLen, ah.stops, cta::kAheadStops)
                : cta::parseTrainAhead(body, bodyLen, ah.stops, cta::kAheadStops);
    uprintf("net: stops ahead (%s): HTTP %d, %d stops\n", bus ? "bus" : "train", code, n);
    ah.n = static_cast<uint8_t>(n > 0 ? n : 0);
    ah.gone = n == 0;
    ah.ts = now;
    ah.state = n >= 0 ? dock::Load::Ready : dock::Load::Failed;
    publish();
}

void servePeek(const Extra& r, uint32_t now) {
    dock::Peek& p = building->peek;
    p = dock::Peek();
    p.id = r.id;
    p.state = dock::Load::Loading;
    publish();
    const Sec& s = secs[r.section];
    static char url[256];
    int code = -1;
    bool ok = false;
    if (hasPick(s) && !isBus(s)) {   // the station's other platform, from the train tables
        if (cta::oppositeTrainPlatform(s.pick.route, s.pick.id, s.pick.dir, p.pick) &&
            cta::trainArrivalsUrl(url, sizeof(url), CTA_TRAIN_KEY, p.pick.id, p.pick.route)) {
            code = get(url);
            snprintf(p.rec.id, sizeof(p.rec.id), "%s", p.pick.id);
            snprintf(p.rec.route, sizeof(p.rec.route), "%s", p.pick.route);
            snprintf(p.rec.name, sizeof(p.rec.name), "%s", p.pick.name);
            snprintf(p.rec.dir, sizeof(p.rec.dir), "%.24s", p.pick.dir);
            ok = code == 200 && cta::parseTrainArrivals(body, bodyLen, now, p.rec, p.pick.route);
        }
    } else if (hasPick(s) && s.hasOpp) {   // the closest stop the other way, found from the route's patterns
        p.pick = s.opp;
        const char* ids[1] = {p.pick.id};
        if (cta::busPredictionsUrl(url, sizeof(url), CTA_BUS_KEY, ids, 1)) code = get(url);
        const int got = code == 200 ? cta::parseBusPredictions(body, bodyLen, now, p.pick.route, busRecs, kMaxBusRecs) : -1;
        ok = got >= 0;
        p.rec.type = cta::Kind::Bus;
        snprintf(p.rec.id, sizeof(p.rec.id), "%s", p.pick.id);
        snprintf(p.rec.route, sizeof(p.rec.route), "%s", p.pick.route);
        snprintf(p.rec.dir, sizeof(p.rec.dir), "%.24s", p.pick.dir);
        snprintf(p.rec.name, sizeof(p.rec.name), "%s", p.pick.name);
        p.rec.ts = now;   // no record for it: no buses predicted right now
        for (int k = 0; k < got; ++k)
            if (strncmp(busRecs[k].dir, p.pick.dir, cta::kNameLen) == 0) { p.rec = busRecs[k]; break; }
    }
    uprintf("net: other way for section %d: HTTP %d, %s\n", r.section + 1, code, ok ? "ok" : "failed");
    p.state = ok ? dock::Load::Ready : dock::Load::Failed;
    publish();
}

// A touch extra the loop asked for, if any: served. True if there was one.
bool serveExtra(uint32_t now) {
    Extra r;
    bool isAhead = false;
    xSemaphoreTake(mu, portMAX_DELAY);
    const bool any = aheadPending || peekPending;
    if (aheadPending) { r = pendingAhead; aheadPending = false; isAhead = true; }
    else if (peekPending) { r = pendingPeek; peekPending = false; }
    xSemaphoreGive(mu);
    if (!any) return false;
    if (isAhead) serveAhead(r, now);
    else servePeek(r, now);
    return true;
}

void netTask(void*) {
    if (!wifiInit()) {
        uprintf("net: Wi-Fi didn't start\n");
        setStatus("No Wi-Fi");
        publish();
        vTaskDelete(nullptr);
    }
    uint32_t discShown = 0, discLogMs = 0;
    startSntp();
    uint32_t lastBegin = 0, lastEnd = 0, joinFailures = 0, clockWait = 0;   // tries failed in a row: cta::wifiRetryDelayMs
    bool wasUp = false, joining = false;
    for (;;) {
        if (discCount != discShown) {   // at most one line a second, as before
            discShown = discCount;
            if (nowMs() - discLogMs >= 1000) uprintf("net: Wi-Fi disconnected, reason %u\n", static_cast<unsigned>(discReason));
            discLogMs = nowMs();
        }
        if (takePendingPicks()) publish();
        for (int k = 0; k < kHosts; ++k)
            if (hosts[k].open && nowMs() - hosts[k].lastUsed > kIdleCloseMs) closeHost(hosts[k]);
        if (scanWanted) {
            scanWanted = false;
            reportScan();
        }
        if (wifiOff) {   // diagnostic: the radio off, nothing polled, until it's turned back on
            static bool off = false;
            if (!off) {
                off = true;
                closeAll();
                gotIp = false;
                esp_wifi_disconnect();
                esp_wifi_stop();
                setStatus("Wi-Fi off (diagnostic)");
                publish();
                uprintf("net: Wi-Fi off (diagnostic)\n");
            }
            vTaskDelay(pdMS_TO_TICKS(200));
            if (wifiOff) continue;
            off = false;
            esp_wifi_start();
            joinFailures = 0;
            joining = false;
            uprintf("net: Wi-Fi back on\n");
        }
        if (fakeData) {   // serial "k": no Wi-Fi, clock or CTA needed
            static uint32_t fakeAt = 0;
            if (!fakeAt || nowMs() - fakeAt >= 10000) {
                fakeAt = nowMs();
                fakeSections(nowUnix());
                setStatus("");
                publish();
            }
            takePendingPicks();
            vTaskDelay(pdMS_TO_TICKS(250));
            continue;
        }
        if (lostIp) {   // as Arduino did on a lost lease: leave and rejoin through the usual path below
            lostIp = false;
            const uint32_t before = discCount;
            esp_wifi_disconnect();
            // its own disconnect event, before the rejoin starts, so it never counts as that join failing
            for (int i = 0; i < 20 && discCount == before; ++i) vTaskDelay(pdMS_TO_TICKS(50));
        }
        if (!gotIp) {
            if (wasUp) { uprintf("net: Wi-Fi lost\n"); joinFailures = 0; closeAll(); }
            wasUp = false;
            if (joining && (joinFailed || nowMs() - lastBegin > kWifiJoinMs)) {   // this try failed
                joining = false;
                ++joinFailures;
                lastEnd = nowMs();
                uprintf("net: couldn't join the dock's Wi-Fi (reason %u); next try in %u s\n", static_cast<unsigned>(discReason),
                              static_cast<unsigned>(cta::wifiRetryDelayMs(joinFailures) / 1000));
                esp_wifi_disconnect();
            }
            if (!joining && (joinFailures == 0 || nowMs() - lastEnd >= cta::wifiRetryDelayMs(joinFailures))) {
                uprintf("net: joining the dock's Wi-Fi\n");
                joinFailed = false;
                wifiJoin();
                lastBegin = nowMs();
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
            uprintf("net: Wi-Fi up, RSSI %d\n", wifiRssi());
            // httpd and mDNS, on core 0, only now the link has an IP (before it, socket() asserts in lwIP); later
            // reconnects call it again and it does nothing
            lan::start(hostLabel);
            if (!clockSet()) startSntp();   // its first request went out before there was a link
            clockWait = nowMs();
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
            if (nowMs() - clockWait > 10000 && (!lastHttpTry || nowMs() - lastHttpTry > 30000)) {
                lastHttpTry = nowMs();
                fetch("https://lapi.transitchicago.com/", false, false);
                clockFromDate();
            }
            vTaskDelay(pdMS_TO_TICKS(500));
            continue;
        }
        if (building->status[0]) { setStatus(""); publish(); }
        if (serveExtra(nowUnix())) continue;   // a tap waits on a person: before any poll
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

static void* psMalloc(size_t n) { return heap_caps_malloc(n, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT); }

void start(const cta::PickedStop picks[cta::kDockStops]) {
    // The client's own error lines can print a URL (they carry the keys): silenced whatever the default level.
    esp_log_level_set("HTTP_CLIENT", ESP_LOG_NONE);
    mu = xSemaphoreCreateMutex();
    dnsDone = xSemaphoreCreateBinary();
    published = new (psMalloc(sizeof(dock::Data))) dock::Data();
    building = new (psMalloc(sizeof(dock::Data))) dock::Data();
    pubLists = new (psMalloc(sizeof(dock::PickerLists))) dock::PickerLists();
    lists = new (psMalloc(sizeof(dock::PickerLists))) dock::PickerLists();
    busRecs = static_cast<cta::StopRecord*>(psMalloc(sizeof(cta::StopRecord) * kMaxBusRecs));
    busVehicles = static_cast<cta::BusVehicle*>(psMalloc(sizeof(cta::BusVehicle) * kMaxBuses));
    hosts = static_cast<Host*>(heap_caps_calloc(kHosts, sizeof(Host), MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
    body = static_cast<char*>(psMalloc(kMaxBody + 1));
    if (!published || !building || !pubLists || !lists || !busRecs || !busVehicles || !hosts || !body) {
        uprintf("net: out of memory\n");
        return;
    }
    building->keys = CTA_TRAIN_KEY[0] && CTA_BUS_KEY[0];
    snprintf(wifiNet.ssid, sizeof(wifiNet.ssid), "%s", DOCK_WIFI_SSID);
    snprintf(wifiNet.pass, sizeof(wifiNet.pass), "%s", DOCK_WIFI_PASS);
    building->wifiSetUp = wifiNet.ssid[0] != 0;
    snprintf(building->ssid, sizeof(building->ssid), "%s", wifiNet.ssid);   // shown in Settings, never logged
    uint8_t mac[6];
    esp_read_mac(mac, ESP_MAC_WIFI_STA);   // from eFuse: Wi-Fi isn't started yet
    snprintf(building->name, sizeof(building->name), "CTA-Dock-%02X%02X", mac[4], mac[5]);
    snprintf(hostLabel, sizeof(hostLabel), "cta-dock-%02x%02x", mac[4], mac[5]);
    hasGeo = store::loadGeo(geoLat, geoLon, geoWhen);
    applyPicks(picks);
    if (!building->keys || !building->wifiSetUp) {
        uprintf("net: %s in firmware/secrets.h; staying offline\n", building->keys ? "no DOCK_WIFI_SSID" : "no CTA keys");
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

static void ask(Extra& slot, volatile bool& pending, uint32_t id, int section, const char* vehicle) {
    if (!mu || section < 0 || section >= cta::kDockStops) return;
    xSemaphoreTake(mu, portMAX_DELAY);
    slot.id = id;
    slot.section = section;
    snprintf(slot.vehicle, sizeof(slot.vehicle), "%s", vehicle ? vehicle : "");
    pending = true;
    xSemaphoreGive(mu);
}
void ahead(uint32_t id, int section, const char* vehicle) { ask(pendingAhead, aheadPending, id, section, vehicle); }
void peek(uint32_t id, int section) { ask(pendingPeek, peekPending, id, section, nullptr); }

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
void setFake(bool on) { fakeData = on; }
bool fake() { return fakeData; }

bool wifiUp() { return gotIp; }

uint32_t unixNow() { return clockSet() ? nowUnix() : 0; }
uint32_t requests() { return nRequests; }

}  // namespace net
