# Day 1: the link on both boards

Ecosystem plan Task 1 (`docs/superpowers/plans/2026-11-14-ecosystem.md`), spec section 10 step 1. The spike
(`spike/link/`, local only) runs Wi-Fi, BLE passkey pairing, an HTTP server, mDNS, and one full TLS handshake to the
train API every 30 s, all at once, and prints internal RAM every 5 s. This page is the procedure and the results.
Networks are named only "home" and "campus".

**Decision (Leo, 2026-11-14):** **LAN pairing, plan Appendix A.** No Bluetooth anywhere for the showcase. BLE works
but costs about 50 KB of internal RAM, which neither board has; see [Decision](#decision).
**BLE library tested:** NimBLE-Arduino 2.5.2 on arduino-esp32 3.2.0 (passes; kept for future work).

## Build

Both envs copy their board's platform, core packages, board, flash, PSRAM and serial flags, and `core_dir =
~/.platformio-dock`, so nothing new is installed but NimBLE-Arduino 2.5.2 (a project library). The dock env also
sends mbedTLS's big blocks to PSRAM as the dock does; the handheld env doesn't, as the handheld doesn't.

| Env | Flash | Static RAM | Result |
|---|---|---|---|
| `dock` | 1,435,990 B of 3,342,336 (43.0%) | 55,452 B of 327,680 (16.9%) | builds, NimBLE-Arduino 2.5.2 on core 3.2.0 |
| `handheld` | 1,457,118 B of 6,553,600 (22.2%) | 55,920 B of 327,680 (17.1%) | builds, NimBLE-Arduino 2.5.2 on core 3.2.0 |

## What the spike prints

| Line | Meaning |
|---|---|
| `ram <what>: min A free B (internal)` | `heap_caps_get_minimum_free_size` / `heap_caps_get_free_size` for `MALLOC_CAP_INTERNAL`. Every 5 s as `ble on` / `ble off`, and at each stage (`boot`, `after wifi`, `after httpd`, `after mdns`, `after https`, `before toggle`, `after toggle`). `min` is since boot. |
| `ble: on as cta-spike-xxxx` | BLE advertising. |
| `ble: gap passkey action 2`, `ble: gap enc change, status 0x…` | raw pairing events. Status 0 is success; 0x206 means the iPhone holds an old bond for this board (Forget it in iOS Settings → Bluetooth and retry); 0x4xx the board refused, 0x5xx the phone refused (low byte = SMP reason). A failed link is disconnected. |
| `ble: PASSKEY NNNNNN` | iOS asked to pair: type these digits on the phone (a new passkey for every pairing). |
| `ble: auth done, encrypted 1 authenticated 1 bonded 1` | pairing finished with a passkey-authenticated, bonded link. |
| `ble: WRITE n bytes, encrypted 1 authenticated 1` | a write to the `wifi` characteristic landed (only the length is printed). |
| `https: train API HTTP 200, n bytes, t ms` | the 30 s TLS request. |
| `mdns: cta-spike-xxxx.local up`, `wifi: joined, ip …` | the board on the network (the network name is never printed). |
| `n: <ip>/info HTTP 200 {"ok":true}` | the answer to serial `n <ip>`. |
| `http: open fd N from <ip>:<port>`, `http: GET /info from <ip>`, `http: close fd N` | a client reached the server (IPv4 peers show as `::ffff:a.b.c.d`). No `open` line means the TCP connection never arrived. |
| `wifi: power save on/off` | serial `p` toggles modem sleep (on by default), which can make the board miss a phone's ARP. |

Serial commands: `b` toggles BLE (`NimBLEDevice::deinit(true)` / init again), `n <ip>` fetches another board's
`/info`, `w` checks whether the configured network is on the air (channel, RSSI, auth; never its name). Until Wi-Fi
joins, each `ram` line ends with `wifi status N`; a failed join prints `wifi: join failed (status N, reason R)` and
retries with the dock's backoff (2 s, then 15 s doubling). The spike boots with BLE **off**, so the `ble off` lines before the first `b` are the no-BLE figure.

## Procedure (Steps 2–4)

Run from the repo root. `<DOCK_PORT>` is the dock's CH343 UART port, `<PET_PORT>` the wearable's USB port
(`ls /dev/cu.usb*` lists them). Every upload names its port.

1. **Flash** (this replaces each board's firmware until step 7):

   ```
   ~/.platformio/penv/bin/pio run -d spike/link -e dock -t upload --upload-port <DOCK_PORT>
   ~/.platformio/penv/bin/pio run -d spike/link -e handheld -t upload --upload-port <PET_PORT>
   ```

2. **Monitor**, one terminal per board (type `b` or `n <ip>` and Return in it):

   ```
   ~/.platformio/penv/bin/pio device monitor -d spike/link -e dock -p <DOCK_PORT>
   ~/.platformio/penv/bin/pio device monitor -d spike/link -e handheld -p <PET_PORT>
   ```

   Wait for `wifi: joined`, `mdns: … up`, and two `https: train API HTTP 200` lines. Note the `free` of a `ble off`
   line after an `https` line: the **no-BLE free** in the RAM table. Note each board's IP.

3. **Pair (Step 2).** Type `b` in the board's monitor. On the iPhone, in LightBlue (or nRF Connect):
   1. If an older `cta-spike-xxxx` is in iOS Settings → Bluetooth, Forget it first.
   2. Connect to `cta-spike-xxxx` (the same `xxxx` as the mDNS name).
   3. Open the service `D8D4388C-…`. Read `FB360799-…` (info): shows `{"ok":true}` in UTF-8.
   4. Write a few bytes (any text) to `8D0820B1-…` (wifi). iOS shows a pairing prompt asking for a code: type the
      six digits from the `ble: PASSKEY` line.
   5. Pass: the monitor shows `ble: auth done, encrypted 1 authenticated 1 bonded 1` and
      `ble: WRITE n bytes, encrypted 1 authenticated 1`. A wrong code must not produce a `WRITE` line.
   Do it on both boards.

4. **RAM (Step 3).** With BLE on and the phone still connected, leave both boards for 10 minutes (20 TLS requests).
   Then note the last `ble on` line's `min` (**lowest with BLE on**) and `free` (**BLE on free**). Type `b`; after the
   next `https` line note a `ble off` line's `free` (**free after BLE off**). Type `b` twice more (on, off) and check
   `free` after the second off is still within 2 KB of the first: no leak across restarts.

5. **Network (Step 4), home:**
   - Mac: `dns-sd -B _ctatracker._tcp` lists both `cta-spike-xxxx` (Ctrl-C to stop). `dns-sd -L cta-spike-xxxx
     _ctatracker._tcp` shows TXT `type=dock` or `type=handheld` and `id=xxxx`.
   - iPhone on the same network: Safari opens `http://<ip>/info` for each board and shows `{"ok":true}`.
   - Dock monitor: `n <wearable ip>` prints `HTTP 200 {"ok":true}`. Wearable monitor: `n <dock ip>`, the same.

6. **Campus**, when there: rebuild with the campus network in `firmware/secrets.h` if it isn't the one compiled in,
   flash, and repeat step 5. Record pass/fail only.

7. **Put the boards back:**

   ```
   ~/.platformio/penv/bin/pio run -d firmware/dock -t upload --upload-port <DOCK_PORT>
   ~/.platformio/penv/bin/pio run -d firmware/handheld/mock -e live -t upload --upload-port <PET_PORT>
   ```

## Results

### Pairing (Step 2): pass

NimBLE-Arduino 2.5.2 builds and pairs on arduino-esp32 3.2.0, so the core's BLE wasn't tried. From LightBlue on the
iPhone, on both boards: the passkey prompt appeared, `auth done` showed encrypted 1, authenticated 1, bonded 1, and
the encrypted write landed.

### Internal RAM (Step 3): fail

Lowest free internal RAM, with a TLS GET to the train API every 30 s, over a soak of about 15 minutes:

| Board | BLE off | BLE on | BLE cost |
|---|---|---|---|
| Dock | ~198 KB | 147.6 KB | ~50 KB |
| Wearable | ~156 KB | 104.7 KB | ~51 KB |

BLE off (`deinit(true)`) gives the memory back: wearable free 208,348 B after BLE off against 208,332 B with no BLE.

The spike carries none of the boards' own work, so what counts is the **BLE cost** taken off the real firmware's low
point, which comes during a TLS handshake: about 49–51 KB on the dock (Task 6) and about 44 KB on the wearable
(before Task 8). BLE at about 50 KB takes both boards to about 0, against the 40 KB floor.

- Setup-only BLE with CTA polling paused would leave the wearable around 30 KB, still under the floor.
- Task 8's TLS-to-PSRAM allocator may change the wearable's figure. It isn't measured yet.

### Network (Step 4)

| Check | Home | Campus |
|---|---|---|
| Mac `dns-sd -B _ctatracker._tcp` lists both boards | pass | not yet tested |
| Mac reaches both boards' `/info` | pass (200) | not yet tested |
| Dock fetches wearable `/info` (`n <ip>`) | pass (200) | not yet tested |
| Wearable fetches dock `/info` (`n <ip>`) | pass (200) | not yet tested |
| iPhone Safari reaches the boards' `/info` | **fail** | not yet tested |

The iPhone failure at home: the phone's TCP never reached either board (the httpd `open` logging showed nothing from
the phone). It held with Wi-Fi power save off and with Safari extensions off. The same iPhone loaded a page from the
Mac on port 80 `/info` fine, and the boards could open TCP to the phone. The likely cause is the router keeping the
phone and the 2.4 GHz boards apart, not the firmware.

Campus gets tested on campus or at the 11/22 dry run. If the phone can't reach the boards there, the backup is the
iPhone's Personal Hotspot as the showcase network.

### Hardware lessons

- Start httpd and mDNS only after Wi-Fi is up.
- On core 3.2.0 use `esp_event_handler_register`, not `WiFi.onEvent` (a race).
- NimBLE 2.x passkey display needs the default static passkey set.
- Opening the dock's CH343 port with RTS asserted holds the dock in reset.
- Wi-Fi power save is `WIFI_PS_MIN_MODEM` by default.

### Decision

**Appendix A: LAN pairing, no Bluetooth anywhere for the showcase** (Leo, 2026-11-14). A fresh board shows a 6-digit
code on its screen and accepts `POST /pair` over the LAN; Wi-Fi comes from `secrets.h`. BLE costs about 50 KB of
internal RAM, which neither board can spare during TLS, and the showcase runs on one network anyway. In Leo's words:
"Bluetooth would be cool later, but for a real version I'd likely use a more powerful device." BLE setup and the
no-Wi-Fi relay are future work (spec section 11), not deleted design.

## ESP-IDF

**The dock is Wi-Fi only (Leo, 2026-11-14):** no BLE on the dock; the table below is history from D4's measurement.

### Dock (ESP-IDF port, D4, 2026-11-14)

The dock build has NimBLE (IDF's own) compiled in, using `firmware/dock/src/ble_setup.*`:
- it advertises `CTA-Dock-xxxx` with the service UUID and serves `info` (the GET /info JSON);
- serial `b` turns it on and off.

The setup: one connection, peripheral only, `BT_CTRL_BLE_MAX_ACT=2`, and the host's memory in PSRAM. The controller
runs from flash (`CONFIG_BT_CTRL_RUN_IN_FLASH_ONLY`, central and scan code out). Here that means PSRAM through XIP,
which flash writes don't stall. Left in IRAM, the controller took 20 KB of heap whether BLE ran or not, and the low
point with BLE advertising was 20.2 KB. Every row below is normal polling: train TLS every 30 s, the other hosts on
their schedule.

| State | Internal free | Lowest over the window |
|---|---|---|
| Boot, "dock: up" (BLE compiled in, off) | 131.1 KB (134.5 KB without BLE compiled in) | – |
| BLE off, 10 min | 79.2 KB | 72.4 KB |
| BLE on and advertising, no connection, 10 min | 48.7 KB | **41.8 KB** |
| Off again, then on/off ×3 | 79.2 KB each time (no leak) | – |

- **Pass, narrowly:** 41.8 KB against the 40 KB floor, with BLE advertising beside TLS. Turning BLE on costs about
  30 KB of internal RAM (controller, host task stack).
- **Not measured yet:** BLE with a phone connected (LightBlue). A connection adds the controller's link buffers. If it
  drops under 40 KB, D4's ladder starts at its step 1: smaller msys/ACL counts, `BLE_MAX_ACT=1`.

### Wearable (ESP-IDF port, Task W5, 2026-11-14)

The real live firmware on native ESP-IDF 5.4.1 with IDF's NimBLE (host memory in PSRAM, one connection, peripheral
only, `BT_CTRL_BLE_MAX_ACT=2`). Serial `b` turns BLE on (advertising the service as `CTA-Pet-xxxx`, with an `info`
read) and off (`nimble_port_stop` + `nimble_port_deinit`). Lowest free internal RAM
(`heap_caps_get_minimum_free_size(MALLOC_CAP_INTERNAL)`), with the normal polls running (trains every 60 s on a
kept-open, resumed TLS connection; buses, alerts, weather):

| Build / state | Free at boot | Lowest over the window |
|---|---|---|
| BLE compiled out (`CONFIG_BT_ENABLED=n`) | 195.6 KB | 123.6 KB (polling) |
| BLE compiled in, off, 10 min | 168.5 KB | 87.5 KB |
| BLE on, advertising, no phone connected, 10 min | | **51.4 KB** |
| BLE off again | | free back to 93,868 B |
| BLE on, iPhone connected in LightBlue, 10 min (main 0359878 plus this task, 70 CTA requests) | | **67.1 KB** |
| BLE off after that | | free back to 109,344 B |

- **Pass:** 67,132 B lowest with BLE on, an iPhone connected (`CTA-Pet-6bcc`) and TLS polling throughout, against
  the 40 KB floor (2026-11-14, 20:40-20:54). No late panel sends, no resets. That build had B1's tearing fix and the
  TLS fixes in, so its figures run higher than the earlier rows'.
- **The controller's memory is taken at boot:** with `CONFIG_BT_ENABLED=y`, 27 KB less internal RAM is free at boot,
  whether BLE ever starts or not. Starting BLE takes about 35 KB more (free 93.9 → 59.0 KB).
- **No leak:** off, on, off: the free figure came back to exactly 93,868 B each time.
- The whole cost, about 72 KB against the build without BLE, is more than the plan's estimate of 25–30 KB. If a
  connected phone takes it under 40 KB: of the ladder's first rung only `BLE_MAX_ACT=1` touches internal RAM (NimBLE's
  pools are already in PSRAM, and `MSYS_1_BLOCK_COUNT` is already 12); then the Wi-Fi and lwIP rungs, then fallback 1.
- Repeated on the follow-up fix commit (TCP_NODELAY, 16 KB outgoing TLS record): BLE off 87.1 KB over 5 min, BLE on
  **50.6 KB** over 10 min, free back to 93,724 B after each off, no TLS errors. That commit with BLE compiled out
  polls at a 114 KB minimum.

### Wearable, controller in flash (Task W6a, 2026-11-14)

The W5 setup plus `CONFIG_BT_CTRL_RUN_IN_FLASH_ONLY=y`, `BT_CTRL_BLE_MASTER=n` and `BT_CTRL_BLE_SCAN=n` (the
controller's code runs from PSRAM through XIP instead of taking IRAM), the same polls:

| State | Free | Lowest over the window |
|---|---|---|
| Boot (BLE compiled in, off) | 200.1 KB (W5: 168.5; compiled out: 195.6) | – |
| BLE off, polling, 10 min | 125.8 KB | 118.7 KB (W5: 87.5) |
| BLE on, advertising, polling, 10 min | 95.5 KB | **88.8 KB** (W5: 51.4) |
| Off, then on/off ×3 | 125,756 B after each off, about 95.6 KB after each on (no leak) | – |
| The W6a link build (GATT `link` and `info`, pairing, BLE on from boot), advertising, polling, 11 min | 93.7 KB | **86.4 KB** |

- **Gate case (c) passes** with 46 KB to spare. The gain over W5 is about 31 KB at boot and 37 KB advertising, more
  than the dock's 20 KB. Starting BLE now takes about 30 KB (125.8 → 95.5 KB).
- **Not measured yet:** case (b), an iPhone bonded and connected while TLS polls (Leo's phone).

### Wearable, relaying from the iPhone (Task W6b/W6c, 2026-11-15)

Main at 4555726, Leo's iPhone 15 Pro bonded over Bluetooth, Trackie on screen, Wi-Fi joined, five nearby bus stops:

| Check | Result |
|---|---|
| RAM, case (a): connected and relaying, Wi-Fi joined, 3 min watched | 90.7 KB free; **83.9 KB** lowest since boot (about 50 min of polling, two relay sessions and four reconnects) |
| `CTA requests` while relaying | flat at 135 for 3 min: 6 `/relay` ticks (every 30 s), 30 `/stop` sends, no CTA polls |
| `CTA requests` with the app away | climbing (31 → 43 in 3 min) |
| NimBLE host stack left | 3,144 B of 6,144 |
| Reconnect after an app relaunch | 1 s from launch to bonded and subscribed (after ea31958; before it, never) |
| Connect → `nearby` → every stop's times | about 1 s: `nearby` 1.0 s after subscribe, five `/stop` within the next 0.6 s |

- **Gate case (a) passes** with 44 KB to spare, on 3 minutes watched rather than 10 (the lowest is since boot, so it
  covers the earlier relay sessions too).
- **Found and fixed:** after a relaunch iOS kept the last run's pending connect, which never reached the wearable
  (scans saw it advertising under its known identifier; the wearable never got a connection request), so every
  relaunch meant pairing again. The app now replaces a pending connect it didn't make (ea31958). A wearable that
  reconnected while the app was away got no `nearby`, so the relay never started; the app now sends it on coming to
  the foreground (4555726).
- **Not measured yet:** case (b), 10-minute windows, off/on ×3 with the phone, pin and follow latency, the HTTP
  fallback after `b`, and the no-Wi-Fi boot.

### Wearable gates with Leo's iPhone, continued (W6c, 2026-11-15 evening)

Main at fbeb68b–7cb3753, Leo's iPhone 15 Pro, Wi-Fi joined:

| Check | Result |
|---|---|
| RAM case (a), 10 min relaying (Trackie on screen), with a pin and a follow | **85.5 KB** lowest; `CTA requests` flat; 102 `/stop`; no disconnects |
| RAM case (b), 10 min with the phone locked (connected, not relaying, the wearable polling CTA itself) | **85.5 KB** lowest; Bluetooth stayed connected; polls every 60 s. Not done: `link_send.py --burst` on top |
| Bluetooth off/on ×3 (serial `b`) with the phone | reconnected and bonded within 1 s each time; 124.5 KB free after each off (no leak) |
| Reconnect after a wearable reboot | connected as it booted; pin and follow back 0.6 s after encryption |
| Pin / follow on receipt | applied within 0.2 s of the write (`net: pinned …, following …`) |
| Phone fetch → wearable card (after 770821b) | within about a second (Leo, watching both) |
| Pair again (wearable Settings) → re-pair from the app | worked with the passkey; no Forget in iOS Settings needed; token and `nearby` within 0.8 s; no old pin |
| Reset (`R`) → pairing by code from the Mac (W6d) | the right code gave 200 and set the token |
| A code-paired wearable, then Bluetooth pairing from the app | worked directly (the app offers "Pair again"; no wearable Pair again needed) |
| A passed follow | Home back to the closest stops after Arrived/Lost (30 s since 7cb3753; Leo 2026-11-15) |

- **Not done (need another network or a later day):** the HTTP fallback after `b` (the home router blocks phone→board
  traffic), app code pairing with the phone's Bluetooth off, a `/wifi` join from the app, and the no-Wi-Fi boot
  with relayed cards (step 7 go/no-go, 11/20 18:00).
