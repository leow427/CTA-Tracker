#include "link.h"
#include "serial_out.h"

#include <stdio.h>

#include <freertos/FreeRTOS.h>
#include <freertos/task.h>

#include "clock_ms.h"
#include <esp_heap_caps.h>
#include <esp_http_server.h>
#include <esp_ota_ops.h>
#include <esp_random.h>
#include <esp_system.h>
#include <esp_wifi.h>
#include <mdns.h>
#include <string.h>

#include <new>

#include "cta_config.h"
#include "leds.h"
#include "net.h"
#include "secrets.h"

#ifndef DOCK_OTA_KEY
#define DOCK_OTA_KEY ""   // undefined or empty: no POST /ota
#endif

namespace lan {

namespace {

// A 1 KB body buffer plus about 700 B of parsing (ConfigMsg, decodePicks) on top of httpd's own needs, with headroom;
// stackLeft() shows what's really used. The stack is in PSRAM (start()), so the headroom costs no internal RAM.
constexpr size_t kStack = 8192;

SemaphoreHandle_t mu;
char token[cta::kTokenLen + 1] = "";
struct Shared {
    Inbox inbox;
    cta::InfoMsg info = {};
    cta::ConfigMsg config;
};
Shared* sh;   // in PSRAM: internal RAM is for Wi-Fi and TLS
cta::PairGate gate;   // under mu, with token
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
    cta::InfoMsg m;
    xSemaphoreTake(mu, portMAX_DELAY);
    m = sh->info;
    m.paired = token[0] != 0;
    xSemaphoreGive(mu);
    m.uptime = nowMs() / 1000;
    wifi_ap_record_t ap;
    m.wifiConnected = net::wifiUp() && esp_wifi_sta_get_ap_info(&ap) == ESP_OK;
    snprintf(m.ssid, sizeof(m.ssid), "%s", m.wifiConnected ? reinterpret_cast<const char*>(ap.ssid) : "");
    char buf[cta::kMaxInfo];   // an SSID of escaped control bytes makes it up to 530 bytes
    return cta::encodeInfo(m, buf, sizeof(buf)) ? reply(r, "200 OK", buf) : reply(r, "500 Internal Server Error", "{}");
}

// POST /ota: the raw firmware.bin, with header X-OTA-Key (DOCK_OTA_KEY; not the household token). The LED bridge's
// handler (firmware/ledbridge/src/main.cpp), run on a task of its own: esp_ota_get_next_update_partition, esp_ota_end
// and esp_ota_set_boot_partition map flash, which disables the cache and asserts the caller's stack is internal, and
// httpd's stack is in PSRAM. That task's stack (xTaskCreate: always internal RAM, 9 KB) exists only during an upload.
// No rollback (CONFIG_BOOTLOADER_APP_ROLLBACK_ENABLE is off): a bad image is fixed over USB.
volatile bool otaBusy = false;   // set on the httpd task only; cleared by the OTA task if it fails

esp_err_t say(httpd_req_t* r, const char* status, const char* body) {
    httpd_resp_set_status(r, status);
    httpd_resp_sendstr(r, body);
    return ESP_OK;
}

bool otaKeyOk(httpd_req_t* r) {
    char got[96] = "";
    httpd_req_get_hdr_value_str(r, "X-OTA-Key", got, sizeof(got));
    const char* want = DOCK_OTA_KEY;
    const size_t n = strlen(want);
    if (strlen(got) != n) return false;
    uint8_t diff = 0;   // every byte compared, so the time says nothing about the key
    for (size_t i = 0; i < n; ++i) diff |= got[i] ^ want[i];
    return diff == 0;
}

[[noreturn]] void otaFail(httpd_req_t* r, const char* status, const char* body, const char* log) {
    uprintf("link: OTA failed: %s\n", log);
    say(r, status, body);
    httpd_req_async_handler_complete(r);
    otaBusy = false;
    vTaskDelete(nullptr);
    for (;;) {}   // not reached
}

void otaTask(void* arg) {
    httpd_req_t* r = static_cast<httpd_req_t*>(arg);
    const esp_partition_t* p = esp_ota_get_next_update_partition(nullptr);
    esp_ota_handle_t h;
    if (!p || esp_ota_begin(p, OTA_WITH_SEQUENTIAL_WRITES, &h) != ESP_OK)
        otaFail(r, "500 Internal Server Error", "no slot\n", "no slot");
    uprintf("link: OTA of %u bytes to %s\n", static_cast<unsigned>(r->content_len), p->label);
    char buf[1024];   // on this task's stack, so it costs internal RAM only during an upload
    int timeouts = 0;
    for (size_t left = r->content_len; left > 0;) {
        const int n = httpd_req_recv(r, buf, left < sizeof(buf) ? left : sizeof(buf));
        if (n == HTTPD_SOCK_ERR_TIMEOUT && ++timeouts < 5) continue;
        if (n <= 0 || esp_ota_write(h, buf, n) != ESP_OK) {
            esp_ota_abort(h);
            otaFail(r, "500 Internal Server Error", "write failed\n", "while receiving");
        }
        left -= n;
    }
    if (esp_ota_end(h) != ESP_OK || esp_ota_set_boot_partition(p) != ESP_OK)   // end checks the image
        otaFail(r, "400 Bad Request", "bad image\n", "image refused");
    say(r, "200 OK", "ok, rebooting\n");
    httpd_req_async_handler_complete(r);
    uprintf("link: OTA done, rebooting\n");
    vTaskDelay(pdMS_TO_TICKS(500));
    esp_restart();
}

esp_err_t postOta(httpd_req_t* r) {
    if (!otaKeyOk(r)) return say(r, "401 Unauthorized", "bad key\n");
    if (otaBusy) return say(r, "409 Conflict", "busy\n");
    httpd_req_t* copy = nullptr;
    if (httpd_req_async_handler_begin(r, &copy) != ESP_OK) return say(r, "500 Internal Server Error", "no memory\n");
    otaBusy = true;
    // 9 KB: the LED bridge runs this in 6 KB with buf static, plus buf, plus headroom. Core 0, never the frame loop's.
    if (xTaskCreatePinnedToCore(otaTask, "ota", 9216, copy, 5, nullptr, 0) != pdPASS) {
        otaBusy = false;
        say(copy, "500 Internal Server Error", "no memory\n");
        httpd_req_async_handler_complete(copy);
    }
    return ESP_OK;
}

// The LED rows for the LED bridge board (firmware/ledbridge), as cta::formatLedState's line. No token: read-only,
// and LED colors aren't private. 503 before the loop's first post.
esp_err_t getLeds(httpd_req_t* r) {
    cta::LedRow rows[cta::kLedRows];
    uint32_t now;
    bool night;
    char buf[128];
    if (!leds::last(rows, now, night)) return reply(r, "503 Service Unavailable", "");
    cta::formatLedState(buf, sizeof(buf), rows, now, night);
    httpd_resp_set_type(r, "text/plain");
    httpd_resp_send(r, buf, HTTPD_RESP_USE_STRLEN);
    noteStack();
    return ESP_OK;
}

esp_err_t getConfig(httpd_req_t* r) {
    cta::ConfigMsg c;
    xSemaphoreTake(mu, portMAX_DELAY);
    c = sh->config;
    xSemaphoreGive(mu);
    char buf[cta::kMaxBody];
    return cta::encodeConfig(c, buf, sizeof(buf)) ? reply(r, "200 OK", buf) : reply(r, "500 Internal Server Error", "{}");
}

// The whole body into buf (kMaxBody + 1 bytes, NUL-terminated); false if the client closed or stalled.
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

// POST /pair (ecosystem spec 4.3): no bearer token. Size (413), then the body (400), then the gate.
esp_err_t postPair(httpd_req_t* r) {
    if (r->content_len > cta::kMaxBody) return reply(r, "413 Payload Too Large", "{\"error\":\"too large\"}");
    char body[cta::kMaxBody + 1];
    size_t len;
    if (!readBody(r, body, len)) return ESP_FAIL;
    cta::PairMsg m;
    if (!cta::parsePair(body, len, m)) return reply(r, "400 Bad Request", "{\"error\":\"bad body\"}");
    xSemaphoreTake(mu, portMAX_DELAY);
    const uint32_t now = nowMs();
    const cta::PairResult res = gate.check(m.code, now, token[0] != 0);
    const unsigned retry = static_cast<unsigned>(gate.retryAfterSec(now));
    if (res == cta::PairResult::Ok) {   // in use at once; the loop saves it (no flash from this PSRAM stack)
        memcpy(token, m.token, sizeof(token));
        memcpy(sh->inbox.token, m.token, sizeof(sh->inbox.token));
        sh->inbox.paired = true;
    }
    xSemaphoreGive(mu);
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

// Every write, in the order the fake board (tools/link_send.py --serve) checks: size before reading, token, JSON.
esp_err_t post(httpd_req_t* r) {
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
    const char* uri = r->uri;
    const bool isConfig = strcmp(uri, "/config") == 0;
    // A body is required on /config only (/reset takes none or {}); any body must parse (parseConfig takes {}), as link_send.py --serve checks.
    cta::ConfigMsg c;
    if ((isConfig && !len) || (len && !cta::parseConfig(body, len, c)))
        return reply(r, "400 Bad Request", "{\"error\":\"bad body\"}");
    if (isConfig) {
        xSemaphoreTake(mu, portMAX_DELAY);
        sh->inbox.config = true;
        if (c.hasStops) { memcpy(sh->inbox.cfg.stops, c.stops, sizeof(c.stops)); sh->inbox.cfg.hasStops = true; }
        if (c.hasPet) { sh->inbox.cfg.pet = c.pet; sh->inbox.cfg.hasPet = true; }
        if (c.hasSettings) { sh->inbox.cfg.settings = c.settings; sh->inbox.cfg.hasSettings = true; }
        xSemaphoreGive(mu);
    } else {   // /reset
        xSemaphoreTake(mu, portMAX_DELAY);
        sh->inbox.reset = true;
        xSemaphoreGive(mu);
    }
    return reply(r, "200 OK", "{}");
}

}  // namespace

void begin(const char* t) {
    void* p = heap_caps_malloc(sizeof(Shared), MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (!p) {
        uprintf("link: out of memory\n");
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
    // mutex, parse, ask the Wi-Fi driver for the network's name, and answer; saving is the loop's job, and /ota's
    // flash work runs on a task of its own (postOta).
    cfg.task_caps = MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT;
    cfg.max_open_sockets = 3;   // the app and a tool at once; each socket costs internal RAM in lwIP
    cfg.max_uri_handlers = 7;
    cfg.lru_purge_enable = true;
    if (httpd_start(&server, &cfg) != ESP_OK) {
        uprintf("link: httpd didn't start\n");
        server = nullptr;
        return;
    }
    uprintf("link: httpd up; internal free %u\n", heap_caps_get_free_size(MALLOC_CAP_INTERNAL));
    const httpd_uri_t uris[] = {
        {"/info", HTTP_GET, getInfo, nullptr},
        {"/leds", HTTP_GET, getLeds, nullptr},
        {"/config", HTTP_GET, getConfig, nullptr},
        {"/config", HTTP_POST, post, nullptr},
        {"/pair", HTTP_POST, postPair, nullptr},
        {"/reset", HTTP_POST, post, nullptr},
    };
    for (const httpd_uri_t& u : uris) httpd_register_uri_handler(server, &u);
    const httpd_uri_t ota = {"/ota", HTTP_POST, postOta, nullptr};
    if (!DOCK_OTA_KEY[0]) uprintf("link: OTA off (no DOCK_OTA_KEY)\n");
    else if (httpd_register_uri_handler(server, &ota) == ESP_OK) uprintf("link: OTA on, POST /ota\n");
    const char* id = hostLabel + strlen(hostLabel) - 4;   // "xxxx"
    // _ctatracker._tcp with its underscores: Arduino's MDNS.addService("ctatracker", "tcp") added them, IDF doesn't.
    mdns_txt_item_t txt[] = {{"type", "dock"}, {"id", id}};
    if (mdns_init() == ESP_OK && mdns_hostname_set(hostLabel) == ESP_OK && mdns_instance_name_set(hostLabel) == ESP_OK &&
        mdns_service_add(nullptr, "_ctatracker", "_tcp", 80, txt, 2) == ESP_OK) {
        uprintf("link: mDNS up; internal free %u\n", heap_caps_get_free_size(MALLOC_CAP_INTERNAL));
    } else {
        uprintf("link: mDNS didn't start\n");
    }
    uprintf("link: serving on port 80 as %s.local\n", hostLabel);
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
    const bool any = sh->inbox.config || sh->inbox.reset || sh->inbox.paired;
    if (any) {
        out = sh->inbox;
        sh->inbox = Inbox();
    }
    xSemaphoreGive(mu);
    return any;
}

PairShow pairShow(char code[cta::kPairCodeLen + 1], uint8_t& waitS) {
    waitS = 0;
    if (!mu) return PairShow::None;
    xSemaphoreTake(mu, portMAX_DELAY);
    const uint32_t now = nowMs();
    // A code (ecosystem spec 4.3; never logged) once httpd is up, so there's a way to pair and esp_random is a true
    // RNG (the radio runs); again after a cooldown or "Pair again". % 1000000 is biased by under 0.03%.
    if (!token[0] && server && gate.needsCode(now)) {
        char c[cta::kPairCodeLen + 1];
        snprintf(c, sizeof(c), "%06u", static_cast<unsigned>(esp_random() % 1000000));
        gate.setCode(c);
    }
    const uint32_t wait = token[0] ? 0 : gate.retryAfterSec(now);
    const PairShow show = token[0] ? PairShow::None : wait ? PairShow::Wait : gate.code[0] ? PairShow::Code : PairShow::None;
    if (show == PairShow::Code) memcpy(code, gate.code, sizeof(gate.code));
    waitS = static_cast<uint8_t>(wait);
    xSemaphoreGive(mu);
    return show;
}

bool paired() {
    if (!mu) return false;
    xSemaphoreTake(mu, portMAX_DELAY);
    const bool p = token[0] != 0;
    xSemaphoreGive(mu);
    return p;
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
