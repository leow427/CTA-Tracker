#ifdef HANDHELD_LIVE   // the mock build has no network
#include "link.h"

#include <esp_heap_caps.h>
#include <esp_random.h>
#include <esp_http_server.h>
#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>
#include <freertos/task.h>
#include <mdns.h>
#include <stdio.h>
#include <string.h>

#include <new>

#include "ble_setup.h"
#include "clock_ms.h"
#include "live.h"
#include "nearby_msg.h"

namespace lan {

namespace {

// A 1 KB body buffer plus about 1 KB of parsing (ConfigMsg, Nearby, FollowMsg) on top of httpd's own needs, with
// headroom; stackLeft() shows what's really used. The stack is in PSRAM (start()), so the headroom costs no internal RAM.
constexpr size_t kStack = 8192;

SemaphoreHandle_t mu;
char token[cta::kTokenLen + 1] = "";
cta::PairGate gate;   // under mu, with token
struct Shared {
    Inbox inbox;
    cta::InfoMsg info = {};
    cta::ConfigMsg config;
};
Shared* sh;   // in PSRAM: internal RAM is for Wi-Fi and TLS
httpd_handle_t server = nullptr;
volatile uint32_t minStack = 0;

void noteStack() {
    const uint32_t left = uxTaskGetStackHighWaterMark(nullptr);
    if (!minStack || left < minStack) minStack = left;
}

esp_err_t reply(httpd_req_t* r, const char* status, const char* body) {
    httpd_resp_set_status(r, status);
    httpd_resp_set_type(r, "application/json");
    httpd_resp_send(r, body, HTTPD_RESP_USE_STRLEN);
    noteStack();
    return ESP_OK;
}

esp_err_t getInfo(httpd_req_t* r) {
    char buf[cta::kMaxInfo];
    return infoJson(buf, sizeof(buf)) ? reply(r, "200 OK", buf) : reply(r, "500 Internal Server Error", "{}");
}

esp_err_t getConfig(httpd_req_t* r) {
    cta::ConfigMsg c;
    xSemaphoreTake(mu, portMAX_DELAY);
    c = sh->config;
    xSemaphoreGive(mu);
    char buf[cta::kMaxBody];
    return cta::encodeConfig(c, buf, sizeof(buf)) ? reply(r, "200 OK", buf) : reply(r, "500 Internal Server Error", "{}");
}

// Reads the whole body into buf (cta::kMaxBody + 1, NUL-terminated); false if the client closed or stalled.
bool readBody(httpd_req_t* r, char* buf, size_t& len) {
    len = 0;
    while (len < r->content_len) {
        const int n = httpd_req_recv(r, buf + len, r->content_len - len);
        if (n <= 0) return false;   // closed, or stalled past httpd's 5 s timeout: drop it, don't hold the task
        len += n;
    }
    buf[len] = 0;
    return true;
}

// POST /pair (ecosystem spec 4.3), as the dock's: no bearer token. Size (413), then the body (400), then the gate. A
// token or a Bluetooth bond counts as paired (409).
esp_err_t postPair(httpd_req_t* r) {
    if (r->content_len > cta::kMaxBody) return reply(r, "413 Payload Too Large", "{\"error\":\"too large\"}");
    char body[cta::kMaxBody + 1];
    size_t len;
    if (!readBody(r, body, len)) return ESP_FAIL;
    cta::PairMsg m;
    if (!cta::parsePair(body, len, m)) return reply(r, "400 Bad Request", "{\"error\":\"bad body\"}");
    xSemaphoreTake(mu, portMAX_DELAY);
    const uint32_t now = nowMs();
    const cta::PairResult res = gate.check(m.code, now, token[0] != 0 || ble_setup::bonded());
    const unsigned retry = static_cast<unsigned>(gate.retryAfterSec(now));
    if (res == cta::PairResult::Ok) {   // in use at once; the loop saves it (no flash from this PSRAM stack)
        memcpy(token, m.token, sizeof(token));
        memcpy(sh->inbox.tokenHex, m.token, sizeof(sh->inbox.tokenHex));
        sh->inbox.token = true;
    }
    xSemaphoreGive(mu);
    memset(&m, 0, sizeof(m));
    switch (res) {
        case cta::PairResult::Ok: return reply(r, "200 OK", "{}");
        case cta::PairResult::WrongCode: return reply(r, "403 Forbidden", "{\"error\":\"wrong code\"}");
        case cta::PairResult::AlreadyPaired: return reply(r, "409 Conflict", "{\"error\":\"already paired\"}");
        case cta::PairResult::CoolingDown: break;
    }
    char cool[48];
    snprintf(cool, sizeof(cool), "{\"error\":\"cooling down\",\"retry_after\":%u}", retry);
    return reply(r, "423 Locked", cool);
}

// Every write, in the order the fake board (tools/link_send.py --serve) checks: size before reading, token, then
// apply's JSON checks.
esp_err_t change(httpd_req_t* r) {
    if (r->content_len > cta::kMaxBody) return reply(r, "413 Payload Too Large", "{\"error\":\"too large\"}");
    char auth[8 + cta::kTokenLen + 1];   // "Bearer " + the token + NUL, plus one so a longer header shows as too long
    char saved[cta::kTokenLen + 1];
    xSemaphoreTake(mu, portMAX_DELAY);
    memcpy(saved, token, sizeof(saved));
    xSemaphoreGive(mu);
    const size_t alen = httpd_req_get_hdr_value_len(r, "Authorization");
    if (!alen || alen >= sizeof(auth) || httpd_req_get_hdr_value_str(r, "Authorization", auth, sizeof(auth)) != ESP_OK ||
        !cta::tokenMatches(auth, saved))
        return reply(r, "401 Unauthorized", "{\"error\":\"unauthorized\"}");
    char body[cta::kMaxBody + 1];
    size_t len;
    if (!readBody(r, body, len)) return ESP_FAIL;
    switch (apply(r->method == HTTP_DELETE, r->uri, body, len, Via::Http)) {
        case 200: return reply(r, "200 OK", "{}");
        case 404: return reply(r, "404 Not Found", "{\"error\":\"not found\"}");
        case 413: return reply(r, "413 Payload Too Large", "{\"error\":\"too large\"}");
        default: return reply(r, "400 Bad Request", "{\"error\":\"bad body\"}");
    }
}

}  // namespace

// POST /config, /nearby, /pin, /follow, /token and /wifi need a body; any other write's body, if it has one, must parse.
int apply(bool del, const char* path, const char* body, size_t len, Via via) {
    if (!mu) return 404;
    if (len > cta::kMaxBody) return 413;
    const bool pin = strcmp(path, "/pin") == 0, follow = strcmp(path, "/follow") == 0;
    const bool ble = via == Via::Ble;
    if (!del && pin) {
        cta::PickedStop s;
        if (!cta::parseStop(body, len, s)) return 400;
        live::setPin(&s);
    } else if (!del && follow) {
        cta::FollowMsg f;
        if (!cta::parseFollow(body, len, f)) return 400;
        live::setFollow(&f);
    } else if (!del && strcmp(path, "/nearby") == 0) {
        cta::Nearby n;
        if (!cta::parseNearby(body, len, n)) return 400;   // checked here; the loop saves and applies it
        xSemaphoreTake(mu, portMAX_DELAY);
        memcpy(sh->inbox.nearbyJson, body, len);
        sh->inbox.nearbyJson[len] = 0;
        sh->inbox.nearbyLen = len;
        sh->inbox.nearby = true;
        xSemaphoreGive(mu);
    } else if (!del && strcmp(path, "/config") == 0) {
        cta::ConfigMsg c;
        if (!len || !cta::parseConfig(body, len, c)) return 400;
        xSemaphoreTake(mu, portMAX_DELAY);
        sh->inbox.config = true;   // stops are the dock's: ignored here
        if (c.hasPet) { sh->inbox.cfg.pet = c.pet; sh->inbox.cfg.hasPet = true; }
        if (c.hasSettings) { sh->inbox.cfg.settings = c.settings; sh->inbox.cfg.hasSettings = true; }
        xSemaphoreGive(mu);
    } else if (!del && ble && strcmp(path, "/token") == 0) {   // never logged
        char t[cta::kTokenLen + 1];
        if (!cta::parseToken(body, len, t)) return 400;
        xSemaphoreTake(mu, portMAX_DELAY);
        memcpy(sh->inbox.tokenHex, t, sizeof(t));
        sh->inbox.token = true;
        xSemaphoreGive(mu);
        memset(t, 0, sizeof(t));
    } else if (!del && ble && strcmp(path, "/wifi") == 0) {   // the password is never logged
        cta::WifiMsg w;
        if (!cta::parseWifi(body, len, cta::kWifiSlots, w)) return 400;
        live::setWifi(w);
        memset(&w, 0, sizeof(w));
    } else if ((del && (pin || follow)) || (!del && strcmp(path, "/reset") == 0)) {
        cta::ConfigMsg c;
        if (len && !cta::parseConfig(body, len, c)) return 400;   // parseConfig takes {}
        if (pin) {
            live::setPin(nullptr);
        } else if (follow) {
            live::setFollow(nullptr);
        } else {
            xSemaphoreTake(mu, portMAX_DELAY);
            sh->inbox.reset = true;
            xSemaphoreGive(mu);
        }
    } else {
        return 404;   // /relay and /stop come with W6b
    }
    return 200;
}

size_t infoJson(char* buf, size_t cap) {
    if (!mu) return 0;
    cta::InfoMsg m;
    xSemaphoreTake(mu, portMAX_DELAY);
    m = sh->info;
    xSemaphoreGive(mu);
    m.uptime = nowMs() / 1000;
    m.wifiConnected = live::wifiConnected();
    live::wifiSsid(m.ssid, sizeof(m.ssid));
    m.rssi = m.wifiConnected ? live::wifiRssi() : 0;
    return cta::encodeInfo(m, buf, cap);
}

void begin(const char* t) {
    void* p = heap_caps_malloc(sizeof(Shared), MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (!p) {
        puts("link: out of memory");
        return;
    }
    sh = new (p) Shared();
    mu = xSemaphoreCreateMutex();
    setToken(t);
}

void start(const char* hostLabel) {
    if (server || !mu) return;
    httpd_config_t cfg = HTTPD_DEFAULT_CONFIG();
    cfg.core_id = 0;   // never the frame loop's core
    cfg.stack_size = kStack;
    // The task's stack in PSRAM (CONFIG_SPIRAM_ALLOW_STACK_EXTERNAL_MEMORY): internal RAM is for Wi-Fi and TLS. Safe
    // because no handler touches flash or NVS (a task with an external stack must not): they read RAM under the
    // mutex, parse, hand pins and follows to the network task, ask the Wi-Fi driver for the network's name, and
    // answer; saving is the loop's job.
    cfg.task_caps = MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT;
    cfg.max_open_sockets = 3;   // the app and a tool at once; each socket costs internal RAM in lwIP
    cfg.max_uri_handlers = 12;   // 10 now, W6b's /relay and /stop next
    cfg.lru_purge_enable = true;
    if (httpd_start(&server, &cfg) != ESP_OK) {
        puts("link: httpd didn't start");
        server = nullptr;
        return;
    }
    printf("link: httpd up; internal free %u (min %u)\n", static_cast<unsigned>(heap_caps_get_free_size(MALLOC_CAP_INTERNAL)),
           static_cast<unsigned>(heap_caps_get_minimum_free_size(MALLOC_CAP_INTERNAL)));
    const httpd_uri_t uris[] = {
        {"/info", HTTP_GET, getInfo, nullptr},     {"/config", HTTP_GET, getConfig, nullptr},
        {"/config", HTTP_POST, change, nullptr},    {"/nearby", HTTP_POST, change, nullptr},
        {"/pin", HTTP_POST, change, nullptr},       {"/pin", HTTP_DELETE, change, nullptr},
        {"/follow", HTTP_POST, change, nullptr},    {"/follow", HTTP_DELETE, change, nullptr},
        {"/reset", HTTP_POST, change, nullptr},     {"/pair", HTTP_POST, postPair, nullptr},
    };
    for (const httpd_uri_t& u : uris)
        if (httpd_register_uri_handler(server, &u) != ESP_OK) printf("link: no route for %s\n", u.uri);
    const char* id = hostLabel + strlen(hostLabel) - 4;   // "xxxx", as in the BLE name
    // The service's full names: Arduino's addService("ctatracker", "tcp") added the underscores.
    mdns_txt_item_t txt[] = {{"type", "handheld"}, {"id", id}};
    if (mdns_init() == ESP_OK && mdns_hostname_set(hostLabel) == ESP_OK &&
        mdns_service_add(nullptr, "_ctatracker", "_tcp", 80, txt, 2) == ESP_OK) {
        printf("link: mDNS up; internal free %u (min %u)\n", static_cast<unsigned>(heap_caps_get_free_size(MALLOC_CAP_INTERNAL)),
               static_cast<unsigned>(heap_caps_get_minimum_free_size(MALLOC_CAP_INTERNAL)));
    } else {
        puts("link: mDNS didn't start");
    }
    printf("link: serving on port 80 as %s.local\n", hostLabel);
}

void setToken(const char* t) {
    if (!mu) return;
    xSemaphoreTake(mu, portMAX_DELAY);
    snprintf(token, sizeof(token), "%s", t ? t : "");
    xSemaphoreGive(mu);
}

void setInfo(const cta::InfoMsg& m) {
    if (!mu) return;
    xSemaphoreTake(mu, portMAX_DELAY);
    sh->info = m;
    xSemaphoreGive(mu);
}

void setConfig(const cta::ConfigMsg& c) {
    if (!mu) return;
    xSemaphoreTake(mu, portMAX_DELAY);
    sh->config = c;
    xSemaphoreGive(mu);
}

bool take(Inbox& out) {
    if (!mu) return false;
    xSemaphoreTake(mu, portMAX_DELAY);
    const bool any = sh->inbox.config || sh->inbox.nearby || sh->inbox.reset || sh->inbox.token;
    if (any) {
        out = sh->inbox;
        sh->inbox.config = sh->inbox.nearby = sh->inbox.reset = sh->inbox.token = false;
        sh->inbox.cfg = cta::ConfigMsg();
        memset(sh->inbox.tokenHex, 0, sizeof(sh->inbox.tokenHex));
    }
    xSemaphoreGive(mu);
    return any;
}

PairShow pairShow(char code[cta::kPairCodeLen + 1], uint8_t& waitS) {
    waitS = 0;
    if (!mu) return PairShow::None;
    const bool bond = ble_setup::bonded();
    xSemaphoreTake(mu, portMAX_DELAY);
    const uint32_t now = nowMs();
    const bool paired = token[0] || bond;
    // A code once httpd is up, so there's a way to pair and esp_random is a true RNG (the radio runs); again after a
    // cooldown or pairAgain. Never 000000, which the Pair screen reads as no code; the modulo's bias is under 0.03%.
    if (!paired && server && gate.needsCode(now)) {
        char c[cta::kPairCodeLen + 1];
        snprintf(c, sizeof(c), "%06u", static_cast<unsigned>(esp_random() % 999999 + 1));
        gate.setCode(c);
    }
    const uint32_t wait = paired ? 0 : gate.retryAfterSec(now);
    // On Wi-Fi only (spec 4.3): the same code comes back when Wi-Fi does.
    const bool on = !paired && live::wifiConnected();
    const PairShow show = !on ? PairShow::None : wait ? PairShow::Wait : gate.code[0] ? PairShow::Code : PairShow::None;
    if (show == PairShow::Code) memcpy(code, gate.code, sizeof(gate.code));
    waitS = static_cast<uint8_t>(wait);
    xSemaphoreGive(mu);
    return show;
}

void pairAgain() {
    if (!mu) return;
    xSemaphoreTake(mu, portMAX_DELAY);
    token[0] = 0;
    gate.reset();   // pairShow picks the fresh code
    xSemaphoreGive(mu);
}

uint32_t stackLeft() { return minStack; }

}  // namespace lan
#endif
