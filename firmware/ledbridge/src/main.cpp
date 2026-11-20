// The LED bridge (README.md): joins the dock's Wi-Fi, finds the dock by mDNS, polls its GET /leds every 500 ms and
// drives the route board's 21 LEDs with the dock's own driver settings and cta::ledFrame.
#include <stdio.h>
#include <string.h>

#include <algorithm>

#include <driver/rmt_encoder.h>
#include <driver/rmt_tx.h>
#include <esp_event.h>
#include <esp_http_client.h>
#include <esp_netif.h>
#include <esp_timer.h>
#include <esp_wifi.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <mdns.h>
#include <nvs_flash.h>

#include "dock_leds.h"
#include "secrets.h"

#ifndef DOCK_WIFI_SSID
#define DOCK_WIFI_SSID ""
#endif
#ifndef DOCK_WIFI_PASS
#define DOCK_WIFI_PASS ""
#endif

// The LED board's DATA screw. GPIO 4: not a strapping, USB or flash/PSRAM pin, and on every common S3 board.
#define LED_DATA_PIN 4
// The dock's cap (firmware/dock/include/pins.h), so 21 LEDs stay inside the 5 V budget.
#define LED_MAX_BRIGHTNESS 96

namespace {

uint32_t nowMs() { return static_cast<uint32_t>(esp_timer_get_time() / 1000); }

// ---------- LEDs: the dock's leds.cpp driver ----------
rmt_channel_handle_t chan = nullptr;
rmt_encoder_handle_t encoder = nullptr;

bool ledSetUp() {
    rmt_tx_channel_config_t cc = {};
    cc.gpio_num = static_cast<gpio_num_t>(LED_DATA_PIN);
    cc.clk_src = RMT_CLK_SRC_DEFAULT;
    cc.resolution_hz = 10000000;   // 0.1 µs ticks
    cc.mem_block_symbols = 48;
    cc.trans_queue_depth = 1;
    rmt_bytes_encoder_config_t ec = {};
    // WS2812B-B-V6 bits at 0.1 µs: a 0 is 0.3 µs high then 0.9 µs low, a 1 is 0.8 µs high then 0.4 µs low.
    ec.bit0.level0 = 1; ec.bit0.duration0 = 3; ec.bit0.level1 = 0; ec.bit0.duration1 = 9;
    ec.bit1.level0 = 1; ec.bit1.duration0 = 8; ec.bit1.level1 = 0; ec.bit1.duration1 = 4;
    ec.flags.msb_first = 1;
    return rmt_new_tx_channel(&cc, &chan) == ESP_OK && rmt_new_bytes_encoder(&ec, &encoder) == ESP_OK &&
           rmt_enable(chan) == ESP_OK;
}

// The colors (0xRRGGBB) in the strip's G, R, B order; waits until they're out.
void show(const uint32_t* rgb) {
    static uint8_t bytes[cta::kLedCount * 3];
    for (int i = 0; i < cta::kLedCount; ++i) {
        bytes[3 * i] = rgb[i] >> 8;
        bytes[3 * i + 1] = rgb[i] >> 16;
        bytes[3 * i + 2] = rgb[i];
    }
    rmt_transmit_config_t tc = {};
    if (rmt_transmit(chan, encoder, bytes, sizeof(bytes), &tc) == ESP_OK) rmt_tx_wait_all_done(chan, 20);
}

// Each LED in chain order, dim white for 50 ms, then all off: proves the wiring and order without the network.
void selfTest() {
    uint32_t rgb[cta::kLedCount] = {};
    for (int i = 0; i < cta::kLedCount; ++i) {
        rgb[i] = 0x606060;
        show(rgb);
        rgb[i] = 0;
        vTaskDelay(pdMS_TO_TICKS(50));
    }
    show(rgb);
}

// The dock's last state, from the poll loop to the LED task.
portMUX_TYPE mux = portMUX_INITIALIZER_UNLOCKED;
cta::LedRow rows[cta::kLedRows];
uint32_t dockNow = 0, gotMs = 0;
bool night = false, have = false;

void ledTask(void*) {
    uint32_t rgb[cta::kLedCount] = {};
    for (;;) {
        vTaskDelay(pdMS_TO_TICKS(40));   // 25 Hz
        cta::LedRow r[cta::kLedRows];
        uint32_t now, at;
        bool n, h;
        portENTER_CRITICAL(&mux);
        memcpy(r, rows, sizeof(r));
        now = dockNow; at = gotMs; n = night; h = have;
        portEXIT_CRITICAL(&mux);
        if (!h) continue;
        const uint32_t ms = nowMs();
        // The dock's time moved on locally, so rows go dark by themselves once stale if the dock stops answering.
        cta::ledFrame(r, now ? now + (ms - at) / 1000 : 0, n, LED_MAX_BRIGHTNESS, ms, rgb);
        show(rgb);
    }
}

// ---------- Wi-Fi (as firmware/dock/src/net.cpp) ----------
volatile bool staStarted = false, gotIp = false;

void onWifiEvent(void*, esp_event_base_t base, int32_t id, void*) {
    if (base == WIFI_EVENT && id == WIFI_EVENT_STA_START) staStarted = true;
    else if (base == WIFI_EVENT && id == WIFI_EVENT_STA_DISCONNECTED) gotIp = false;
    else if (base == IP_EVENT && id == IP_EVENT_STA_GOT_IP) gotIp = true;
}

void wifiStart() {
    esp_netif_init();
    esp_event_loop_create_default();
    esp_netif_set_hostname(esp_netif_create_default_wifi_sta(), "cta-ledbridge");
    const wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    esp_wifi_init(&cfg);
    esp_wifi_set_storage(WIFI_STORAGE_RAM);
    esp_event_handler_register(WIFI_EVENT, ESP_EVENT_ANY_ID, onWifiEvent, nullptr);
    esp_event_handler_register(IP_EVENT, IP_EVENT_STA_GOT_IP, onWifiEvent, nullptr);
    esp_wifi_set_mode(WIFI_MODE_STA);
#ifdef WIFI_MAC
    {
        uint8_t mac[6] = WIFI_MAC;   // a replacement MAC from secrets.h, for a router that has blocked the board's own
        printf("ledbridge: using the MAC from secrets.h%s\n", esp_wifi_set_mac(WIFI_IF_STA, mac) == ESP_OK ? "" : " (refused)");
    }
#endif
    esp_wifi_start();
    for (int i = 0; i < 200 && !staStarted; ++i) vTaskDelay(pdMS_TO_TICKS(10));   // joining before this fails
}

void wifiJoin() {
    wifi_config_t c = {};
    // Literals: all 32 / 64 bytes at most, unterminated when full, as the driver takes them.
    memcpy(c.sta.ssid, DOCK_WIFI_SSID, std::min(sizeof(DOCK_WIFI_SSID) - 1, sizeof(c.sta.ssid)));
    memcpy(c.sta.password, DOCK_WIFI_PASS, std::min(sizeof(DOCK_WIFI_PASS) - 1, sizeof(c.sta.password)));
    c.sta.scan_method = WIFI_FAST_SCAN;
    c.sta.sort_method = WIFI_CONNECT_AP_BY_SIGNAL;
    c.sta.threshold.rssi = -127;
    if (DOCK_WIFI_PASS[0]) c.sta.threshold.authmode = WIFI_AUTH_WPA2_PSK;
    c.sta.pmf_cfg.capable = true;
    esp_wifi_set_config(WIFI_IF_STA, &c);
    esp_wifi_connect();
}

// ---------- the dock ----------
// The dock's IPv4 address from its _ctatracker._tcp record with TXT type=dock (a handheld may advertise the same
// service); 0 if none answered.
uint32_t findDock() {
    mdns_result_t* res = nullptr;
    uint32_t ip = 0;
    if (mdns_query_ptr("_ctatracker", "_tcp", 2000, 4, &res) != ESP_OK) return 0;
    for (mdns_result_t* r = res; r && !ip; r = r->next) {
        bool dock = false;
        for (size_t i = 0; i < r->txt_count; ++i)
            dock |= !strcmp(r->txt[i].key, "type") && r->txt[i].value && !strcmp(r->txt[i].value, "dock");
        if (!dock) continue;
        for (mdns_ip_addr_t* a = r->addr; a && !ip; a = a->next)
            if (a->addr.type == ESP_IPADDR_TYPE_V4) ip = a->addr.u_addr.ip4.addr;
        esp_ip4_addr_t a4 = {};
        if (!ip && r->hostname && mdns_query_a(r->hostname, 2000, &a4) == ESP_OK) ip = a4.addr;
    }
    mdns_query_results_free(res);
    return ip;
}

// GET http://<ip>/leds into the shared state; false on any failure.
bool fetch(uint32_t ip) {
    char url[40];
    esp_ip4_addr_t a = {ip};
    snprintf(url, sizeof(url), "http://" IPSTR "/leds", IP2STR(&a));
    esp_http_client_config_t cfg = {};
    cfg.url = url;
    cfg.timeout_ms = 1500;
    esp_http_client_handle_t c = esp_http_client_init(&cfg);
    if (!c) return false;
    char body[160];
    int n = -1;
    if (esp_http_client_open(c, 0) == ESP_OK && esp_http_client_fetch_headers(c) >= 0 &&
        esp_http_client_get_status_code(c) == 200)
        n = esp_http_client_read_response(c, body, sizeof(body) - 1);
    esp_http_client_cleanup(c);
    if (n <= 0) return false;
    body[n] = 0;
    cta::LedRow r[cta::kLedRows];
    uint32_t now;
    bool nt;
    if (!cta::parseLedState(body, r, &now, &nt)) return false;
    portENTER_CRITICAL(&mux);
    memcpy(rows, r, sizeof(rows));
    dockNow = now; gotMs = nowMs(); night = nt; have = true;
    portEXIT_CRITICAL(&mux);
    return true;
}

}  // namespace

extern "C" void app_main() {
    printf("ledbridge: LED data on GPIO %d\n", LED_DATA_PIN);
    if (!ledSetUp()) {
        printf("ledbridge: RMT setup failed\n");
        return;
    }
    selfTest();
    xTaskCreate(ledTask, "leds", 3072, nullptr, 5, nullptr);

    esp_err_t nvs = nvs_flash_init();   // the Wi-Fi driver needs NVS
    if (nvs == ESP_ERR_NVS_NO_FREE_PAGES || nvs == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        nvs_flash_erase();
        nvs_flash_init();
    }
    wifiStart();
    mdns_init();
    uint32_t dockIp = 0, lastJoin = 0, lastFailLog = 0;
    int failures = 0;
    bool joined = false, first = true, joining = false;
    for (;;) {
        vTaskDelay(pdMS_TO_TICKS(500));
        if (!gotIp) {
            joined = false;
            if (!joining || nowMs() - lastJoin > 15000) {   // the driver never retries on its own
                wifiJoin();
                lastJoin = nowMs();
                joining = true;
            }
            continue;
        }
        joining = false;
        if (!joined) {
            joined = true;
            printf("ledbridge: Wi-Fi joined\n");
        }
        if (!dockIp) {
            dockIp = findDock();
            if (dockIp) {
                esp_ip4_addr_t a = {dockIp};
                printf("ledbridge: dock found at " IPSTR "\n", IP2STR(&a));
            }
            if (!dockIp) continue;
        }
        if (fetch(dockIp)) {
            failures = 0;
            if (first) {
                first = false;
                printf("ledbridge: first good frame\n");
            }
            continue;
        }
        ++failures;
        if (nowMs() - lastFailLog > 10000) {
            lastFailLog = nowMs();
            printf("ledbridge: fetch failed (%d in a row)\n", failures);
        }
        if (failures % 6 == 0) dockIp = 0;   // about 3 s of failures: ask mDNS again (the dock may have a new address)
    }
}
