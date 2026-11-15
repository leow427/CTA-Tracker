# Handheld mock and live prototype

The handheld's real screens and gestures (spec §8.1, `docs/handheld-ui.md`) running on the Waveshare 1.75" round AMOLED, a port of `docs/ideas/2026-11-04-handheld-ui-mockup.html` to C++. Two builds share them:

- **`live` (the default):** real arrivals, alerts, and weather over Wi-Fi, polled on the spec's schedule (§6.6) for a preset nearby list. The app's `POST /nearby` over the local network will replace the preset (ecosystem spec 5.5).
- **`mock`:** simulated arrivals around a fixed location, no network. The clock starts at the build time, read as Chicago time.

## Run it

```bash
cd firmware/handheld/mock && ~/.platformio/penv/bin/pio run -t upload
```

Use PlatformIO's own `pio` (above), not Homebrew's: the pinned platform runs `python -m pip`, and Homebrew's Python has no pip. With the dock plugged in too, add `--upload-port` with the handheld's port: the dock's serial chip is also a `/dev/cu.usbmodem…`.

That's the live build. It needs `firmware/secrets.h` (git-ignored; template `firmware/secrets.example.h`) with the two CTA keys and a Wi-Fi network:

```c
#define WIFI_SSID "your network"
#define WIFI_PASS "its password"
```

For the mock: `~/.platformio/penv/bin/pio run -e mock -t upload`.

## Toolchain

Native ESP-IDF 5.4.1, no Arduino code or library (`docs/superpowers/plans/2026-11-14-esp-idf-port.md`). **The rule (Leo, 2026-11-14): no Arduino whatsoever, only ESP-IDF.** No Arduino core or Arduino-named library, no `framework = arduino` or `arduino, espidf`; there is no Arduino build or fallback (`docs/Agent-Workflow.md`, Rules). PlatformIO still builds it (`framework = espidf` on the pinned pioarduino platform 54.03.20), in the dock's PlatformIO folder (`~/.platformio-dock`). The build output goes to `~/.platformio-dock/build/<folder>-<hash>`, one folder per checkout, because ESP-IDF refuses a build path with a space in it and the project folder has one. `sdkconfig.defaults` holds the SDK settings: it reproduces what arduino-esp32 3.2.0's prebuilt "high_perf" libraries were built with (`-O2`, code and read-only data running from PSRAM, 64-byte data cache lines, Wi-Fi and lwIP buffers in PSRAM, driver interrupt code out of IRAM), and moves all of mbedTLS and mDNS to PSRAM. `src/idf_component.yml` pulls Espressif's CO5300 panel driver and mDNS (1.8.2, the version arduino-esp32 3.2.0 shipped). The boot log says which SDK is running: `sdk: esp-idf 5.4.1, code runs from PSRAM (XIP), data cache line 64 B`.

What replaced each Arduino piece:

- **Display:** `esp_lcd_co5300` over QSPI at 80 MHz, with Arduino_GFX's own CO5300 init sequence and column offset, so the panel is set up exactly as before. A draw only queues the transfer, so the sender waits for its done interrupt before the strip buffer is reused. The serial `h` command prints a hash of four fixed frames (see Using it): the same hashes as the Arduino build means the same pixels.
- **Touch and power chip:** two small drivers of our own on `i2c_master` (`src/cst9217.*`, and the AXP2101 registers in `src/power.cpp`), with the register sequences from SensorLib 0.3.1 and XPowersLib 0.3.3 (MIT; their notices are in the files). The bus runs at 100 kHz, as `Wire` did.
- **Network:** `esp_wifi` with the driver's events, `esp_http_client` (one handle per host), `esp_sntp`, mDNS, NVS (the same `trackie` keys and types Preferences stored, so a reflash keeps the token, the nearby list, and the app's settings), and the USB-Serial/JTAG driver for serial.

## The live build

- **Nearby list (spec §8.3).** The handheld polls the stops in the `nearby` message, kept in flash. Until the app's BLE link exists, the Mac sends it over USB serial: `python3 tools/send_nearby.py` (from the repo root) takes the Mac's location from CoreLocation, picks the 5 nearest stations and 5 bus stops (each route and direction's nearest stop, the 5 nearest of those; 10 of each was too slow), and writes the list to the board, which starts over with the new stops. The first run builds a small helper app, `Trackie Locate`, and macOS asks once to let it use your location. `--lat … --lon …` sends a spot instead; `--dry-run` shows the stops without sending. The location goes only to the board (and on to Open-Meteo for weather); nothing prints it or writes it to the repo. Until a list arrives, Trackie is Sleepy and says "Waiting for location".
- **Polls (spec §6.6).** The 3 closest stations every 60 s and the other 2 every 3 min (one `ttarrivals?mapid=` each, split into one card per line and direction by `cta::parseTrainCards`); all 5 bus stops in one `getpredictions` call every 60 s; alerts every 5 min for the routes seen; weather every 15 min. That's about 5 CTA requests a minute. Every due station goes before the buses, so they share one connection.
- **One kept-open connection.** The train API's certificate is ECDSA, which the ESP32-S3 verifies in software: a fresh TLS handshake there takes about 2.4 s. The live build keeps that connection open between train requests (closing it after 90 s idle, or before a request to another host), so a station takes about 0.25 s after the first. The other hosts get one request a minute or less, so their connections close when done. Only one is ever open.
- **Resumed TLS sessions.** Each host has its own `esp_http_client` handle, which keeps that host's last TLS session (never logged), and a connection opened again offers it back to that host only. When the server agrees (the train API does: its connection is reopened every minute, after the bus poll), the handshake skips the certificate checks and the key exchange and takes about one round trip (70 ms on the board, against about 1.8 s in full); when it declines, it's a full handshake with every certificate checked, as before. A handshake that fails within 3 s while offering a session is tried once more in full; one that timed out isn't. The bus API's sessions carry a ticket of about 4 KB; the outgoing TLS record is 16 KB (in PSRAM, as Arduino had it), so they resume too. Nagle is off on every TLS connection, as Arduino's client had it: esp-tls leaves it on, and each handshake flight then waited out a delayed ACK (about 0.3 s per connection).
- **DNS.** The live build looks hosts up itself. Arduino's lookup returned its error as success, so a failed lookup (lwIP gives each DNS server 7 s) was followed by 8 s connecting to 0.0.0.0: a request on a network passing no traffic took 22 s. Now a failed lookup fails the request at once, and the CPU stays slow while a lookup waits. A lookup that gets no answer at all pauses polling until 15 s after that request began, but never past the moment the old request would have ended, so nothing is tried later than before; in between the radio is idle and the CPU slow. Any other failure (a negative DNS answer, no TCP connection, an HTTP error) goes straight on to the next due poll, as before. Each stop keeps its own 30 s retry.
- **Buses once each.** A route and direction shows once, at the nearest listed stop that serves it.
- **Alerts.** Only unplanned service alerts count (`accessibility=false&planned=false`). Without that filter, elevator outages and planned bus-stop notes marked Red, Brown, Purple, 22, and 8 as alerted on a normal weekday, and Trackie would be Sad all day.
- **Offline.** Until Wi-Fi and the clock are up, the bubble says "No signal" and a tap shows why ("Connecting to Wi-Fi", "Setting the clock", "No Wi-Fi set up"). Data older than 5 min greys out, as in the mock's Offline state.
- **Docked (spec §8.4).** USB power, a battery, and the home network (`WIFI_SSID`). A board with no battery runs on USB whenever it's on, so it never counts as docked.
- **Battery log (spec §8.5).** `power: usb …, battery …%, … mV` at boot and every 15 min.
- **TLS.** Every request verifies the server against the ESP-IDF certificate bundle. Request URLs carry the keys and are never logged.
- **Countdown.** Between polls, minutes count down from each record's fetch time; arrivals that have passed drop, as in the app. Once a record is stale (over 5 min) it stays as fetched, greyed out (spec §8.3).
- **Slow mode.** Docked or at night, arrivals poll every 4.5 min (30 s before the 5-min stale mark, so cards don't grey out while a poll is in flight); alerts every 5 min, weather every 15.
- **Home card.** Fresh data first, then a close stop (one that may hype), then the soonest.
- **Why Wi-Fi didn't join.** The log says `net: disconnected, reason N` (the driver's reason: 15 or 204 is a wrong password, 201 the network out of reach) and, after a failed try, whether the network is in reach and on which channel. The ESP32-S3 has 2.4 GHz only.
- **Clock.** NTP servers are given by IP address: while SNTP's own name lookup is pending (as on a network that blocks NTP), an HTTPS request's DNS lookup tripped an lwIP assert and rebooted the board (core 3.3, 2026-11-09). SNTP starts again when Wi-Fi comes up (its first request goes out before there's a link, and lwIP waits 15 s to try again). Until NTP answers, the clock comes from the train API's HTTP `Date` header: from the first station's own response, sent as soon as Wi-Fi is up, so the stops load without waiting for the clock; with no nearby list yet, from a bare request after 10 s without NTP. Retried every 30 s.
- **Slow networks.** A join gets 20 s (a campus network took about 12). The log has the latest disconnect reason (at most one a second) and the reset reason at boot.
- **Blocked MAC.** A router can stop answering the board's own MAC (one did, 2026-11-09, after a reboot loop: no error, just no reply). `WIFI_MAC` in `secrets.h` (see `secrets.example.h`) sets a replacement. That network then took 1–2.5 min after a first join to pass traffic; requests time out until it does.
- **Wi-Fi out of reach.** Joining backs off from 15 s to 5 min, so the radio isn't scanning nonstop away from home; a tap on the dim screen (a wake) retries at once.
- **I2C.** The power chip shares the touch controller's bus, and a touch read is two transfers (a write, then a read) that must not be split, so only the touch task uses the bus: it reads the AXP2101 once a second and hands the state to the loop.
- **Before data.** The clock shows `--:--` until NTP syncs, and the weather stays hidden until Open-Meteo answers.
- **Internal RAM.** Wi-Fi and each TLS connection need about 100 KB of internal RAM between them, so the live build keeps the model, the card scratch, and the parsers' buffers in PSRAM. Measured on the board (2026-11-09): about 103 KB internal free with no connection open, about 57 KB while the train connection is kept open, and a low of about 30 KB during a handshake (a TLS connection takes about 45–50 KB), under the foundation plan's 40 KB floor; BLE will need some of it back. On core 3.2 high_perf (see Toolchain) the Wi-Fi and lwIP buffers live in PSRAM, and the lowest point over a whole stress test is about 44 KB (2026-11-13; `docs/power/log.md`). Scratch in PSRAM cost the Stops list 0.7 ms a frame while scrolling (27.5 vs 26.8 ms; 23 fps either way).

## Using it

- **Home:** swipe up, or tap the card, for Stops. Tap Trackie for his reason. Swipe left for the Map (a placeholder for now).
- **Stops:** drag to scroll, or flick and the list carries on and slows to a stop on a card. Touch a moving list to stop it. Swipe left and right (or tap the pill) for Trains and Buses, and tap a card for its detail. Swipe down from the top, or pull past the first card, for Home.
- **Detail:** drag or flick to scroll, swipe right or tap the left edge to go back.
- **Dim:** after 15 s with no touch, tap to wake.
- **Debug:** hold Trackie (or hold two fingers anywhere) to cycle Live, Happy, Hyped, Worried, Sad, Sleepy, Offline, Docked, First run, and Pairing. Hold the clock row on Home to cycle the weather (clear, rain, snow).

Live computes Trackie's mood from the simulated arrivals with `cta::deviceMood`, as the device will. The other states force one mood and nudge the data to match, like the mockup's panel.

## Files

| File | What it is |
|---|---|
| `src/mock_data.*` | The model the screens draw; the mock's preset stations and simulated arrivals; `finish()`, which sorts the cards and sets the mood, reason, and home card for both builds |
| `src/live.*` | Live build: Wi-Fi, NTP, HTTPS, and the poll schedule, on a task of its own (core 0) |
| `src/live_store.*` | Live build: what each response adds to the records (plain C++, so the preview tool runs it too) |
| `src/live_model.*` | Live build: the records, power, and weather into the model |
| `src/power.*` | Live build: the AXP2101 (USB, battery, charging) over `i2c_master`, read-only for the rails |
| `src/ui.*` | The screens, navigation, and gestures (plain C++) |
| `../../lib/gfx/src/gfx2d.*` | Shared with the dock: a small canvas over RGB565: pixel plates, text, pixel digits, anti-aliased shapes, scaled blits, block lines. It draws either a whole screen or one strip in the panel's layout |
| `../../lib/gfx/src/fonts.*` | Shared: Manrope glyphs (weights 900/800 drawn at Manrope 800/700); `fonts.cpp` is generated by `firmware/lib/gfx/tools/font_build.py` |
| `../../lib/gfx/src/card_theme.*` | Shared: the card themes, glyphs, icons, Trackie and his bubble |
| `src/main.cpp` | Display, touch, and the frame loop: strips, changed-row sends, the touch task |
| `src/cpu_clock.*` | The CPU clock: 240 MHz while anything the user could notice is worked on, 80 MHz otherwise (the dim always-on screen, between the frames of a still screen, a request waiting on the network) |
| `src/cst9217.*` | The touch controller over `i2c_master`: its start-up check and point reads |
| `src/clock_ms.h` | `nowMs()` and `nowUs()`, the 32-bit millisecond and microsecond clocks |
| `src/touch_filter.h` | Turns raw touch reads into presses, riding out the controller's empty reads |
| `src/row_hash.h` | The hash of one panel row, for sending only the rows that changed |
| `tools/preview.cpp` | Renders the screens to PNGs on a laptop; build line at the top of the file. `--strips` renders through the device's strip path, which must match the plain render exactly. `--live <dir> <unix>` renders the live screens from recorded responses (and `nearby.json` there, else the Belmont fixture) |
| `tools/touch_test.cpp` | Checks the touch filter on a laptop, and replays a touch log recorded with `t` |
| `tools/live_test.cpp` | Checks the live build's data path on a laptop: `live::Store` and `mock::fromLive` (the nearby list, one card per bus route and direction, countdown, stale data, alerts, offline, docked); build line at the top |
| `tools/frame_test.cpp` | Checks on a laptop that the frame shortcuts change no pixel: frames at rest with the card cache match rendering every card fresh, a tab switch's first frame renders no card once both tabs are cached, always-on frames with the same key match, a list at rest redrawn only where its live marks blink matches the whole frame, and the row hash catches any one-word change; build line at the top |

## How a frame gets to the panel

- **Strips.** A frame is drawn as ten strips of 48 screen columns into two buffers in internal RAM, alternating, so core 1 draws one while the other is sent. The canvas writes each strip already in the panel's layout: the panel is mounted a quarter turn off, so a strip of screen columns is a band of whole panel rows, rotated 90° right and byte-swapped. There's no full framebuffer and no rotation pass.
- **In step with the scan.** The panel scans its rows (the screen's columns) at 59.2 Hz and signals each scan on its TE line (GPIO13, unused in Waveshare's examples). Strips go out in the scan's order, each once the scan has passed its rows and before the next scan reaches them, so a frame never shows half written (todo B1: strips of screen rows ran across the scan and tore every moving frame). While the list moves, each frame starts just before a scan, so frames show at a steady two scans each. The stats line counts `late sends`, the sends that missed.
- **Only what changed.** Each panel row is hashed (FNV-1a in two interleaved lanes, `src/row_hash.h`). Only the rows whose hash changed are sent, in runs (gaps under 8 rows are sent anyway to save a window). On idle Home that's about a tenth of the screen.
- **Sending on core 0.** A send task on core 0 sends the runs while core 1 draws the next strip. It also applies brightness changes, since they use the same bus. The bus driver busy-waits, so the watchdog's idle-task check is off.
- **Touch on its own task.** Touch is sampled every 8 ms on core 0. The loop gets the events with their own timestamps, so flicks are measured from real timings, not frame times. Between frames the loop waits on touch, not a fixed delay.
- **Touch dropouts.** The CST9217 driver reports "no points" whenever it's read before the controller has a new report, not only when the finger is gone. At 8 ms reads, about one in three comes back empty mid-swipe (gaps of 8–24 ms; real lifts measured 176 ms and up). `src/touch_filter.h` only counts a lift after 40 ms with no point, and gives the up the last point's time. Without that, one swipe arrived as dozens of 24 ms presses: stray taps, flings and swipes. `tools/touch_test.cpp` checks it on a laptop and can replay a recorded touch log.

## Gestures

- **Release speed.** It comes from the last 80 ms of samples, and is zero if the finger stopped before lifting.
- **Flings.** A release faster than 0.3 px/ms flings. The list eases toward the scroll position plus speed × 250 ms, snapped to a card, with a 250 ms time constant, so it leaves the finger at the finger's speed. A slow release settles with a 51 ms time constant.
- **Swipes.** A swipe is a drag over 40 px, or a quick flick: over 16 px at more than 0.5 px/ms.

## Speed

Measured on the device (240 MHz), drawing time per frame:

| Screen | Drawing | Frame rate |
|---|---|---|
| Home, idle | 9 ms; redraws twice a second for the bob and blink | 2 fps by design |
| Stops, scrolling | 25 ms | 39 fps |
| Stops, at rest (smooth card scaling) | 37–62 ms for the first frame, then 2–3 ms: only the live marks are redrawn as they blink | 2 fps by design |
| Detail, scrolling | 20 ms | 42 fps |
| Transitions (both screens drawn) | 27–32 ms | 18–20 fps |

The table is from core 3.3. The same firmware on core 3.2 high_perf (see Toolchain), measured back to back on the board with the mock build (2026-11-12, two runs each, within 1%): Home 9.2 → 7.8 ms a frame, Stops scrolling 25.7 → 19.9 ms (37.8 → 49.4 fps), Stops at rest 36.4 → 35.0 ms, Detail 12.0 → 11.0 ms, a whole frame drawn into PSRAM 22.0 → 16.4 ms. The row hash is unchanged (2.0 ms).

What made the difference:

- **No `-Os`.** The framework adds `-Os` after `build_flags`, which silently overrode `-O2`: tiny helpers became calls in pixel loops. `platformio.ini` unflags it.
- **One-multiply blending.** RGB565 is spread out as `00000ggg ggg00000 rrrrr000 000bbbbb`, so one multiply blends all three channels with a 5-bit alpha.
- **Faster scaling.** Bilinear scaling is separable (two source rows are mixed once per row). An unscaled card at rest is a plain copy. Cards fade by scaling, since the list's background is black.
- **No software maths in hot loops.** This FPU has no divide, and `hypotf`, `atan2f`, `fmodf` and `lroundf` are library calls. Loops use reciprocals, `sqrtf` (inline with `-fno-math-errno`), cross products for arc ends, and integer colour lerps.
- **Internal RAM didn't matter.** Drawing into internal RAM wasn't faster than PSRAM. The cost was arithmetic, not memory.

## Power

- **CPU clock.** The chip runs at 240 MHz whenever anything the user could notice is being worked on, and at 80 MHz otherwise: on the dim always-on screen, between the frames of a still screen, and while a request waits on the network (`src/cpu_clock.*`). At 240 MHz it draws about 10 mA more than at 80 even with both cores idle, and the handheld spends most of its time showing something still (power analysis, `docs/power/log.md`).
- **Who holds full speed.** The loop, while a frame is due or still being sent, while anything moves (a scroll, a transition, Trackie's hop), with a finger down, and while it takes in new data; so every frame is drawn and sent at full speed, and the clock drops between the half-second frames of a still Home, list, or detail. On the dim always-on screen it lets go once that screen is drawn (the frame that dims to it is drawn at full speed, as in round 1). The touch task holds it through every press: a tap is read at full speed and everything that answers it is drawn at full speed. The network task holds it from each TLS handshake (or each request on a kept-open connection) to the end of the poll, and through every Wi-Fi join and scan; the DNS lookup and TCP connection before a handshake only wait on the network. A full handshake to the train API takes about 2.4 s at 240 MHz and roughly three times that at 80. The clock drops when the last hold is released.
- **Why switching is safe here.** On the ESP32-S3, 80, 160, and 240 MHz are dividers of the same PLL, which stays on: the APB bus (and so the panel's QSPI) stays at 80 MHz, the FreeRTOS tick and `millis()`/`micros()` run from the system timer on the crystal, I2C runs from the crystal, and USB serial from the PLL.
- **Dim screen frames.** The always-on screen changes once a second at most (Trackie's bob), plus the clock and the burn-in shift. A frame that would draw it exactly as the panel already shows it is skipped: `ui::AodKey` holds everything that screen draws (and its brightness), and `tools/frame_test.cpp` checks that frames with the same key are the same pixels.
- **The list at rest.** Twice a second only the live marks blink, so the frame redraws and sends just their 16-pixel squares (`ui::restKey` holds everything else the list draws; a frame with a new key is drawn whole).
- **Waking.** A tap that wakes the dim screen is drawn at once, not at the next half-second frame.
- **Requests.** Resumed TLS sessions (above) take most of the radio and CPU time out of reopening a connection, and a request whose DNS lookup gets no answer fails without the 8 s connect to 0.0.0.0 and pauses polling (above). Poll intervals are unchanged, and no poll goes later than before.

## Notes

- **Touch mapping.** The touch panel reads a half turn from the display, so a touch at (tx, ty) is screen point (465 − ty, tx).
- **Bus colour.** Bus cards use the app's bus blue (`0x00A1DE`, `LineColors.busAccent`), not `cta::kBusColor`, which the LEDs use.
- **Card cache.** The list draws cards from a cache of 32 rendered cards in PSRAM (2.2 MB, keys included), enough for both tabs at once; 16 didn't hold 12 trains and 5 buses, so switching tabs re-rendered cards mid-transition. A card renders straight into the entry it replaces. Cards scale with nearest sampling while moving and smoothly at rest. While the list moves or slides in a transition, any cached width of a card will do (the exact one first), so a slide doesn't stop to render cards; at rest each card is drawn at its exact width (the frame a slide ends on renders any it lacks). A card whose content changed (its minutes, say) is rendered whenever it's next drawn. Re-rendering every card every frame ran under 1 fps.
- **Serial barely waits.** Plugged into a computer with nothing reading the port, the host stops taking data. Serial goes through ESP-IDF's USB-Serial/JTAG driver: a `printf` then waits at most 50 ms once and drops output until the host reads again, and whole lines (the frame stats, the power log) wait at most 5 ms and go whole or not at all. A 1 KB buffer (4 KB with the power log) holds a burst while the host reads. Flashed and left plugged in with no monitor, the loop keeps running (checked on the board).
- **Serial (115200).**

  | Command | What it does |
  |---|---|
  | `d x y`, `m x y`, `u x y` | Pointer down, move, and up, in screen coordinates |
  | `c` | Cycle the debug state |
  | `a` | Auto-scroll the list for 6 s |
  | `p` | Profile the current screen: per strip, hash, the same frame drawn whole into PSRAM, and time per drawing primitive |
  | `h` | A hash of four fixed frames (`tools/preview.cpp`'s `home_live`, `trains`, `detail_train`, `home_dim` at its fixed clock), drawn through the strips: two builds that print the same hashes draw the same pixels |
  | `s` | Print the frame stats now and start a new window (they also print every 10 s) |
  | `t` | Touch log on or off: every touch event (`T d\|m\|u x y ms`) and what each release did (`G …`) |
  | `O` | Live build: turn the board off. The AXP2101 cuts every rail, screen included, so a board left plugged in can't burn in. Tried with USB in and no battery (2026-11-13): it stayed off, and its USB port went away (flashing needs it on again). The PWR button should turn it back on (not tried yet) |
