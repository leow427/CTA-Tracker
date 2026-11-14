#include "link.h"
#include "serial_out.h"

#include <stdio.h>

#include <freertos/FreeRTOS.h>
#include <freertos/task.h>

#include "clock_ms.h"
#include <esp_heap_caps.h>
#include <esp_http_server.h>
#include <esp_wifi.h>
#include <mdns.h>
#include <string.h>

#include <new>

#include "net.h"

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
    char buf[384];
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
    size_t len = 0;
    while (len < r->content_len) {
        const int n = httpd_req_recv(r, body + len, r->content_len - len);
        if (n <= 0) return ESP_FAIL;   // closed, or stalled past httpd's 5 s timeout: drop it, don't hold the task
        len += n;
    }
    body[len] = 0;
    const char* uri = r->uri;
    const bool isConfig = strcmp(uri, "/config") == 0;
    // A body is required on /config only; any body must parse (parseConfig takes {}), as link_send.py --serve checks.
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
    } else {
        xSemaphoreTake(mu, portMAX_DELAY);
        if (strcmp(uri, "/reset") == 0) sh->inbox.reset = true;
        else sh->inbox.openBle = true;
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
    // mutex, parse, ask the Wi-Fi driver for the network's name, and answer; saving is the loop's job.
    cfg.task_caps = MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT;
    cfg.max_open_sockets = 3;   // the app and a tool at once; each socket costs internal RAM in lwIP
    cfg.max_uri_handlers = 5;
    cfg.lru_purge_enable = true;
    if (httpd_start(&server, &cfg) != ESP_OK) {
        uprintf("link: httpd didn't start\n");
        server = nullptr;
        return;
    }
    uprintf("link: httpd up; internal free %u\n", heap_caps_get_free_size(MALLOC_CAP_INTERNAL));
    const httpd_uri_t uris[] = {
        {"/info", HTTP_GET, getInfo, nullptr},
        {"/config", HTTP_GET, getConfig, nullptr},
        {"/config", HTTP_POST, post, nullptr},
        {"/ble", HTTP_POST, post, nullptr},
        {"/reset", HTTP_POST, post, nullptr},
    };
    for (const httpd_uri_t& u : uris) httpd_register_uri_handler(server, &u);
    const char* id = hostLabel + strlen(hostLabel) - 4;   // "xxxx", as in the BLE name
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
    const bool any = sh->inbox.config || sh->inbox.reset || sh->inbox.openBle;
    if (any) {
        out = sh->inbox;
        sh->inbox = Inbox();
    }
    xSemaphoreGive(mu);
    return any;
}

size_t infoJson(char* buf, size_t cap) {
    if (!mu) return 0;
    cta::InfoMsg m;
    xSemaphoreTake(mu, portMAX_DELAY);
    m = sh->info;
    xSemaphoreGive(mu);
    m.uptime = nowMs() / 1000;
    wifi_ap_record_t ap;
    m.wifiConnected = net::wifiUp() && esp_wifi_sta_get_ap_info(&ap) == ESP_OK;
    snprintf(m.ssid, sizeof(m.ssid), "%s", m.wifiConnected ? reinterpret_cast<const char*>(ap.ssid) : "");
    return cta::encodeInfo(m, buf, cap);
}

uint32_t stackLeft() { return minStack; }

}  // namespace lan
