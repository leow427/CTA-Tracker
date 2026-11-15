# Wearable Bluetooth Link Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or
> superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.
> Every session (orchestrator, workers, reviewers) runs `ponytail:ponytail` at **full** first. Workflow:
> `docs/Agent-Workflow.md` (worker plus fresh Opus reviewers, at most 3 rounds, early stop on small findings).
> **ESP-IDF only:** IDF's NimBLE C API and cJSON; no Arduino core or library anywhere.

**Goal:** The wearable and the iPhone app talk over **Bluetooth first**. The phone sends the nearby list, pin, follow,
Trackie's look and settings, and **relays the arrivals themselves**, so the wearable works with no Wi-Fi at all.
The wearable reports battery, charging, Wi-Fi state and signal, and its settings. Wi-Fi is optional, set up from the
app over the bonded link (home network or the phone's hotspot). When Bluetooth isn't connected and the wearable
answers on the LAN, the app falls back to today's HTTP link. The dock doesn't change: it stays Wi-Fi only.

**Architecture:** One GATT service on the wearable with two characteristics: `link` (phone → wearable, encrypted
write) and `info` (wearable → phone, encrypted read and notify). A `link` message is an HTTP-shaped envelope,
`"POST /pin\n{json}"`, carrying **the same bodies and the same parsers as the HTTP link**. One dispatcher in
`link.cpp` serves both transports. Messages larger than one ATT write are split into chunks with a 1-byte header.
Passkey pairing and bonding authenticate Bluetooth. The HTTP bearer token is still used on the LAN, and the app
writes it over the bonded link during setup. While the phone relays, the wearable stops its own CTA polling.

**Tech stack:** native ESP-IDF 5.4.1 (pioarduino 54.03.20, `framework = espidf`) with IDF's NimBLE (host memory in
PSRAM, controller from flash) and cJSON; Unity native tests. SwiftUI iOS 17, CoreBluetooth (central, the
`bluetooth-central` background mode, state restoration), Swift Testing in CTAKit.

**Spec:** `docs/superpowers/specs/2026-11-14-ecosystem-design.md`, changed by Task X1 below (Leo approves the diff
before any build task starts). Base spec `2026-11-03-cta-tracker-design.md` §8.3, §9.5, §10; app spec
`2026-11-04-ios-app-design.md` §6.

**Builds on:** main at `9735f8e` (both boards on ESP-IDF; dock pairing over the network; wearable HTTP link with
the token set over serial `T`, no `/pair`), plus branch `worktree-agent-a3343ec2c89239b45` at `9aa8b43` (W5: a
minimal `ble_setup.*`, NimBLE in `sdkconfig.defaults`, serial `b`; **not on main yet**).

**Showcase:** 2026-11-23, dry run 11/22, feature freeze 11/21 18:00. Today is 11/14.

---

## Open questions for Leo

Only the real decisions. Everything else is in "Assumed defaults" below.

1. **Docked without Wi-Fi.** Base spec 8.4 says docked = USB power **and** a battery **and** on home Wi-Fi (slot 0).
   With Bluetooth only, the wearable never counts as docked, so Trackie never sleeps on the dock (showcase step 8).
   **Recommendation:** docked = USB power and a battery, plus slot 0 joined *only if any Wi-Fi network is saved*. A
   wearable with no Wi-Fi docks on power alone. It's a one-line change in `main.cpp` and a spec 8.4 edit.
2. **Bring showcase step 7 ("No Wi-Fi") back.** The relay makes it possible: forget Wi-Fi on the wearable, and its
   cards keep updating from the phone. **Recommendation:** yes, as step 7, but only if Task W6c's gates pass by 11/20
   18:00. Show it with serial `W` (Wi-Fi off until reboot), or with F3's switch if F3 lands. If the gates miss,
   step 7 stays cut and Bluetooth still carries steps 4–6.
3. **Pairing again in the demo.** iOS can't remove a bond from an app. A reset wearable that forgets its bond gets
   refused by an iPhone that still holds it (the spike's status 0x206).
   **Recommendation:** on reset the wearable also makes a new random static address (kept in NVS), so the iPhone sees
   a new device and shows the passkey prompt again. Stale `CTA-Pet-xxxx` entries pile up in iOS Settings, which is
   cosmetic. This is unverified (Task W6a Step 6). If it fails, the demo script adds "Forget CTA-Pet-xxxx in iOS
   Settings → Bluetooth" before pairing.

---

## Assumed defaults (assumed; Leo can change)

1. **Polling while relaying.** *Assumed; Leo can change.* With Wi-Fi joined and no phone connected, the wearable
   polls CTA itself, as today. While the phone is connected over Bluetooth and relaying (a `/relay` tick in the last
   90 s), the wearable polls nothing: no CTA, alerts or weather. It resumes the moment the ticks stop or the link
   drops. For each stop, the record with the newest `ts` wins.
2. **One GATT service.** *Assumed; Leo can change.*
   - A write characteristic `link` for phone → wearable messages, carrying the HTTP link's JSON (`link_msg`
     parsers, cJSON).
   - A notify characteristic `info` for wearable → phone status: battery %, charging, Wi-Fi joined and RSSI,
     settings, paired.
   - Chunking for messages bigger than the MTU.
   - Passkey pairing and bonding replace the HTTP bearer token on Bluetooth. The token stays on the LAN, and the app
     writes it over Bluetooth during setup.
3. **Wi-Fi over Bluetooth.** *Assumed; Leo can change.* Up to 5 networks (`kWifiSlots`, `parseWifi`). Passwords
   cross only the encrypted, bonded link and are never logged. `secrets.h` stays as the fallback list when NVS has
   none.
4. **iOS.** *Assumed; Leo can change.* CoreBluetooth central with the `bluetooth-central` background mode and state
   restoration. The relay runs **in the foreground only** for the showcase. The background keeps the connection and
   the status, nothing more (see "iOS: what really works in the background").
5. **Battery gate.** *Assumed; Leo can change.* A power A/B on the wearable is required (Task W6c):
   - relaying over Bluetooth must project **no worse than Wi-Fi polling in the same session**, with an absolute
     floor of **44.7 h**;
   - Bluetooth on with no phone (advertising, Wi-Fi polling) must project **≥ 43.6 h**, which is 5% under today's
     45.9 h.

   See Gates.

Smaller calls of mine, built in unless Leo says otherwise:

6. **Wi-Fi stays joined while relaying** (if a network is saved). The app's LAN fallback stays instant, and docked
   detection keeps working. Only the polling stops.
7. **Bluetooth is always on** in the live build: advertising whenever no phone is connected. `POST /ble` and the old
   "BLE only without Wi-Fi, or 120 s after `/ble`" rule go away.
8. **No `/pair` on the wearable.** Ecosystem Task 9 (pairing over the network) isn't built. The token arrives over
   Bluetooth. Task 9 comes back only as the fallback if Bluetooth fails (see Cut lines).
9. **The relay rides the app's own arrivals fetches.** The wearable's stops are added to what `ArrivalsStore`
   tracks, so a stop that is both on screen and on the wrist costs one request, not two.

---

## Baseline (what the code has today)

- **Wearable, main:**
  - `link.cpp` serves `/info`, `/config`, `/nearby`, `/pin`, `/follow`, `/ble` and `/reset` on httpd (a PSRAM stack).
    The handler `change()` checks size, token and JSON, then hands off. Pins and follows go straight to
    `live::setPin`/`setFollow`. Config, nearby and reset wait in `lan::Inbox` for the loop.
  - `live.cpp`'s `netTask` deletes itself when no network is saved ("No Wi-Fi set up"). Its clock comes only from
    NTP or the train API's Date header.
  - `live::Store` applies raw CTA bodies per station (`applyTrains`) and for all buses in one batch
    (`applyBuses`).
- **Wearable, `9aa8b43`:** `ble_setup.*` advertises `CTA-Pet-xxxx` with the service and a plain `info` read.
  `stop()` is `nimble_port_stop` + `deinit`. Its `sdkconfig.defaults` turns NimBLE on but **lacks**
  `CONFIG_BT_CTRL_RUN_IN_FLASH_ONLY`, `BT_CTRL_BLE_MASTER=n` and `BT_CTRL_BLE_SCAN=n`. On the dock those took the
  advertising low point from 20.2 to 41.8 KB.
- **Measured (W5):**
  - BLE on, an iPhone connected in LightBlue (idle), TLS polling: lowest **67.1 KB** internal. On the follow-up TLS
    commit, BLE advertising: 50.6 KB.
  - BLE compiled in costs 27 KB at boot even while off. Starting it costs about 35 KB more.
  - Floor 40 KB.
- **Shared lib:** `encodeStopRecord`/`decodeStopRecord` (spec 6.2 records, with `veh`), `parseWifi`, `parseToken`,
  `parseConfig`, `parseStop`, `parseFollow`, `parseNearby`, `encodeInfo`. `ble_ids.h` has the base spec §10 UUIDs.
- **App:**
  - `DeviceLink` (HTTP, Bonjour, 2 s `/info` polls into `DeviceWatch`).
  - `LinkMessages.swift` (`InfoMessage`, `FollowMessage`, `TokenMessage`, `Token`).
  - `BLEMessages.swift` (`WifiMessage`, `NearbyMessage`, `ConfigMessage`, `ConfigStop`).
  - `StopRecord.make` and `Prediction.vehicle`.
  - The Swift `Arrival` has **no `veh`** yet, and Follow needs it in relayed records.
  - `NSBluetoothAlwaysUsageDescription` is still in the project. **Keep it:** Task 12's note to drop it is reversed.
- **Why Bluetooth helps the demo:** on day 1, the iPhone couldn't reach either board over the home router
  (`docs/ecosystem/day1-link.md`). Bluetooth doesn't depend on the router, so the wearable works on any network or
  none. The dock still needs the LAN.

---

## Global Constraints

- **Internal RAM floor:** 40 KB at the worst moment, read with `heap_caps_get_minimum_free_size(MALLOC_CAP_INTERNAL)`.
  The worst moment is Bluetooth connected and relaying, Wi-Fi joined, a TLS handshake, and an HTTP burst.
- **Security:**
  - `link` is `BLE_GATT_CHR_F_WRITE | WRITE_ENC | WRITE_AUTHEN`; `info` is `READ | READ_ENC | READ_AUTHEN | NOTIFY`.
    The firmware notifies only on an encrypted, authenticated connection: check `ble_gap_conn_find` →
    `sec_state` before every notify, since the CCCD's own permission isn't guaranteed (unverified).
  - Pairing: `sm_io_cap = BLE_HS_IO_DISPLAY_ONLY`, `sm_bonding = sm_mitm = sm_sc = 1`. The passkey is
    `esp_random() % 1000000`, shown in Trackie's bubble, **never logged**.
  - `/wifi` and `/token` are accepted **only over Bluetooth** (404 on HTTP).
  - Passwords and the token are never logged, printed, or stored on the phone (the token lives in the Keychain).
- **Threads:**
  - Bluetooth writes arrive on the NimBLE host task (internal stack). The dispatcher there only parses and copies
    into RAM, as the httpd handlers do. Flash writes and Wi-Fi calls stay on the loop and the network task.
  - The reassembly buffer is static in PSRAM.
  - The host task stack goes up from IDF's default only as far as its measured high-water mark requires
    (`CONFIG_BT_NIMBLE_HOST_TASK_STACK_SIZE`; print `uxTaskGetStackHighWaterMark` in the 10 s stats line).
- **Sizes:**
  - HTTP bodies stay at `kMaxBody` (1 KB).
  - A Bluetooth message is at most `kMaxBleMsg = 8192` bytes (a whole station's records, Clark/Lake included).
  - Chunk payload is at most ATT MTU − 3 − 1.
- **Spec values** (UUIDs, intervals, error codes) live once in `ble_ids.h`, `cta_config.h` and `link_msg.h`, with
  their Swift mirrors in `Config.swift`.
- **Tests:** offline and deterministic. No test touches Bluetooth. Frames, envelopes and relay bodies are pure code
  in `firmware/lib` and CTAKit.
- **Commits:** named paths only, no attribution lines, no network names or home paths in tracked files.

---

## The link

### GATT

| Item | UUID | Properties | Payload |
|---|---|---|---|
| Service | `d8d4388c-c8e3-4ff0-b269-015d0979c266` (unchanged) | | advertised as `CTA-Pet-xxxx` |
| `info` | `fb360799-3925-45c3-90be-c32d504bbc99` (unchanged) | read, notify (encrypted) | status, framed (below) |
| `link` | **new** `4a1eedc7-ebeb-42b2-9ce5-e1cc4f05e398` | write with response (encrypted, authenticated) | framed envelope (below) |

The base spec §10 characteristics `wifi`, `config`, `request`, `nearby` and `snapshot` are retired, and so is W6's
planned `token` UUID: all of them become paths on `link`. Their UUIDs stay in `ble_ids.h` history comments only.

### Frames (both directions)

- **Byte 0:** bit 7 = first chunk, bit 6 = last chunk, bits 0–5 = sequence number. The sequence restarts at 0 on
  the first chunk and goes up by 1 (mod 64) per chunk. A single-chunk message sets both bits.
- **The rest:** up to MTU − 4 payload bytes.
- **Phone → wearable:** writes with response, one at a time. The ATT round trip is the flow control.
- **Wearable → phone:** notifications, at most one message in flight.
- **Receiver:** a first chunk resets the buffer. A sequence gap, a continuation with no first chunk, or more than
  `kMaxBleMsg` bytes drops the message; on the wearable, that write also fails with `0x83`. The app drops a broken
  notify and waits for the next one, because status messages are idempotent.

### Envelope (phone → wearable)

`<METHOD> <path>\n<json body>`, with the method `POST` or `DELETE`. Reads aren't needed: status comes by notify.
The wearable dispatches the message when its last chunk arrives, then answers that write:

| Result | ATT answer | HTTP equivalent |
|---|---|---|
| applied | success | 200 |
| bad body, bad envelope | `0x80` | 400 |
| too large | `0x81` | 413 |
| unknown path, or HTTP-only on Bluetooth | `0x82` | 404 |
| broken chunk sequence | `0x83` | — |
| link not encrypted and bonded | NimBLE's insufficient authentication/encryption | 401 |

### Messages

| Path | Body | Parser | Transport | Effect |
|---|---|---|---|---|
| `POST /config` | `{pet, settings}` any subset | `parseConfig` | both | as today (inbox → loop → NVS) |
| `POST /nearby` | base spec `nearby` | `parseNearby` | both | as today |
| `POST/DELETE /pin` | `ConfigStop` | `parseStop` | both | as today |
| `POST/DELETE /follow` | follow body | `parseFollow` | both | as today |
| `POST /reset` | `{}` | — | both | as today, plus Bluetooth bonds cleared and a new address (open question 3) |
| `POST /token` | `{"token"}` | `parseToken` | **Bluetooth only** | saves the HTTP bearer token (NVS, `lan::setToken`) |
| `POST /wifi` | `{"slot","ssid","pass"}` | `parseWifi(…, kWifiSlots, …)` | **Bluetooth only** | saves the slot to NVS `wifi`; the network task rejoins strongest-first |
| `POST /relay` | **new** `{"now", "wmo"?, "temp"?}` | `parseRelayTick` | both | sets the clock if it isn't set yet; weather if present; marks "relaying" for 90 s when it came over Bluetooth |
| `POST /stop` | **new** `{"id","type","records":[stop record…]}` | `parseRelayStop` | both (HTTP ≤ 1 KB, for `link_send.py`) | replaces every card at that stop when its newest `ts` ≥ the stored newest; a stop not in the list or the pin is ignored (200) |

`POST /ble` is removed. HTTP keeps `GET /info` and `GET /config`.

### Status (wearable → phone, `info`)

- **The payload:** `encodeInfo` as today, plus:
  - `wifi.rssi` (dBm; 0 when not joined);
  - `settings` (the `config.settings` shape);
  - `relaying` (bool).

  `GET /info` returns the same JSON.
- **When it goes out:** on subscribe, on any change (battery % step, charging, Wi-Fi joined, an RSSI change of
  5 dB or more, `wake`, `docked`, `settings`, `paired`, `relaying`), and as a heartbeat every 30 s. At most one a
  second.
- **The app's side:** each notify goes into `DeviceWatch` as a poll would, so `wake` going up still sends `nearby`.
  The base spec's `request` characteristic isn't needed.

### Relay

- **Phone side**, while the app is in the foreground and the wearable is connected over Bluetooth:
  - every 30 s, `POST /relay {now, wmo, temp}`;
  - then `POST /stop` for each of the wearable's stops (the last `nearby` the app sent, plus the pin) whose
    arrivals were fetched since the last send.
  - Each stop is sent at least every 60 s.
  - Records: one per line and direction, built with `StopRecord.make` from the app's predictions, with `veh`. Train
    records are keyed (station ID, line, the `stops.json` direction label), as the wearable's train cards are (base
    spec 6.2).
- **Wearable side:**
  - `Store::applyRelay` puts each stop's records where that stop's poll would have put them: the station's slot, the
    bus cards for that stop ID, or the pin's own poll.
  - Alert flags are OR'd with the alerts poll.
  - `followView` runs on relayed records as on polled ones, so Follow works with no Wi-Fi.
- **Clock:** with no Wi-Fi there's no NTP. `/relay`'s `now` sets the clock with `settimeofday` when `ntpSynced()`
  is false. NTP corrects it later if Wi-Fi comes up.

### Transport selection (app)

- **Writes:** `DeviceLink.post`/`delete` to the wearable go over Bluetooth when `BLELink` holds an encrypted
  connection to it. Otherwise they go over HTTP, if the board has an address. A Bluetooth write that fails on a
  disconnect is retried once over HTTP.
- **Status:** while Bluetooth is connected, the app skips the wearable's HTTP `/info` poll, since notifies replace
  it. On disconnect the 2 s poll resumes at once, and `DeviceWatch` declares it offline only after 3 misses with
  neither link working.
- **The dock:** HTTP only, unchanged.

---

## iOS: what really works in the background

| Situation | What iOS allows | Plan |
|---|---|---|
| App in the foreground | Everything | **The showcase case.** Relay, writes, status. Set the demo iPhone's Auto-Lock to Never. |
| App in the background, or the phone locked, connected | With `bluetooth-central`: the connection stays up; each notify wakes the app for about 10 s; writes work in that window; a `URLSession` fetch may finish if it's quick (or under `beginBackgroundTask`, about 30 s) | Keep the connection and take status notifies. **No relay in the background for the showcase.** Stretch (after 11/23): fetch and relay in the window the 30 s heartbeat notify opens. Unverified how reliably iOS grants it. |
| App in the background, out of range | A pending `connect()` to a known peripheral never times out, and iOS reconnects when it comes back in range, waking the app | Always keep a pending connect to the known wearable. It's the cheapest keep-alive. |
| App suspended, then terminated by iOS for memory | State restoration (`CBCentralManagerOptionRestoreIdentifierKey`) relaunches it in the background for Bluetooth events | `willRestoreState` re-adopts the peripheral. |
| User force-quit the app | iOS never relaunches it for Bluetooth | Known limit (base spec 9.5). The wearable shows its last data greying out, and falls back to its own Wi-Fi polling if Wi-Fi is set. |
| Phone rebooted | No relaunch until the user opens the app (unverified for iOS 17) | Same as force-quit. |
| Background fetch (`BGAppRefreshTask`) | Opportunistic: minutes to hours apart, at iOS's choosing | Useless for a 60 s relay. Not used. |
| A new `nearby` on wake while backgrounded | Needs a location fix, and "Always" permission in the background | Not done. The wearable keeps its last list, and the next foreground answer refreshes it. |

**Realistic for the showcase:** the app in the foreground on an unlocked phone, relaying and mirroring. In the
background the wearable stays connected, and if it has Wi-Fi it polls for itself again within 90 s of the last tick.

---

## Gates

| Gate | Measure | Pass |
|---|---|---|
| **RAM** | lowest internal free over 10 min, three cases: (a) Bluetooth connected and relaying, Wi-Fi joined, CTA polling stopped; (b) Bluetooth connected, phone not relaying, Wi-Fi TLS polling plus `link_send.py --burst`; (c) Bluetooth advertising, Wi-Fi polling | **≥ 40 KB** in every case (expected ≥ 60 KB with the controller in flash); no leak across 3 off/on cycles of serial `b` |
| **Power P1** (phone away) | `power_run.py` interleaved A/B: A = main (Wi-Fi polling, BT compiled out), B = this build, BLE advertising | projected **≥ 43.6 h** (5% under 45.9 h), dim idle within 5% of A |
| **Power P2** (relaying) | the same session: B = this build, the iPhone app connected and relaying in the foreground | projected **≥ A's projection in the same session** (within A's noise), floor **44.7 h** |
| **Latency** | app action to the wearable's screen, over Bluetooth | pin or follow ≤ 1 s; a fresh `nearby` fills its first cards ≤ 10 s; relayed cards never older than 60 s while connected; foreground reconnect after a wearable reboot ≤ 10 s; the HTTP fallback takes over ≤ 6 s after Bluetooth drops |
| **Frames** | Stops scroll fps and tap→screen in the power run's churn and scroll phases | no worse than W4 (about 70 fps while moving, tap→screen 17–21 ms) |

Why the power gate is set this way: relaying replaces 5–6 TLS requests a minute with a few Bluetooth writes, so it
should come in at or under Wi-Fi polling. Requiring "no worse than A" catches a design that costs more than it
saves. The 44.7 h floor is the gate the ESP-IDF port already passed. P1 is the everyday case, the phone left at home.
It may cost a little (advertising, coexistence), so it gets the 5% allowance and no more.

**Power model caveat:** `tools/power_model.json` has `ble.advertising_ma` and `ble.connected_ma` at 0 ("zero until
BLE exists"), and the board has no current sensor. Task W6c fills them from Espressif's datasheet figures and logs the
Bluetooth state per sample. So P1 and P2 measure the saving (fewer requests, CPU time) and *model* Bluetooth's radio
cost. That part is an estimate, not a measurement. A battery discharge run per mode (AXP2101 percent over 4 h) is the
real check; it's recommended after the showcase and is not a gate.

---

## Review Focus

1. **Bonded-only writes.** A write from an unbonded central gets an authentication error and never reaches the
   dispatcher. `/wifi` and `/token` over HTTP get 404. Test: `test_link_ble` `wifi_token_ble_only` on the dispatcher
   with a transport flag; on device, LightBlue unpaired.
2. **Frames.** Gaps, duplicates, a continuation first, overflow at `kMaxBleMsg` + 1, a single chunk, and exactly 64
   chunks (the sequence wraps). Test: `test_ble_frame` (C++) and `BLEFrameTests` (Swift) on the same vectors.
3. **The host task stays light.** No NVS, no `esp_wifi_*` and no `vTaskDelay` on the NimBLE host task. Its stack
   high-water mark is printed and stays above 1 KB under the largest `/stop`.
4. **Relay merge.** Newest `ts` wins per stop. A `/stop` for a stop not in the list is ignored. A relayed record
   keeps Follow working (`followView`). Polling resumes 90 s after the last tick. Test: `live_test.cpp` relay
   cases.
5. **Secrets.** A Wi-Fi password never appears in serial output, logs, `WifiMessage`'s descriptions, or the NimBLE
   log (`CONFIG_BT_NIMBLE_LOG_LEVEL_NONE`). The passkey is never printed.

---

## Tracks and the order to run them

| Track | Owns (files) | Tasks | Waits on |
|---|---|---|---|
| **X — spec and docs** | `docs/superpowers/specs/2026-11-14-ecosystem-design.md`, base spec §8.3/8.4/9.5/10, app spec §6, this plan, `docs/superpowers/plans/2026-11-14-ecosystem.md` (Tasks 9, 12, 19 notes), `docs/superpowers/plans/2026-11-14-esp-idf-port.md` (W6 pointer) | X1, X2 | nothing (X1 first) |
| **L — shared library** | `firmware/lib/cta/src/ble_frame.*` (new), `link_msg.*`, `ble_ids.h`, `cta_config.h` (relay constants), `firmware/native/test/test_ble_frame`, `test_link` (new cases), `tools/link_send.py` (`relay`, `stop` commands) | L1 | X1 approved |
| **K — CTAKit** | `ios/CTAKit/**` (`BLEFrames.swift` new, `LinkMessages.swift`, `BLEMessages.swift`, `StopRecord.swift`, `Config.swift`) | K1 | X1 approved |
| **W — wearable** | `firmware/handheld/mock/src/ble_setup.*`, `link.*`, `live.*`, `live_store.*`, `main.cpp`, `sdkconfig.defaults`, `tools/live_test.cpp`; `ui.cpp` setup states only (passkey, "Joining Wi-Fi…"), coordinated with H-UI | W6a, W6b, W6c | L1 merged before W6a Step 3 |
| **AB — app Bluetooth** | `ios/CTATracker/Devices/BLELink.swift`, `WearableRelay.swift`, `WearableSetup.swift` (all new); the routing seam in `DeviceLink.swift`; `UIBackgroundModes` in `project.pbxproj` | AB1, AB2, AB3 | K1 merged |
| **A — app (existing)** | as in the ecosystem plan; adds Wi-Fi RSSI and "via Bluetooth / Wi-Fi" to Task 11's device page | 11, 12 (dock branch), 13–17 | K1 for the new `InfoMessage` fields |

**Files shared between tracks:**
- **AB and A share `DeviceLink.swift` and the setup flow.** AB lands the routing seam first, as AB1's first small
  commit (`post`/`delete`/`feed` go through one `route(id)`), and A rebases on it. Task 12's found card and flow shell
  are A's. AB adds the wearable branch (`WearableSetup.swift`), and A's dock branch keeps `POST /pair`.
- **W and H-UI share `ui.cpp`.** W touches only the Pairing state's text (the passkey) and adds a "Joining Wi-Fi…"
  bubble. F3's settings page, if it lands, shows "Phone: connected" and holds the Wi-Fi off switch (open question 2).

### Order, day by day

| Day | X | L | K | W (wearable board) | AB | A |
|---|---|---|---|---|---|---|
| **11/15** | X1 spec diff, morning → Leo approves | L1 | K1 | W6a Steps 1–2 (rebase `9aa8b43`, controller in flash, RAM re-measure) | — | Tasks 11–12 as planned |
| **11/16** | — | `link_send.py` relay commands | — | W6a Steps 3–6 (GATT, frames, dispatcher, pairing, token/Wi-Fi) | AB1 (transport) | 12, 13 |
| **11/17** | — | — | — | W6b (relay intake, polling switch, no-Wi-Fi boot) | AB1 on the board; AB2 (setup) | 13, 14 |
| **11/18** | — | — | — | W6c RAM gates (morning), power A/B (afternoon, needs the iPhone) | AB2, AB3 (relay) | 15, 16 |
| **11/19** | — | — | — | fixes from the gates | AB3 on the board | 16, 17 |
| **11/20** | — | — | — | full showcase run over Bluetooth, home network and no Wi-Fi; **18:00: step 7 go/no-go** | fixes | fixes |
| **11/21** | X2 docs and demo script | | | | | freeze 18:00 |
| **11/22** | dry run twice on campus, plus no Wi-Fi | | | | | |

**Merge order onto main:** X1, then L1 and K1, then W6a, then AB1, then W6b, then AB2 and AB3, then W6c (results
only), then X2. Each track rebases on main before merging. Finished, reviewed work ships without asking.

**Estimates (agent-days):** X1 0.25 · L1 0.5 · K1 0.5 · W6a 1.5 · W6b 1.0 · W6c 0.5 (plus about 3 h of board and
phone time) · AB1 1.0 · AB2 0.75 · AB3 0.75 · X2 0.25. About **7 agent-days** over three parallel tracks. The
critical paths are L1 → W6a → W6b → W6c (done by 11/18 evening) and K1 → AB1 → AB3 (on the board 11/19). That leaves
**about one day of slack** before the freeze. Track A's own Tasks 11–17 still need all of 11/15–11/20, which is why
AB is a separate session.

### Cut lines

- **11/18 12:00, W6a's RAM gate fails** after the ladder (`BLE_MAX_ACT=1`, then the Wi-Fi and lwIP buffer rungs from
  D4): Bluetooth carries status, pin, follow, nearby and config, but no relay (the 8 KB buffer and `/stop` go). Wi-Fi
  is required again, and step 7 stays cut.
- **11/19 12:00, Bluetooth unstable on the board** (drops, coexistence stalls TLS): the wearable goes back to HTTP
  only, with Task 9 Appendix A pairing (`POST /pair`, the code in the bubble, about 0.5 day, as the dock's
  `9735f8e`). The app's Bluetooth stays in the code, switched off by a flag.
- **11/20 18:00, P2 or the latency gate fails:** the relay ships off by default (the app doesn't send `/relay`); the
  rest of the Bluetooth link stays.

---

### Task X1: Spec and plan changes for the Bluetooth link (Track X)

**Files:** `docs/superpowers/specs/2026-11-14-ecosystem-design.md`, `docs/superpowers/specs/2026-11-03-cta-tracker-design.md`
(§8.3, §8.4 if open question 1 is yes, §9.5, §10), `docs/superpowers/specs/2026-11-04-ios-app-design.md` (§6
notes), `docs/superpowers/plans/2026-11-14-ecosystem.md` (Task 9: replaced by W6a here; Task 12: wearable branch is
AB2; Task 19: replaced by W6b/AB3), `docs/superpowers/plans/2026-11-14-esp-idf-port.md` (W6 points here; E1 is
superseded by this task), `todo.md` F17.

- [ ] **Step 1:** In the ecosystem spec:
  - The status line records Leo's 2026-11-14 decisions: Bluetooth first for the wearable, the dock Wi-Fi only.
  - §1 and §2: step 7 comes back conditionally (open question 2).
  - §4.1 and §4.2: a new picture, with the phone ⇄ wearable link over Bluetooth, the LAN as the fallback, and the
    dock on the LAN.
  - §4.3: the wearable's Bluetooth bond, and the token written over it.
  - §4.4: "The link" section of this plan (GATT, frames, envelope, messages, status).
  - §5.1: wearable setup over Bluetooth (pair → Wi-Fi optional → Trackie); the dock stays network pairing.
  - §5.8: the relay is back.
  - §6: RAM and power gates from this plan.
  - §7: risks (coexistence; iOS background limits; re-pairing).
  - §12 becomes "built", and points here.
- [ ] **Step 2:** The base spec §10's table matches this plan's GATT (retired characteristics struck through, with
  the date), and §8.3 says that the phone relays over Bluetooth and the wearable needs no Wi-Fi. The app spec §6
  note: the wearable sets up over Bluetooth again, the dock over the network.
- [ ] **Step 3:** Put the spec diff in the report for Leo. **No build task starts until he approves it.** Check the
  diff for network names, home paths and tokens. Commit (docs only, no review):
  `docs(ecosystem): Bluetooth first for the wearable, the relay back`.

### Task L1: Frames, envelope, relay bodies in the shared library (Track L)

**Files:**
- Create: `firmware/lib/cta/src/ble_frame.h`, `ble_frame.cpp`, `firmware/native/test/test_ble_frame/test_main.cpp`
- Modify: `firmware/lib/cta/src/link_msg.h/.cpp` (envelope, relay tick, relay stop, `InfoMsg` fields),
  `stop_record.h/.cpp` (a cJSON-node decoder that `decodeStopRecord` and `parseRelayStop` share),
  `ble_ids.h` (`kLinkUuid`), `cta_config.h` (`kRelayHoldSec = 90`, `kRelayTickSec = 30`, `kInfoHeartbeatSec = 30`),
  `firmware/native/test/test_link/test_main.cpp`, `tools/link_send.py` (+ `relay`, `stop`), `tools/test_link_send.py`

**Interfaces (namespace `cta`):**

```cpp
// ble_frame.h
constexpr size_t kMaxBleMsg = 8192;
constexpr uint8_t kFrameFirst = 0x80, kFrameLast = 0x40, kFrameSeqMask = 0x3F;
enum class FrameResult : uint8_t { More, Done, Error };
class Reassembler {   // caller owns buf (PSRAM on the board)
public:
    Reassembler(char* buf, size_t cap);
    FrameResult add(const uint8_t* chunk, size_t len);   // Done: message() is complete; Error: dropped
    const char* message() const; size_t length() const;
};
// Splits msg into chunks of at most chunkMax bytes (header included); calls emit(chunk, len) for each; false if
// msg > kMaxBleMsg or chunkMax < 2.
bool splitFrames(const char* msg, size_t len, size_t chunkMax, bool (*emit)(const uint8_t*, size_t, void*), void* ctx);

// link_msg.h additions
enum class Method : uint8_t { Post, Delete };
bool parseEnvelope(const char* msg, size_t len, Method& m, char path[16], const char*& body, size_t& bodyLen);
struct RelayTick { uint32_t now; bool hasWeather; int wmo; float tempC; };
bool parseRelayTick(const char* json, size_t len, RelayTick& out);          // now required, > 1700000000
constexpr int kMaxRelayRecords = 16;                                       // Store::kCardsPerStation
struct RelayStop { char id[8]; Kind type; StopRecord records[kMaxRelayRecords]; int n; };
bool parseRelayStop(const char* json, size_t len, RelayStop& out);         // each record: decodeStopRecord's rules,
                                                                           // and its id and type must match the stop's
// InfoMsg gains: int8_t rssi; bool hasSettings; DeviceSettings settings; bool relaying;
// encodeInfo writes "wifi":{"connected","ssid","rssi"}, "settings" (when hasSettings), "relaying".
```

- [ ] **Step 1: Write the failing tests:**

```cpp
// test_ble_frame
void single_chunk_has_both_bits();     void three_chunks_reassemble();
void gap_drops_message();              void continuation_first_is_error();
void overflow_at_max_plus_one();       void sequence_wraps_after_64();
void first_chunk_resets_half_message();void split_then_reassemble_round_trips();   // MTU 20, 185, 509
// test_link additions
void envelope_post_pin();              void envelope_delete_no_body();
void envelope_rejects_get_and_no_newline();
void relay_tick_needs_now();           void relay_tick_weather_optional();
void relay_stop_parses_six_records();  void relay_stop_rejects_record_of_other_stop();
void relay_stop_caps_at_16();          void relay_stop_bad_json_leaves_out();
void info_has_rssi_settings_relaying();
```

- [ ] **Step 2:** Run `cd firmware/native && ~/.platformio/penv/bin/pio test -e native -f test_ble_frame -f test_link`.
  Expect FAIL.
- [ ] **Step 3:** Implement. Reuse `decodeStopRecord`'s body by splitting it into a cJSON-node decoder, and keep the
  "fill a temporary, copy on success" pattern. `splitFrames` and `Reassembler` stay free of allocation.
- [ ] **Step 4:** Run the whole native suite (`pio test -e native`). All pass.
- [ ] **Step 5:** Add to `link_send.py`:
  - `relay [now]`, which posts `/relay`;
  - `stop <json>`, which posts `/stop`;
  - the fake board (`--serve`), which accepts both with the same checks.

  `tools/test_link_send.py` covers the fake's answers.
- [ ] **Step 6:** Commit: `feat(lib): BLE frames, link envelope, relay tick and stop messages`.

### Task K1: CTAKit mirrors (Track K)

**Files:** Create `ios/CTAKit/Sources/CTAKit/BLEFrames.swift`, `ios/CTAKit/Tests/CTAKitTests/BLEFrameTests.swift`;
modify `LinkMessages.swift`, `BLEMessages.swift`, `StopRecord.swift`, `Config.swift`, and their tests.

**Interfaces:**

```swift
public enum BLEFrames {
    public static let maxMessage = 8192
    public static func split(_ message: Data, chunkMax: Int) -> [Data]?          // nil if too big or chunkMax < 2
    public struct Reassembler { public mutating func add(_ chunk: Data) -> Data?  // a whole message, or nil
                                public init() }
    public static func envelope(_ method: String, _ path: String, _ body: Data?) -> Data   // "POST /pin\n{…}"
}
public struct RelayTick: Encodable, Sendable { let now: Int; let wmo: Int?; let temp: Double? }
public struct RelayStop: Encodable, Sendable { let id: String; let type: StopRecord.RecordType; let records: [StopRecord] }
// Arrival gains `veh: String?` (encoded only when non-nil; cut to Config.idLen). StopRecord.make takes it from
// Prediction.vehicle.
// InfoMessage gains `paired: Bool`, `relaying: Bool` (default false when absent), `settings: DeviceSettings?`,
// and WifiState gains `rssi: Int` (default 0 when absent), so the dock's /info still decodes.
// BLEID gains `link = "4a1eedc7-ebeb-42b2-9ce5-e1cc4f05e398"`.
```

- [ ] **Step 1:** Write the tests first:
  - `BLEFrameTests`: the same vectors as `test_ble_frame`, including the wrap at 64 and the overflow.
  - `relayStopRoundTrip`: decodes with `StopRecord.decode`.
  - `arrivalVehOmittedWhenNil`.
  - `infoDecodesDockWithoutNewFields`.
  - `infoDecodesWearableWithRSSI`.
  - `wifiMessageNeverShowsPassword` (exists; keep it).
- [ ] **Step 2:** Implement. Run the CTAKit tests (`swift test` in `ios/CTAKit`) in full.
- [ ] **Step 3:** Commit: `feat(ctakit): BLE frames, relay messages, vehicle on records, Wi-Fi signal in info`.

### Task W6a: Wearable Bluetooth link (Track W; ecosystem Task 9 and the old W6, replaced)

**Files:** `firmware/handheld/mock/src/ble_setup.h/.cpp` (grows into the link; the name stays), `link.h/.cpp`
(dispatcher), `main.cpp`, `live.cpp` (Wi-Fi list from NVS written over Bluetooth; no exit with no networks),
`sdkconfig.defaults`, `ui.cpp` (Pairing state text only).

**Interfaces:**

```cpp
namespace lan {
enum class Via : uint8_t { Http, Ble };
// The transport-neutral handler: change()'s body after the size and token checks. Returns the HTTP status (200,
// 400, 404, 413). Runs on httpd's task or NimBLE's host task: parses, then copies into RAM (Inbox, live::setPin…).
int apply(Method m, const char* path, const char* body, size_t len, Via via);
// Inbox gains: bool token; char tokenHex[33]; bool wifi; cta::WifiMsg wifiMsg (wiped after the loop takes it).
}
namespace ble_setup {
bool start(const char* name);   // as 9aa8b43, plus the link and info characteristics, pairing, bonding
void stop();
bool running();
bool connected();                 // an encrypted, bonded link is up
void notifyInfo(const char* json, size_t len);   // frames and notifies; no-op when not connected
uint32_t passkey();               // 0 = none showing; the loop draws it in Trackie's bubble
void forgetBonds();               // ble_store_clear() plus a new random static address in NVS (reset)
}
```

- [ ] **Step 1: Rebase the W5 work.** Cherry-pick `a55d8b7` and `9aa8b43` onto main and resolve `main.cpp`. Add to
  the wearable's `sdkconfig.defaults`:
  - `CONFIG_BT_CTRL_RUN_IN_FLASH_ONLY=y` (safe: `CONFIG_SPIRAM_XIP_FROM_PSRAM=y` is set);
  - `CONFIG_BT_CTRL_BLE_MASTER=n`, `CONFIG_BT_CTRL_BLE_SCAN=n`.

  If Kconfig names an option unknown, find its current name with menuconfig and say so in the report.
- [ ] **Step 2: Re-measure (board):** boot free; BLE advertising plus polling, 10 min; LightBlue connected plus
  polling, 10 min; off/on ×3. Record the rows in `docs/ecosystem/day1-link.md` under "ESP-IDF". The expected gain
  over W5 is about 20 KB, as on the dock (unverified).
- [ ] **Step 3: The dispatcher.** Move `change()`'s per-path code into `lan::apply` (behaviour unchanged for HTTP).
  The httpd handler keeps the size, token and receive steps, then calls `apply(…, Via::Http)`. Remove `/ble` and
  `openBle`. Add `/token` and `/wifi` (Bluetooth only; 404 on HTTP), and `/relay` and `/stop`, which return 404
  until W6b. Native-testable parts go in `link_msg`; the board part is a thin switch.
- [ ] **Step 4: GATT and frames.**
  - `link` write: the host task feeds a `cta::Reassembler` over a static `kMaxBleMsg` PSRAM buffer. On `Done` it
    calls `parseEnvelope`, then `lan::apply(…, Via::Ble)`, and maps the status to the ATT answer (table above).
  - `info`: read returns the current `encodeInfo` JSON. Notify frames it with `splitFrames` at
    `ble_att_mtu(conn) − 3`.
  - Set `CONFIG_BT_NIMBLE_ATT_PREFERRED_MTU=256` (unverified what iOS settles on; it picks 185 on many models).
  - The loop calls `notifyInfo` on the change rules and the 30 s heartbeat (see Status).
- [ ] **Step 5: Pairing, token, Wi-Fi.**
  - Pairing: DISPLAY_ONLY. On `BLE_GAP_EVENT_PASSKEY_ACTION` pick the passkey, set `passkey()` for the loop to draw
    in the Pairing bubble ("Type 123 456 on your phone"), and inject it. Clear it on `ENC_CHANGE`. Never print it.
  - A `/token` write saves the token through `main.cpp`'s token path (NVS plus `lan::setToken`), from the loop.
  - A `/wifi` write saves the slot into NVS `wifi` (`cta::WifiList`), from the loop. The network task re-reads the
    list and rejoins strongest-first.
  - With no networks saved at boot, `netTask` no longer deletes itself. It waits for a list, and starts Wi-Fi on the
    first one without a reboot.
  - `info.paired` = a bond exists or a token is saved. While a Wi-Fi join after a `/wifi` write is under way, the
    bubble says "Joining Wi-Fi…", and the result shows in `info.wifi`.
  - BLE starts at boot in the live build (assumed default 7) and stays on. Serial `b` still toggles it for
    measurements.
- [ ] **Step 6: Reset and re-pair.** `resetLink` (serial `R`, `POST /reset`, the two-finger hold) also calls
  `forgetBonds()`: `ble_store_clear()`, a new static random address from `ble_hs_id_gen_rnd(0, …)` saved in NVS, and
  `ble_hs_id_set_rnd` at every start. **Verify on the board:** after a reset, the iPhone that held the old bond
  pairs again with a fresh passkey prompt and no Forget in iOS Settings (open question 3). If not, report it, and
  X2's demo script adds the Forget step.
- [ ] **Step 7: Verify (board plus LightBlue; app checks come with AB1):**
  - An unpaired write gets an authentication error. Paired with the passkey from the bubble, writes work:
    - `POST /pin` puts a ★ on the card;
    - `/wifi` joins (two slots, the stronger wins);
    - `/token` lets `link_send.py --token` work on the LAN.
  - A 3-chunk message reassembles.
  - `--garbage` over HTTP still gives 400/413/401/401.
  - RAM gate cases (b) and (c).
  - The passkey and password never appear on serial.
- [ ] **Step 8:** Commit: `feat(handheld): Bluetooth link: paired writes, status notify, Wi-Fi and token over BLE`.

### Task W6b: Relay intake and polling while relaying (Track W)

**Files:** `link.cpp` (`/relay`, `/stop`), `live.h/.cpp` (relaying flag, polling switch, clock), `live_store.h/.cpp`
(`applyRelay`), `main.cpp` (`info.relaying`, docked rule if open question 1 is yes, serial `W`), `tools/live_test.cpp`.

**Interfaces:**

```cpp
namespace live {
void relayTick(const cta::RelayTick& t, bool viaBle);   // clock if unsynced; weather; relaying for kRelayHoldSec if viaBle
bool relayStop(const cta::RelayStop& s);                // false if the stop isn't in the list or the pin (ignored)
bool relaying();                                        // a Bluetooth tick in the last kRelayHoldSec
}
// Store gains: bool applyRelay(const cta::RelayStop& s);  // replaces the stop's cards if its newest ts >= stored newest
```

- [ ] **Step 1: Tests first,** in `tools/live_test.cpp` (laptop):
  - a relayed station replaces its cards, and an older relay doesn't;
  - a relayed bus stop replaces only that stop's cards;
  - a relay for a stop not in the list is ignored;
  - a pin outside the list takes its relay;
  - Follow counts down and goes Due from relayed records;
  - an alert flag survives the alerts merge.
- [ ] **Step 2:** Implement:
  - `netTask` skips `pollOne` while `relaying()`. Wi-Fi joins and httpd are untouched.
  - Polls resume within one pass after the hold ends.
  - `/relay` over Bluetooth with no NTP sets the clock (`settimeofday`). NTP takes over when it syncs.
  - Serial `W` forgets nothing: it drops Wi-Fi and keeps it off until reboot, for the step 7 demo.
  - Docked rule: as open question 1's answer.
- [ ] **Step 3: Verify (board):**
  - Over HTTP from the Mac, `link_send.py relay` then `stop '<Belmont records>'` shows the relayed cards (with Wi-Fi
    up, as a stand-in until AB3). Then over Bluetooth with the app (AB3):
  - With no Wi-Fi saved (fresh after `R`), the wearable boots, pairs, gets `nearby`, sets its clock from `/relay`,
    and fills its cards from the phone.
  - With Wi-Fi joined, the serial stats line shows `CTA requests` flat while relaying, and climbing within 90 s of
    closing the app.
  - RAM gate case (a).
- [ ] **Step 4:** Commit: `feat(handheld): arrivals relayed from the phone; no CTA polling while relaying`.

### Task W6c: Gates on the wearable (Track W; Leo's board and iPhone time)

**Files:** `tools/power_model.json` (`ble.advertising_ma`, `ble.connected_ma`, with a datasheet source), the power
log columns for the Bluetooth state (`power_log.cpp`, `tools/power_model.py`, and their tests), `docs/power/log.md`,
`docs/ecosystem/day1-link.md`.

- [ ] **Step 1:** Fill the BLE model terms from Espressif's ESP32-S3 datasheet, marked "estimate". Log the Bluetooth
  state (off, advertising, connected) and the relaying flag per sample. Add a `test_power_model` case for them.
- [ ] **Step 2: RAM** (all three cases in Gates; 10 min each; off/on ×3 with no leak).
- [ ] **Step 3: Power:** interleaved runs A, B1, B2, A, B1, B2, at the same hour, with no touches:
  - A = main;
  - B1 = this build with the phone away (Bluetooth advertising, Wi-Fi polling);
  - B2 = this build with the iPhone app in the foreground, connected and relaying (Auto-Lock off).

  Gates P1 and P2. Also note scroll fps and tap→screen.
- [ ] **Step 4: Latency:** pin and follow (stopwatch on video, or a serial timestamp on receipt against the app's
  log); `nearby` → first card; a relayed card's age; reconnect after a wearable reboot; the fallback to HTTP after
  `b` turns Bluetooth off.
- [ ] **Step 5:** Write the results to `docs/power/log.md` and `docs/ecosystem/day1-link.md`. If a gate fails, apply
  the ladder below, then the cut lines. Commit:
  `docs(power): Bluetooth link on the wearable, RAM and power gates`.

**Power ladder, if P1 or P2 fails (no feature loss):**
1. Connection parameters: request a 100–200 ms interval with slave latency 4 once the link is idle (the app's writes
   come in bursts every 30 s).
2. Advertising interval 1022.5 ms (one of Apple's recommended values) after 60 s with no connection.
3. Turn `info` RSSI updates down to the heartbeat only.

### Task AB1: The Bluetooth transport in the app (Track AB)

**Files:** Create `ios/CTATracker/Devices/BLELink.swift`; modify `DeviceLink.swift` (routing seam: `post`, `delete`,
the `/info` poll skip, and `feed` from notifies), `ios/CTATracker.xcodeproj/project.pbxproj` (`UIBackgroundModes` =
`bluetooth-central`; keep `NSBluetoothAlwaysUsageDescription`), `App/AppModel.swift` (one line: start `BLELink`);
tests in `ios/CTATrackerTests/BLELinkTests.swift` (the pure parts).

**Interfaces:**

```swift
@Observable final class BLELink: NSObject {
    private(set) var connected: [String: UUID] = [:]                 // device id -> peripheral, encrypted links only
    private(set) var found: [UUID: String] = [:]                     // unknown CTA-Pet-* in range -> advertised name
    var onInfo: ((String, InfoMessage) -> Void)?                     // DeviceLink.feed
    func setActive(_ active: Bool)                                   // scan in the foreground; pending connects always
    func connect(_ peripheral: UUID)                                 // setup and known devices
    func send(_ method: String, _ path: String, _ body: Data?, to id: String) async throws   // frames, writes
                                                                                            // with response, in order
}
// DeviceLink: post/delete -> ble.connected[id] != nil ? ble.send : HTTP; HTTP retry once on a BLE disconnect error.
// Device gains `peripheral: UUID?` (saved in KnownDevices).
```

- [ ] **Step 1:** Implement the central with `CBCentralManagerOptionRestoreIdentifierKey` and `willRestoreState`:
  - scan for the service UUID in the foreground only;
  - keep a pending `connect` to each known wearable;
  - discover `link` and `info`, subscribe to `info`, and reassemble notifies with `BLEFrames.Reassembler`;
  - chunk size from `maximumWriteValueLength(for: .withoutResponse)` (MTU − 3; the `.withResponse` value is 512 on
    iOS, unverified), sent with response, one message at a time through an `AsyncStream` queue;
  - map ATT errors 0x80–0x83 to `LinkError.status(400/413/404/0)`;
  - log no bodies.
- [ ] **Step 2:** Pure tests: the routing rule (BLE when connected, HTTP otherwise, a retry after a disconnect), the
  ATT-to-status map, and a notify reassembled across 3 chunks into an `InfoMessage`.
- [ ] **Step 3: Verify (board, after W6a):**
  - The paired wearable (paired from LightBlue first, or from AB2) shows online with battery and RSSI while
    Wi-Fi is off.
  - "Send to Trackie" works with the phone on cellular.
  - Turning Bluetooth off on the wearable (`b`) moves the app to HTTP within 6 s when Wi-Fi is up.
  - Rebooting the wearable reconnects within 10 s.
- [ ] **Step 4:** Commit: `feat(ios): Bluetooth link to the wearable, HTTP as the fallback`.

### Task AB2: Wearable setup over Bluetooth (Track AB; with Task 12's shell)

**Files:** Create `ios/CTATracker/Devices/WearableSetup.swift`; hook into Task 12's `FoundCard`/`SetupFlow` (A owns
the shell; this adds the wearable branch).

- [ ] **Step 1:** Build the wearable branch:
  - `BLELink.found` raises "Trackie found · Set up".
  - **Pair:** "Type the code shown on Trackie when your iPhone asks." `connect`, then subscribing to `info` brings up
    iOS's pairing prompt. Once encrypted, the app writes `POST /token` with the household token
    (`TokenStore.token() ?? Token.random()`, saved on success) and adds the device, keyed by `info.id`. An existing
    entry with the same id gets the new peripheral UUID.
  - **Wi-Fi (optional, "Skip"):** network name and password, "Add another" up to 5 (slot order: home first). Each
    `POST /wifi`, then the app watches `info.wifi.connected` up to 20 s. On failure: "Couldn't join. Check the
    password." The password field is cleared after sending and is never stored or logged.
  - **Pet** (first device only).
  - **Ready.**
  - **Forget:** removes the device and says to finish in iOS Settings → Bluetooth (app spec 6.3).
- [ ] **Step 2: Verify (board):**
  - From fresh (`R`): pair with the passkey, skip Wi-Fi, and the wearable works over Bluetooth alone.
  - Again from fresh with the phone's hotspot as the Wi-Fi: it joins, and the HTTP fallback works with the token.
  - Re-pair after a reset with no Forget in iOS Settings (open question 3).
- [ ] **Step 3:** Commit: `feat(ios): set up the wearable over Bluetooth, Wi-Fi optional`.

### Task AB3: The relay (Track AB)

**Files:** Create `ios/CTATracker/Devices/WearableRelay.swift`; modify `Data/ArrivalsStore.swift` (extra fetch IDs
the relay asks for, deduplicated with the list's), `App/AppModel.swift` (start and stop with the foreground and
the Bluetooth connection; the last `NearbyMessage` sent and `pinned` are the stop set).

**Interfaces:**

```swift
@Observable final class WearableRelay {
    func update(stops nearby: NearbyMessage?, pin: ConfigStop?)    // what the wearable shows
    func setActive(_ active: Bool)                                  // foreground && BLE-connected
    // every Config.relayTickSec: /relay {now, weather}, then /stop for each stop fetched since its last send;
    // every stop at least every 60 s
}
```

- [ ] **Step 1: Pure tests first** (`ios/CTATrackerTests/WearableRelayTests.swift`, or CTAKit if the record
  building lives there):
  - records for a station: one per line and direction, with the wearable's keys, checked against the same
    `fixtures/` train response that `test_train_cards` uses, so the (id, route, dir) keys match the C++ cards;
  - bus records split per route and direction;
  - `veh` carried;
  - a stop not fetched since its last send isn't re-sent before 60 s.
- [ ] **Step 2:** Implement. Reuse `StopRecord.make` and `CardBuilder`'s slot rules, not a new card builder. The
  weather comes from the app's own weather fetch if it has one; otherwise omit it.
- [ ] **Step 3: Verify (board):**
  - The wearable with no Wi-Fi shows live cards from the phone, never older than 60 s.
  - Pan the map: the mirror sends a new `nearby`, and the new stops' cards arrive within 10 s.
  - Follow a train: the countdown runs on relayed data.
  - Close the app: the wearable keeps its last cards greying out, and with Wi-Fi it polls again within 90 s.
- [ ] **Step 4:** Commit: `feat(ios): relay arrivals to the wearable over Bluetooth`.

### Task X2: Docs and the demo script (Track X; with ecosystem Task 21)

- [ ] **Step 1:** Update the wearable README:
  - Bluetooth pairing, serial `b`, `W`, `R`;
  - `link_send.py relay` and `stop`;
  - the HTTP fallback.
- [ ] **Step 2:** Update the demo script (`docs/ecosystem/demo-script.md`):
  - reset → pair over Bluetooth;
  - step 7 if go;
  - the iPhone's Auto-Lock off;
  - the Forget step if open question 3's address change didn't work;
  - what to do if Bluetooth drops (the app falls back to the LAN if Wi-Fi is set).
- [ ] **Step 3:** Check the diff for network names, home paths and tokens. Commit:
  `docs: Bluetooth link in the READMEs and the demo script`.

---

## Not verified (check on the board or at first build)

- **RAM and memory:**
  - The internal RAM gain from `CONFIG_BT_CTRL_RUN_IN_FLASH_ONLY` on the wearable. It was about 20 KB on the dock;
    W5's 67.1 KB was measured without it.
  - The internal RAM cost of a connection *with traffic* (8 KB messages, notifies). W5's 67.1 KB was an idle
    LightBlue connection.
  - The NimBLE host task's stack needs under the largest `/stop`, and whether its default fits.
- **Radio and timing:**
  - Wi-Fi and Bluetooth coexistence on the S3: whether TLS fetch times, the HTTP burst latency and the Bluetooth link
    stay stable together.
  - The ATT MTU iOS negotiates with this board (often 185), the value of `maximumWriteValueLength(.withResponse)`,
    and the transfer time of a Clark/Lake-sized `/stop`.
- **Security and pairing:**
  - That a CCCD subscribe from an unencrypted link can't receive notifies. The firmware checks `sec_state` anyway.
  - That a new static random address after a reset makes the iPhone pair afresh, with no Forget in iOS Settings.
- **iOS:**
  - Background behaviour: how reliably a heartbeat notify wakes the app on a locked phone; state restoration after a
    phone reboot on iOS 17.
- **Power:**
  - Bluetooth's current on this board. The power model's BLE terms are datasheet estimates; nothing measures current
    directly.
- **Fit with the app's data:**
  - Whether the app's train cards already use the wearable's direction labels for every station (the AB3 fixture
    test settles it).
  - Kconfig names (`BT_CTRL_*`, `BT_NIMBLE_ATT_PREFERRED_MTU`, `BT_NIMBLE_HOST_TASK_STACK_SIZE`) in IDF 5.4.1's NimBLE.
