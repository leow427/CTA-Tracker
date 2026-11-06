# Firmware toolchain (pinned)

Recorded 2026-11-05 while building the dock firmware. The day-1 spike's hardware steps (plan 1 task 1: flashing,
iPhone pairing, HTTPS alongside BLE) are still Leo's; everything below builds and links on the laptop.

| Piece | Version | Where |
|---|---|---|
| PlatformIO Core | 6.1.19+ | `pipx install platformio` |
| Platform | pioarduino `55.03.311` | `https://github.com/pioarduino/platform-espressif32/releases/download/55.03.311/platform-espressif32.zip` |
| Arduino-ESP32 core | 3.3.11 (ESP-IDF 5.5) | pulled in by the platform |
| LVGL | 9.2.2 | dock (`lvgl/lvgl@9.2.2`); the handheld should pin the same |
| NimBLE-Arduino | 2.3.6 | builds and links next to core 3.3's own BLE library; pairing still to be proven on the board |
| ArduinoJson | 7.4.3 | `firmware/lib/cta` |
| Adafruit NeoPixel | 1.15.1 | dock LEDs (RMT) |
| Unity | 2.6.1 | native tests |

**Dock board settings** (`firmware/dock/platformio.ini`): `board = esp32-s3-devkitc-1`,
`board_build.arduino.memory_type = qio_opi`, `board_build.psram_type = opi`, `board_upload.flash_size = 8MB`,
`board_build.partitions = default_8MB.csv`, USB CDC on boot.

**Display stack (dock).** Instead of ESP32_Display_Panel + ESP32_IO_Expander, the dock drives the panel with
ESP-IDF's own `esp_lcd` RGB driver (16-bit RGB565, 16 MHz pixel clock, 800×10 bounce buffer, frame buffer in
PSRAM), plus about 60 lines for the GT911 touch controller and the CH422G expander. The registry copies of those
libraries are 2023 versions without board presets, and IDF's driver is already in the core. Pins and timings are
Waveshare's demo values (`firmware/dock/include/pins.h`). LVGL draws portrait (480×800) and the flush callback
rotates into the landscape frame buffer.

**HTTPS.** TLS is verified against the Mozilla CA bundle that ESP-IDF links into mbedTLS
(`CONFIG_MBEDTLS_CERTIFICATE_BUNDLE_DEFAULT_FULL=y` in the core's sdkconfig):

```cpp
extern const uint8_t kCaBundleStart[] asm("_binary_x509_crt_bundle_start");
extern const uint8_t kCaBundleEnd[] asm("_binary_x509_crt_bundle_end");
NetworkClientSecure tls;
tls.setCACertBundle(kCaBundleStart, kCaBundleEnd - kCaBundleStart);
```

**Commands**

| What | Command |
|---|---|
| Shared library tests | `cd firmware/native && pio test -e native` |
| Dock firmware | `cd firmware/dock && pio run` (flash: `pio run -t upload -t monitor`) |
| Dock UI preview on the laptop | `cd firmware/dock && pio run -e preview -t exec` → `firmware/dock/preview/out/*.png` |
| Sprites | `python3 tools/sprite_build.py` (after editing `art/*.txt`) |
