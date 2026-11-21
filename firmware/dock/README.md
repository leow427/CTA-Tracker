# Dock firmware

The dock on the Waveshare ESP32-S3-Touch-LCD-4.3 (8 MB flash, 8 MB octal PSRAM): the stops, map and picker screens
in portrait, live CTA data over Wi-Fi, the three LED rows, and the HTTP link to the app (ecosystem spec 4).

## Build and flash

```bash
cd firmware/dock
~/.platformio/penv/bin/pio device list            # the dock is the CH343, USB 1A86:55D3
~/.platformio/penv/bin/pio run -t upload --upload-port /dev/cu.usbmodem<the dock's>
```

- It needs `firmware/secrets.h` (git-ignored; template `firmware/secrets.example.h`) with the CTA keys and
  `DOCK_WIFI_SSID` / `DOCK_WIFI_PASS`.
- Always pass `--upload-port`: the wearable's native USB is also a `/dev/cu.usbmodem…`.
- `-e perf` is the same build plus the perf logger and the stress test (`tools/dock_perf_run.py` flashes and runs it).
- Serial runs on the same port at 115200. Open it with DTR and RTS off: RTS on the CH343 holds the dock in reset.

## Over-the-air updates

With `DOCK_OTA_KEY` set in `secrets.h`, the dock takes a new build over Wi-Fi at `POST /ota` (the two app slots in
`partitions.csv`; NVS, the pairing and the picks stay). The first install of an OTA-capable build needs a USB flash.

```bash
curl --data-binary @firmware.bin -H "X-OTA-Key: <key>" http://<dock>.local/ota
```

`firmware.bin` is in the build folder (`~/.platformio-dock/build/dock/dock/`). The dock checks the image, replies,
and reboots into it. There's no rollback: a build that won't boot is fixed with a USB flash.

## Toolchain

The dock is native ESP-IDF 5.4.1, built by PlatformIO (`framework = espidf`, pioarduino platform 54.03.20) in its
own PlatformIO folder, `~/.platformio-dock`.

- **Build output:** it goes to `~/.platformio-dock/build/dock`, because ESP-IDF refuses a build path with a space in
  it. For the same reason, `CONFIG_COMPILER_HIDE_PATHS_MACROS` is off.
- **`sdkconfig.defaults` holds every SDK setting:**
  - code and read-only data run from PSRAM (XIP), with 64-byte data cache lines, so flash writes never stall the
    RGB panel's refills;
  - Wi-Fi and lwIP buffers are in PSRAM;
  - driver interrupt code is out of IRAM;
  - all of mbedTLS, mDNS and the httpd stack are in PSRAM;
  - Bluetooth is off: the dock is Wi-Fi only.
  - After changing it, delete `sdkconfig.dock` / `sdkconfig.perf`: an existing one wins.
- **Components:** `src/idf_component.yml` pulls mDNS, and `dependencies.lock` pins it.
- **Boot log:** it says which SDK is running, for example `display: SDK 5.4.1, XIP from PSRAM on, data cache line 64 B`.

## What runs where

- **Core 1:** the frame loop (`main.cpp`, `app_main`) and the panel's interrupts.
- **Core 0:**
  - the network task (`net.cpp`): `esp_wifi`, `esp_http_client` with one client per host and TLS session resumption,
    and SNTP;
  - the touch task (GT911 over `i2c_master`);
  - the LED task (RMT);
  - the httpd and mDNS link (`link.cpp`).
- **Saved state:** NVS namespace `dock` (`store.cpp`) holds the picks, the location, the token, Trackie's colors and
  the settings.
- **Serial:** output goes out one UART write per line (`serial_out.h`). The commands are listed at the top of
  `src/main.cpp`.

## Numbers

Measured with `tools/dock_perf_run.py` (`docs/perf/log.md`, "ESP-IDF port"):

| | |
|---|---|
| Lowest free internal RAM in a stress run | 73 KB |
| Picker scroll | 34 fps |
| Map pan | 15.7 fps |
| Taps | 24–65 ms |
| Requests | 344–637 ms |

The dock has no Bluetooth (Leo, 2026-11-14). What it cost while it was compiled in is in
`docs/ecosystem/day1-link.md`, "ESP-IDF".
