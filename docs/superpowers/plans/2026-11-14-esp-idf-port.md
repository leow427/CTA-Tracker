# ESP-IDF Port Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or
> superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.
> Every session (orchestrator, workers, reviewers) runs `ponytail:ponytail` at **full** first. Workflow:
> `docs/Agent-Workflow.md` (worker plus fresh Opus reviewers, at most 3 rounds, early stop on small findings).

**Goal:** Both boards on native ESP-IDF, with no Arduino code or library anywhere (Leo, 2026-11-14: "NO ARDUINO").
They keep today's behaviour, pixels, speed and power, and gain the internal RAM that brings Bluetooth setup back.
The work starts today (2026-11-14). The showcase is 2026-11-23, with the dry run on 11/22.

**Architecture:** PlatformIO stays the build system, now with `framework = espidf` on the same pioarduino platform.
Each board gets its own `sdkconfig.defaults`, which reproduces what the prebuilt high_perf libraries gave us and then
moves mbedTLS, mDNS, the httpd stack and the NimBLE host to PSRAM. Each Arduino API is swapped for its ESP-IDF
counterpart inside the file that uses it. There is no compatibility layer. The shared library moves from ArduinoJson
to IDF's cJSON, and the native tests run against a vendored copy of the same cJSON version.

**Tech stack:** pioarduino `platform-espressif32` 54.03.20 with `framework = espidf` (ESP-IDF 5.4.1, the IDF the
high_perf libraries were built from). From the component manager: `espressif/mdns`, plus `espressif/esp_lcd_co5300`
2.2.0 (needs IDF ≥ 5.4) on the wearable. From IDF itself: `json` (cJSON 1.7.18), esp_wifi, esp_netif,
esp_http_client, esp_http_server, nvs_flash, i2c_master, NimBLE. Unity native tests.

**Spec:** the base spec `docs/superpowers/specs/2026-11-03-cta-tracker-design.md` and the ecosystem spec
`2026-11-14-ecosystem-design.md`. Bluetooth comes back by Leo's decision; the spec text changes in Task E1.

---

## Open questions for Leo

1. **The four `unfilteredPasses` assertions.** These are `test_train` (two), `test_bus` and `test_alerts_weather`.
   They check that big CTA replies go through ArduinoJson's filter, and cJSON has no filter. My recommendation is to
   keep their intent (big replies stay bounded in memory) and assert a peak-bytes bound read from the cJSON
   allocator hooks instead. That changes test code, and the workflow says tests are never weakened, so it needs your
   OK. Every other parser assertion stays as it is.
   **Approved (Leo, 2026-11-14).** Done in J1 as its own commit: each check is now `assertParseBounded(len)`
   (`firmware/native/test/json_bound.h`): peak cJSON bytes ≤ 8 × the reply's length, and nothing left allocated.
2. **The fallback cut-off.** If a board still misses its gates on IDF at 18:00 on 11/17, should it demo from the last
   Arduino commit (tagged `arduino-final`, Task J0) or keep going on IDF? My recommendation: the cut-off as stated,
   decided per board.

---

## Decisions

**Settled by Leo, 2026-11-14:**

- **No Arduino.** This covers the framework, the libraries (`GFX Library for Arduino`, `SensorLib`, `XPowersLib`)
  and ArduinoJson.
- **cJSON replaces ArduinoJson** in `firmware/lib`. The existing parser tests are the contract and must pass with
  identical results (open question 1 is the only exception). cJSON nodes go to PSRAM through `cJSON_InitHooks`.
- **Bluetooth setup comes back on ESP-IDF.** It uses NimBLE with its host memory in PSRAM and trimmed pools. The
  target is internal RAM minimum ≥ 40 KB on both boards with BLE on and TLS fetches running. Appendix A of the
  ecosystem plan is undone and its BLE Tasks 7, 9, 12 and 19 return (Task E1).
  - Fallback 1: setup-only BLE, with CTA polling paused while BLE runs.
  - Fallback 2: Appendix A (pair over the network) as it stands on main.
- **The dock port lands Task 6 too.** It starts from `57241f7`, Task 6 rebased onto main `3598b0b`. On Arduino,
  Task 6 failed two gates: internal RAM 35.8 KB against 40, and the map at 15.7 fps against 16. The port has to pass
  both.

**Recommended here (the orchestrator may override):**

| # | Decision | Recommendation | Why |
|---|---|---|---|
| D1 | Build system | **PlatformIO `framework = espidf`**, not `idf.py` | Keeps `pio test -e native`, `lib_extra_dirs = ../lib`, `--upload-port`, and `tools/dock_perf_run.py` / `power_run.py` (they call `pio run -e perf/power -t upload --upload-port`) unchanged. `idf.py` would mean rewriting every tool and wrapping `firmware/lib` as components. |
| D2 | IDF version | **5.4.1 via pioarduino 54.03.20** (the platform both boards already pin) | It's the IDF the high_perf libraries were built from, so drivers and timing stay the same and only the build changes. `esp_lcd_co5300` needs ≥ 5.4. IDF 5.5 adds nothing we need this week. The boot log's `esp_get_idf_version()` confirms it (verify on board). |
| D3 | Shared code between boards | **None new.** Each board ports its own `net.cpp` / `live.cpp`. | The two tracks run in parallel on separate files. Merging the duplicate network code is post-showcase work. |
| D4 | HTTPS | **`esp_http_client`** with `crt_bundle_attach = esp_crt_bundle_attach`, `keep_alive_enable`, and `save_client_session = true` (needs `CONFIG_ESP_TLS_CLIENT_SESSION_TICKETS`; the field is in 5.4.1's header). One client handle is kept per host, so each host keeps its session. | It replaces `HTTPClient`, `NetworkClientSecure`, the `Tls` subclass (and its 3.2.0 CA-bundle workaround), `BodySink` and the `Session` store. It only resumes by **ticket**. If the train API doesn't resume (perf log `tls,…,full` on every reopen), go back to `esp_tls` plus today's `mbedtls_ssl_session_save/load` code (see the HTTPS bites). |
| D5 | DNS | **Keep today's `resolve()`.** It is already plain lwIP (`dns_gethostbyname_addrtype` on the tcpip thread). Swap `IPAddress` for `uint32_t`. | Its fast failure on no answer and the wearable's 15 s DNS pause stay as they are. Once it resolves, the lwIP cache makes `esp_http_client`'s own `getaddrinfo` instant. |
| D6 | Response bodies | **One 256 KB PSRAM buffer per network task**, allocated at start, replacing `String` | The parsers already take `(const char*, len)`. This removes the String-growth bug and `BodySink`. `shortcut: fixed 256 KB cap (today's kMaxBody), grow it only if a real reply hits the cap`. |
| D7 | Wearable display | **`espressif/esp_lcd_co5300`** over QSPI (SPI2, 80 MHz), with the init sequence copied from Arduino_GFX's `Arduino_CO5300` and `esp_lcd_panel_set_gap(panel, 6, 0)` (today's col offset 6) | The UI already draws into its own strips; `main.cpp` only calls `draw16bitBeRGBBitmap` and `setBrightness`. So `ui.cpp` doesn't change, and the send path becomes `esp_lcd_panel_draw_bitmap` plus brightness command 0x51. |
| D8 | Wearable touch and PMU | **Two small drivers of our own on `i2c_master`**: `cst9217.*` (read points at address 0x5A) and `axp2101.*` (the few registers `power.cpp` reads, plus shutdown). The register sequences come from SensorLib's and XPowersLib's sources (both MIT; keep their notice on any copied code). | Both libraries are Arduino-first, and Waveshare's own IDF examples go through a BSP (`waveshare/esp32_s3_touch_amoled_1_75`, validated on IDF 5.5/6.0) that would pull in LVGL-era pieces we don't use. About 150 lines in total. |
| D9 | Wearable CPU clock | **`rtc_clk_cpu_freq_mhz_to_config` + `rtc_clk_cpu_freq_set_config`**, the calls Arduino's `setCpuFrequencyMhz` makes. No `CONFIG_PM_ENABLE`. | Same behaviour, so the same power numbers. PM locks would change the power profile and need a new A/B. |
| D10 | cJSON on the laptop | **Vendor `cJSON.c`/`cJSON.h` 1.7.18 into `firmware/native/lib/cJSON/`** (MIT, with LICENSE), the same version as IDF 5.4.1's `json` component | Native tests stay offline and deterministic, with no path into the PlatformIO core folder. The boards use IDF's copy, so nothing is linked twice. |
| D11 | cJSON memory | **`cta::setJsonHeap` keeps its signature**, and its body now calls `cJSON_InitHooks` (PSRAM first, internal if PSRAM is out). Both boards call it at the start of `app_main`. | The dock's call site doesn't change, so Track J doesn't block the board tracks. All cJSON nodes go to PSRAM, not only big documents, because `CONFIG_SPIRAM_MALLOC_ALWAYSINTERNAL=4096` would otherwise put every small node in internal RAM. |
| D12 | BLE stack | **IDF's NimBLE C API** (the `bleprph` pattern) | It ships with IDF and needs no dependency. `h2zero/esp-nimble-cpp` would be closer to the spike's code but is one more library. |
| D13 | Source filters | **`#ifdef HANDHELD_LIVE` / `POWER_LOG` guards in the files** instead of `build_src_filter` | ESP-IDF builds `src/` as one component from `src/CMakeLists.txt`, and whether PlatformIO's filter applies there is unverified. Guards work either way. |
| D14 | Logging | **`printf`**, with `CONFIG_LOG_DEFAULT_LEVEL_ERROR` | `esp_http_client` debug logs print URLs, which carry the keys. Never raise the log level above WARN. |

---

## Tracks and the order to run them

| Track | Base | Owns (files) | Tasks | Waits on |
|---|---|---|---|---|
| **J — cJSON** | main `3598b0b` | `firmware/lib/cta/**`, `firmware/native/**`, the build-command comments in `firmware/dock/tools/preview.cpp` and `firmware/handheld/mock/tools/{preview,frame_test,live_test}.cpp` | J0, J1 | nothing |
| **D — dock** | `57241f7` (Task 6) | `firmware/dock/**` except the tool comments above; `firmware/lib/cta/src/ble_ids.h` (token UUID, D5) | D1–D5 | J merged before D2's first board build |
| **W — wearable** | main `3598b0b` | `firmware/handheld/mock/**` except the tool comments above | W1–W6 | J merged before W3's first board build; `ble_ids.h` from D5 for W6 |
| **E — ecosystem docs** | main | `docs/superpowers/plans/2026-11-14-ecosystem.md`, `docs/superpowers/specs/2026-11-14-ecosystem-design.md` §12, `todo.md` F17 | E1 | D4 and W5 measured |

**What runs at once:** J, D and W all start now. Each board track has its own board (the dock on the CH343 port, the
wearable on native USB), so there is no board booking between D and W. J is small (2–3 h) and **merges first**. D1
and W1–W2 don't touch `firmware/lib` and can run before J lands. D2 and W3 rebase on J before their first full
build. If J isn't merged by then, keep `bblanchon/ArduinoJson@^7` in `lib_deps` for that one build (it is
header-only; verify it compiles under espidf) and drop it after rebasing.

**Merge order onto main:** J, then D (with Task 6 inside), then W, then E. Each track rebases on main before it
merges. Finished, reviewed work ships without asking (commit, push, merge).

**Ecosystem tasks while the port runs:**

- **Continue:**
  - Track A, Tasks 10, 11, 13–17. They talk to the boards over the same HTTP API, against `link_send.py --serve` or
    whichever build a board has.
  - Track K.
  - F3 and F5 (`ui.cpp` only; the port doesn't touch it).
  - F4, F6, F7.
- **Pause until the port lands on that board:**
  - B1 (wearable send path, rewritten in W2) and B2 (dock drawing; D shares the board).
  - F2 (battery level; `power.cpp` is rewritten in W2).
  - Tasks 7, 9, 12 and 19, which come back as BLE tasks after D4/W5 (E1). Their Appendix A versions (`POST /pair`)
    are not built unless BLE fails both fallbacks.
  - Task 20 extras, until the showcase steps pass on IDF.

### Day plan

| When | J | D (dock) | W (wearable) |
|---|---|---|---|
| 11/14 evening | J0, J1, merged | D1, D2 | W1, W2 |
| 11/15 | — | D3 (gates), D4 (BLE measure) | W3, W4 (gates, power A/B) |
| 11/16 | — | D5 (BLE setup) | W5 (BLE measure), W6 (BLE setup) |
| 11/17 | E1; app Task 12 (BLE flow) on Track A | showcase steps on IDF | showcase steps on IDF |
| 11/17 18:00 | **Cut-off** (open question 2): a board that still fails its gates demos from `arduino-final` | | |
| 11/18–21 | the ecosystem plan's remaining days, unchanged | | |
| 11/22 | dry run | | |

**Estimates:**

- **J:** 0.3 day.
- **D:** 1.5 days: the port 0.75, gates 0.25, BLE 0.5. D2 should land tonight; the gates need the board tomorrow.
- **W:** 2 days: the port 1.0 (display, touch and PMU drivers are new), gates and power runs 0.5, BLE 0.5.
- The IDF port without BLE is realistic by the end of 11/15 on both boards, not all of it today.

---

## Shared reference (both board tracks)

### Project files (PlatformIO + ESP-IDF)

- `platformio.ini`:
  - Set `framework = espidf`.
  - Drop the `platform_packages` line (high_perf libs) and `board_build.arduino.memory_type`.
  - Keep `platform`, `board`, `core_dir`, flash size, `-O2`, `-std=gnu++17` (and `build_unflags`), `-I../`,
    `lib_extra_dirs`, `monitor_speed`.
  - Set `board_build.partitions = partitions.csv` and `board_build.f_flash = 80000000L`.
  - `lib_deps`: none after J lands.
  - Remove `-DARDUINO_USB_*` and `-DBOARD_HAS_PSRAM`.
- `CMakeLists.txt` (project root): the standard three lines, `cmake_minimum_required`,
  `include($ENV{IDF_PATH}/tools/cmake/project.cmake)` and `project(<name>)`.
- `src/CMakeLists.txt`: `idf_component_register(SRCS ${app_sources} INCLUDE_DIRS "." ../include)` with a glob of
  `src/*.cpp`, as PlatformIO's espidf template does.
- `src/idf_component.yml`: `espressif/mdns` (any 1.x that resolves for 5.4) and, on the wearable,
  `espressif/esp_lcd_co5300: "^2.2.0"`. Commit `dependencies.lock` if one is produced.
- `partitions.csv`: copy Arduino's `default_8MB.csv` (dock) / `default_16MB.csv` (wearable) byte for byte. That keeps
  `nvs` at 0x9000, size 0x5000, so the saved picks, nearby list, token and geo survive the reflash; Preferences
  stored them as plain NVS entries.
- `.gitignore` (repo root) adds `firmware/**/sdkconfig.*` except `sdkconfig.defaults`, and
  `firmware/**/managed_components/`.
- Entry point: `extern "C" void app_main()` runs today's `setup()` and then loops `loop()`.

### sdkconfig.defaults

Common lines for both boards, reproducing high_perf (values read from its `sdkconfig`) plus the RAM moves:

```
# high_perf parity
CONFIG_COMPILER_OPTIMIZATION_PERF=y
CONFIG_COMPILER_CXX_EXCEPTIONS=y
CONFIG_ESP_DEFAULT_CPU_FREQ_MHZ_240=y
CONFIG_FREERTOS_HZ=1000
CONFIG_ESP_MAIN_TASK_AFFINITY_CPU1=y
CONFIG_ESP_MAIN_TASK_STACK_SIZE=8192
CONFIG_ESPTOOLPY_FLASHMODE_QIO=y
CONFIG_ESPTOOLPY_FLASHFREQ_80M=y
CONFIG_PARTITION_TABLE_CUSTOM=y
CONFIG_PARTITION_TABLE_CUSTOM_FILENAME="partitions.csv"
CONFIG_SPIRAM=y
CONFIG_SPIRAM_MODE_OCT=y
CONFIG_SPIRAM_SPEED_80M=y
CONFIG_SPIRAM_XIP_FROM_PSRAM=y
CONFIG_SPIRAM_USE_MALLOC=y
CONFIG_SPIRAM_MALLOC_ALWAYSINTERNAL=4096
CONFIG_SPIRAM_MALLOC_RESERVE_INTERNAL=0
CONFIG_SPIRAM_TRY_ALLOCATE_WIFI_LWIP=y
CONFIG_SPIRAM_ALLOW_STACK_EXTERNAL_MEMORY=y
CONFIG_ESP32S3_INSTRUCTION_CACHE_16KB=y
CONFIG_ESP32S3_DATA_CACHE_32KB=y
CONFIG_ESP32S3_DATA_CACHE_LINE_64B=y
CONFIG_ESP_WIFI_STATIC_RX_BUFFER_NUM=8
CONFIG_ESP_WIFI_DYNAMIC_RX_BUFFER_NUM=32
CONFIG_ESP_WIFI_STATIC_TX_BUFFER=y
CONFIG_ESP_WIFI_STATIC_TX_BUFFER_NUM=8
CONFIG_ESP_WIFI_RX_BA_WIN=16
CONFIG_ESP_WIFI_TX_BA_WIN=6
CONFIG_ESP_WIFI_NVS_ENABLED=n
CONFIG_LWIP_MAX_SOCKETS=16
CONFIG_LWIP_TCP_SND_BUF_DEFAULT=5760
CONFIG_LWIP_TCP_WND_DEFAULT=5760
CONFIG_LWIP_TCPIP_TASK_STACK_SIZE=4096
CONFIG_HTTPD_MAX_REQ_HDR_LEN=1024
CONFIG_ESP_TASK_WDT_TIMEOUT_S=5
CONFIG_ESP_TASK_WDT_PANIC=y
CONFIG_ESP_TASK_WDT_CHECK_IDLE_TASK_CPU0=y
CONFIG_ESP_TASK_WDT_CHECK_IDLE_TASK_CPU1=n
CONFIG_ESP_INT_WDT_TIMEOUT_MS=300
CONFIG_LOG_DEFAULT_LEVEL_ERROR=y
CONFIG_BOOTLOADER_LOG_LEVEL_ERROR=y
CONFIG_ESP_COREDUMP_ENABLE_TO_FLASH=y
# TLS: verified, tickets, all of mbedTLS in PSRAM (replaces tlsCalloc)
CONFIG_MBEDTLS_CERTIFICATE_BUNDLE=y
CONFIG_MBEDTLS_CERTIFICATE_BUNDLE_DEFAULT_FULL=y
CONFIG_MBEDTLS_CLIENT_SSL_SESSION_TICKETS=y
CONFIG_ESP_TLS_CLIENT_SESSION_TICKETS=y
CONFIG_MBEDTLS_EXTERNAL_MEM_ALLOC=y
CONFIG_MBEDTLS_SSL_IN_CONTENT_LEN=16384
CONFIG_MBEDTLS_ASYMMETRIC_CONTENT_LEN=y
CONFIG_MBEDTLS_SSL_OUT_CONTENT_LEN=4096
# mDNS (component) in PSRAM
CONFIG_MDNS_MAX_SERVICES=2
CONFIG_MDNS_TASK_CREATE_FROM_SPIRAM=y
CONFIG_MDNS_MEMORY_ALLOC_SPIRAM=y
# BLE (D4/W5; off until then: CONFIG_BT_ENABLED=n)
CONFIG_BT_ENABLED=y
CONFIG_BT_NIMBLE_ENABLED=y
CONFIG_BT_NIMBLE_MEM_ALLOC_MODE_EXTERNAL=y
CONFIG_BT_NIMBLE_MAX_CONNECTIONS=1
CONFIG_BT_NIMBLE_MAX_BONDS=4
CONFIG_BT_NIMBLE_ROLE_CENTRAL=n
CONFIG_BT_NIMBLE_ROLE_OBSERVER=n
CONFIG_BT_NIMBLE_NVS_PERSIST=y
CONFIG_BT_NIMBLE_SM_SC=y
CONFIG_BT_CTRL_BLE_MAX_ACT=2
```

Notes on these lines:

- **Kconfig names:** `MDNS_*_SPIRAM` and `BT_NIMBLE_MSYS_*` vary by component version. When `pio run` reports a
  symbol as unknown, use `pio run -t menuconfig` to find the current name, and say so in the report.
- **The main task:** Arduino's loop ran on core 1 with an 8 KB stack; IDF's `app_main` defaults to core 0 with 3.5 KB.
- **The tick rate:** IDF defaults to 100 Hz; Arduino ran 1000. Without `CONFIG_FREERTOS_HZ=1000`,
  `pdMS_TO_TICKS(8)` in the touch task becomes 0 ticks.
- **BLE's place in the file:** D1/W1 ship with `CONFIG_BT_ENABLED=n`. D4/W5 turn it on.

Per board:

- **Dock:**
  - `CONFIG_ESPTOOLPY_FLASHSIZE_8MB=y`.
  - `CONFIG_ESP_CONSOLE_UART_DEFAULT=y`, `CONFIG_ESP_CONSOLE_UART_BAUDRATE=115200`,
    `CONFIG_ESP_CONSOLE_SECONDARY_NONE=y`.
  - Copy any `CONFIG_LCD_RGB_*` or `CONFIG_GDMA_*` that high_perf's `sdkconfig` sets (none found by name; verify
    with a grep of `~/.platformio-dock/packages/framework-arduinoespressif32-libs/esp32s3/sdkconfig` before
    deleting that package).
- **Wearable:**
  - `CONFIG_ESPTOOLPY_FLASHSIZE_16MB=y`.
  - `CONFIG_ESP_CONSOLE_USB_SERIAL_JTAG=y` (Arduino used HWCDC on USB-Serial/JTAG, `ARDUINO_USB_MODE=1`).

### API replacements (every file that uses them)

| Arduino | ESP-IDF | Bites |
|---|---|---|
| `setup()`/`loop()` | `app_main` on core 1 (above) | Arduino called `nvs_flash_init` and started the loop task; IDF does neither. Call `nvs_flash_init()`. On `ESP_ERR_NVS_NO_FREE_PAGES`/`NEW_VERSION_FOUND`, erase and retry, as Arduino's `initArduino` did; never on any other error, because that would wipe the token. |
| `millis()` | `static inline uint32_t nowMs() { return (uint32_t)(esp_timer_get_time() / 1000); }` in each board's own header | Same 32-bit wrap. Replace mechanically. |
| `delay(n)` | `vTaskDelay(pdMS_TO_TICKS(n))` | Needs the 1000 Hz tick. |
| `ESP.getCycleCount()`, `ESP.restart()` | `esp_cpu_get_cycle_count()`, `esp_restart()` | — |
| `WiFi.*` | `esp_netif_init`, `esp_event_loop_create_default`, `esp_netif_create_default_wifi_sta`, `esp_wifi_init(WIFI_INIT_CONFIG_DEFAULT)`, `esp_wifi_set_storage(WIFI_STORAGE_RAM)` (= `persistent(false)`), `esp_wifi_set_mode(STA)`, `esp_wifi_set_config`, `esp_wifi_connect`/`disconnect`; status from `WIFI_EVENT_STA_DISCONNECTED` / `IP_EVENT_STA_GOT_IP` handlers setting a flag; RSSI and SSID from `esp_wifi_sta_get_ap_info`; scans with `esp_wifi_scan_start(nullptr, true)` + `esp_wifi_scan_get_ap_records` + `esp_wifi_clear_ap_list`; `WIFI_MAC` with `esp_wifi_set_mac` before start; hostname with `esp_netif_set_hostname` | Set `scan_method = WIFI_ALL_CHANNEL_SCAN`, `sort_method = WIFI_CONNECT_AP_BY_SIGNAL` (Arduino's), `threshold.authmode = WIFI_AUTH_WPA2_PSK` and `pmf_cfg.capable = true`. There is no auto-reconnect unless you call it, which is what both join loops already want; the 3.2.0 `WiFi.disconnect()` workaround goes. Event handlers stay tiny: they set flags and copy the reason, and the network task logs it, as now. |
| `configTzTime(tz, ip…)` | `esp_sntp_setoperatingmode(ESP_SNTP_OPMODE_POLL)`, `esp_sntp_setservername(i, "<ip>")` ×3, `esp_sntp_init()`, `setenv("TZ", tz, 1)`, `tzset()` | Keep the servers by IP (the lwIP assert note). Restart SNTP on Wi-Fi up, as now. |
| `HTTPClient` + `NetworkClientSecure` + `Tls` | `esp_http_client` (decision D4): `esp_http_client_open` → `fetch_headers` → `read` loop into the PSRAM body buffer → `close` when not keeping; `esp_http_client_get_status_code`, `get_content_length` (−1 when chunked; `read` handles chunked) | **Response headers:** `esp_http_client_get_header` reads request headers, so take `Date` in the `HTTP_EVENT_ON_HEADER` event handler. **Timeouts:** `timeout_ms` covers connect and read; keep today's values. **Errors:** they are `esp_err_t`, not `HTTPC_ERROR_*`; map them to the same negative codes the perf log prints. **Resume detection:** for the perf log's `tls,resumed|full`, use the connect time, with a one-line `shortcut:` comment (`esp_http_client` doesn't report it). |
| `Preferences` | `nvs_open(ns)`, then `nvs_get/set_str`, `_u8`, `_u32`, `_blob`, `nvs_commit` | Same namespaces and keys. Arduino's `putFloat`/`getFloat` stored a 4-byte **blob**, so `geoLat`/`geoLon` stay blobs or the saved location is lost. `getUInt` → `u32`. Verify on board that the saved picks, nearby list and token load after the reflash. |
| `Wire` | `i2c_new_master_bus` (I2C0, the board's SDA/SCL, 400 kHz) + `i2c_master_bus_add_device` per address; `i2c_master_transmit` / `transmit_receive` | One device handle per **7-bit address**: the dock's CH422G expander answers on several addresses. The bus has its own lock. The wearable still keeps all I2C on the touch task (no need to change proven timing). |
| `Serial` (dock, UART0) | `printf` for output; `uart_driver_install(UART_NUM_0, 1024, 4096, …)` + `uart_vfs_dev_use_driver(0)`; input with `uart_read_bytes(…, 0)`; `availableForWrite` → `uart_get_tx_buffer_free_size` | The perf log writes whole lines only, as now. |
| `Serial` (wearable, USB-Serial/JTAG) | `usb_serial_jtag_driver_install({tx 4096, rx 1024})` + `usb_serial_jtag_vfs_use_driver()`; input with `usb_serial_jtag_read_bytes(…, 0)`; log lines with `usb_serial_jtag_write_bytes(buf, n, pdMS_TO_TICKS(5))`, dropping the line on timeout | Today's 3.2.0 bug (a print waits forever with no host) is the thing to re-test. Flash, close the monitor, and the loop must still run 60 s later (verify on board). `power_log`'s "whole lines only" check becomes "skip the line if fewer than n bytes went". |
| `ESPmDNS` | `mdns_init()`, `mdns_hostname_set("cta-dock-xxxx")`, `mdns_service_add(nullptr, "_ctatracker", "_tcp", 80, txt, 2)` | The **leading underscores**: Arduino's `addService("ctatracker","tcp")` added them, and IDF takes the full names. Check with `dns-sd -B _ctatracker._tcp`. |
| `esp_http_server` (already IDF) | `httpd_config_t cfg = HTTPD_DEFAULT_CONFIG(); cfg.task_caps = MALLOC_CAP_SPIRAM \| MALLOC_CAP_8BIT;` | The stack goes to PSRAM (the field is in 5.4.1). Handlers must not write flash themselves; they already hand writes to the loop through the inbox. |
| `heap_caps_*`, `esp_timer`, FreeRTOS, `esp_lcd_panel_rgb`, RMT, `esp_task_wdt` | unchanged | Only the `#include <Arduino.h>` lines go. |

### Board checks (shape shared by D3 and W4)

- **The before numbers** are Arduino's, from the coordinator, 2026-11-14:
  - **Dock (Task 6 build):**
    - internal free 122.4 KB at boot, 72.1 KB after httpd, 65.7 KB after mDNS;
    - steady minimum 47.6 KB; perf-run minimum 35.8 KB;
    - picker 34.1 fps, map 15.7 fps;
    - tap 24–63 ms, requests 431–716 ms;
    - Wi-Fi up 4.5 s, all stops loaded 10.1 s after boot;
    - CSVs `docs/perf/runs/2026-11-14-task6-{1,2}.csv`.
  - **Wearable (Task 8 build):**
    - internal minimum 73.2 KB over TLS polling plus the HTTP burst;
    - burst of 20 answered in 33–132 ms;
    - dim idle about 40.2 mA, projected battery life 47.1 h.
- **Expected internal RAM gain (my estimate; D4/W5 measure it):**

  | Change | Dock | Wearable |
  |---|---|---|
  | All of mbedTLS to PSRAM (today only blocks ≥ 4 KB go there) | +8–15 KB | +8–15 KB |
  | httpd stack to PSRAM | +4 KB | +4 KB |
  | mDNS task and allocations to PSRAM | +4–8 KB | +4–8 KB |
  | Arduino runtime gone (event task, HWCDC/UART rings, WiFi-lib objects) | +3–8 KB | +3–8 KB |
  | **Total** | **+20–35 KB** | **+20–35 KB** |

  - **Low points without BLE:** dock about 55–70 KB (from 35.8), wearable about 93–108 KB (from 73.2).
  - **BLE cost on IDF:** with the host in PSRAM, one connection and `BLE_MAX_ACT=2`, the controller's internal RAM
    plus the host task stack should be about 25–30 KB, against the ~50 KB measured with NimBLE-Arduino's defaults
    (`docs/ecosystem/day1-link.md`).
  - **Low points with BLE:** dock about 25–45 KB, which is tight and the board most likely to need fallback 1;
    wearable about 63–83 KB.
  - Not verified: whether the S3 controller's memory is taken at boot whenever `CONFIG_BT_ENABLED=y` (static) or only
    while BLE runs. The BLE-off figure in D4/W5 answers it.

---

## Track J — cJSON in the shared library

### Task J0: Tag the last Arduino build

- [x] **Step 1:** `git tag arduino-final 3598b0b && git push origin arduino-final`, the fallback for open question 2.
  No code. (Tagged and pushed 2026-11-14.)

### Task J1: Port `firmware/lib` from ArduinoJson to cJSON

**Files:**
- Modify: `firmware/lib/cta/src/json_util.h`, `json_heap.cpp` (and `json_heap.h` comments only),
  `alerts_weather.cpp`, `approach.cpp`, `bus_parse.cpp`, `bus_picker.cpp`, `link_msg.cpp`, `nearby_msg.cpp`,
  `picks.cpp`, `stop_record.cpp`, `train_cards.cpp`, `train_parse.cpp`, `firmware/lib/cta/library.json` (drop the
  ArduinoJson dependency)
- Create: `firmware/native/lib/cJSON/{cJSON.c,cJSON.h,LICENSE,library.json}` (cJSON 1.7.18, from the upstream
  v1.7.18 tag)
- Modify: `firmware/native/platformio.ini` (`lib_deps` drops ArduinoJson; `lib_extra_dirs = ../lib, lib`), the four
  tests' `unfilteredPasses` assertions **only with Leo's OK** (open question 1), the build-command comments in the
  laptop tools (`-I<ArduinoJson>/src` → `-I../native/lib/cJSON` (path from each tool) plus `cJSON.c` on the
  compile line)

**Interfaces:**
- Unchanged: every public function in `firmware/lib/cta/src/*.h`, and `cta::setJsonHeap(const JsonHeap*)`.
- Changed:
  - `json_util.h` becomes cJSON helpers: `forEachItem(const cJSON*, F)`, `toInt32(const cJSON*, int32_t&)`,
    `noListResult(const cJSON*)`, `minutesFrom`, and `cta::json::Doc`, an RAII owner that calls `cJSON_Delete`.
  - `parseShapes`/`parseFiltered` are deleted: cJSON parses a lone object and a list the same way, and
    `forEachItem` already handles both.
  - `bigAllocator()` is deleted.
  - `setJsonHeap` calls `cJSON_InitHooks({malloc_fn, free_fn})` (cJSON has no realloc hook; it falls back to
    malloc+copy).

**Behaviour to keep (the bites):**
- **Key case:** use `cJSON_GetObjectItemCaseSensitive` everywhere. Plain `cJSON_GetObjectItem` ignores case;
  ArduinoJson didn't.
- **Lengths:** parse with `cJSON_ParseWithLength(json, len)`. Keep every existing length check: `kMaxBody` 1024 for
  link bodies, and the boards' 256 KB body cap.
- **Strings vs numbers:** ArduinoJson's `as<const char*>()` was null for a number, and `is<int32_t>()` false for
  2.5. Mirror that with `cJSON_IsString`/`cJSON_IsNumber` plus an integral check, so mixed-type CTA fields behave
  the same.
- **Writing into fixed buffers:** ~~`cJSON_PrintPreallocated(root, tmp, cap + 5, false)`~~ As built:
  `json::printInto(root, buf, cap, needNul)` prints with `cJSON_PrintUnformatted` (through the hooks), checks the
  length (`n < cap` with the NUL, `n ≤ cap` for a stop record), copies and frees. No spare-byte rule, and it keeps
  "a record over 512 bytes encodes to 0" exact.
- **Floats:** cJSON prints doubles with `%1.15g`, falling back to `%1.17g`. A value that came from a `float` (lat/lon)
  would print 17 digits. Round it first by `snprintf("%.7g")` → `strtod`, so the output stays short and inside the
  512-byte budget.
- **Control characters:** cJSON writes them as `\u00XX`. `test_control_characters_stay_valid_json` must still pass
  unchanged.
- **Nesting (added in J1):** ArduinoJson failed past 10 levels; cJSON recurses up to 1000, which a 1 KB body of
  brackets could use to overflow a task stack. `json::parse` scans the bytes first and rejects nesting past 10.

**As built (J1):**
- `json_util.h` helpers: `parse`, `printInto`, `Doc`, `at` (case-sensitive, the last duplicate wins, as
  ArduinoJson), `str`/`strOr`, `isInt<T>` (an integral number in range), `asInt<T>` and `asBool` (ArduinoJson's
  `as<T>()` conversions), `numOr`, `forEachItem`, `toInt32`, `noListResult`, `minutesFrom`; counters `liveBytes`,
  `peakBytes`, `resetPeakBytes`.
- The hooks are installed by a static initializer in `json_heap.cpp` (so nothing is ever freed by the wrong
  allocator, and the native tests count bytes without calling `setJsonHeap`) and again by `setJsonHeap`. Each block
  carries a size header for the counter (`alignof(max_align_t)`: 8 bytes on the ESP32, 16 on the laptop), so cJSON
  output is freed only with `cJSON_free`/`cJSON_Delete`, and IDF's wifi_provisioning (it `free()`s `cJSON_Print`
  output) must not be linked. `JsonHeap::reallocate` is kept for the signature but unused.
- "PSRAM first, internal if that fails" is each board's `JsonHeap::allocate`: the dock's `psAlloc`, and the
  wearable's `jsonAlloc`, which J1 added to `setup()` (before, the wearable never called `setJsonHeap`, and every
  node would have gone to internal RAM). D2/W3 carry these into `app_main`.
- No encoder writes a float (records, picks, info and config are strings, integers and booleans), so the float
  rounding bite above has nothing to apply to yet.
- The laptop tools compile cJSON as C first (`cc -O2 -c …/native/lib/cJSON/cJSON.c -o /tmp/cJSON.o`), then link
  `/tmp/cJSON.o`.
- Behaviour differences found by running the old and new parsers on the fixtures and ~1500 mutants: a whole number
  written as a float (`2.0`, `1e0`) now reads as an integer; doubles keep full precision (ArduinoJson stored
  41.8781 as 41.878098); single-quoted strings, unquoted keys and a lone surrogate escape are now malformed; control
  characters in `encodeInfo`/`encodeConfig`/`encodePicks` strings are escaped instead of written raw; a lone `pt`
  object inside a list of bus patterns is now read (the old list filter dropped it).
- Version: the installed Arduino libs package (IDF 5.5.5) carries cJSON 1.7.19, so whether 54.03.20's espidf is
  1.7.18 is unverified until the first espidf build. 1.7.18 → 1.7.19 is a bug-fix release.

**Memory:** the hooks send every node to PSRAM (`heap_caps_malloc(n, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT)`, internal
if that fails); on the laptop they use plain malloc.
- A train reply (~7 KB) becomes roughly 40–70 KB of nodes in PSRAM.
- A 70 KB bus-patterns reply becomes up to about 1 MB, against today's ~100 KB filtered document. Both boards have
  8 MB of PSRAM.
- Add one debug counter in the hooks: live bytes and peak bytes, exposed as `cta::json::peakBytes()`. It feeds
  open question 1's replacement assertion and the boards' perf/power logs.
- `shortcut: unfiltered trees cost more PSRAM and parse time; parse patterns incrementally only if the dock's PSRAM
  low point drops under 1 MB`.

- [x] **Step 1:** Vendor cJSON into `firmware/native/lib/cJSON` and switch `native/platformio.ini`. Run
  `pio test -e native`; expect build failures in the lib (ArduinoJson includes).
- [x] **Step 2:** Port `json_util.h` and `json_heap.cpp`, then the parsers one at a time, running each one's suite
  (`-f test_train`, `test_bus`, `test_alerts_weather`, `test_stop_record`, `test_link`, `test_link_follow`,
  `test_nearby`, `test_approach`, `test_train_cards`, `test_dock`) until it passes unchanged.
- [x] **Step 3:** The `unfilteredPasses` assertions: as Leo decides (open question 1). Without his answer, leave them
  failing in a scratch run, don't commit a change to them, and ask.
- [x] **Step 4:** `pio test -e native` passes in full. Grep: `grep -rn "ArduinoJson\|JsonDocument" firmware/lib
  firmware/native` finds nothing. Build the laptop tools with their updated command lines; `tools/preview.cpp`
  renders the same PNGs (byte-compare against a render from before the change).
- [ ] **Step 5:** Review (Agent-Workflow), then commit: `refactor(lib): cJSON instead of ArduinoJson, nodes in
  PSRAM`. Merge to main first of all tracks.

---

## Track D — dock

### Task D1: The dock builds and boots on ESP-IDF

**Files:**
- Modify: `firmware/dock/platformio.ini` (both envs; `perf` keeps `-DPERF_LOG`)
- Create: `firmware/dock/CMakeLists.txt`, `firmware/dock/src/CMakeLists.txt`, `firmware/dock/src/idf_component.yml`,
  `firmware/dock/sdkconfig.defaults`, `firmware/dock/partitions.csv`
- Modify: `.gitignore` (D owns the edit; W rebases onto it)

- [ ] **Step 1:** Write the project files from the shared reference, with BLE off.
- [ ] **Step 2:** `main.cpp`: `app_main` runs today's setup and loop. `perf_log`'s boot line keeps its format, so
  `tools/dock_perf_report.py` still parses it.
- [ ] **Step 3:** `pio run` builds once the Arduino includes are stubbed out file by file (D2 replaces them). Flash
  it with `--upload-port` on the dock's CH343 port. The serial boot line shows IDF 5.4.1, XIP from PSRAM, and a
  64-byte data cache line (verify on board).

### Task D2: Replace every Arduino API on the dock

**Files:** `firmware/dock/src/main.cpp`, `net.cpp`/`net.h`, `link.cpp` (Task 6), `store.cpp`, `display.cpp` (GT911
and CH422G over `i2c_master`), `leds.cpp`, `perf_log.cpp`, `stress.cpp`, and a new `firmware/dock/src/clock_ms.h`
(`nowMs()`).

**Interfaces:** every module's header stays as it is today (`net::`, `link::`, `store::`, `display::`), so the UI and
`main.cpp` logic don't change.

- [ ] **Step 1:** Make the mechanical swaps: `millis`, `delay`, `ESP.*`, `Serial`, includes.
- [ ] **Step 2:** `store.cpp` → NVS (same keys and types; geo floats as blobs). `link.cpp`: mDNS with underscores;
  httpd `task_caps` in PSRAM.
- [ ] **Step 3:** `net.cpp`:
  - esp_wifi with event flags, the same backoff, and the serial `w` scan check;
  - `esp_http_client` per decision D4, with one handle kept per host (trains, alerts, weather, location; the bus
    host gets a fresh handle per request, as today);
  - the PSRAM body buffer;
  - `Date` taken from the header event;
  - `resolve()` kept;
  - `tlsCalloc` and `mbedtls_platform_set_calloc_free` deleted (`CONFIG_MBEDTLS_EXTERNAL_MEM_ALLOC`).
- [ ] **Step 4:** `display.cpp`: `Wire` → `i2c_master` (one handle per address). The RGB panel and bounce-buffer code
  stays as is.
- [ ] **Step 5:** `pio run -e dock` and `-e perf` build. `grep -rn "Arduino\|WiFi\.\|Serial\.\|String\b\|millis"
  firmware/dock/src` finds nothing but comments.

### Task D3: Dock board checks against the gates

Run on the board, with Wi-Fi from `secrets.h` and the token set with serial `T <hex>`.

- [ ] **Boot and pixels:** with no picks, then with 3 picks, grab `python3 firmware/dock/tools/frame_grab.py <port>
  out.png` on the Home, map and picker screens. Each must be byte-identical to the same grab from the `arduino-final`
  build at the same data (use the stress test's fixed data, or freeze with the mock picks).
- [ ] **Touch:** taps and the picker scroll respond; the 3-finger 10 s reset works.
- [ ] **Network:** Wi-Fi up ≤ 4.5 s, all stops loaded ≤ 10.1 s after boot. `link_send.py cta-dock-xxxx.local info`,
  `config` and `--garbage` behave as Task 6's Step 3. `dns-sd -B _ctatracker._tcp` lists the dock.
- [ ] **Gates:** `python3 tools/dock_perf_run.py`, 3 runs. Pass means all of:
  - lowest internal RAM **≥ 40 KB** (Arduino 35.8);
  - picker **≥ 34 fps**, map **≥ 16 fps** (Arduino 15.7);
  - taps no worse than 24–63 ms, requests no worse than 431–716 ms;
  - no late panel frame;
  - perf log `tls,t,resumed` on the train host's reopens.
  - If the map is still under 16, it is a drawing problem, not the port; flag it to the orchestrator for B2 rather
    than tuning inside this task.
- [ ] **Commit:** review first, then `feat(dock): native ESP-IDF build, no Arduino (with the Task 6 link)`, and
  write the run results into `docs/perf/log.md`.

### Task D4: Measure BLE on the dock

**Files:** `firmware/dock/sdkconfig.defaults` (BLE lines on), `firmware/dock/src/ble_setup.h/.cpp` (just enough:
init, advertise `CTA-Dock-xxxx` with the service from `ble_ids.h`, an `info` read, deinit), `main.cpp` (serial `b`
toggles BLE, as the spike did).

- [ ] **Step 1:** Boot with BLE off and record internal free at boot and the minimum over 10 minutes of normal
  polling (TLS every 30 s or faster).
- [ ] **Step 2:** `b` on, with an iPhone connected in LightBlue, for 10 minutes; record the minimum. Then `b` off,
  then on and off again; free memory must return to within 2 KB each time (no leak).
- [ ] **Step 3:** Pass means a minimum **≥ 40 KB with BLE on**. If it fails, work down this ladder, re-measuring each
  time, and record every step:
  1. `CONFIG_BT_NIMBLE_MSYS_1_BLOCK_COUNT` down to 12 and the ACL buffer counts down; `BLE_MAX_ACT=1`.
  2. Wi-Fi static RX 8 → 6 and dynamic TX buffers (`CONFIG_ESP_WIFI_DYNAMIC_TX_BUFFER=y`, which go to PSRAM).
  3. `LWIP_MAX_SOCKETS` 16 → 10.
  4. **Fallback 1:** BLE only while unconfigured, with the net task pausing CTA polling while BLE runs.
  5. **Fallback 2:** Appendix A.
- [ ] **Step 4:** Write the table (boot, BLE off, BLE on, after off) into `docs/ecosystem/day1-link.md` under a new
  "ESP-IDF" heading. No network names.

### Task D5: Dock BLE setup (ecosystem Task 7, BLE version)

The original Task 7 from `21a0fd7` (the plan before Appendix A), on IDF.

**Files:**
- Modify: `firmware/dock/src/ble_setup.h/.cpp`, `main.cpp`, `dock_ui.cpp` (the passkey and "Joining Wi-Fi…" setup
  messages), `store.*` (a saved Wi-Fi slot: `saveWifi`/`loadWifi`, NVS), `net.cpp` (join the saved slot plus the
  `secrets.h` networks, strongest first)
- Modify: `firmware/lib/cta/src/ble_ids.h`: add `kTokenUuid = "5b1e6f0a-3c2d-4e8f-9a7b-1d2c3e4f5a60"`, mirrored in
  CTAKit `BLEID.token` (Track A/K).

**Interfaces:**
`namespace ble_setup { void start(const char* name); void stop(); bool running(); uint32_t passkey(); }`

**What changes for IDF, compared with the original task:**
- **Library and config:** NimBLE-Arduino is replaced by IDF's NimBLE (`nimble_port_init`,
  `ble_svc_gap_init`/`ble_svc_gatt_init`, `ble_gatts_count_cfg`/`ble_gatts_add_svcs`,
  `nimble_port_freertos_init`). There is no `platformio.ini` library line; it is all `sdkconfig`.
- **Characteristics:** `wifi` and `token` use `BLE_GATT_CHR_F_WRITE_ENC | BLE_GATT_CHR_F_WRITE_AUTHEN`.
- **Pairing:** `ble_hs_cfg.sm_io_cap = BLE_HS_IO_DISPLAY_ONLY`, `sm_bonding = sm_mitm = sm_sc = 1`. On
  `BLE_GAP_EVENT_PASSKEY_ACTION`, pick a passkey with `esp_random() % 1000000`, show it on screen (never log it),
  then call `ble_sm_inject_io`.
- **Stopping:** `stop()` calls `nimble_port_stop()` + `nimble_port_deinit()`; deinit releases the controller too.
  Never call `esp_bt_mem_release`, because BLE must be able to start again (`POST /ble`).
- **Bonds and reset:** bonds live in NVS (`CONFIG_BT_NIMBLE_NVS_PERSIST`). `store::wipeLink()` also calls
  `ble_store_clear()`.
- **The writes:** they arrive on the NimBLE host task. Copy them into the link inbox under its mutex; the loop saves
  them to NVS and joins. No flash writes or Wi-Fi calls from the host task.

- [ ] **Step 1:** Implement under the Global Constraints rule: BLE runs only with no working Wi-Fi (none joined for
  60 s, or none saved) or for 120 s after `POST /ble`; otherwise it is stopped and its memory freed. Writes:
  - `info` (read);
  - `wifi` (encrypted write, `parseWifi(…, 1, …)`: save, then join);
  - `token` (encrypted write, `parseToken`: save).
- [ ] **Step 2: Verify with LightBlue:**
  - a fresh dock (`R`) advertises and passkey pairing works;
  - writing `{"slot":0,"ssid":"…","pass":"…"}` joins the home network;
  - writing a token makes `link_send.py config` accepted;
  - BLE stops itself, and the serial RAM line returns to the D3 level;
  - re-run D3's gates with one BLE window open during a fetch round.
- [ ] **Step 3:** Commit: `feat(dock): BLE setup on ESP-IDF (passkey, Wi-Fi, token)`.

---

## Track W — wearable

### Task W1: The wearable builds and boots on ESP-IDF

**Files:**
- Modify: `firmware/handheld/mock/platformio.ini`: envs `mock`, `live` and `power`, each keeping its `-D` flags;
  `build_src_filter` goes (decision D13).
- Create: `CMakeLists.txt`, `src/CMakeLists.txt`, `src/idf_component.yml` (mdns, `esp_lcd_co5300 ^2.2.0`),
  `sdkconfig.defaults`, `partitions.csv` (all under `firmware/handheld/mock/`)
- Modify: `live.cpp`, `live_model.cpp`, `power.cpp` (wrap them in `#ifdef HANDHELD_LIVE`); `power_log.cpp`,
  `stress.cpp` (`#ifdef POWER_LOG`), the files `build_src_filter` used to exclude

- [ ] **Step 1:** Write the project files, with BLE off and the console on USB-Serial/JTAG.
- [ ] **Step 2:** `app_main` runs today's setup and loop on core 1. `pio run -e mock` builds once each Arduino
  include is stubbed (W2/W3 replace them). The boot line shows IDF 5.4.1 (verify on board).

### Task W2: Display, touch and PMU without Arduino libraries

**Files:**
- Modify: `firmware/handheld/mock/src/main.cpp` (display init, the send task, brightness, touch reads)
- Create: `firmware/handheld/mock/src/cst9217.h/.cpp`, `axp2101.h/.cpp`
- Modify: `power.cpp` (uses `axp2101`)

**Interfaces:**

```cpp
namespace cst9217 {
bool begin(i2c_master_bus_handle_t bus, int rstPin, int intPin);
uint8_t read(int16_t* x, int16_t* y, uint8_t max);   // touch points, panel coordinates as SensorLib's getPoint
}
namespace axp2101 {
bool begin(i2c_master_bus_handle_t bus);   // enables the battery, VBUS, system and temperature ADCs, as power.cpp's begin did
bool vbusIn();
bool batteryConnected();
bool charging();
uint16_t vbusMv();
uint16_t battMv();
uint16_t sysMv();
int8_t percent();
float tempC();
void shutdown();
}
```

`power::State` and `power::` functions don't change.

- [ ] **Step 1: The display.** Use `spi_bus_initialize(SPI2_HOST, CO5300_PANEL_BUS_QSPI_CONFIG(38, 4, 5, 6, 7,
  kStripBytes), SPI_DMA_CH_AUTO)`, then `esp_lcd_new_panel_io_spi` with CS 12 at 80 MHz, then
  `esp_lcd_new_panel_co5300` (`use_qspi_interface = 1`, RST 39, 16 bpp).
  - **Init commands:** before removing the library, copy `Arduino_CO5300`'s init sequence from `.pio/libdeps` (GFX
    Library for Arduino 1.6.9) into `vendor_config.init_cmds`, so the panel state (COLMOD, MADCTL, TE, brightness
    mode) is identical.
  - **Offset:** `esp_lcd_panel_set_gap(panel, 6, 0)`.
  - **Brightness:** `esp_lcd_panel_io_tx_param(io, 0x51, &v, 1)` replaces `setBrightness`.
- [ ] **Step 2: The send task.** `gfx->draw16bitBeRGBBitmap(x, y, px, w, h)` becomes
  `esp_lcd_panel_draw_bitmap(panel, x, y, x + w, y + h, px)`; the bytes are already big-endian panel order, so pass
  them as is.
  - **Wait after each call:** draw_bitmap is **asynchronous**, so take a semaphore given by `on_color_trans_done`
    after every call. Arduino_GFX returned with the buffer free, and the strip double-buffering relies on that.
  - **Odd edges:** if odd x or width shows garbage, round the window out to even columns (the CO5300 wants even
    bounds; verify on board).
  - **TE:** no TE sync is in the code today (B1's report isn't on main), so none is added. B1 resumes on the IDF
    send path.
- [ ] **Step 3: Touch and PMU.** `i2c_new_master_bus` on SDA 15, SCL 14. The touch task (8 ms) reads `cst9217` and,
  once a second, `axp2101`, as now. Touch reset is 40, INT 11. The touch-to-screen mapping `(W-1-ty, tx)` stays.
- [ ] **Step 4: Pixels.** The serial `profile` command prints the frame hash per screen. On the mock build, check
  that Home, Stops, Detail and Dim each hash identically to the `arduino-final` mock build at the same build-time
  clock, and look at the panel once.

### Task W3: Network, storage, serial and clock on the wearable

**Files:** `live.cpp`/`live.h`, `link.cpp` (Task 8), `live_store.*` (unchanged unless it includes Arduino),
`cpu_clock.cpp`, `power_log.cpp`, `stress.cpp`, `main.cpp` (serial), and a new `src/clock_ms.h` (`nowMs()`).

- [ ] **Step 1:** Make the mechanical swaps (shared table).
- [ ] **Step 2:**
  - `live.cpp`:
    - esp_wifi with the Wi-Fi list joining the strongest (`strongestSaved`), and `homeWifi` from
      `esp_wifi_sta_get_ap_info`'s SSID;
    - `esp_http_client` per decision D4; the PSRAM body buffer;
    - `resolve()` and the DNS pause kept;
    - `tlsCalloc` deleted;
    - `Preferences` → NVS (namespace `trackie`, key `nearby`, a string).
  - `link.cpp`: mDNS with underscores; httpd stack in PSRAM.
- [ ] **Step 3:** `cpu_clock.cpp`: `setCpuFrequencyMhz` → `rtc_clk_cpu_freq_mhz_to_config` +
  `rtc_clk_cpu_freq_set_config` (decision D9).
- [ ] **Step 4:** Serial on USB-Serial/JTAG per the shared table. The `nearby` line (about 250 bytes) still arrives
  whole: rx buffer 1024.
- [ ] **Step 5:** `pio run` for `mock`, `live` and `power`; the same grep as D2 is clean.

### Task W4: Wearable board checks against the gates

- [ ] **Boot, pixels, touch:** W2 Step 4's hashes. Gestures as listed in the handheld README ("Using it"). With no
  monitor attached, the loop keeps running past 60 s (the USB print bug).
- [ ] **Network:**
  - `send_nearby.py` fills the cards;
  - `link_send.py cta-pet-xxxx.local info`, `nearby`, `pin` and `follow` behave as Task 8's Step 3;
  - `--burst` answers all 20 within 500 ms (Arduino 33–132 ms); `--garbage` gives 400/413/401/401;
  - `dns-sd -B _ctatracker._tcp` lists it.
- [ ] **RAM:** internal minimum over TLS polling plus the burst **≥ 40 KB** and not below Arduino's 73.2 KB.
- [ ] **Power:** `python3 tools/power_run.py`, interleaved A/B against `arduino-final`, under the power analysis
  rules (`docs/superpowers/specs/2026-11-09-handheld-power-analysis-design.md`). Dim idle within 5% of 40.2 mA, and
  projected battery life ≥ 44.7 h (5% under 47.1). Stops scroll no slower than today (about 23 fps, 27 ms frames).
- [ ] **TLS:** the train host resumes (power log `tls` events).
- [ ] **Commit:** review first, then `feat(handheld): native ESP-IDF build, no Arduino`, and write the run results
  into `docs/power/log.md`.

### Task W5: Measure BLE on the wearable

The same as D4, on the wearable: `ble_setup.*` minimal, serial `b`, 10 minutes BLE off, then 10 minutes on with
LightBlue connected and TLS polling running, then off/on/off with no leak. Pass means **≥ 40 KB with BLE on**. If it
fails, use D4's ladder. Results go into `docs/ecosystem/day1-link.md` under "ESP-IDF".

### Task W6: Wearable BLE setup (ecosystem Task 9, BLE version)

The original Task 9 from `21a0fd7`, on IDF, with the same IDF changes as D5.

**Files:** `firmware/handheld/mock/src/ble_setup.h/.cpp` (the same interface as the dock's), `main.cpp`, `ui.cpp`
setup states only (the passkey and "Joining Wi-Fi…" in Trackie's bubble; coordinate with H-UI), `live.cpp` (Wi-Fi
slots 0–4 saved in NVS take priority over `secrets.h`).

- [ ] **Step 1:** Implement as D5, with 5 slots (`parseWifi(…, cta::kWifiSlots, …)`), the token and the passkey in
  the bubble.
- [ ] **Step 2: Verify with LightBlue:** a fresh wearable pairs; two networks written to slots 0 and 1; it joins the
  stronger; BLE stops; `POST /ble` reopens it for 120 s. Power A/B with BLE stopped is within 5% of W4's run.
- [ ] **Step 3:** Commit: `feat(handheld): BLE setup on ESP-IDF with up to 5 networks`.

---

## Track E — undo Appendix A in the docs

### Task E1: Bring the BLE tasks back into the ecosystem plan and spec

Run after D4 and W5 have numbers. If either needed fallback 1, write that rule in instead.

**Files:** `docs/superpowers/plans/2026-11-14-ecosystem.md`, `docs/superpowers/specs/2026-11-14-ecosystem-design.md`
(§12 and the Appendix A decision note), `todo.md` F17 (done: the port), and `docs/ecosystem/day1-link.md` (link
the ESP-IDF table). The spec edit carries out Leo's 2026-11-14 decision, so put the diff in the report for him to
see.

- [ ] **Step 1:** In the ecosystem plan:
  - Replace the Appendix A decision box with "Decision, 2026-11-14 (Leo): native ESP-IDF, BLE setup back"
    (numbers from D4/W5).
  - Restore the Global Constraints lines on BLE (Wi-Fi passwords cross only the bonded BLE link; BLE only without
    working Wi-Fi or 120 s after `POST /ble`).
  - Change the tech stack line from arduino-esp32 to ESP-IDF 5.4.1, NimBLE (IDF), cJSON.
  - Tasks 7 and 9 point at D5/W6 here (done there).
  - **Task 12:** back to the BLE setup flow from `21a0fd7`: `BLESetup.swift`, Pair → Wi-Fi → Pet → Ready, keeping
    `NSBluetoothAlwaysUsageDescription`. Task 10's note stops removing it.
  - **Task 19:** un-dropped. Its 19a lands on the IDF wearable through `ble_setup.cpp`'s `snapshot`
    characteristic, and its RAM is checked against W5's number.
  - Appendix A stays as the documented fallback.
- [ ] **Step 2:** In the spec, §12 "Future work: Bluetooth" becomes the current design: setup over BLE, and the
  relay in scope for showcase step 7 if Task 19 passes by 11/21. Check the diff for network names, home paths and
  tokens.
- [ ] **Step 3:** Commit (docs only, no review): `docs(ecosystem): ESP-IDF port lands; Bluetooth setup is back`.

---

## Review focus (for every reviewer on this plan)

1. **Behaviour parity, not just a build.** Look for each bite above:
   - the core 1 loop and the 1000 Hz tick;
   - NVS float blobs;
   - mDNS underscores;
   - response headers read in the event handler;
   - case-sensitive cJSON lookups;
   - `printInto`'s length check (with or without the NUL);
   - float printing;
   - draw_bitmap's asynchrony;
   - the USB print with no host.
2. **Memory:**
   - every cJSON node and all of mbedTLS in PSRAM;
   - no task writes flash from a PSRAM stack;
   - the 40 KB floor logged with `heap_caps_get_minimum_free_size(MALLOC_CAP_INTERNAL)`.
3. **Secrets:**
   - log level ≤ WARN, so no URL with keys is ever printed;
   - the passkey, Wi-Fi password and token never logged;
   - TLS verified with the certificate bundle on every full handshake.
4. **Tests:** the native suite unchanged apart from Leo's answer on the `unfilteredPasses` assertions.

## Not verified from vendor docs (verify on board or at first build)

- That pioarduino 54.03.20's `framework = espidf` package is IDF 5.4.1 (expected from the platform's numbering).
- Exact Kconfig names for mDNS PSRAM placement and the NimBLE pool sizes in the resolved component versions.
- Whether `save_client_session` resumes with the train API (it only offers tickets).
- Whether the CO5300 needs even column bounds through `esp_lcd_co5300`, and whether its default init matches
  Arduino_GFX's (the plan copies Arduino_GFX's sequence to avoid depending on it).
- The CST9217 protocol details (taken from SensorLib's source at port time), and Waveshare's own IDF touch and PMU
  path (their examples use a BSP component that wasn't inspected).
- The S3 BLE controller's internal RAM, and whether it is reserved at boot with `CONFIG_BT_ENABLED=y`.
- USB-Serial/JTAG driver behaviour with no host attached in 5.4.1.
- Whether ArduinoJson (header-only) builds under espidf, which matters only if J isn't merged before a board build.
