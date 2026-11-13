# The handheld on the dock's core (2026-11-12/13)

The handheld (`firmware/handheld/mock`, envs `live`, `mock`, `power`) moved from stock pioarduino to the dock's pinned build: Espressif's prebuilt "high_perf" Arduino SDK. Same firmware otherwise. Power and speed A/B: `../log.md` (last entry) and `core32h.md`.

## Versions

| | Before | After |
|---|---|---|
| Platform | pioarduino 55.03.311 | pioarduino 54.03.20 (the dock's pin) |
| Arduino core | 3.3.11 | 3.2.0 |
| SDK libraries | 5.5.5+sha.b774170ff46 (lib-builder master) | 5.4.1+sha.2f7dcd862a (lib-builder high_perf/v3.2.0), `esp32-3.2.0-h.zip` |
| PlatformIO folder | `~/.platformio` | `~/.platformio-dock`, shared with the dock |

**Why share `~/.platformio-dock`:** it already held exactly this platform and these libraries, so the handheld added 0 bytes there. `~/.platformio` keeps 3.3.11 for anything else. Both projects pin the same URLs, so they can't pull the folder different ways.

**Build with `~/.platformio/penv/bin/pio`:** the pinned platform runs `python -m pip`, and Homebrew's Python has no pip.

## sdkconfig (esp32s3 `qio_opi`, this board's 16 MB QIO flash and octal PSRAM)

| Setting | 3.3.11 | 3.2.0 high_perf |
|---|---|---|
| SDK optimisation | `COMPILER_OPTIMIZATION_SIZE` (-Os) | `COMPILER_OPTIMIZATION_PERF` (-O2) |
| Code and read-only data | flash | PSRAM: `SPIRAM_XIP_FROM_PSRAM`, `SPIRAM_FETCH_INSTRUCTIONS`, `SPIRAM_RODATA` |
| Data cache line | 32 B | 64 B (`ESP32S3_DATA_CACHE_LINE_64B`) |
| Wi-Fi and lwIP buffers | internal RAM | PSRAM when possible (`SPIRAM_TRY_ALLOCATE_WIFI_LWIP`) |
| Wi-Fi TX buffers | 8 static | 32 dynamic |
| Wi-Fi RX block-ack window | 8 | 6 |
| BLE host | NimBLE | Bluedroid |

**Unchanged:** 32 KB data cache; 16 KB instruction cache with 32 B lines; 240 MHz default; power management off; FreeRTOS run-time stats on (the power log needs them); 80 MHz octal PSRAM; WPA2-Enterprise support on.

**Boot line:** `sdk: arduino 3.2.0, esp-idf v5.4.1-1-g2f7dcd862a-dirty, code runs from PSRAM (XIP), data cache line 64 B`. On 3.3.11 it reads `arduino 3.3.11, esp-idf v5.5.5, code runs from flash, data cache line 32 B`.

## Code changes

- **`platformio.ini`:** the dock's platform, SDK libraries and PlatformIO folder.
- **`main.cpp`, boot line:** prints the SDK, where code runs, and the data cache line.
- **`live.cpp`, `Tls::stop()` re-attaches the CA bundle.**
  - Core 3.2's `stop_ssl_socket` zeroes the TLS context, callback included; 3.3 keeps it.
  - Shown on the board: without the re-attach, every HTTPS request failed ("useRootCABundle is set, but attach_ssl_certificate_bundle … was not called"), the first one included. It fails closed: verification stays on, so the handshake can't find a trusted root.
- **`main.cpp`, `Serial.setTxTimeoutMs(5)` instead of 0.**
  - On 3.2 a timeout of 0 makes `HWCDC::write` wait forever once the host stops taking data: its retry count starts at the timeout and wraps.
  - Shown on the board: flashed and left with no monitor, the loop froze 20 s after boot and stayed frozen 130 s until the port was opened. The old core and the fix both ran 150 s without a gap.
- **`live.cpp`, `esp_wifi_disconnect()` at the 20 s join timeout.** 3.2's `WiFi.disconnect()` does nothing until the board is associated, so a slow join (WPA2-Enterprise) would carry on into the back-off and the scan.
- **`live.cpp`, the disconnect logger moved to ESP-IDF's event loop.** 3.2's Arduino event list has no lock (`NETWORK_EVENTS_MUTEX` is never defined), and the handler was added while the event task was dispatching. The handler only notes the reason, since that task's 2 KB stack is too small for `printf`; the network task prints it.
- **`power.*` and `main.cpp`, serial `O` turns the board off.** The AXP2101 cuts every rail. It's done by whichever task owns the I2C bus, and it says so when there's no power chip. Leo asked for the board to be off after testing, against burn-in.
  - Tried with USB in and no battery: it stayed off for 3 minutes, and its USB port went away.
  - The PWR button should turn it back on; not tried yet.
- **`tools/power_run.py`:** flashes with PlatformIO's own `pio` (Homebrew's can't build the pinned platform) and only on the port it's given (the dock's serial chip is a `usbmodem` port too).
- **`README.md`:** the run command, a Toolchain section on these differences, serial notes, the new speed numbers, and the internal RAM note.

**The dock's `NetworkClient setSocketOption … errno 9`:** harmless. After `stop()`, 3.2 leaves the TLS socket number at 0, and 3.2's `connected()` calls `read()` even when closed, which sets a socket timeout on fd 0. That isn't an lwIP socket, so the call fails with EBADF and does nothing; 3.3 only reads while connected. The handheld logs it too, and every request after it succeeded.

**Also checked, no change needed:** HTTPClient (only header storage changed), Preferences/NVS, Wire (reads are small), `setCpuFrequencyMhz` (the same on the S3), the WPA2-Enterprise `begin` signature, Wi-Fi sleep defaults, the `default_16MB` partitions (identical, so the nearby list survives a reflash), and Arduino_GFX's QSPI sends (DMA from internal RAM).

## Features

| Feature | Before (3.3.11) | After (3.2.0 high_perf) | How verified |
|---|---|---|---|
| Panel (CO5300, QSPI) | works | works | No "display: begin failed" on either core. Frames and changed rows sent in every run. Leo handled the new-core board with the screen on. **Pixels not yet checked by eye** |
| Touch (CST9217) and the touch filter | works | works | "touch: ok" on both. Leo's own finger on the new-core mock build: 519 samples in 25 s, drags and a fling settling on a card (`G up on stops … settle`). Laptop: `touch_test` passes |
| Home: clock, weather icon, Trackie, bubble, urgent card | works | works | `p` profile draws the same primitives on both cores (mock). Stress phases 2 and 4 in every run |
| Weather cycle (hold the clock row) | works | works | Hold over serial on both cores: clear → rain, Home animates |
| Swipe up, or tap the card, for Stops | works | works | Mock script swipe on both cores; stress churn |
| Stops: Trains/Buses tabs, scroll, fling, touch to stop | works | works | Stress-v2 phase 3 (scrolling with a tab switch every 5 s) and phase 6, 3 runs each; `a` auto-scroll on both cores |
| Detail: open, scroll, swipe right or tap the left edge to go back | works | works | Mock script on both cores; stress churn |
| Swipe left for the Map placeholder, tap Trackie for his reason | not run on the board | not run on the board | The UI code is unchanged, plain C++, and laptop-tested (`frame_test`, `preview`) |
| Dim after 15 s, tap to wake, always-on screen | works | works | Power log `dim` and brightness columns and wake events in phases 1, 2, 5 and 7 of every run. The 10 s burn-in shift is visual |
| Ten debug states (Live … Pairing), incl. Docked, First run, Pairing | works | works | Serial `c` through all ten on both cores' live builds; no reset |
| Live trains, buses, alerts, weather | works | works | Boot captures and every power run: all requests HTTP 200 (old core: one run had 10 server timeouts) |
| Kept-open TLS connection, resumed sessions | works | works (with the fix) | `tls,t,resumed` and `tls,b,full` in both cores' logs. Without the fix, every HTTPS request failed on 3.2 |
| Nearby list from `tools/send_nearby.py`, kept in NVS | works | works | `send_nearby.py` and `power_run.py --here` on both. After a reboot: "saved nearby list: 5 stations, 5 bus stops" |
| Wi-Fi join, back-off, disconnect reasons, scan report | works | works | On a network slow to admit the board: reason 2 logged on both cores; the back-off (15 → 30 → 60 → 120 s) and "in reach: channel 6" scan report on the new core (the old one joined on its retry). The moved logger's path wasn't hit at home, where it joins first time |
| Replacement MAC from `secrets.h` | works | works | New core: the MAC read back the same after setting it and after `begin()` |
| WPA2-Enterprise (PEAP) | builds | builds | No enterprise network to try. The API is the same in both cores |
| AXP2101, battery log, docked rule | works | works | "AXP2101 ok", "power: usb 1, battery no …; docked 0" on both (no battery, so never docked, by design) |
| CPU clock 80/240 MHz | works | works | Power log clock columns in every run: dim phases mostly at 80 MHz |
| Card cache (32 cards in PSRAM) | works | works | PSRAM free after it allocates: 6.04 MB old, 3.75 MB new (XIP takes 2.3 MB). Laptop: `frame_test` passes |
| Power build: logger and stress test | works | works | 3 complete runs each, no resets, no dropped data |
| Serial commands (`d m u c a p s t N S X`) | works | works | All used in these tests on both cores |
| Serial never freezes the device | works | **fixed** | Idle-port test: the old core and the fixed new core keep running; 3.2 with a timeout of 0 froze |
| Board off (`O`) | – | new | Turned off with USB in; it stayed off |

## Performance and power

| Measure | 3.3.11 | 3.2.0 high_perf | Change |
|---|---|---|---|
| Home frame, profiled (mock, same frame) | 9.2 ms | 7.8 ms | −15% |
| Whole frame drawn into PSRAM (Home) | 22.0 ms | 16.4 ms | −25% |
| Stops auto-scroll | 37.8 fps, 25.7 ms draw | 49.4 fps, 19.9 ms draw | +31% fps |
| Detail frame, profiled | 12.0 ms | 11.0 ms | −8% |
| Stops at rest (smooth scaling) | 36.4 ms | 35.0 ms | −4% |
| Row hash per frame | 2.0 ms | 2.0 ms | same |
| Setup done (from boot) | 684 ms | 654 ms | −30 ms |
| First train data (from boot, home) | 7.2–7.4 s | 6.5–6.6 s | −0.7 s |
| Lowest free internal RAM (stress test) | 19–22 KB | 44 KB | +22 KB |
| Free PSRAM (lowest) | 5.57 MB | 3.27 MB | −2.3 MB |
| Stress fps while moving (scroll, churn, everything) | 75.7, 34.8, 66.0 | 89.2, 48.3, 78.5 | +18%, +39%, +19% |
| Slowest frame (scroll, churn, everything) | 49, 47, 52 ms | 36, 37, 38 ms | about −13 ms |
| Tap→screen (scroll, churn, everything) | 18, 24, 22 ms | 14, 18, 17 ms | about −5 ms |
| Est. current, dim idle | 41.0 mA | 40.7 mA | −0.6% (noise) |
| Est. current, awake still | 50.1 mA | 48.2 mA | −3.8% |
| Est. current, scroll, churn | 89.1, 57.7 mA | 90.1, 59.5 mA | +1.2%, +3.1% (noise; 18% and 39% more frames) |
| Est. current, network burst, everything | 69.4, 109.5 mA | 68.2, 98.7 mA | −1.7%, −9.8% (noise) |
| Projected battery life (typical day) | 46.8 h | 47.2 h | +0.9% |
| Flash used (live, mock, power) | 2,303,255; 1,481,752; 2,314,443 B | 2,373,966; 1,461,450; 2,385,506 B | +3%, −1%, +3% |
| Static RAM (live, mock, power) | 79,872; 55,496; 80,712 B | 79,724; 49,516; 80,572 B | about the same |

Current figures are model-v2 estimates (USB power, no battery). Two disturbed phases are left out, and the reasons are in the log entry.

## Risks

- **PSRAM:** 2.3 MB less free, as the code now runs from it. 3.27 MB stays free at the lowest point.
- **Four 3.2 workarounds** to drop or keep if the dock moves cores. They're commented where they're handled and listed in the README.
- **BLE:** the 3.2.0 libraries enable Bluedroid, not NimBLE. The spec's NimBLE-Arduino 2.x on these libraries is untested, on both boards.
- **Weather animation:** Home in rain or snow draws without a frame cap on either core. That's 85 fps and about +60% current in an awake-still phase. It's existing behavior and a candidate for a later round.
- **Not checked on the board:**
  - the panel by eye;
  - swiping to the Map and tapping Trackie;
  - the new Wi-Fi disconnect logger firing;
  - a WPA2-Enterprise join;
  - turning the board back on with PWR.

## Spec changes proposed (not made)

- **Main spec §15, risk "NimBLE-Arduino vs. core 3.3+":** both boards are now on core 3.2.0, whose libraries enable Bluedroid. The open question is NimBLE-Arduino 2.x on those libraries.
- **Power analysis spec §2:** "FreeRTOS run-time stats are on in Arduino core 3.3.11" could say "in the pinned core"; they're on in 3.2.0 high_perf too.
- **Dock (not changed here, the dock's file):** `firmware/dock/platformio.ini` says the handheld stays on the stock core, and that the dock has its own PlatformIO folder so it doesn't replace the handheld's. Both stop being true once this lands.

## Disk

- **Toolchain:** 0 bytes added; `~/.platformio-dock` already had everything.
- **Build output:** about 440 MB in this worktree's git-ignored `.pio`. The A/B's old-core builds are in a session scratch folder, deleted with the session.
