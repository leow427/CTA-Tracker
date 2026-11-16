#ifdef HANDHELD_LIVE   // the mock build has no network
#include "live.h"

#include <esp_crt_bundle.h>
#include <esp_eap_client.h>
#include <esp_event.h>
#include <esp_heap_caps.h>
#include <esp_http_client.h>
#include <esp_mac.h>
#include <esp_netif.h>
#include <esp_sntp.h>
#include <esp_wifi.h>
#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>
#include <freertos/task.h>
#include <lwip/dns.h>
#include <lwip/ip_addr.h>
#include <lwip/sockets.h>
#include <lwip/tcpip.h>
#include <nvs.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <sys/time.h>
#include <time.h>

#include <new>

#include "clock_ms.h"
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
    bool wifi;              // a network from setWifi() (its password is wiped once taken)
    cta::WifiMsg wifiMsg;
};
PendingLink* pendingLink;

// Under mu, in PSRAM: the phone's relay waiting for the network task, the store's one writer. One slot per stop (every
// listed stop and the pin), so a burst of stops is never dropped; a newer relay of a stop replaces the waiting one.
constexpr int kRelaySlots = 2 * cta::kNearbyPerTab + 1;
struct PendingRelay {
    bool weather;
    int wmo;
    float tempC;
    bool full[kRelaySlots];
    cta::RelayStop stops[kRelaySlots];
};
PendingRelay* pendingRelay;
cta::RelayStop* relayTaken;          // the network task's copy (PSRAM: 3.6 KB)
volatile uint32_t relayUntilMs = 0;  // relaying while nowMs() is before this (relayOn)
volatile bool relayOn = false;
volatile bool wifiDisabled = false;  // serial "W"

// The saved Wi-Fi networks (ecosystem spec 5.1, F1), slot 0 home, from NVS ("wifi"), else secrets.h's network as
// slot 0. In PSRAM.
cta::WifiList* nets;
bool netsFromSecrets = false;   // slot 0 is secrets.h's network (it may be WPA2-Enterprise)
char hostLabel[16];             // "cta-pet-xxxx", for mDNS

bool ntpSynced() { return time(nullptr) > 1700000000; }

void setStatus(const char* s) { snprintf(status, sizeof(status), "%s", s); }

// SNTP, with the servers by address (see netTask). Called again when Wi-Fi comes up without the clock: SNTP sends its
// first request when started (here, before Wi-Fi is up, so it goes nowhere) and lwIP waits 15 s before the next.
void startSntp() {   // what Arduino's configTzTime did
    if (esp_sntp_enabled()) esp_sntp_stop();
    esp_sntp_setoperatingmode(ESP_SNTP_OPMODE_POLL);
    esp_sntp_setservername(0, "216.239.35.0");
    esp_sntp_setservername(1, "216.239.35.4");
    esp_sntp_setservername(2, "162.159.200.1");
    esp_sntp_init();
    setenv("TZ", cta::kTz, 1);
    tzset();
}

// ---------- Wi-Fi state, from the driver's events ----------

// The handlers run on the system event task (small stack): they only note what happened; the network task logs it.
volatile bool wifiUp = false;      // joined, with an IP address
volatile bool staStarted = false;  // the driver is ready to join
volatile uint32_t wifiIp = 0;      // network order
volatile uint8_t discReason = 0;   // wifi_err_reason_t of the last disconnect
volatile uint32_t discCount = 0;

void onWifiEvent(void*, esp_event_base_t base, int32_t id, void* data) {
    if (base == WIFI_EVENT && id == WIFI_EVENT_STA_START) {
        staStarted = true;
    } else if (base == WIFI_EVENT && id == WIFI_EVENT_STA_DISCONNECTED) {
        wifiUp = false;
        discReason = static_cast<wifi_event_sta_disconnected_t*>(data)->reason;
        discCount = discCount + 1;
    } else if (base == IP_EVENT && id == IP_EVENT_STA_GOT_IP) {
        wifiIp = static_cast<ip_event_got_ip_t*>(data)->ip_info.ip.addr;
        wifiUp = true;
    } else if (base == IP_EVENT && id == IP_EVENT_STA_LOST_IP) {
        wifiUp = false;
    }
}

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
Dns resolve(const char* host, uint32_t& out) {
    ip4_addr_t lit;
    if (ip4addr_aton(host, &lit)) { out = lit.addr; return Dns::Ok; }
    while (xSemaphoreTake(dnsDone, 0) == pdTRUE) {}   // a late answer to a lookup given up on
    static uint32_t lastId = 0;
    if (++lastId == 0) ++lastId;   // 0 means none
    const uint32_t id = lastId;
    snprintf(dnsName, sizeof(dnsName), "%s", host);
    dnsWant = id;
    Dns r = Dns::Failed;
    const uint32_t t0 = nowMs();
    if (tcpip_callback(dnsStart, reinterpret_cast<void*>(static_cast<uintptr_t>(id))) == ERR_OK) {
        r = Dns::NoAnswer;
        for (;;) {
            const uint32_t spent = nowMs() - t0;
            if (spent >= kDnsWaitMs || xSemaphoreTake(dnsDone, pdMS_TO_TICKS(kDnsWaitMs - spent)) != pdTRUE) break;
            if (dnsGot != id) continue;   // an answer to an earlier lookup, given just before this one began
            // No address after lwIP's own retries ran out is silence; a server's "no such name" arrives within a
            // round trip or two.
            r = dnsAddr ? Dns::Ok : nowMs() - t0 >= kDnsSilentMs ? Dns::NoAnswer : Dns::Failed;
            if (r == Dns::Ok) out = dnsAddr;
            break;
        }
    }
    dnsWant = 0;
    dnsLastMs = nowMs() - t0;
    return r;
}

// ---------- the connections ----------

// One esp_http_client handle per host, each keeping its host's last TLS session (save_client_session), so a
// connection opened again resumes it when the server agrees: the train API's certificate chain is ECDSA, which this
// chip checks in software, so a full handshake there takes about 2.2 s, and a resumed one about one round trip. A
// session is only ever offered back to the host it came from, and certificates are checked in full (the certificate
// bundle) on every handshake that doesn't resume. All of mbedTLS's memory is in PSRAM (sdkconfig.defaults).
//
// Only one connection is open at a time: a request to another host closes the open one first. Within one poll pass
// the due stations share the train API's connection; each pass's bus poll closes it, so the next pass's first
// station opens it again (resuming its session); the idle close only matters when polling stops.
struct Conn {
    char host[48];
    esp_http_client_handle_t h;
    bool open;          // a kept-alive connection the next request may reuse
    bool resumable;     // a handshake has finished, so the next one offers its session
    uint32_t lastUsed;
};
constexpr int kConns = 5;   // trains, buses, alerts, weather, and one spare
Conn conns[kConns] = {};
constexpr uint32_t kIdleCloseMs = 90000;
constexpr uint32_t kQuickFailMs = 3000;   // a resuming handshake that fails faster was refused, not lost (fetch)
// The handshake took less than this: resumed. shortcut: esp_http_client doesn't say whether a session resumed, so
// the power log's tls event goes by time (a full ECDSA handshake takes about 2.2 s at 240 MHz); read the session from
// the TLS context if a log ever needs it exact.
constexpr uint32_t kResumedMs = 1000;
constexpr int kTimeoutMs = 12000;   // connect, handshake, and each read (HTTPClient's response timeout)

// The response body (decision D6): one buffer in PSRAM for the network task, NUL-terminated.
// shortcut: a fixed 256 KB cap; grow it only if a real reply hits the cap.
constexpr size_t kMaxBody = 256 * 1024;
char* body = nullptr;
size_t bodyLen = 0;

// From the header event of the response being read.
int64_t respDate = 0;     // its Date header (Unix time), 0 without one
bool respClose = false;   // the server will close the connection after it

esp_err_t onHttpEvent(esp_http_client_event_t* e) {   // the network task, inside fetch()
    if (e->event_id != HTTP_EVENT_ON_HEADER) return ESP_OK;
    int64_t t;
    if (strcasecmp(e->header_key, "Date") == 0 && cta::parseHttpDate(e->header_value, t)) respDate = t;
    if (strcasecmp(e->header_key, "Connection") == 0 && strcasecmp(e->header_value, "close") == 0) respClose = true;
    return ESP_OK;
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

void closeConn(Conn& c) {
    if (c.h && c.open) esp_http_client_close(c.h);
    c.open = false;
}

// Forgets the connection and its session (not what's learned about the host): the next handshake is a full one.
void dropConn(Conn& c) {
    if (c.h) esp_http_client_cleanup(c.h);
    c.h = nullptr;
    c.open = c.resumable = false;
}

// The certificate bundle, attached by esp-tls between the TCP connect and the handshake. It also turns Nagle off on
// that connection, as Arduino's NetworkClientSecure did (esp-tls never does): with it on, each handshake flight waited
// out a delayed ACK, about 250-375 ms per new connection. esp-tls gives no handle on the socket here, so this finds the
// open sockets to port 443; only the network task makes TLS connections.
esp_err_t attachBundle(void* conf) {
    for (int fd = LWIP_SOCKET_OFFSET; fd < LWIP_SOCKET_OFFSET + CONFIG_LWIP_MAX_SOCKETS; ++fd) {
        sockaddr_storage peer;
        socklen_t n = sizeof(peer);
        if (getpeername(fd, reinterpret_cast<sockaddr*>(&peer), &n) != 0 || peer.ss_family != AF_INET) continue;
        if (ntohs(reinterpret_cast<sockaddr_in*>(&peer)->sin_port) != 443) continue;
        const int one = 1;
        setsockopt(fd, IPPROTO_TCP, TCP_NODELAY, &one, sizeof(one));
    }
    return esp_crt_bundle_attach(conf);
}

// The handle for url's host, made on first use. nullptr if out of memory.
Conn* connFor(const char* url, const char* host) {
    Conn* c = nullptr;
    for (Conn& k : conns)
        if (k.host[0] && strcmp(k.host, host) == 0) c = &k;
    if (!c) {   // a new host: a free slot, else the least recently used
        for (Conn& k : conns) {
            if (!k.host[0]) { c = &k; break; }
            if (!c || k.lastUsed < c->lastUsed) c = &k;
        }
        dropConn(*c);
        snprintf(c->host, sizeof(c->host), "%s", host);
    }
    if (!c->h) {
        esp_http_client_config_t cfg = {};
        cfg.url = url;
        cfg.crt_bundle_attach = attachBundle;   // TLS is always verified (spec 11)
        cfg.timeout_ms = kTimeoutMs;
        cfg.event_handler = onHttpEvent;
        cfg.disable_auto_redirect = true;   // as HTTPClient: a redirect is just a status
        // Over CONFIG_SPIRAM_MALLOC_ALWAYSINTERNAL (allocations up to it stay internal), so both buffers are in PSRAM.
        cfg.buffer_size = CONFIG_SPIRAM_MALLOC_ALWAYSINTERNAL + 1;
        cfg.buffer_size_tx = CONFIG_SPIRAM_MALLOC_ALWAYSINTERNAL + 1;   // the alerts URL is up to 768 bytes
        cfg.save_client_session = true;
        c->h = esp_http_client_init(&cfg);
        c->open = c->resumable = false;
    }
    return c->h ? c : nullptr;
}

// GETs url, into body unless wantBody is false, and notes the response's Date header (respDate). Returns the HTTP
// status, or a negative code as HTTPClient's: -1 no connection, -5 connection lost, -8 out of memory, -10 body over
// kMaxBody, -11 no response in time. Never logs the URL (it has keys). keep: leave the connection open for the next
// request to the same host. Full speed (fast) from the connection, or from the request on a kept-open one. kind is
// for the power log, as in get().
int fetch(const char* url, bool wantBody, bool keep, cpuclk::Lazy& fast, char kind) {
    lastSilent = false;
    respDate = 0;
    bodyLen = 0;
    if (body) body[0] = 0;
    char host[sizeof(Conn::host)];
    hostOf(url, host, sizeof(host));
    Conn* c = connFor(url, host);
    if (!c || (wantBody && !body)) return -8;
    for (Conn& o : conns)
        if (&o != c) closeConn(o);
    bool fullRetried = false;
    for (int attempt = 0;; ++attempt) {
        const bool reused = c->open;
        if (!reused) {
            uint32_t ip;   // the lookup that tells a silent network from a missing name; lwIP caches the answer
            const Dns d = resolve(host, ip);
            if (d != Dns::Ok) {
                printf("net: %s for %s\n", d == Dns::NoAnswer ? "no DNS answer" : "DNS lookup failed", host);
                lastSilent = d == Dns::NoAnswer;
                return -1;
            }
        }
        // shortcut: full speed from the TCP connect, not the handshake (esp_http_client does both in open()); that
        // costs one round trip at 240 MHz. Split them only if the power A/B shows it.
        fast.on();
        if (esp_http_client_set_url(c->h, url) != ESP_OK) return -8;   // else the last URL would go again
        respClose = false;
        const uint32_t t0 = nowMs();
        if (esp_http_client_open(c->h, 0) != ESP_OK) {
            const bool quick = nowMs() - t0 < kQuickFailMs;
            const bool offered = !reused && c->resumable;
            closeConn(*c);
            if (reused && attempt == 0) continue;   // the server had closed the idle connection: retry fresh
            // A server that refuses the session answers at once; a handshake that timed out (lost replies) would
            // only time out again, so it isn't tried twice.
            if (offered && quick && !fullRetried) {
                fullRetried = true;
                dropConn(*c);
                if (!connFor(url, host)) return -8;
                continue;
            }
            if (offered) dropConn(*c);   // perhaps over the offered session: the next try starts fresh
            return -1;
        }
        if (!reused) {
#ifdef POWER_LOG
            plog::event("tls,%c,%s", kind, c->resumable && nowMs() - t0 < kResumedMs ? "resumed" : "full");
#else
            (void)kind;
#endif
            c->resumable = true;
        }
        c->open = true;
        int code = esp_http_client_fetch_headers(c->h) < 0 ? -11 : esp_http_client_get_status_code(c->h);
        if (code <= 0 && reused && attempt == 0) {   // the idle connection had died: retry fresh
            closeConn(*c);
            continue;
        }
        if (code <= 0) code = -11;
        if (code == 200 && wantBody) {
            for (;;) {
                if (bodyLen >= kMaxBody) { code = -10; break; }
                const int n = esp_http_client_read(c->h, body + bodyLen, kMaxBody - bodyLen);
                if (n < 0) { code = -5; break; }
                if (n == 0) break;
                bodyLen += n;
            }
            if (code == 200 && !esp_http_client_is_complete_data_received(c->h)) code = -5;
            if (code != 200) bodyLen = 0;
            body[bodyLen] = 0;
        }
        c->lastUsed = nowMs();
        // An error or an unread body: start clean next time, as the server's "Connection: close" does.
        if (code != 200 || !wantBody || !keep || respClose) closeConn(*c);
        return code;
    }
}

// kind, for the power log: t trains, b buses, a alerts, w weather. The reply is in body/bodyLen.
int get(const char* url, char kind, cpuclk::Lazy& fast, bool keep = false) {
#ifdef POWER_LOG
    plog::reqStart(kind);
    const uint32_t t0 = nowMs();
    const int code = fetch(url, true, keep, fast, kind);
    plog::reqEnd(kind, code, code == 200 ? bodyLen : 0, nowMs() - t0);
    return code;
#else
    return fetch(url, true, keep, fast, kind);
#endif
}

// Sets the clock from the last response's Date header, for networks that block NTP (UDP 123), as some campus and
// guest networks do. Good to about a second, which is plenty for minute countdowns; NTP corrects it if it ever
// gets through.
bool clockFromDate(int code) {
    if (code <= 0 || !respDate) {
        printf("net: clock from HTTP failed (HTTP %d)\n", code);
        return false;
    }
    const timeval tv = {static_cast<time_t>(respDate), 0};
    settimeofday(&tv, nullptr);
    puts("net: clock set from HTTP (NTP gave nothing)");
    return true;
}

// The clock from a bare request to the train API, for when there's no station to poll (no nearby list yet).
bool clockFromHttp() {
    cpuclk::Lazy fast;   // a TLS handshake: full speed, as every request
    const int code = fetch("https://lapi.transitchicago.com/", false, false, fast, 'c');
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
    printf("net: pinned %s, following %s\n", id[0] ? id : "nothing",
                  store.pin() && p.follow > 0 ? p.followMsg.vehicle : p.follow ? "nothing" : "as before");
    return true;
}

void publish() {
    takeLink();
    if (ntpSynced()) store.updateFollow(static_cast<uint32_t>(time(nullptr)));   // after every poll (spec 5.7)
    clear(*building);
    store.fill(*building);
    building->wifiUp = wifiUp;
    char ssid[33];
    wifiSsid(ssid, sizeof(ssid));
    building->homeWifi = building->wifiUp && nets->nets[0].ssid[0] && strcmp(ssid, nets->nets[0].ssid) == 0;
    snprintf(building->status, sizeof(building->status), "%s", status);
    building->requests = requests;
    building->relaying = relaying();
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
    const uint32_t t0 = nowMs();
    const int code = get(url, 't', fast, true);
    ++requests;
    if (!now) {
        if (!ntpSynced() && !clockFromDate(code)) return;
        now = static_cast<uint32_t>(time(nullptr));
        stationPolled[st] = now;
    }
    const int n = code == 200 ? store.applyTrains(st, body, bodyLen, now) : -1;
    printf("net: trains %s: HTTP %d, %d cards, %u ms\n", id, code, n, static_cast<unsigned>(nowMs() - t0));
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
    const uint32_t t0 = nowMs();
    const int code = get(url, 'b', fast);
    ++requests;
    const int cards = code == 200 ? store.applyBuses(body, bodyLen, now) : -1;
    printf("net: buses, %d stops: HTTP %d, %d cards, %u ms\n", n, code, cards, static_cast<unsigned>(nowMs() - t0));
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
    const uint32_t t0 = nowMs();
    const int code = get(url, 't', fast, true);
    ++requests;
    const int n = code == 200 ? store.applyPin(body, bodyLen, now) : -1;
    printf("net: pinned %s: HTTP %d, %d cards, %u ms\n", p.id, code, n, static_cast<unsigned>(nowMs() - t0));
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
    const int code = get(url, 'a', fast);
    const int n = code == 200 ? store.applyAlerts(body, bodyLen) : -1;
    printf("net: alerts, %d routes: HTTP %d, %d alerted\n", nRoutes, code, n);
    if (n < 0) alertsPolled = retryStamp(now, cta::kAlertsPollSec);
}

void pollWeather(uint32_t now) {
    cpuclk::Lazy fast;
    static char url[160];
    weatherPolled = now;
    if (!cta::weatherUrl(url, sizeof(url), store.nearby().lat, store.nearby().lon)) return;
    const int code = get(url, 'w', fast);
    const bool ok = code == 200 && store.applyWeather(body, bodyLen);
    printf("net: weather: HTTP %d, %s\n", code, ok ? "ok" : "failed");
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

// A blocking scan of every channel, as Arduino's scanNetworks() ran it (active, 100-300 ms a channel). The records
// (up to max) go to out; returns how many networks were seen in all.
int scan(wifi_ap_record_t* out, uint16_t max) {
    wifi_scan_config_t cfg = {};
    cfg.scan_type = WIFI_SCAN_TYPE_ACTIVE;
    cfg.scan_time.active.min = 100;
    cfg.scan_time.active.max = 300;
    if (esp_wifi_scan_start(&cfg, true) != ESP_OK) return 0;
    uint16_t found = 0;
    esp_wifi_scan_get_ap_num(&found);
    uint16_t n = max;
    if (esp_wifi_scan_get_ap_records(&n, out) != ESP_OK) n = 0;
    esp_wifi_clear_ap_list();   // the driver's copy of whatever didn't fit
    return n < found ? found : n;
}

constexpr uint16_t kMaxSeen = 24;
wifi_ap_record_t* seenAps;   // kMaxSeen scan records, in PSRAM (start)

// After a failed join: is the network in reach, and on what? The ESP32-S3 has 2.4 GHz only, and the most common
// failures are a 5 GHz-only network, a WPA3-only one, or a name that differs by a character. Logs only our SSID.
void reportScan(const char* ssid) {
    const cpuclk::Fast fast;
    const int found = scan(seenAps, kMaxSeen);
    for (int i = 0; i < found && i < kMaxSeen; ++i)
        if (strcmp(reinterpret_cast<const char*>(seenAps[i].ssid), ssid) == 0) {
            printf("net: %s is in reach: channel %d, RSSI %d, auth %d\n", ssid, seenAps[i].primary, seenAps[i].rssi,
                   static_cast<int>(seenAps[i].authmode));
            return;
        }
    printf("net: %s not seen among %d networks on 2.4 GHz; check the name, and that the router offers 2.4 GHz\n", ssid,
           found);
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
    cta::SeenNet seen[kMaxSeen];
    const int found = scan(seenAps, kMaxSeen);
    const int n = found < kMaxSeen ? found : kMaxSeen;
    for (int i = 0; i < n; ++i) seen[i] = {reinterpret_cast<const char*>(seenAps[i].ssid), seenAps[i].rssi};
    int slot = cta::strongestSaved(*nets, seen, n, skip);
    if (slot < 0 && skip) slot = cta::strongestSaved(*nets, seen, n, 0);
    printf("net: %d networks in reach; joining slot %d\n", n, slot);
    return slot;
}

// Joins net, as Arduino's WiFi.begin did: a fast scan, the strongest access point, WPA2 or better (open if it has
// no password), PMF when offered. eapUser: WPA2-Enterprise (PEAP) with this account and net.pass as its password.
void join(const cta::SavedNet& net, const char* eapUser) {
    wifi_config_t cfg = {};
    memcpy(cfg.sta.ssid, net.ssid, strnlen(net.ssid, sizeof(cfg.sta.ssid)));   // all 32 bytes may be used
    cfg.sta.scan_method = WIFI_FAST_SCAN;
    cfg.sta.sort_method = WIFI_CONNECT_AP_BY_SIGNAL;
    cfg.sta.threshold.rssi = -127;
    cfg.sta.pmf_cfg.capable = true;
    if (eapUser) {
        cfg.sta.threshold.authmode = WIFI_AUTH_WPA2_ENTERPRISE;
        esp_eap_client_set_identity(reinterpret_cast<const uint8_t*>(eapUser), strlen(eapUser));
        esp_eap_client_set_username(reinterpret_cast<const uint8_t*>(eapUser), strlen(eapUser));
        esp_eap_client_set_password(reinterpret_cast<const uint8_t*>(net.pass), strlen(net.pass));
        esp_wifi_sta_enterprise_enable();
    } else {
        memcpy(cfg.sta.password, net.pass, strnlen(net.pass, sizeof(cfg.sta.password)));   // a 64-digit hex key fills it
        cfg.sta.threshold.authmode = net.pass[0] ? WIFI_AUTH_WPA2_PSK : WIFI_AUTH_OPEN;
        esp_wifi_sta_enterprise_disable();
    }
    esp_wifi_set_config(WIFI_IF_STA, &cfg);
    esp_wifi_connect();
}

// The phone's relayed weather and stops into the store. True if anything changed.
bool takeRelays() {
    bool any = false;
    xSemaphoreTake(mu, portMAX_DELAY);
    if (pendingRelay->weather) {
        store.setWeather(pendingRelay->wmo, pendingRelay->tempC);
        pendingRelay->weather = false;
        any = true;
    }
    xSemaphoreGive(mu);
    for (int i = 0; i < kRelaySlots; ++i) {
        xSemaphoreTake(mu, portMAX_DELAY);
        const bool full = pendingRelay->full[i];
        if (full) *relayTaken = pendingRelay->stops[i];
        pendingRelay->full[i] = false;
        xSemaphoreGive(mu);
        if (full && store.applyRelay(*relayTaken)) any = true;
    }
    return any;
}

// A new nearby list from setNearby(): start over with its stops. True if there was one.
bool takeNearby() {
    if (!nearbyPending) return false;
    static cta::Nearby n;
    xSemaphoreTake(mu, portMAX_DELAY);
    n = pendingNearby;
    nearbyPending = false;
    xSemaphoreGive(mu);
    store.setNearby(n);
    memset(stationPolled, 0, sizeof(stationPolled));
    busPolled = alertsPolled = weatherPolled = 0;
    printf("net: nearby list: %d stations, %d bus stops\n", n.nTrains, n.nBuses);
    return true;
}

bool anySaved() {
    for (const cta::SavedNet& s : nets->nets)
        if (s.ssid[0]) return true;
    return false;
}

// A network written over Bluetooth (setWifi): into its slot and saved to NVS ("wifi"), here on the network task. The
// first one replaces secrets.h's fallback network. True if one was taken. Passwords are never logged.
bool takeWifi() {
    static cta::WifiMsg w;   // not on the stack: wiped below either way
    xSemaphoreTake(mu, portMAX_DELAY);
    const bool any = pendingLink->wifi;
    if (any) w = pendingLink->wifiMsg;
    pendingLink->wifi = false;
    memset(&pendingLink->wifiMsg, 0, sizeof(pendingLink->wifiMsg));
    xSemaphoreGive(mu);
    if (!any) return false;
    if (netsFromSecrets) {
        memset(nets, 0, sizeof(cta::WifiList));
        netsFromSecrets = false;
    }
    cta::SavedNet& n = nets->nets[w.slot];
    memcpy(n.ssid, w.ssid, sizeof(n.ssid));
    memcpy(n.pass, w.pass, sizeof(n.pass));
    memset(&w, 0, sizeof(w));
    nvs_handle_t h;
    bool saved = nvs_open("trackie", NVS_READWRITE, &h) == ESP_OK;
    if (saved) {
        saved = nvs_set_blob(h, "wifi", nets, sizeof(cta::WifiList)) == ESP_OK && nvs_commit(h) == ESP_OK;
        nvs_close(h);
    }
    printf("net: Wi-Fi slot %u set%s\n", static_cast<unsigned>(&n - nets->nets), saved ? "" : " (saving failed)");
    return true;
}

void netTask(void*) {
    if (!CTA_TRAIN_KEY[0] || !CTA_BUS_KEY[0]) {
        setStatus("No API key");
        puts("net: no CTA keys in firmware/secrets.h; staying offline");
        publish();
        vTaskDelete(nullptr);
        return;
    }
    if (!anySaved()) {   // until the app sends a network over Bluetooth; no reboot needed
        setStatus("No Wi-Fi set up");
        puts("net: no Wi-Fi saved; waiting for one");
        publish();
        while (!takeWifi()) {   // the phone's link and relay work meanwhile
            bool changed = takeNearby();   // in this order: a relayed pin's stop is accepted once it's pinned
            changed |= takeLink();
            changed |= takeRelays();
            changed |= ntpSynced() && store.followOver(static_cast<uint32_t>(time(nullptr)));
            if (changed) publish();
            vTaskDelay(pdMS_TO_TICKS(500));
        }
    }
    // Wi-Fi in station mode, settings in RAM only (as Arduino's WiFi.persistent(false)). Nothing reconnects by itself:
    // this loop joins and backs off. Why a join or a connection ended comes from the driver's events, in its words
    // (wifi_err_reason_t: 15 and 204 are a wrong password, 201 the network out of reach, 8 left on purpose); the loop
    // below logs it.
    esp_netif_init();
    esp_event_loop_create_default();
    esp_netif_t* sta = esp_netif_create_default_wifi_sta();
    esp_netif_set_hostname(sta, hostLabel);
    const wifi_init_config_t wcfg = WIFI_INIT_CONFIG_DEFAULT();
    esp_wifi_init(&wcfg);
    esp_wifi_set_storage(WIFI_STORAGE_RAM);
    esp_event_handler_register(WIFI_EVENT, WIFI_EVENT_STA_START, onWifiEvent, nullptr);
    esp_event_handler_register(WIFI_EVENT, WIFI_EVENT_STA_DISCONNECTED, onWifiEvent, nullptr);
    esp_event_handler_register(IP_EVENT, IP_EVENT_STA_GOT_IP, onWifiEvent, nullptr);
    esp_event_handler_register(IP_EVENT, IP_EVENT_STA_LOST_IP, onWifiEvent, nullptr);
    esp_wifi_set_mode(WIFI_MODE_STA);
#ifdef WIFI_MAC
    {
        // A replacement MAC from secrets.h, for a router that has blocked the board's own (one did, 2026-11-09,
        // after a reboot loop: it ignored every join from the factory MAC and admitted a new one at once).
        uint8_t mac[6] = WIFI_MAC;
        printf("net: using the MAC from secrets.h%s\n", esp_wifi_set_mac(WIFI_IF_STA, mac) == ESP_OK ? "" : " (refused)");
    }
#endif
    esp_wifi_start();
    // Joining before the driver has started fails with "no AP found" (seen on the first boot on IDF); Arduino's
    // WiFi.mode() waited for this too.
    for (int i = 0; i < 200 && !staStarted; ++i) vTaskDelay(pdMS_TO_TICKS(10));
    uint32_t discShown = 0;
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
            printf("net: disconnected, reason %u\n", discReason);
        }
        if (takeNearby()) {   // a new list: start over with its stops
            restUntil = 0;
            publish();
        }
        if (takeLink()) publish();     // a pin or follow from the app: poll its stop from now on
        if (ntpSynced() && store.followOver(static_cast<uint32_t>(time(nullptr)))) publish();   // Home back on time
        if (takeRelays()) publish();   // the phone's arrivals and weather, after the pin they may be for
        if (takeWifi()) {   // a new or changed network: rejoin, strongest saved first
            if (wifiUp) {
                for (Conn& c : conns) closeConn(c);
                wifiUp = false;
            }
            esp_wifi_disconnect();   // also ends a join under way, which would refuse the next scan or config
            setJoining(false);
            wasUp = false;
            lastBegin = 0;
            retryMs = kWifiRetryMinMs;
            skip = 0;
            scanned = false;
        }
        for (Conn& c : conns)
            if (c.open && nowMs() - c.lastUsed > kIdleCloseMs) closeConn(c);
        if (woke) {
            woke = false;
            if (!wasUp && !joining) { lastBegin = 0; retryMs = kWifiRetryMinMs; scanned = false; }
        }
        if (wifiDisabled) {   // serial "W": off until reboot; the relay keeps the cards coming
            if (wifiUp || joining) {
                for (Conn& c : conns) closeConn(c);
                esp_wifi_disconnect();
                wifiUp = false;
                setJoining(false);
                puts("net: Wi-Fi off until reboot");
            }
            wasUp = false;
            setStatus("Wi-Fi off");
            publish();
            vTaskDelay(pdMS_TO_TICKS(1000));
            continue;
        }
        if (!wifiUp) {
            if (wasUp) {   // F1: rescan and move to the next strongest saved network
                puts("net: Wi-Fi lost");
                for (Conn& c : conns) closeConn(c);
                esp_wifi_disconnect();
                lastBegin = 0;
                retryMs = kWifiRetryMinMs;
                skip = slot >= 0 ? static_cast<uint8_t>(1u << slot) : 0;
            }
            wasUp = false;
            if (joining && nowMs() - lastBegin > kWifiJoinMs) {   // this attempt failed: radio off until the next
                setJoining(false);
                esp_wifi_disconnect();   // ends the join still running, so the radio rests
                printf("net: no %s (reason %u); next try in %u s\n", nets->nets[slot].ssid, discReason,
                       static_cast<unsigned>(retryMs / 1000));
                skip |= static_cast<uint8_t>(1u << slot);
                if (!scanned) {   // once per boot or wake: say whether the network is in reach at all
                    scanned = true;
                    reportScan(nets->nets[slot].ssid);
                }
            }
            if (!joining && (lastBegin == 0 || nowMs() - lastBegin > retryMs)) {
                if (lastBegin != 0) retryMs = retryMs * 2 > kWifiRetryMaxMs ? kWifiRetryMaxMs : retryMs * 2;
                lastBegin = nowMs();
                slot = pickNet(skip);
                if (slot < 0) {
                    printf("net: no saved network in reach; next try in %u s\n", static_cast<unsigned>(retryMs / 1000));
                    skip = 0;
                } else {
                    const cta::SavedNet& net = nets->nets[slot];
                    printf("net: connecting to %s\n", net.ssid);
                    setJoining(true);
                    join(net, slot == 0 && netsFromSecrets && WIFI_EAP_USER[0] ? WIFI_EAP_USER : nullptr);
                    lastBegin = nowMs();
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
            wifi_ap_record_t ap = {};
            esp_wifi_sta_get_ap_info(&ap);
            const uint32_t ip = wifiIp;
            printf("net: Wi-Fi up, %u.%u.%u.%u, RSSI %d\n", static_cast<unsigned>(ip & 0xFF), static_cast<unsigned>(ip >> 8 & 0xFF),
                   static_cast<unsigned>(ip >> 16 & 0xFF), static_cast<unsigned>(ip >> 24), ap.rssi);
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
            if (!waitingSince) waitingSince = nowMs();
            if (strcmp(status, "Setting the clock") != 0) { setStatus("Setting the clock"); publish(); }
            const bool station = store.located() && store.nearby().nTrains > 0;
            if ((station || nowMs() - waitingSince > 10000) && (!lastHttpTry || nowMs() - lastHttpTry > 30000)) {
                lastHttpTry = nowMs();
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
            if (nowMs() - burstLast >= kBurstGapMs) {
                burstLast = nowMs();
                ++burstCount;
                burstOne(static_cast<uint32_t>(time(nullptr)));
                publish();
            } else {
                vTaskDelay(pdMS_TO_TICKS(20));
            }
            continue;
        }
#endif
        if (relaying()) {   // the phone fetches for us: no CTA, alerts or weather polls until its ticks stop
            vTaskDelay(pdMS_TO_TICKS(250));
            continue;
        }
        if (restUntil && static_cast<int32_t>(nowMs() - restUntil) < 0) {
            vTaskDelay(pdMS_TO_TICKS(250));
            continue;
        }
        restUntil = 0;
        const uint32_t pollMs = nowMs();
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
    constexpr uint32_t kPsram = MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT;
    published = static_cast<Snapshot*>(heap_caps_malloc(sizeof(Snapshot), kPsram));
    building = static_cast<Snapshot*>(heap_caps_malloc(sizeof(Snapshot), kPsram));
    pendingLink = static_cast<PendingLink*>(heap_caps_calloc(1, sizeof(PendingLink), kPsram));
    pendingRelay = static_cast<PendingRelay*>(heap_caps_calloc(1, sizeof(PendingRelay), kPsram));
    relayTaken = static_cast<cta::RelayStop*>(heap_caps_malloc(sizeof(cta::RelayStop), kPsram));
    nets = static_cast<cta::WifiList*>(heap_caps_calloc(1, sizeof(cta::WifiList), kPsram));
    body = static_cast<char*>(heap_caps_malloc(kMaxBody + 1, kPsram));
    seenAps = static_cast<wifi_ap_record_t*>(heap_caps_malloc(sizeof(wifi_ap_record_t) * kMaxSeen, kPsram));
    if (!published || !building || !pendingLink || !pendingRelay || !relayTaken || !nets || !body || !seenAps || !store.init()) {
        puts("net: out of memory");
        return;
    }
    clear(*published);
    uint8_t mac[6];
    esp_read_mac(mac, ESP_MAC_WIFI_STA);   // the factory MAC, as in GET /info's id and the BLE name
    snprintf(hostLabel, sizeof(hostLabel), "cta-pet-%02x%02x", mac[4], mac[5]);
    // The last nearby list (spec 8.3: kept in flash), and the saved networks (F1; written by BLE setup).
    nvs_handle_t h;
    if (nvs_open("trackie", NVS_READONLY, &h) == ESP_OK) {   // Preferences' entries: a string and a blob
        size_t len = 0;
        if (nvs_get_str(h, "nearby", nullptr, &len) == ESP_OK && len > 1 && len <= kMaxBody &&
            nvs_get_str(h, "nearby", body, &len) == ESP_OK) {
            cta::Nearby n;
            if (cta::parseNearby(body, len - 1, n)) {
                store.setNearby(n);
                printf("net: saved nearby list: %d stations, %d bus stops\n", n.nTrains, n.nBuses);
            }
        }
        len = sizeof(cta::WifiList);
        if (nvs_get_blob(h, "wifi", nets, &len) != ESP_OK || len != sizeof(cta::WifiList)) memset(nets, 0, sizeof(cta::WifiList));
        nvs_close(h);
    }
    if (!store.located()) puts("net: no nearby list yet; send one with tools/send_nearby.py");
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
    // Saved as a string, as Preferences' putString did. The caller's buffer isn't NUL-terminated at len, so a copy
    // (PSRAM: up to cta::kMaxBody) gets one.
    char* str = static_cast<char*>(heap_caps_malloc(len + 1, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
    if (!str) return false;
    memcpy(str, json, len);
    str[len] = 0;
    nvs_handle_t h;
    bool saved = nvs_open("trackie", NVS_READWRITE, &h) == ESP_OK;
    if (saved) {
        saved = nvs_set_str(h, "nearby", str) == ESP_OK && nvs_commit(h) == ESP_OK;
        nvs_close(h);
    }
    heap_caps_free(str);
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

void relayTick(const cta::RelayTick& t, bool viaBle) {
    if (!pendingRelay) return;
    if (!ntpSynced()) {   // no NTP (no Wi-Fi): the phone's clock; NTP corrects it if Wi-Fi comes up
        const timeval tv = {static_cast<time_t>(t.now), 0};
        settimeofday(&tv, nullptr);
    }
    xSemaphoreTake(mu, portMAX_DELAY);
    if (t.hasWeather) {
        pendingRelay->weather = true;
        pendingRelay->wmo = t.wmo;
        pendingRelay->tempC = t.tempC;
    }
    xSemaphoreGive(mu);
    if (viaBle) {
        relayUntilMs = nowMs() + cta::kRelayHoldSec * 1000;
        relayOn = true;
    }
}

void relayStop(const cta::RelayStop& s) {
    if (!pendingRelay) return;
    xSemaphoreTake(mu, portMAX_DELAY);
    int slot = -1, free = -1;
    for (int i = 0; i < kRelaySlots && slot < 0; ++i) {
        const cta::RelayStop& w = pendingRelay->stops[i];
        if (pendingRelay->full[i] && w.type == s.type && strcmp(w.id, s.id) == 0) slot = i;
        else if (!pendingRelay->full[i] && free < 0) free = i;
    }
    if (slot < 0) slot = free;
    if (slot >= 0) {   // all full: more stops than the list holds, so this one isn't ours anyway
        pendingRelay->stops[slot] = s;
        pendingRelay->full[slot] = true;
    }
    xSemaphoreGive(mu);
}

bool relaying() {   // the time left, 1 ms to the whole hold; unsigned, so nowMs's 49-day wrap can't revive it
    const uint32_t left = relayUntilMs - nowMs();
    return relayOn && left - 1 < cta::kRelayHoldSec * 1000;
}

void wifiOff() { wifiDisabled = true; }

void setWifi(const cta::WifiMsg& w) {
    if (!pendingLink) return;
    xSemaphoreTake(mu, portMAX_DELAY);
    pendingLink->wifiMsg = w;
    pendingLink->wifi = true;
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

bool wifiConnected() { return wifiUp; }

int8_t wifiRssi() {
    wifi_ap_record_t ap;
    return wifiUp && esp_wifi_sta_get_ap_info(&ap) == ESP_OK ? ap.rssi : 0;
}

void wifiSsid(char* out, size_t cap) {
    wifi_ap_record_t ap;
    if (cap) out[0] = 0;
    if (wifiUp && esp_wifi_sta_get_ap_info(&ap) == ESP_OK) snprintf(out, cap, "%s", reinterpret_cast<const char*>(ap.ssid));
}

}  // namespace live
#endif
