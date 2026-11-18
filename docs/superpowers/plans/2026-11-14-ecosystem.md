# One Ecosystem Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** The iPhone app, the dock, and the wearable work as one system for the 2026-11-23 showcase: pairing from
the app, one Trackie, dock stops both ways, nearby and mirrored stops on the wrist, Send to Trackie, Follow a train,
and docking with find my handheld. The rest of `todo.md` runs alongside as its own tracks.

**Architecture:** The dock is Wi-Fi only: it joins its network from `secrets.h` and serves a small HTTP API
(ESP-IDF `esp_http_server`, announced over mDNS as `_ctatracker._tcp`). A fresh dock shows a 6-digit code; the app
finds it by Bonjour, and `POST /pair` with that code hands over the household token. The **wearable talks Bluetooth
first** (`docs/superpowers/plans/2026-11-14-wearable-ble-link.md`): the app pairs it with the passkey, hands it the
token and Wi-Fi over BLE, and relays its stops (`POST /stop` over the `link` characteristic); its HTTP API, its own
polling and its code pairing stay as the fallback. The app polls `GET /info` every 2 s while it's open. Message
parsing lives in the shared C++ library and CTAKit, both tested on the laptop; board code only wires handlers to it.

**Tech Stack:** native ESP-IDF 5.4.1 on both boards (PlatformIO `framework = espidf`, pioarduino 54.03.20),
`esp_http_server` and `espressif/mdns`, IDF's cJSON, Unity native tests; SwiftUI iOS 17, Network.framework
(`NWBrowser`), Swift Testing in CTAKit. **No Arduino whatsoever (Leo, 2026-11-14):** no Arduino core or library, no
`framework = arduino`, no Arduino build or fallback (`docs/Agent-Workflow.md`, Rules). Task steps below that name
Arduino APIs (`MDNS.begin`, ArduinoJson) were written for the Arduino build; build them with the ESP-IDF equivalents.

**Spec:** `docs/superpowers/specs/2026-11-14-ecosystem-design.md` (approved 2026-11-14). Base spec
`2026-11-03-cta-tracker-design.md`, app spec `2026-11-04-ios-app-design.md`.

> **History (superseded by the next box, the same day):** *Decision, 2026-11-14 (Leo, after Task 1): Appendix A,
> pairing over the network, no Bluetooth anywhere for the showcase.* BLE pairing works on both boards but costs about 50 KB of internal RAM, which takes both boards to about
> 0 KB free during a TLS handshake (`docs/ecosystem/day1-link.md`). Tasks 7, 9, and 12 are rewritten for `POST /pair`;
> Task 19 (the Bluetooth relay, showcase step 7) is dropped; `POST /ble` is gone. BLE setup and the relay are future
> work (spec section 12): "Bluetooth would be cool later, but for a real version I'd likely use a more powerful
> device."

> **Decision, 2026-11-14 (Leo, after the ESP-IDF port): Bluetooth first for the wearable, the dock Wi-Fi only.**
> The wearable's Bluetooth link, setup and relay are planned in `docs/superpowers/plans/2026-11-14-wearable-ble-link.md`
> (approved 2026-11-14). Task 7 stays the dock's pairing over the network (done, `9735f8e`). **Task 9** is the
> wearable's pairing over the network, kept as the **fallback** and built as that plan's Task W6d, on the same
> `cta::PairGate`. **Task 12**'s code flow stays for the dock and for the wearable when Bluetooth isn't available; the
> wearable's Bluetooth branch is that plan's Task AB2. **Task 19** is replaced by its Tasks W6b and AB3 (the relay
> over Bluetooth, showcase step 7 if its gates pass by 11/20 18:00). Task 10's `NSBluetoothAlwaysUsageDescription`
> stays, and Task 12 no longer removes it. The wearable keeps its whole Wi-Fi path working (the HTTP link, its own
> polling, its own pairing) in case Bluetooth doesn't work out.

## Global Constraints

- Showcase on one network: the lab network checked on 11/16 (Leo, 2026-11-16; this replaces "a home network" from 11/15; no hotspot), with the dock and the phone on it. There the phone reaches the dock and pairs it, but the access point refuses the wearable until its MAC is registered; development on Leo's home network. The dock needs both networks (one `secrets.h` network today), and the wearable's saved list gets the showcase network from the app.
  **Never write either network's name into a tracked file**, a test, a fixture, or a log line that gets committed.
- Internal RAM floor: 40 KB free at the worst moment, logged with `heap_caps_get_minimum_free_size(MALLOC_CAP_INTERNAL)`.
- All new board work runs off the dock's frame loop (core 1): HTTP handlers on the httpd task, hand-offs to the loop
  through a mutex-guarded inbox. The dock perf runner must not show worse frames than round 4
  (`d0769a0`: picker 34 fps, map 15.7 fps, mixed 25 fps).
- Writes need `Authorization: Bearer <token>`; the token is 32 lowercase hex characters (128 bits), compared in
  constant time, never logged, kept in NVS on the boards and in the Keychain on the phone. `GET /info` and
  `POST /pair` (which delivers the token) need no token.
- Wi-Fi networks and passwords live in the git-ignored `firmware/secrets.h`, and on the wearable also in NVS when the
  app hands them over Bluetooth. They never cross the plain network and are never logged.
- Pairing: a board with no token shows a 6-digit code (never logged) and stays open until it pairs:
  `POST /pair {"code","token"}`, code compared in constant time. The 5th wrong code in a row starts a 30 s cooldown
  (423), then the board shows a new code. `POST /reset` (or the serial and gesture resets, or "Pair again" on the
  wearable) wipes the token, so the board shows a code again. The wearable pairs over Bluetooth first (passkey);
  this code is its fallback.
- Pin and follow bodies for **trains** carry the parent **station** ID (4xxxx, the app's `Slot.fetchID`) and the
  platform's direction label as in `stops.json` (the wearable's train cards are keyed that way, base spec 6.2); for
  **buses** the stop ID and `rtdir` ("Southbound"). Dock `config.stops` keep platform IDs (3xxxx), as `cta::PickedStop`
  does today.
- Sizes from the base spec: IDs and routes `char[8]` (7 bytes), names/dirs/dests 24 bytes, pet name 16 bytes,
  at most 4 arrivals a record, a `snapshot` at most 512 bytes. Vehicle IDs (`rn`, `vid`) `char[8]`.
- Wi-Fi, per board: the **wearable** keeps up to 5 networks (`kWifiSlots = 5`) in NVS, sent from the app over
  Bluetooth, with the `secrets.h` list as the fallback, joined strongest first (`strongestSaved`); slot 0 is home
  (docked detection). The **dock** joins one network from `secrets.h` (several networks on the dock is F1, not
  built), so a network change means editing `secrets.h` and reflashing.
- mDNS names `cta-dock-xxxx` / `cta-pet-xxxx` (`xxxx` = last 2 MAC bytes in hex), service
  `_ctatracker._tcp` port 80, TXT `type=dock|handheld`, `id=xxxx`.
- Commits: named paths only (never `git commit -a`), no attribution lines, no home paths or network names in docs.

## Review Focus

1. **A board reboots mid-demo.** The app sees `/info` come back with `uptime` lower than before and re-sends pet,
   settings, nearby, pin, and follow. Test: `DeviceWatchTests.rebootResendsState` (Task 4).
2. **A write arrives while the board is mid-HTTPS-fetch.** The handler must answer at once from its own task and hand
   the body off; the network task applies it later. Test: on-device, `tools/link_send.py --burst` sends 20 pins in
   2 s during a station poll and every one answers 200 within 500 ms (Task 8).
3. **Malformed or oversized bodies.** Bodies over 1 KB answer 413 without allocating; bad JSON answers 400 and leaves
   state alone. Test: `test_link` cases `rejects_oversize`, `bad_json_leaves_state` (Task 2), plus an on-device
   `link_send.py --garbage` run (Tasks 6, 8).
4. **The followed vehicle drops out of the feed, or the feed fails.** A failed poll is not "lost": `followView` only
   says Lost when a fresh record (newer `ts`) lacks the vehicle. Test: `follow_failed_poll_keeps_waiting` (Task 3).
5. **The map pans fast in the app.** At most one `nearby` per 10 s, only when the stop IDs changed, sent after the
   list settles. Test: `NearbyThrottleTests.panBurstSendsOnce` (Task 5).

---

## Parallel tracks and the order to run them

**Who runs it (Leo, 2026-11-14):** one **orchestrator** agent runs the whole plan. It spawns a worker for each task
and fresh reviewers to check it (`docs/Agent-Workflow.md`, `docs/reviewer.md`: at most 3 rounds, early stop on small
findings), and it decides which branches get spawned and when, following the tracks and the day-by-day order below.
It also hands out the boards, so two workers never flash the same board at once. **Every session runs with ponytail
set to full** (the `ponytail:ponytail` skill, level full: the smallest change that fully solves the task): the
orchestrator, each worker, and each reviewer, so each one invokes it before starting its work.

Work runs as **tracks**: each track's workers run on their own branch and worktree and own a set of files no other
track writes. A track's tasks run in order; tracks run at the same time. Hardware is the other limit: there is **one dock**
(Tracks D and B2 share it) and **one wearable** (Tracks S, H, H-UI, and B1 share it; Leo, 2026-11-14). Whoever
doesn't hold a board works on the laptop: `tools/preview.cpp` renders, the native tests, the mock build's logic,
profile logs captured earlier. The orchestrator books board time in blocks (a morning, an afternoon), and the
wearable goes to the critical path first: S, then H, then H-UI and B1 in the gaps.

| Track | Owns (files) | Tasks | Waits on |
|---|---|---|---|
| **L — shared library** | `firmware/lib/cta/src/link_msg.*`, `follow.*`, `wifi_list.*`, `cta_types.h`, `train_parse.*`, `train_cards.*`, `bus_parse.*`, `stop_record.*`, `firmware/lib/pet/src/pet_palette.*`, `firmware/native/test/test_link*`, `tools/link_send.py` | 2, 3 | nothing |
| **K — CTAKit** | `ios/CTAKit/**` | 4, 5 | nothing |
| **S — link spike** | a throwaway `spike/link/` PlatformIO project (local only) | 1 (done) | nothing |
| **D — dock link** | `firmware/dock/src/link.*`, `store.*`, `net.cpp` (Wi-Fi join only), `main.cpp`, setup messages in `dock_ui.cpp`; new in Task 7: `firmware/lib/cta/src/pair.*`, `firmware/native/test/test_pair` | 6, 7 | S (decision), L (Task 2) |
| **H — wearable link** | `firmware/handheld/mock/src/link.*`, `live.cpp`, `live_store.*`, `main.cpp`, `power.*` | 8, 9 (9 built as W6d); the Bluetooth link is the BLE plan | S, L (Tasks 2, 3) |
| **A — app** | `ios/CTATracker/Devices/**`, `Settings/**`, `App/AppModel.swift`, `Stop/StopDetail.swift`, project build settings | 10–17 (12 built as AB2); left: 11, 13, 14, 17 | K (Task 4), then D/H for on-device checks |
| **H-UI — wearable screens** | `firmware/handheld/mock/src/ui.*`, `docs/handheld-ui.md` | 18, then F2, F3, F5 | L (Task 3) for 18 |
| **F4 — font** | `firmware/lib/gfx/src/fonts.*`, `firmware/lib/gfx/tools/font_build.py`, `ios/CTATracker/Design/Theme.swift` | its own design + plan | nothing |
| **F6 — app map vehicles** | `ios/CTATracker/Map/**` except `StopIcons.swift`, `Data/` vehicle polling | its own plan | K (Task 4: `Prediction.vehicle`) |
| **F7 — pixel icons** | `ios/CTATracker/Map/StopIcons.swift` | its own plan | nothing |
| **B1 — wearable scroll tearing** | the handheld's display send path (`main.cpp` frame send, display driver setup) | investigation | coordinate `main.cpp` edits with H |
| **B2 — dock frame rate** | `firmware/dock/src/bands.*`, `display.*`, `dock_ui.cpp` drawing | investigation + rounds | shares the dock board with D |
| **X — finish** | `docs/superpowers/specs/2026-11-03-cta-tracker-design.md` §8.3/9.5/10, READMEs, `docs/ecosystem/demo-script.md` | 20, 21, BLE plan X2 | everything |

**F2, F3, F4, F6, F7, B1, B2 are not planned task-by-task here.** Each starts in its own session with a short design
(F3 and F4 say so in `todo.md`), then its own plan. The hooks they share with this plan are in spec section 6.1.

### Order, day by day

Approved by Leo, 2026-11-15. Showcase 2026-11-23, dry run 11/22. **Day 1 (11/14–15) is done:** Tasks 1–10, 15, 16,
18 and the wearable's Bluetooth link (setup, relay, push on fetch) are on `main`; Task 12 is built as AB2.

| Day | Work |
|---|---|
| Mon 11/16 | The biggest risk first, the phone reaching the dock: on the home network (Leo, 2026-11-15: the dock and the phone stay on the home network; no hotspot). Day 1 found the phone's connections never reached either board there (Task 1). Retry from the app (Bonjour finds the dock, `GET /info`, iOS's Local Network permission); if it's still blocked, find the router setting (client isolation, the 2.4/5 GHz bands kept apart, mDNS). Book a visit to the showcase network (a different home network) before 11/19 to run the same check there. Then pair the dock from the app by code, test the wearable's HTTP fallback after `b` and app code pairing with the phone's Bluetooth off. Start Task 11 (devices list and device page). |
| Tue 11/17 | Task 14 (dock stops from the app, both ways; done 11/16). Task 22 (dock touch extras) on the dock, alongside Task 13 (one Trackie: presets, resend on `.resendState`, uncomment the wearable's `ui::petPalette565 = petPalette`). |
| Wed 11/18 | Task 17 (find my handheld) and the step 8 docking check. Draft the demo script (Task 21 and BLE plan X2 Step 2). |

> **2026-11-17 (Leo): Trackie runs on Bluetooth only at the showcase.** No MAC registration and no wearable Wi-Fi tests;
> the phone relays its cards (step 7 is the normal mode on the day). Docking counts "no Wi-Fi joined" as home (Task 26).
> Also done 11/17: the iPhone app polish (Task 27) and the demo script draft (`docs/ecosystem/demo-script.md`).
| Thu 11/19 | The dock joins the stronger of the home and showcase networks from `secrets.h` (small; or rebuild it for the showcase network). First full run of showcase steps 1–8, on the showcase network if Leo can be there, else at home; fix list. |
| Fri 11/20 | Step 7 go/no-go at 18:00 (no-Wi-Fi wearable boot with relayed cards, serial `W` demo). Fixes. B2 dock map fps if time. |
| Sat 11/21 | Task 20 extras only if the full run passed twice. Final READMEs. Feature freeze 18:00. |
| Sun 11/22 | Dry run on the showcase network, twice, from `docs/ecosystem/demo-script.md`. Fix only what breaks. |
| Mon 11/23 | Showcase. |

Merge order onto `main`: each track rebases on `main` before merging. Finished, reviewed work is committed, pushed
and merged to `main` without asking (Leo, 2026-11-14).

---

### Task 1: Prove the link on both boards (Track S)

**Files:**
- Create: `spike/link/platformio.ini` (envs `dock` and `handheld`, copied from each board's pins and core; local only)
- Create: `spike/link/src/main.cpp` (local only)
- Create: `docs/ecosystem/day1-link.md` (results; no network names)

**Interfaces:**
- Produces: a decision recorded in `docs/ecosystem/day1-link.md`: **BLE** (Tasks 7, 9, 12 as written) or
  **LAN pairing fallback** (Appendix A), and which BLE library. *Result:* Appendix A (Leo, 2026-11-14); NimBLE-Arduino
  2.5.2 paired fine, but BLE costs about 50 KB of internal RAM.

- [x] **Step 1:** Write `main.cpp`: join Wi-Fi from `secrets.h`; start BLE with the base spec service UUID
  (`ble_ids.h`), one readable `info` and one encrypted-write characteristic, passkey display pairing (print the 6
  digits to serial); start `esp_http_server` with `GET /info` returning `{"ok":true}`; `MDNS.begin("cta-spike-xxxx")`
  and `MDNS.addService("ctatracker","tcp",80)`; every 30 s run one HTTPS GET to the train API with the existing
  `Tls` setup; every 5 s print `heap_caps_get_minimum_free_size(MALLOC_CAP_INTERNAL)` and the current free internal.
  Serial `b` toggles BLE off (`deinit(true)`) and on.
- [x] **Step 2:** Flash to the dock and a wearable. Pair from the iPhone with LightBlue (or nRF Connect): the
  passkey prompt appears and an encrypted write lands.
- [x] **Step 3:** With BLE on and a TLS request every 30 s for 10 minutes, record the lowest free internal RAM per
  board. Then BLE off: record that it returns to within 2 KB of the no-BLE figure.
  Pass: ≥ 40 KB on both with BLE on. If NimBLE-Arduino fails to build or pair on core 3.2, try the core's BLE.
  *Result:* the spike passes, but the ~50 KB BLE cost would leave the real firmware at about 0 KB.
- [x] **Step 4:** On the home network: `dns-sd -B _ctatracker._tcp` on the Mac lists both boards; the iPhone reaches
  `http://<ip>/info` in Safari; the dock fetches the wearable's `/info` (serial `n <ip>`). Repeat on the campus
  network when on campus; record pass/fail only.
  *Result:* at home the Mac and both boards reach each other, but **the iPhone check failed**: its TCP never reached
  either board (likely the router keeping the phone and the 2.4 GHz boards apart). The showcase runs on a home network
  Leo has access to (Leo, 2026-11-15; no campus, no hotspot): 11/16 makes the phone reach the dock there.
- [x] **Step 5:** Write `docs/ecosystem/day1-link.md` (RAM numbers, library, mDNS and reachability per network as
  "home" / "campus", the decision). Commit the doc only: `spike/link` stays local.

### Task 2: Link messages in the shared library (Track L)

**Files:**
- Create: `firmware/lib/cta/src/link_msg.h`, `firmware/lib/cta/src/link_msg.cpp`
- Create: `firmware/lib/cta/src/wifi_list.h`, `firmware/lib/cta/src/wifi_list.cpp`
- Create: `firmware/lib/pet/src/pet_palette.h`, `firmware/lib/pet/src/pet_palette.cpp`
- Create: `firmware/native/test/test_link/test_main.cpp`
- Create: `tools/link_send.py`, `tools/test_link_send.py`

**Interfaces:**
- Consumes: `cta::PickedStop`, `cta::decodePicks`, `cta::encodePicks` (`picks.h`); `pet::rgb565` (`pet_render.h`).
- Produces (all in `namespace cta` unless noted):

```cpp
constexpr int kWifiSlots = 5;
constexpr int kSsidLen = 32, kPassLen = 64, kTokenLen = 32, kPetNameLen = 16, kVehicleLen = 7;
constexpr size_t kMaxBody = 1024;

struct WifiMsg { uint8_t slot; char ssid[kSsidLen + 1]; char pass[kPassLen + 1]; };
bool parseWifi(const char* json, size_t len, uint8_t maxSlots, WifiMsg& out);   // {"slot","ssid","pass"}
bool parseToken(const char* json, size_t len, char out[kTokenLen + 1]);         // {"token":"<32 lowercase hex>"}
bool tokenMatches(const char* authHeader, const char* token);                    // "Bearer <hex>", constant time

bool parseStop(const char* json, size_t len, PickedStop& out);   // {type, route, dir, id, name}
struct FollowMsg { PickedStop stop; char vehicle[kVehicleLen + 1]; char dest[kNameLen + 1]; };
bool parseFollow(const char* json, size_t len, FollowMsg& out);  // stop fields + "vehicle", "dest"

struct PetConfig { char name[kPetNameLen + 1]; uint32_t colors[2]; };           // palette slots 2 and 3
struct DeviceSettings { uint8_t screen; uint8_t led; uint16_t nightStart; uint16_t nightEnd; };  // %, %, min, min
struct ConfigMsg {
    bool hasStops = false, hasPet = false, hasSettings = false;
    PickedStop stops[kDockStops]; PetConfig pet; DeviceSettings settings;
};
bool parseConfig(const char* json, size_t len, ConfigMsg& out);     // any subset; "night_start", "night_end"
size_t encodeConfig(const ConfigMsg& in, char* buf, size_t cap);    // only the has* parts

struct InfoMsg {
    bool dock; char id[5]; char fw[16]; int8_t battery; bool charging;   // battery -1 = none
    bool wifiConnected; char ssid[kSsidLen + 1]; uint32_t lastUpdate;
    uint32_t uptime; uint32_t picksVer; uint32_t wake; bool docked;
};
size_t encodeInfo(const InfoMsg& in, char* buf, size_t cap);
// {"type":"dock"|"handheld","id","fw","battery","charging","wifi":{"connected","ssid"},
//  "last_update","uptime","picks_ver","wake","docked"}

struct SavedNet { char ssid[kSsidLen + 1]; char pass[kPassLen + 1]; };
struct WifiList { SavedNet nets[kWifiSlots]; };   // empty ssid = free slot
struct SeenNet { const char* ssid; int8_t rssi; };
int strongestSaved(const WifiList& list, const SeenNet* seen, int nSeen, uint8_t skipMask);   // slot or -1

namespace pet { void applyColors(uint16_t palette565[], const uint32_t colors[2]); }   // slots 2, 3 only
```

`uptime` is new beyond the spec's table: seconds since boot, so the app can tell a reboot (Review Focus 1).

**As built (worker notes):** signatures as above; `kWifiSlots` and the length constants live in `link_msg.h`
(`wifi_list.h` includes it). Value rules the boards and the app can rely on:
- `parseStop`/`parseFollow`: `type` train or bus; `route` 1–7 bytes and `id` 1–7 bytes, **rejected** if longer (a
  cut ID would poll the wrong stop); `dir` and `name` must be strings and are cut to fit (`dir` 39 bytes, `name`
  24); `vehicle` 1–7 bytes, rejected if longer; `dest` required, cut to 24. IDs aren't format-checked here (pin
  bodies carry 4xxxx stations, dock stops 3xxxx platforms); the board decides.
- `parseWifi`: `slot` an integer below `maxSlots`, `ssid` 1–32 bytes, `pass` 0–64 bytes (empty for an open network).
- `parseConfig`: a key that is present but bad fails the whole body. `pet` needs `name` (cut to 16) and exactly 2
  `colors` in 0–0xFFFFFF; `settings` needs all four keys, `screen`/`led` 0–100, `night_*` 0–1439; `stops` goes
  through `decodePicks` (exactly 3 entries). `{}` parses with no `has*` set.
- `encodeInfo`: `battery` -1 is sent as `null`.
- `tokenMatches` is false when the saved token isn't a valid 32-hex token (an unset token never matches).
- `link_send.py`: `pet <json>` posts `{"pet": <json>}` to `/config`; `config` with no json is `GET /config`;
  `--serve` also takes `--port` (default 80) and advertises as `cta-dock-f4ce` / `cta-pet-f4ce`. The fake checks in
  the boards' order: size (413, before reading), token (401), JSON (400), and answers 400 to any body the
  boards' parsers would reject (the rules above, and `nearby_msg.h`'s for `/nearby`), leaving its state alone; an
  empty body is 400 except on `/ble` and `/reset`. `--garbage` posts to `/config`, which
  both boards serve, with `{}` as the well-formed body for the token cases.
- *After the Appendix A decision:* `parseWifi`/`WifiMsg` have no caller in the showcase (Wi-Fi comes from
  `secrets.h`); they stay for the BLE future work. `link_send.py ble` and the fake's `/ble` are replaced by `pair`
  in Task 7.

- [x] **Step 1: Write the failing tests** in `test_link/test_main.cpp`:

```cpp
void wifi_accepts_slot_4_rejects_5()   { /* parseWifi(...,5,..) true for slot 4, false for slot 5 */ }
void wifi_rejects_empty_or_long_ssid() { /* "" false; 33-byte ssid false; 32-byte true */ }
void token_must_be_32_lower_hex()      { /* 31 chars false, "ABC…" false, 32 hex true */ }
void token_matches_bearer_only()       { /* "Bearer <t>" true; "<t>" false; "Bearer <other>" false */ }
void stop_parses_config_stop_shape()   {
  // {"type":"train","route":"Red","dir":"95th/Dan Ryan","id":"41320","name":"Belmont"} →
  // type Train, route "Red", id "41320"
}
void follow_needs_vehicle()            { /* same stop + "vehicle":"812","dest":"95th/Dan Ryan" true; no vehicle false */ }
void config_subset_sets_only_has()     { /* {"pet":{"name":"Trackie","colors":[2976742,8238335]}} → hasPet only */ }
void config_settings_keys()            { /* night_start 60, night_end 300, screen 80, led 40 */ }
void config_round_trips()              { /* encodeConfig(parseConfig(x)) parses to the same ConfigMsg */ }
void bad_json_leaves_state()           { /* out pre-filled, parse "{oops" false, out unchanged */ }
void rejects_oversize()                { /* len kMaxBody + 1 → false for every parser */ }
void info_has_new_fields()             { /* encodeInfo → contains "picks_ver":3,"wake":7,"docked":true,"uptime":42 */ }
void strongest_picks_highest_rssi()    { /* saved A,B; seen A -70, B -55 → slot of B */ }
void strongest_skips_masked_slot()     { /* skipMask has B → slot of A */ }
void strongest_none_seen()             { /* → -1 */ }
void palette_changes_slots_2_3_only()  { /* applyColors → [2]==rgb565(c0), [3]==rgb565(c1), others unchanged */ }
```

- [x] **Step 2: Run** `cd firmware/native && ~/.platformio/penv/bin/pio test -e native -f test_link`.
  Expected: FAIL (undefined references).
- [x] **Step 3: Implement** the declarations above. Parsers use ArduinoJson (cJSON since the ESP-IDF port) with the same "fill a temporary, copy
  on success" pattern as `parseNearby`; `parseConfig` reuses `decodePicks` for `stops`.
- [x] **Step 4: Run** the same command plus the whole suite (`pio test -e native`). Expected: all PASS.
- [x] **Step 5: Write `tools/link_send.py`**: `link_send.py <host> info|config|pet|nearby|pin|follow|unpin|unfollow|ble|reset
  [json] --token <hex>`, `--burst` (20 pins in 2 s, prints each status and time), `--garbage` (bad JSON, 2 KB body,
  no token, wrong token: expects 400, 413, 401, 401), and `--serve [--type dock|handheld]` (a fake board on the Mac
  answering the same endpoints from memory and advertising `_ctatracker._tcp` with `dns-sd -R`, for Track A).
  The token comes from `--token` or `LINK_TOKEN`, never printed. `tools/test_link_send.py` checks the fake server's
  answers to each `--garbage` case.
- [x] **Step 6: Commit** the files above: `feat(lib): link messages, Wi-Fi list, pet palette swap, link_send tool`. *(8c5ee0d)*

### Task 3: Vehicle IDs on arrivals and the follow rules (Track L)

**Files:**
- Modify: `firmware/lib/cta/src/cta_types.h` (`Arrival`), `train_parse.cpp`, `train_cards.cpp`, `bus_parse.cpp`,
  `stop_record.cpp`, `cta_config.h` (the follow values), `link_msg.h` (`kVehicleLen` moves out)
- Create: `firmware/lib/cta/src/follow.h`, `firmware/lib/cta/src/follow.cpp`
- Test: `firmware/native/test/test_link_follow/test_main.cpp`; update `test_train`, `test_bus`, `test_stop_record`

**Interfaces:**
- Consumes: `FollowMsg` (Task 2).
- Produces:

```cpp
// cta_types.h
struct Arrival { int16_t min; char dest[kNameLen + 1]; bool live; bool delayed; char vehicle[kVehicleLen + 1]; };
// follow.h
enum class FollowState : uint8_t { Waiting, Due, Arrived, Lost };
struct FollowMemo { bool seen = false; int16_t lastMin = 0; uint32_t lastTs = 0; uint32_t endedAt = 0; };
struct FollowView { FollowState state; int16_t min; };
// recs: the wearable's cards; finds the card with f.stop's id/route/dir, then the arrival with f.vehicle.
FollowView followView(const StopRecord* recs, int n, const FollowMsg& f, FollowMemo& memo, uint32_t now);
bool followExpired(const FollowMemo& memo, uint32_t now);   // Arrived or Lost for 60 s
```

**As built (worker notes):**
- `kVehicleLen` moved from `link_msg.h` to `cta_types.h`. `parseTrainArrivals`, `parseTrainCards` (the wearable's
  train cards, so `train_cards.*` joined Track L), and `parseBusPredictions` set `Arrival::vehicle` to `rn`/`vid`,
  `""` when missing.
- **512 bytes:** every field at its limit plus four 7-byte vehicles is 523 bytes, so it can't fit. `encodeStopRecord`
  writes `"veh"` (after `delayed`, only when non-empty) and, if the record then doesn't fit in `cap`, writes it again
  without vehicles. A real busy card (5-byte station, 4-byte line, 24-byte names and dests, 2-digit minutes, 4-digit
  vehicles: CTA run numbers are 3 digits, bus vehicle IDs 4) fits with them; `record_with_vehicles_fits_512` checks
  both. CTAKit's snapshot encoder needs the same rule for the relay (Task 4/19; the relay is now future work).
- `followView` matches the card by type, id, route, and `dir` (the follow's `dir` cut to 24 bytes, as cards keep
  it). Only a record with a newer `ts` counts, so a failed poll never ends a follow. On the card: Due at 0 min, else
  Waiting. Seen before and missing from a fresh record: `expected = lastMin - (ts - seenTs)/60`; Arrived if
  `expected <= 1`, Waiting if the card is full (4 arrivals) and `expected` is at or past its last arrival (it slid
  off), else Lost. A record whose arrivals carry no vehicle IDs at all changes nothing. A vehicle never on the card
  is Waiting with `min` -1. The parsers make no empty cards, so when the follow's card is missing but another card
  at the same stop (type and id) has a newer `ts`, that poll is judged as a fresh record with no arrivals (`ts` = the
  stop's newest).
- Every follow ends (spec 5.7): a seen vehicle is Arrived once `now > seenTs + (lastMin + kFollowGraceMin)*60`,
  card or no card; one never seen is Lost after `kFollowUnseenCapMin` minutes of fresh records (`ts`, not `now`),
  counted from the later of the first fresh record's `ts` and `now` then, so an old card can't start it early.
  Arrived and Lost are final; `followExpired` after `kFollowEndedSecs`. The three values are in `cta_config.h`
  (60 s, 2 min, 3 min). `FollowMemo` grew `state`, `seenTs`, `firstTs`; `endedAt` is no longer a sentinel, so a
  clock at 0 still ends. A never-seen follow whose stop gets no fresh poll at all still waits.
  `FollowMsg::dest` isn't used for matching.

- [x] **Step 1: Write the failing tests:** `train_keeps_rn` (a fixture arrival's `vehicle` equals its `rn`),
  `bus_keeps_vid`, `record_with_vehicles_fits_512` (4 arrivals, 24-byte names and dests, 7-byte vehicles: encoded
  ≤ 512), and in `test_link_follow`: `follow_waiting_4_min`, `follow_due_at_0`,
  `follow_gone_after_min_1_is_arrived`, `follow_gone_at_min_9_is_lost` (only once a record with a newer `ts` lacks
  it), `follow_failed_poll_keeps_waiting` (same `ts` → still Waiting with the last minutes),
  `follow_expires_after_60_s`.
- [x] **Step 2: Run** `pio test -e native -f test_link_follow -f test_train -f test_bus -f test_stop_record`.
  Expected: FAIL.
- [x] **Step 3: Implement.** Keep `rn` (`ttarrivals`) and `vid` (`getpredictions`) in the parse filters;
  `stop_record` encodes a vehicle as `"veh"` only when non-empty.
- [x] **Step 4: Run** the full native suite. Expected: PASS.
- [x] **Step 5: Commit:** `feat(lib): vehicle IDs on arrivals, follow rules`. *(90d91a9)*

### Task 4: CTAKit messages, vehicle on predictions, device watch (Track K)

**Files:**
- Create: `ios/CTAKit/Sources/CTAKit/LinkMessages.swift`, `ios/CTAKit/Sources/CTAKit/DeviceWatch.swift`
- Modify: `ios/CTAKit/Sources/CTAKit/Models.swift` (`Prediction`), `TrainParse.swift`, `BusParse.swift`
- Test: `ios/CTAKit/Tests/CTAKitTests/LinkMessageTests.swift`, `DeviceWatchTests.swift`; update parse tests

**Interfaces:**
- Consumes: `ConfigMessage`, `ConfigStop`, `PetConfig`, `DeviceSettings`, `NearbyMessage` (`BLEMessages.swift`).
- Produces:

```swift
// Models.swift
public struct Prediction { … public let vehicle: String? }   // rn or vid; init gains `vehicle: String? = nil`
// LinkMessages.swift
public struct InfoMessage: Decodable, Equatable, Sendable {
    public let type: DeviceKind; public let id: String; public let fw: String
    public let battery: Int?; public let charging: Bool; public let wifi: WifiState
    public let lastUpdate: Int; public let uptime: Int; public let picksVer: Int; public let wake: Int; public let docked: Bool
}
public enum DeviceKind: String, Codable, Sendable { case dock, handheld }
public struct WifiState: Codable, Equatable, Sendable { public let connected: Bool; public let ssid: String }
public struct FollowMessage: Encodable, Sendable { public let stop: ConfigStop; public let vehicle: String; public let dest: String }
    // encodes flat: {type, route, dir, id, name, vehicle, dest}
public struct TokenMessage: Encodable, Sendable { … }   // {"token": hex}; description redacts it
public enum Token { public static func random() -> String }   // 32 lowercase hex from SecRandomCopyBytes
// DeviceWatch.swift — pure: what the app should do after each poll of one device
public struct DeviceWatch: Sendable {
    public enum Action: Equatable, Sendable { case fetchConfig, sendNearby, resendState, wentOffline, cameOnline }
    public mutating func polled(_ info: InfoMessage?) -> [Action]   // nil = the request failed
}
// `battery` is `null` on the wire when there is no battery (the dock), so it decodes as nil.
// As built: first poll, or the first after going offline → [.cameOnline, .resendState], plus .fetchConfig for a dock
// (a pick made meanwhile). A reboot (uptime lower)
// → .resendState and skips the wake check (the resend carries nearby). picks_ver or wake *changing* (not only
// going up) → .fetchConfig / .sendNearby, so counters that restart at boot still trigger. Misses only count once
// online; the third in a row → .wentOffline once. FollowMessage cuts vehicle to 7 bytes and dest to 24 like ConfigStop.
```

- [x] **Step 1: Write the failing tests:** `trainPredictionKeepsRn`, `busPredictionKeepsVid` (from the shared
  fixtures), `infoDecodesSnakeCase`, `followEncodesFlat`, `tokenIs32LowerHex`, `tokenNeverInDescription`, and in
  `DeviceWatchTests`: `picksVerUpFetchesConfig`, `wakeUpSendsNearby`, `firstPollIsCameOnlinePlusResend`,
  `rebootResendsState` (uptime drops → `.resendState`), `threeMissesGoOffline` (third nil → `.wentOffline`, once).
- [x] **Step 2: Run** `cd ios/CTAKit && swift test`. Expected: FAIL.
- [x] **Step 3: Implement** the declarations.
- [x] **Step 4: Run** `swift test`. Expected: PASS.
- [x] **Step 5: Commit:** `feat(ctakit): link messages, vehicle on predictions, device watch`. *(b815a33)*

### Task 5: The nearby builder and its throttle (Track K)

**Files:**
- Create: `ios/CTAKit/Sources/CTAKit/NearbyBuilder.swift`
- Modify: `ios/CTAKit/Sources/CTAKit/Config.swift` (`nearbyPerTab` 10 → 5, the firmware's `kNearbyPerTab`)
- Test: `ios/CTAKit/Tests/CTAKitTests/NearbyBuilderTests.swift`, `NearbyThrottleTests.swift`

**Interfaces:**
- Consumes: `StopIndex.byDistance(from:kinds:)`, `SlotList`, `Slot`, `NearbyMessage`.
- Produces:

```swift
public enum NearbyBuilder {   // caps from Config.nearbyPerTab (5) and Config.hypedClosestStops (3)
    // From a location: what tools/send_nearby.py builds (5 stations; each route+direction's nearest bus stop,
    // then the 5 nearest of those; close = the 3 nearest of either kind).
    public static func fromLocation(_ c: CLLocationCoordinate2D, index: StopIndex) -> NearbyMessage
    // Mirror: the stops behind the phone's list (favorites, then nearby), in list order; stations by Slot.fetchID,
    // each stop once (a station has a card per line and direction). As built: close = the 3 nearest listed stops
    // by Slot.meters, ties in list order, as the phone's Trackie counts them (a far favorite can't make it Hyped).
    public static func fromList(_ slots: SlotList, center: CLLocationCoordinate2D) -> NearbyMessage
}
public struct NearbyThrottle: Sendable {
    public static let minInterval: TimeInterval = 10
    public mutating func offer(_ m: NearbyMessage, now: Date) -> NearbyMessage?   // nil = don't send yet
    public mutating func flush(now: Date) -> NearbyMessage?                     // a held message once due
}
```

- [x] **Step 1: Write the failing tests:** `fromLocationMatchesSendNearby` (Belmont at 41.9399, -87.6533: same IDs
  as `tools/send_nearby.py --dry-run --lat 41.9399 --lon -87.6533`, recorded into the test),
  `fromListKeepsListOrder`, `fromListUsesStationIDs`, `fromListCapsAt5Each`, `closeIsFirst3OfEither`,
  `panBurstSendsOnce` (10 offers in 3 s with changing stops → 1 sent, then `flush` after 10 s sends the last),
  `sameStopsNotResent`; also `fromLocationTakesEachRouteDirectionsNearestBusStop`, `closeIsNearestNotAFarFavorite`,
  `emptyListIsStillValid`, `dueOfferSendsAtOnce`, `closeOnlyChangeIsResent`, `clockSetBackIsDue`, and every builder test checks the message against `parseNearby`'s rules.
- [x] **Step 2: Run** `swift test`. Expected: FAIL.
- [x] **Step 3: Implement.**
- [x] **Step 4: Run** `swift test`. Expected: PASS.
- [x] **Step 5: Commit:** `feat(ctakit): nearby builder for location and mirror, 10 s throttle`. *(9ad3474)*

### Task 6: The dock on the network (Track D)

**Files:**
- Create: `firmware/dock/src/link.h`, `firmware/dock/src/link.cpp`
- Modify: `firmware/dock/src/store.h/.cpp` (token, pet, settings), `net.cpp` (as built: one network; Task 7 makes it join the strongest compiled
  into `secrets.h`, with `strongestSaved`), `main.cpp` (apply inbox each frame; `picks_ver`),
  `firmware/dock/platformio.ini` (`build_src_filter` unchanged; no new lib for httpd/mDNS)

**Interfaces:**
- Consumes: Task 2 (`parseConfig`, `encodeConfig`, `encodeInfo`, `tokenMatches`, `pet::applyColors`).
- Produces:

```cpp
namespace link {
struct Inbox { bool config = false; cta::ConfigMsg cfg; bool reset = false; };
void start(const char* hostLabel);        // httpd on port 80 + mDNS; call once Wi-Fi is up
void setInfo(const cta::InfoMsg& info);   // the loop publishes; GET /info serves the latest copy
void setConfig(const cta::ConfigMsg& c);  // what GET /config returns
bool take(Inbox& out);                    // the loop drains writes; false if none
}
namespace store {
bool loadToken(char out[cta::kTokenLen + 1]); bool saveToken(const char* t);
bool loadPet(cta::PetConfig& out); bool savePet(const cta::PetConfig& p);
bool loadSettings(cta::DeviceSettings& out); bool saveSettings(const cta::DeviceSettings& s);
void wipeLink();   // the token; pet and settings stay (Wi-Fi is in secrets.h)
}
```

**As built (worker notes):**
- The namespace is **`lan`**, not `link`: newlib's `unistd.h` declares POSIX `int link(const char*, const char*)`, and a
  namespace can't share its name (the build fails). Task 8 hits the same clash on the wearable.
- Two calls beyond the interface above: `lan::begin(token)` in `setup()` (the mutex, the shared state in PSRAM, the
  token from `store::loadToken`) and `lan::setToken()` (serial `T`, later BLE). `lan::stackLeft()` reports the httpd
  task's least free stack; the 30 s `stats:` line prints it.
- `lan::start` is called by the network task (core 0) the first time Wi-Fi comes up (with an IP: before that,
  `socket()` asserts in lwIP), not by the loop; later reconnects do nothing. httpd: core 0, 8 KB stack **in PSRAM**
  (`task_caps`; no handler touches flash or NVS, which a task with an external stack must not), 3 sockets,
  5 handlers. Boot logs internal free after httpd and after mDNS start. Not done yet: `link_msg.*`'s
  `JsonDocument`s still allocate from the default (internal) heap during a request; moving them to
  `cta::json::bigAllocator()` waits for the board's RAM measurement to show it's needed.
  A body is required only on `/config`; any body on `/ble` or `/reset` must parse (`parseConfig`, `{}` is fine).
  `net.cpp`'s disconnect handler is on ESP-IDF's default event loop (`esp_event_handler_register`), as the
  handheld's: core 3.2.0's `WiFi.onEvent()` list isn't locked and crashed the spike at boot.
  The touch task also queues a report when the finger count changes (`display.cpp`), so lifting to one still finger
  restarts the three-finger hold. Checks run size (413 from
  `Content-Length`, before reading), token (401), then JSON (400), as `link_send.py --serve` does. `GET /info` fills
  `uptime` and the Wi-Fi state on the httpd task. A config that arrives before the loop drains is merged part by part.
- `screen` brightness: the backlight is on/off only (CH422G EXIO2), so `screen` 0 turns it off and anything else on.
  `led` scales each row's color before `leds::post` (under `LED_MAX_BRIGHTNESS`). Night hours replace
  `kNightStartMin`/`kNightEndMin` (also for slow polling). Saves skip unchanged values (flash writes upset the panel).
- Pet recolor: `ui::drawPet` (`firmware/lib/gfx/src/card_theme.*`) draws with `ui::petPalette565`, which defaults
  to `pet::kDefaultPalette565`; the dock points it at its own copy with `pet::applyColors` applied (`main.cpp`
  `petPalette`). Board-neutral: the wearable (Task 8) sets the same pointer.
- `R` / `POST /reset` / the three-finger hold wipe Wi-Fi and the token, but while `secrets.h` has `DOCK_WIFI_SSID`
  the dock rejoins from it after the reboot (the fallback). Task 7 adds the "fresh" flag and the BLE bonds to the wipe.
- The three-finger hold: any touch report with 3+ fingers drops the current press (never a tap, hold or drag) and
  starts the 10 s; a lift or a report with fewer fingers restarts it.
- Built before the Appendix A decision with `POST /ble` and a stored Wi-Fi slot; Task 7 (9735f8e) removed them.
  Ported to ESP-IDF in 4c50fe4 (`esp_restart()`, not `ESP.restart()`).
- Board check (2026-11-14, Arduino core, before the ESP-IDF port): `info`, `config` (GET and POST),
  pet, `reset` and `--garbage` (400/413/401/401) pass from the Mac; 20 `POST /config` in 2 s all 200
  (median 38 ms, worst 1117 ms). `dock_perf_run.py` 2 of 3 runs (`docs/perf/runs/2026-11-14-task6-*`):
  picker 34.1 fps, map 15.7 fps, no late panel frames, lowest internal **35.8 KB: fails the 40 KB gate**.

- [x] **Step 1:** Implement `link.cpp`: endpoints `GET /info`, `GET /config`, `POST /config`, `POST /reset`
  (`POST /pair` comes in Task 7); bodies over `cta::kMaxBody` → 413, bad JSON → 400, missing or wrong token → 401 (no token set
  yet → 401 for every write). Handlers copy into the inbox under a mutex and answer 200 at once.
- [x] **Step 2:** Wire it: `main.cpp` drains `link::take` every frame and applies `config.stops` through
  `setPicks()` (which now bumps `picksVer`), `config.pet` through `pet::applyColors` on the dock's palette copy plus
  `savePet`, `config.settings` to backlight, `LED_MAX_BRIGHTNESS` scaling, and night hours; `reset` →
  `store::wipeLink()` then `ESP.restart()`. Serial `R` and the hidden reset gesture (three fingers held 10 s
  anywhere on screen) do the same.
- [x] **Step 3: Verify on the board** (Wi-Fi from `secrets.h`, token set with serial `T <hex>` for now):
  `python3 tools/link_send.py cta-dock-xxxx.local info` shows the fields; `config` with new stops changes the dock
  and its LED rows; a pick on the dock raises `picks_ver`; `pet` recolors Trackie; `--garbage` gives 400/413/401/401.
  *Done from the Mac on the Arduino build (above); pairing and config from the app are Tasks 12 and 14.*
- [x] **Step 4: Verify no regression:** `python3 tools/dock_perf_run.py` (3 runs): picker ≥ 34 fps, map ≥ 16 fps,
  lowest internal RAM ≥ 40 KB, no late panel frames.
  *On ESP-IDF (port plan D3): RAM 73–74 KB, picker 34.0, no late frames; the map at 15.7 fps is B2 in `todo.md`.*
- [x] **Step 5: Commit:** `feat(dock): HTTP link on the home network (info, config, reset)`. *(c66a5cf; on ESP-IDF in 4c50fe4)*

### Task 7: Dock pairing over the network (Track D)

**The dock's Wi-Fi pairing, for good (Leo, 2026-11-14: "the dock is only wifi").** The dock never has BLE; only the
wearable talks Bluetooth. Built on ESP-IDF as the port plan's D5 (`2026-11-14-esp-idf-port.md`), which has the
as-built notes; joining the strongest of several `secrets.h` networks is not done yet (F1).

Appendix A. The pairing gate is shared C++ so the wearable (Task 9) uses the same one and it's tested on the laptop.

**Files:**
- Create: `firmware/lib/cta/src/pair.h`, `firmware/lib/cta/src/pair.cpp`, `firmware/native/test/test_pair/test_main.cpp`
- Modify: `firmware/lib/cta/src/cta_config.h` (the pairing values), `link_msg.h/.cpp` (`InfoMsg.paired`, encoded as
  `"paired"`), `firmware/native/test/test_link/test_main.cpp` (`info_has_paired`)
- Modify: `firmware/dock/src/link.cpp` (`POST /pair`), `main.cpp` (code at boot), `dock_ui.cpp` (the setup messages)
- Modify: `tools/link_send.py`, `tools/test_link_send.py` (`pair <code>` replaces `ble`; the fake serves `/pair` with
  the same gate; coordinate with Track L, which owns these files)
- Clean up after Task 6, which was built before the Appendix A decision (2026-11-14): remove `POST /ble`, the BLE
  window and the stored Wi-Fi from `link.cpp`/`store.*`, and make `net.cpp` join the strongest of the `secrets.h`
  networks with `strongestSaved`

**Interfaces:**
- Consumes: `parseToken`'s token rules, `encodeInfo` (Task 2); `store::saveToken`, `loadToken` (Task 6).
- Produces:

```cpp
// cta_config.h
constexpr uint32_t kPairWindowMs = 5 * 60 * 1000;   // pairing open for 5 minutes after boot
constexpr uint8_t kPairMaxWrong = 5;                // then locked until reboot
// pair.h (namespace cta)
constexpr int kPairCodeLen = 6;
struct PairMsg { char code[kPairCodeLen + 1]; char token[kTokenLen + 1]; };
bool parsePair(const char* json, size_t len, PairMsg& out);   // {"code":"6 digits","token":"32 lowercase hex"}
enum class PairResult : uint8_t { Ok, WrongCode, Locked, Closed, AlreadyPaired };
struct PairGate {
    char code[kPairCodeLen + 1] = {};   // set once at boot (esp_random, 000000–999999), shown on screen, never logged
    uint8_t wrong = 0;                  // RAM only, so a reboot clears the lock
    PairResult check(const char* code, uint32_t nowMs, bool paired);   // nowMs since boot; constant-time compare
};
```

`check` order: `paired` → AlreadyPaired; `wrong >= kPairMaxWrong` → Locked; `nowMs > kPairWindowMs` → Closed; code
mismatch → `wrong++`, WrongCode; else Ok. HTTP: Ok 200 (save the token, then `paired` in `info` is true), bad body 400,
WrongCode 403, AlreadyPaired 409, Locked or Closed 423 (the app says to restart the board). `/pair` needs no bearer
token and takes the same 1 KB limit.

**Changed 2026-11-14 (Leo, built with Task L1 of the wearable BLE link plan):**
- There is no 5-minute window: an unpaired board stays in pairing mode until it pairs. `kPairWindowMs`, `Locked`
  and `Closed` are gone.
- The 5th wrong code in a row drops the code and starts a 30 s cooldown (`kPairCooldownMs`). During it, `check`
  returns `CoolingDown`: HTTP 423 with `{"error":"cooling down","retry_after":<seconds>}`
  (`PairGate::retryAfterSec`).
- After the cooldown, `PairGate::needsCode(now)` is true. The board picks a new code with `setCode` and redraws it.
- "Pair again" (from the board's settings page) wipes the token or bond, then calls `PairGate::reset()`. That leaves
  no code, so `needsCode` asks for a fresh one.
- `open(now)` now means "a code is set and no cooldown is running".
- Built on both boards: the screens now say "Too many tries, new code in N s", and the app says "Too many tries, a
  new code will appear on the device." The test names and steps below describe the old gate (history).

- [x] **Step 1: Write the failing tests** in `test_pair`: `right_code_pairs`, `wrong_code_counts`,
  `locks_after_5_wrong` (the 6th try, right code, is Locked), `closed_after_5_minutes`, `paired_answers_already`,
  `parse_rejects_bad_code` (5 digits, 7 digits, letters), `parse_rejects_bad_token`, `bad_json_leaves_state`,
  `rejects_oversize`; in `test_link`, `info_has_paired`.
- [x] **Step 2: Run** `pio test -e native -f test_pair` (FAIL), implement, run it and the whole suite (PASS).
- [x] **Step 3: Wire the dock:** at boot, if no token is saved, fill `PairGate.code` and show it on screen ("Pair with
  the CTA app" over the 6 digits); after 5 wrong tries, "Too many tries, new code in N s" (built; first written as a
  5-minute window with "Restart the dock to pair"). On Ok, save the
  token and go to the usual screen. `wipeLink` (reset) brings the code back on the next boot.
- [ ] **Step 4: Verify on the board:** fresh dock (`R`) shows a code; `link_send.py cta-dock-xxxx.local pair <code>`
  answers 200 and then `config` with that token is accepted; a second `pair` is 409; after a reset, 5 wrong codes
  give 403 ×5 then 423 with "Too many tries", and a new code after 30 s. Then pair the dock from the app by code
  (Task 12's flow; 11/16 on the home network).
- [x] **Step 5: Commit:** `feat(dock): pair over the network with the code on screen`. *(9735f8e)*

### Task 8: The wearable on the network (Track H)

**Files:**
- Create: `firmware/handheld/mock/src/link.h`, `firmware/handheld/mock/src/link.cpp`
- Modify: `live.h/.cpp` (pin and follow polled with the close stations; Wi-Fi list from `secrets.h` with
  `strongestSaved`; `secrets.example.h` and the handheld README show the list form), `live_store.*` (pinned/followed records), `main.cpp` (inbox, wake counter, docked, apply pet and
  settings, serial `R` and the three-finger 10 s hold reset)

**Interfaces:**
- Consumes: Task 2 (`parseConfig`, `parseStop`, `parseFollow`, `encodeInfo`, `tokenMatches`, `strongestSaved`,
  `pet::applyColors`), Task 3 (`followView`, `followExpired`), `live::setNearby`.
- Produces:

```cpp
namespace link {   // same shape as the dock's, plus:
struct Inbox { bool config; cta::ConfigMsg cfg; bool nearby; char nearbyJson[cta::kMaxBody + 1]; size_t nearbyLen;
               int pin; cta::PickedStop pinStop; int follow; cta::FollowMsg followMsg;   // +1 set, -1 clear, 0 none
               bool reset; };
}
namespace live {
void setPin(const cta::PickedStop* stop);        // nullptr clears the pin and ends the follow
void setFollow(const cta::FollowMsg* f);         // nullptr clears the follow; a follow also pins f->stop
}
// Contract: setPin(nullptr) clears the published snapshot's hasPin and hasFollow under mu and bumps its version at
// once, so the loop's next read() drops them (not on the network task's next pass: main.cpp rebuilds the model from
// the cached snapshot every second, and a swipe-unpin would come back for 1-3 s). setFollow(nullptr) does the same
// for hasFollow. A snapshot the network task was building when the clear came is cleared too before it's published.
// live::Snapshot gains: bool hasPin; Rec pin; bool hasFollow; cta::FollowView follow;
```

**As built (worker notes):**
- Namespace **`lan`** in `link.h/.cpp`, as on the dock (newlib's `link()`); `link.cpp` is wrapped in
  `HANDHELD_LIVE`, so `platformio.ini` is unchanged. Same calls as the dock: `lan::begin(token)` in `setup()`,
  `setToken`, `setInfo`, `setConfig`, `take`, `stackLeft` (printed in the 10 s stats line). `lan::start` runs from the
  network task the first time Wi-Fi is up (never before an IP; later reconnects do nothing). httpd: core 0, 8 KB stack
  in PSRAM, 3 sockets, 10 handlers. Boot logs internal free (and its minimum) after httpd and after mDNS.
- **The inbox is smaller than planned:** a pin or follow goes straight from the handler to `live::setPin` /
  `live::setFollow`, which only copy it under `live`'s mutex for the network task (RAM only, so safe on httpd's PSRAM
  stack). The inbox carries what touches flash or reboots: `config` (pet, settings; `stops` ignored), `nearby` (raw
  JSON, checked with `parseNearby` in the handler, saved and applied by the loop through `live::setNearby`), `reset`,
  `openBle`. The loop's copy of the inbox is in PSRAM (it holds a 1 KB body).
- Checks run size (413 from `Content-Length`), token (401), JSON (400). POST `/config`, `/nearby`, `/pin`, `/follow`
  need a body; any body on `DELETE /pin`, `DELETE /follow`, `/ble`, `/reset` must parse (`parseConfig`, `{}` is fine).
- `live::Store` keeps the pin and follow. The pin polls at `kHandheldClosePollSec` with no duplicate request: a
  bus pin's stop joins the batched bus poll (stpid list, at most 6 of Bus Tracker's 10); a train pin at a listed
  station makes that station poll at the close rate; only a train pin outside the list has its own poll, a 6th
  `Station` slot (one PSRAM block with the others). `followView` gets every record at the pinned stop, from its own poll and
  the nearby list's, one per line and direction (the newer `ts`); it runs on every `publish()` once the clock is set.
  `Snapshot` gains `hasPin`, `pin` (with no data yet: the pin's names, no arrivals, `ts` 0), `hasFollow`, `follow`.
  The pin's ID joins `Snapshot::close` (now `kHypedClosestStops + 1`), so its list cards hype; `mock::finish` also
  passes the pin's card to `deviceMood` as eligible when no list card is the same stop, line and direction. A pin at another stop ends the
  follow; a new pin at the same stop keeps it. Docking (`model.docked` going true) calls `live::setPin(nullptr)`.
  `live::Store` itself moved to PSRAM. `fromLive` copies `hasPin`/`pin`/`hasFollow`/`follow` into Task 18's
  `mock::Model` fields, building `pin` with the lists' card code (`trainLooks`/`busLooks`: heading, compass, pips
  from the same station's other lines) and counting the follow's minutes down with the card's. `mock_data.h`
  includes `live.h` for `setPin`/`setFollow` instead of redeclaring them (the weak stubs in `mock_data.cpp` stay).
  `tools/live_test.cpp` checks the pin, the follow, and the model's pin card.
- Wi-Fi (F1): the list is read from NVS (`trackie`/`wifi`, a `cta::WifiList` blob that Task 9's BLE setup writes);
  with none saved, `secrets.h`'s network is slot 0 (keeping `WIFI_EAP_USER`). With one saved network the join is as
  before (no scan). With several, each join attempt scans and takes `strongestSaved`, skipping slots that failed or
  just dropped (all skipped → any in range); attempts keep the old 15 s–5 min backoff. `homeWifi` is slot 0. On a
  drop, `esp_wifi_disconnect()` first: core 3.2's own reconnect try would fail the rescan (`ESP_ERR_WIFI_STATE`).
- **TLS buffers in PSRAM:** `live::start` sets `mbedtls_platform_set_calloc_free(tlsCalloc, heap_caps_free)`, as
  the dock's `net.cpp` does: blocks of 4 KB and up (the 16 KB and 4 KB record buffers) go to PSRAM. This board's
  build had all of TLS in internal RAM (about 33 KB), which with httpd, mDNS (1.9 KB static, a 4 KB task stack) and a
  request's JSON pools would have gone under the 40 KB floor during a handshake. Not done: the link's
  `JsonDocument`s still use the default (internal) heap for their small pools; move them if the board says so.
- **Reset gesture: two fingers held 10 s, not three.** The CST9217 reports at most two points, and a read with more
  comes back as none, so three fingers can't be seen. A one-finger read or 40 ms with no point restarts the hold
  (the controller's empty reads between reports are shorter). The 650 ms two-finger debug cycle still fires first;
  harmless. `R` / `POST /reset` / the hold forget `wifi` and `token` (pet and settings stay); the board then rejoins
  `secrets.h`'s network. Serial `T <32 hex>` sets the token (never echoed; the line buffer is wiped).
- Settings: `screen` scales full brightness (220) by percent; night hours go to `mock::Model::nightStart`/`nightEnd`
  (default `kNightStartMin`/`kNightEndMin`; `Model::night()` wraps a span past midnight), used for slow polling and
  by `finish()` for the mood (Sleepy, Sad) and the reason; `led` is ignored (no LEDs). Pet and settings are saved to NVS only when
  they changed.
- **Pet recolor:** the wearable draws Trackie through `ui::drawPet`, which reads `pet::kDefaultPalette565`.
  `main.cpp` keeps a palette copy with `pet::applyColors` applied and a commented `ui::petPalette565 = petPalette;`
  marked `shortcut:`; Task 13 uncomments it (11/17).

- [x] **Step 1:** Implement `link.cpp`: endpoints `GET /info`, `GET/POST /config` (pet, settings), `POST /nearby`,
  `POST/DELETE /pin`, `POST/DELETE /follow`, `POST /reset`, same error rules as the dock (`POST /pair` comes in
  Task 9). As built it also serves `POST /ble` (from the earlier BLE plan); Task 9 replaces it.
- [x] **Step 2:** Wire `live`: the pinned stop is polled at the 3 closest stations' rate even when not nearby and
  counts as close for Hyped; the follow is evaluated with `followView` after every poll of its stop and cleared by
  `followExpired`. Pass `followView` the raw polled records (`live_store`'s, not `live_model`'s counted-down
  cards), keyed by the parent station ID (4xxxx) a follow carries; when the pinned stop and the nearby list both
  hold the same card, use the one with the newer `ts`. The Wi-Fi join picks `strongestSaved` and, on a drop, rescans skipping the failed slot.
  `info.wake` goes up on every wake (`live::wake()` call site), `info.docked` mirrors `model.docked`.
- [x] **Step 3: Verify on the board:** `link_send.py cta-pet-xxxx.local nearby '<json>'` fills the cards (same as
  `send_nearby.py`); `pin` then `info`; `follow` with a live `rn` from a current arrival counts down in the serial
  log; `--burst` during a station poll: every request answers within 500 ms; `--garbage` gives 400/413/401/401.
  *Result (2026-11-14, from the Mac by IP; the screen check left for later):* `nearby` 200 and the station polls
  ran at once; `--burst` straight after it, overlapping all 5 station polls and the bus poll: 20/20 answered 200 in
  33-132 ms; `--garbage` 400/413/401/401; a bus pin outside the list gave `net: buses, 6 stops` and no pinned
  request; a train pin plus a follow (live `rn`) went due, arrived, then cleared. Lowest internal free over the run
  73.2 KB (floor 40 KB). The weather request failed once at boot (`startTLS: -1`), then worked.
- [x] **Step 4: Verify power:** `tools/power_run.py` dim, app closed: within 5% of `docs/power/summary.md`.
  (No more power runs unless Leo asks, 2026-11-14.)
  *Result:* one A/B run at the same hour (A = cc4f38f, B = this task; the second A was cut short). Dim idle 39.4 →
  40.2 mA (+2.2%), awake still +7.0%, scroll +3.0%, churn +5.2%, burst −4.6%, everything −0.6%; 48.4 → 47.1 h.
  Lowest internal RAM 43 → 68 KB (TLS buffers in PSRAM). Single runs, so the noise isn't known.
- [x] **Step 5: Commit:** `feat(handheld): HTTP link (nearby, pin, follow, config), Wi-Fi list joins the strongest`.

### Task 9: Wearable pairing over the network (Track H)

> **2026-11-14 (Leo): the Wi-Fi fallback, built as Task W6d** of `docs/superpowers/plans/2026-11-14-wearable-ble-link.md`,
> before the Bluetooth tasks. The wearable pairs over Bluetooth first; this code pairing is what the app uses when
> Bluetooth isn't available. The `/ble` clean-up below moves to that plan's Task W6a.

**Files:**
- Modify: `firmware/handheld/mock/src/link.cpp` (`POST /pair`), `main.cpp` (code at boot), `ui.cpp` setup states
  only (the code in Trackie's bubble: the existing Pairing state, spec'd in `docs/handheld-ui.md`)
- Clean up after Task 8, built before the Appendix A decision: remove `POST /ble` and `openBle` from `link.cpp`

**Interfaces:**
- Consumes: `parsePair`, `PairGate`, `InfoMsg.paired` (Task 7); Task 8's token store and `wipeLink`.

- [x] **Step 1:** Implement as Task 7's Step 3 with the current gate: no token saved → the Pairing screen with the 6
  digits until it pairs; 5 wrong tries → "Too many tries, new code in N s"; on Ok, save the token and go Home.
- [ ] **Step 2: Verify on the board:** the same checks as Task 7's Step 4 against `cta-pet-xxxx.local`.
  *2026-11-15: after `R`, the right code from the Mac gave 200 and set the token (`docs/ecosystem/day1-link.md`).
  Left: app code pairing with the phone's Bluetooth off (11/16, home network).*
- [x] **Step 3: Commit:** `feat(handheld): pair over the network with the code on screen`. *(c950a87, as W6d)*

### Task 10: App device discovery and the link client (Track A)

**Files:**
- Create: `ios/CTATracker/Devices/DeviceLink.swift`, `ios/CTATracker/Devices/KnownDevices.swift`,
  `ios/CTATracker/Devices/TokenStore.swift`, `ios/CTATracker/Info.plist` (only `NSBonjourServices`)
- Modify: `ios/CTATracker.xcodeproj/project.pbxproj` (`INFOPLIST_FILE = CTATracker/Info.plist` merged with the
  generated one; `INFOPLIST_KEY_NSLocalNetworkUsageDescription = "Finds your Trackie dock and wearable on this Wi-Fi."`;
  `INFOPLIST_KEY_NSBluetoothAlwaysUsageDescription = "Sets up your Trackie dock and wearable."`, kept: the wearable
  pairs over Bluetooth)

**Interfaces:**
- Consumes: `InfoMessage`, `DeviceWatch`, `Token` (Task 4).
- Produces:

```swift
@Observable final class DeviceLink {
    struct Device: Identifiable, Codable { let id: String; let kind: DeviceKind; var name: String; var lastIP: String? }
    private(set) var online: [String: InfoMessage]        // by device id
    func setActive(_ active: Bool)                        // 2 s polling while the app is in the foreground
    func get<T: Decodable>(_ path: String, from id: String) async throws -> T
    func post(_ path: String, _ body: some Encodable, to id: String) async throws
    func delete(_ path: String, to id: String) async throws
    var onAction: ((String, DeviceWatch.Action) -> Void)?   // AppModel reacts (Tasks 13–17)
}
enum TokenStore { static func token() -> String?; static func setToken(_ t: String) }   // Keychain
```

- [x] **Step 1:** Implement: `NWBrowser` for `_ctatracker._tcp`, resolve to an IP, remember `lastIP`, fall back to
  it when Bonjour finds nothing; every write adds the bearer token; each poll feeds `DeviceWatch`.
  *As built:* `lastIP` holds `"ip:port"` (IPv4, so the fake board's port works too); `devices: [Device]` is a settable,
  saved list on `DeviceLink` (Tasks 11–12 add and forget through it); `DeviceLink.pollSec = 2` lives in the app since
  `Config` is CTAKit's; AppModel calls `setActive` from `UIApplication` did-become-active / will-resign-active. The
  app had no test target, so this task adds `CTATrackerTests` (hosted, Swift Testing, `ios/CTATrackerTests/`) for the
  pure parts (`DeviceLinkTests`); `Info.plist` is excluded from the synced folder's resources. Run the tests with
  ad-hoc signing (`CODE_SIGN_IDENTITY=- CODE_SIGN_STYLE=Manual DEVELOPMENT_TEAM=`): the Keychain test needs a
  signed app, and `CODE_SIGNING_ALLOWED=NO` fails it.
- [ ] **Step 2: Verify against the fake board:** `python3 tools/link_send.py --serve --type dock` on the Mac, app on
  the iPhone on the same network: the local network prompt appears once, the fake board shows as online, and
  stopping the server shows "Not connected" within 6 s.
- [x] **Step 3: Commit:** `feat(ios): find boards on the network and poll them`. *(09b54cc)*

### Task 11: Devices list (Track A)

**Files:** Create `ios/CTATracker/Devices/DeviceList.swift`, `DevicePage.swift`; modify `Settings/SettingsScreen.swift`.

**Interfaces:** Consumes `DeviceLink` (Task 10). Produces the Settings → Devices section and a device page
(online state, last update, Wi-Fi, battery and charging for the wearable, its stops for the dock, Forget).

- [x] **Step 1:** Build the list and page from `DeviceLink.online` (app spec §6.3 wording; Forget removes it from
  `KnownDevices`, and the board keeps its token until it's reset). For the wearable the iPhone's Bluetooth bond stays:
  the app can't remove it, so the page says to forget "CTA-Pet-xxxx" in iOS Settings → Bluetooth as well (or use Pair
  again on the wearable, which re-pairs without that).
- [ ] **Step 2: Verify** against both real boards, paired from the app (Task 12 / AB2): both online, battery
  shows, Forget works.
- [x] **Step 3: Commit:** `feat(ios): devices list and device page`.

As built: the Devices section sits at the top of Settings; the wearable's page points to Pair again on the wearable
first (day 1 showed it re-pairs without forgetting it in iOS) and to forgetting `CTA-Pet-<id>` in iOS Settings →
Bluetooth only as the fallback. The dock's stops come from `GET /config` while it's online.

### Task 12: Setup flow over the network (Track A)

> **2026-11-14 (Leo):** build this as written for the dock, and for the wearable as its fallback (pairing with the
> code from Task 9 / W6d). The wearable's Bluetooth branch is Task AB2 of `docs/superpowers/plans/2026-11-14-wearable-ble-link.md`,
> which hooks into this found card and flow shell. **Keep `NSBluetoothAlwaysUsageDescription`**; don't drop it.

**Built as AB2 (3dc686e)** with the Bluetooth branch; the steps below are the code flow it contains. The dock's code
pairing from the app is still unverified on the board (11/16, home network).

**Files:** Create `ios/CTATracker/Devices/FoundCard.swift`, `SetupFlow.swift`; modify `DeviceLink.swift` (unpaired
boards found by Bonjour, `pair`), and in coordination with Track K
`ios/CTAKit/Sources/CTAKit/LinkMessages.swift` (`InfoMessage.paired`, `PairMessage`) and its tests.

**Interfaces:**
- Consumes: `Token.random()`, `TokenStore`, `KnownDevices`, Task 7's `/pair` and its status codes.
- Produces: `PairMessage: Encodable` (`{"code","token"}`, description redacts the token, test
  `pairNeverShowsToken`); `DeviceLink.found: [Device]` (on the network, `paired == false`, not known);
  `DeviceLink.pair(_ id: String, code: String) async throws`; the found card and the full-screen flow
  Pair → Pet (first device only) → Ready.

- [x] **Step 1:** Implement: the `NWBrowser` from Task 10 also reads `GET /info` once from every board it doesn't
  know; one with `paired == false` raises the "Dock found · Set up" / "Trackie found · Set up" card. Pair: "Type the
  code shown on your dock" (or "on Trackie"), then `POST /pair` with the household token
  (`TokenStore.token() ?? Token.random()`, saved on 200). 403: "That code didn't match." 423: "Too many tries, a new
  code will appear on the device." 409: "Already set up. Start pairing again from <device>'s Settings." On 200 the
  board joins `KnownDevices` and polling.
- [ ] **Step 2: Verify:** against `link_send.py --serve` first, then reset a real board (`R`) and set it up from the
  card on the home network (Task 1: day 1's Safari test failed there; 11/16 retries from the app); both boards
  end up online with the same token.
  *2026-11-15: the wearable over Bluetooth passes, including Pair again → re-pair from the app with no Forget in
  iOS Settings (`docs/ecosystem/day1-link.md`). Left for 11/16 on the home network: the dock by code from the app (day 1's Safari test
  couldn't load the board's pages), and the wearable by code with the phone's Bluetooth off.*
- [x] **Step 3: Commit:** `feat(ios): set up the dock and wearable with the code on screen`. *(3dc686e, as AB2)*

### Task 13: One Trackie and device settings (Track A)

**Files:** Create `art/presets.txt`, `ios/CTATracker/Settings/PetSettings.swift`, `DeviceSettingsScreen.swift`;
modify `ios/CTATracker/Design/Pixel.swift` (palette slots 2–3 from the chosen preset), `App/AppModel.swift`.

**Interfaces:** Consumes `ConfigMessage`, `PetConfig`, `DeviceSettings`, `DeviceLink.post`. Produces
`AppModel.pet: PetConfig`, `AppModel.deviceSettings: DeviceSettings`, saved in the App Group; on change and on
`.cameOnline`/`.resendState`, `POST /config` to every online device.

Also: resend pet and settings on `.resendState`, and uncomment the wearable's `ui::petPalette565 = petPalette`
(Task 8's note) so Trackie recolors on the wrist.

- [x] **Step 1:** Write `art/presets.txt`: 6 presets, two `0xRRGGBB` each for slots 2 and 3, the first being today's
  blue (`0x2D6BE6 0x7DB4FF`). Add `PetPresetsTests.matchesArtFile` in CTAKit tests (a `Pet.presets` table in CTAKit,
  Track K reviews) that reads `art/presets.txt` and compares.
- [x] **Step 2:** Build the Pet screen (name, 6 swatches with a 128 pt Trackie preview) and Device settings (screen
  brightness, LED brightness, night hours).
- [x] **Step 3: Verify:** change the color and name in the app; Trackie changes on the dock and the wearable within
  3 s; reboot the wearable and it comes back recolored.
  *Done 2026-11-16 (Leo): the color changed on both boards; a wearable reboot kept it. Leo: the dock's screen
  brightness doesn't change (its backlight is on/off only); settings should be per device (follow-up).*
- [x] **Step 4: Commit:** `feat(ios): one Trackie and device settings, synced to both boards`.

**As built (worker notes):** presets are Blue, Green, Orange, Purple, Pink, Teal (`<name> 0xRRGGBB 0xRRGGBB` per
line; `Pet.presets` and `Pet.defaultConfig` in `CTAKit/PetPresets.swift`). `AppModel.pet` and `.deviceSettings` live
in the App Group (keys `pet`, `deviceSettings`; defaults blue "Trackie", 100/100, night 1:00–5:00 as on the boards).
A change writes `{pet, settings}` to every paired online device 400 ms after it settles; `.resendState` (which comes
with every `.cameOnline` and on a reboot) and `onAdded` write it to that one device. The setup flow's Pet step now
names `AppModel.pet` instead of writing its own blue. `Glyphs.pet(_:colors:)` swaps slots 2–3, defaulting to
`Glyphs.petColors`, which `AppModel` keeps current; the map's avatar and the Pet preview pass the colors so they
redraw at once. The screen slider runs 10–100 % (0 would leave the wearable dark; the dock's backlight is on/off only).
Firmware: both boards already parsed, saved (NVS) and reapplied pet and settings at boot; the only change is the
wearable's `ui::petPalette565 = petPalette` (plus the `card_theme.h` include it needs). No new native test: no
parser changed.

### Task 14: Dock stops from the app (Track A)

**Files:** Modify `ios/CTATracker/Devices/DevicePage.swift`, `App/AppModel.swift`; create
`ios/CTATracker/Devices/DockStopPicker.swift`.

**Interfaces:** Consumes `ConfigStop`, `GET/POST /config`, `.fetchConfig`. Produces the dock page's 3 sections; tapping
one opens the search picker; a chosen card sends all 3 sections (platform IDs for trains).

- [x] **Step 1:** Build it; on `.fetchConfig` read `GET /config` and refresh the sections.
- [x] **Step 2: Verify:** pick in the app → dock section and LED row change; pick on the dock → the app's page shows
  it within 3 s. *Done 2026-11-16 on Leo's phone and the dock (lab network): picker, Send to dock, and a pick on the
  dock all worked.*
- [x] **Step 3: Commit:** `feat(ios): pick the dock's stops from the app, and send a stop to the dock`.

**As built:** `AppModel.dockPicks` holds each dock's 3 sections (read on `.fetchConfig`, retried while online);
`DockConfig.replacing` builds the body (empty `{}`, other sections' `rtnm` kept; app picks send none); plus Send to
dock (Leo, 2026-11-16): the stop page's paper plane becomes a menu, "Send to Trackie" and "Send to dock" → "Replace
<name>" / "Empty section", sending the shown group's platform.

### Task 15: Nearby and the mirror (Track A)

**Files:** Modify `ios/CTATracker/App/AppModel.swift`, `App/LocationService.swift`.

**Interfaces:** Consumes `NearbyBuilder`, `NearbyThrottle` (Task 5), `.sendNearby`, `.cameOnline`, `DeviceLink.post`.

- [x] **Step 1:** On `.sendNearby` or `.cameOnline`: while the map is at the user's location, send
  `fromLocation`; while panned, `fromList(slots, center)`. When the list settles after a pan, offer
  `fromList` to the throttle; a timer calls `flush`. The recenter button sends `fromLocation`.

**As built (worker notes):**
- The map's `following` moved from `MapScreen`'s `@State` to `AppModel.following` (also touches `Map/MapScreen.swift`),
  so the model knows whether the map is at the user. Spec 5.5 wins over this step's `fromLocation`: the wrist always
  gets the phone's card list, built at the location fix while following and from the list on screen while panned.
- Only wearables (`DeviceKind.handheld`) get `nearby`. A wake or come-online while following with no fix yet waits:
  `LocationService.onFix` sends it when the first fix arrives. With location off nothing goes; the wearable keeps its
  last list.
- Direct sends (the wearable asked, recenter) go around the throttle but are recorded with the new
  `NearbyThrottle.sent`, so a later pan compares against what the wearable really has; it also drops a held pan offer,
  and recenter cancels the flush. A wake (`.sendNearby`) and recenter skip wearables that already have those stops
  (each new list blanks the wearable's cards; closing stop detail restores `following` too); `.cameOnline` always
  sends. Actions are ignored while the app isn't active: the wearable keeps its last list (spec 5.5).
- The mirror also runs when the filter or favorites change while panned (the list changed), but not while stop detail
  is open (its focus clears `following` without a pan). The held offer is flushed by a one-shot 10 s task, restarted
  on each held offer.
- A failed post still counts as sent in the throttle; the next wake asks again.
- [x] **Step 2: Verify:** wake the wearable → nearby stops fill (no Mac script); pan the map to the Loop → within
  10 s the wearable shows the phone's Loop stops; recenter → back to home stops.
  *Leo, 2026-11-15, on his iPhone: his stops fill and a pan shows on the wearable "nearly immediately" once Bluetooth
  is connected (the earlier minute-long waits were Bluetooth not reconnecting, fixed in ea31958). Recenter not
  checked separately.*
- [x] **Step 3: Commit:** `feat(ios): nearby on the wrist, mirrored from the map`. *(b8a7223)*

### Task 16: Send to Trackie and Follow (Track A)

**Files:** Modify `ios/CTATracker/Stop/StopDetail.swift` (`StarButton` row and `DepartureTile`), `App/AppModel.swift`.

**Interfaces:** Consumes `ConfigStop`, `FollowMessage`, `Prediction.vehicle`, `.resendState`. Produces
`AppModel.pinned: ConfigStop?`, `AppModel.followed: FollowMessage?`, re-sent on `.resendState`.

- [x] **Step 1:** "Send to Trackie" next to the ★ (only while the wearable is online) posts `/pin` with the station
  ID for trains (`Slot.fetchID`) and the card's direction; a long press on a departure tile with a `vehicle`
  shows "Follow on Trackie" and posts `/follow`. Tapping elsewhere sends nothing.

**As built (worker notes):**
- The ★ row lives in `Map/MapScreen.swift` (`DetailHeroView`, over the map), so the button sits there under ★;
  `SendToTrackieButton` is in `StopDetail.swift` beside `StarButton`. It shows while any paired wearable is online
  (`AppModel.onlineWearables`, now internal).
- The bodies come from two CTAKit builders (`LinkMessages.swift`, tested in `LinkMessageTests`):
  `ConfigStop(pinning: stop, line:)` (station ID for trains, the line's direction label as the wearable keys cards)
  and `FollowMessage(stop:line:departure:)`, nil without a vehicle.
- The follow is a context menu on the tile ("Follow on Trackie"), offered only while a wearable is online and the
  departure has a vehicle; also a VoiceOver action. A tap does nothing.
- Feedback: the button turns into a check (or ⚠) for 2 s, the tile shows a FOLLOWING (or NOT SENT) tag for 2 s, with
  a success or error haptic and a VoiceOver announcement. "Sent" means at least one online wearable answered 200.
- `AppModel.pin`/`follow` post to every online wearable and set `pinned`/`followed` only on success. A pin of another
  stop clears `followed`, as the firmware does (`AppModel.follow(after:followed:)`). On `.resendState` (first seen,
  back online, or rebooted) AppModel re-sends the follow while its departure is less than `Config.departedGraceSec`
  past, else the pin (`AppModel.resend`, tested in `WristPinTests`). This runs even with the app in the background (a
  Bluetooth reconnect can land there); nearby still waits for the foreground.
- Review round 1: a wearable that's unpaired (reset, spec 5.4) or docked when `.resendState` comes gets nothing and
  the app forgets its pin and follow; `DeviceWatch` gained a `.docked` action (the `docked` false→true edge), which
  clears them too. Send targets (`isWearable`, `onlineWearables`) are only devices set up from this app, never a
  wearable merely found over Bonjour (fixes Task 15's fallback to `online[id].type`).
- Known gap: the app can't see a pin swiped off on the wearable: `/info` carries no pin, so any later
  `.resendState` (reconnect or reboot) brings it back. Fix would be a `pin` field in `/info` (spec and firmware
  change).
- A new pairing of a wearable (`DeviceLink.onAdded`, called from `add`) clears `pinned` and `followed`, so a reset
  wearable re-paired over Bluetooth doesn't get the old pin back.
- 2026-11-15 (Leo): a follow whose departure is past `departedGraceSec` is no longer resent and doesn't fall back to
  the pin: `AppModel.resend` returns nil, so the app forgets `pinned` and `followed` (a pin with no follow still
  resends), matching the wearable clearing a passed follow with its pin.
- [x] **Step 2: Verify:** pin Belmont Red → the wearable's Home card is Belmont with ★ (after Task 18); follow the
  next train → the wearable counts down to it; reboot the wearable → pin and follow come back.
  *Leo, 2026-11-15, his iPhone and the wearable: pin and follow work and come back after a reboot; reset, docking and
  a new pairing clear the app's pin; a passed follow returns Home to the closest stops after 30 s. Relay push on
  fetch (641d93a, 770821b): each stop reaches the wrist within about a second.*
- [x] **Step 3: Commit:** `feat(ios): send a stop to Trackie and follow a departure`. *(bf9a65f, fixes 727e7b3, 7efedc8, fbeb68b; passed follow 3981625, 473e205, 7cb3753)*

### Task 17: Find my handheld (Track A)

**Files:** Create `ios/CTATracker/Devices/LastSeen.swift`; modify `App/AppModel.swift`, `App/LocationService.swift`,
`Devices/DevicePage.swift` (a "Last seen" section on the wearable's page, per the 2026-11-16 Settings layout).

**Interfaces:** Consumes `.wentOffline` for the wearable. Produces `AppModel.lastSeen: [String: LastSeen]` (by device
id) in the App Group.

- [x] **Step 1:** On `.wentOffline` take one location fix and save it; the page shows the pin and time, or
  "Connected now".
- [x] **Step 2: Verify:** power the wearable off → within 6 s the page shows the pin and the time.
  *Done 2026-11-16 (Leo): the pin and time showed after the wearable was switched off, and "Connected now" when it
  came back.*
- [x] **Step 3: Commit:** `feat(ios): find my handheld`.

**As built (worker notes):**
- `.wentOffline` already fired for a wearable online over Bluetooth only: its `info` notifies feed the same
  `DeviceWatch` as the poll, and the poll skips a device only while `ble.connected` holds it, so a disconnect plus
  3 missed polls (no address counts as a miss) gives `.wentOffline`. No DeviceLink change.
- The fix is `LocationService.latest` (the one CLLocationManager's `location`): current while the app is in the
  foreground, where polling runs; else the last known one; nil with location off, which shows "· location
  unavailable". No wait for a new fix. iOS notices a powered-off wearable's Bluetooth drop after its supervision
  timeout, so the 6 s count starts then.
- `LastSeen.record` (when to save) and `LastSeen.shown`/`label` (what to show) are pure, tested in `LastSeenTests`.
  The map is MapKit, not interactive; a tap opens `maps.apple.com` at the pin (the coordinate only).
- Round 1 review: a `.wentOffline` within `LastSeen.settleSec` (8 s) of coming to the foreground isn't recorded;
  polling stops in the background, so that drop happened elsewhere, earlier.

### Task 18: The pinned and followed card on the wearable (Track H-UI)

**Files:** Modify `firmware/handheld/mock/src/ui.cpp/.h`, `docs/handheld-ui.md`.

**Interfaces:** Consumes `live::Snapshot::hasPin`, `pin`, `hasFollow`, `follow` (Task 8) and `cta::FollowState`
(Task 3). Until Task 8 merges, drive it from the mock build (`mock_data.cpp` sets a pin and a follow).

- [x] **Step 1:** Home card: a pinned stop takes the card with a ★ before the stop name; a follow shows
  "Red · 4 min · to 95th", then "Due", then "Arrived" or "Lost the train" (bus: "Lost the bus") for 60 s.
  A swipe left on the pinned card clears it (calls `live::setPin(nullptr)`).
- [x] **Step 2: Verify** in the mock build on the board and in `tools/preview.cpp` renders; update
  `docs/handheld-ui.md`. (Renders and the doc done; the ★ pinned card and the follow countdown seen on the board,
  2026-11-15.)

**As built (worker notes):**
- `mock::Model` gained `hasPin`, `Card pin`, `hasFollow`, `cta::FollowView follow`; `drawHome` draws `pin` ahead of
  `glance`. After Task 8 and this task both merge, Track H copies `hasPin`/`pin`/`hasFollow`/`follow` from
  `live::Snapshot` in `fromLive`. The follow line takes the line from the pin's `route` and the destination from its
  `heading` (a bus's `dir` is its `rtdir`). A pin with no arrivals shows no time.
- 2026-11-15 (Leo): when Arrived or Lost has shown for 60 s, `Store::updateFollow` clears the follow and its pin
  (`setPin(nullptr)`), so Home goes back to the closest stops instead of an empty pinned card.
- The ★ goes before the card's title: the stop on a train card, the route on a bus card (its stop line is too small).
- The swipe calls `live::setFollow(nullptr)` and `live::setPin(nullptr)` and clears the model's pin for that frame;
  it stays gone only if `setPin(nullptr)` clears the published snapshot. `mock_data.cpp` holds weak stubs of both
  (declared in `mock_data.h`) that live.cpp's real ones replace at link time. Keep the weak stubs: env:mock and the
  laptop tools have no live.cpp.
- The mock's bus cards carry Bus Tracker's `rtdir` ("Southbound") in `dir`, the destination in their arrivals.
- The mock pins Belmont's Red to 95th in Live and follows its 9-minute train: Waiting, Due, Arrived for a minute,
  then the pin alone.
- [x] **Step 3: Commit:** `feat(handheld): pinned stop and follow countdown on Home`. *(cc4f38f)*

### Task 19: Relay with no Wi-Fi — DROPPED: built in the BLE plan

> **Replaced 2026-11-14 (Leo)** by Tasks W6b and AB3 of `docs/superpowers/plans/2026-11-14-wearable-ble-link.md`: the
> relay over Bluetooth with whole stops (`POST /stop`), not one `snapshot` per card; built and verified on Leo's
> phone 11/15. Showcase step 7 (no Wi-Fi on the wearable, cards from the phone) is in if its go/no-go passes on
> 11/20 at 18:00. Don't build the steps below; everything under this box is history.

**Dropped with the Appendix A decision:** the relay is phone to wearable only: the dock has no BLE (Leo, 2026-11-14,
"the dock is only wifi"). It needs Bluetooth on the wearable, and BLE's ~50 KB of internal RAM
doesn't fit (`docs/ecosystem/day1-link.md`). Showcase step 7 is out; BLE relay is future work (spec section 12). The
steps below are kept as the design for that later version; don't build them now.

**Files:** 19a `firmware/handheld/mock/src/ble_setup.cpp` (+ `snapshot` characteristic), `live.cpp` (accept relayed
records); 19b `ios/CTATracker/Devices/HandheldRelay.swift`.

**Interfaces:** Consumes `cta::decodeStopRecord` (existing `stop_record.h`), CTAKit `StopRecord` encoding.

- [ ] **Step 1 (19a):** After 60 s without Wi-Fi, start BLE (Global Constraints) with `snapshot` writable; a
  written record replaces the matching card (same `id`, `route`, `dir`) with its `ts`. Serial `W` toggles Wi-Fi
  off for the demo, and so does a switch on F3's settings page once it exists.
- [ ] **Step 2 (19b):** While the wearable is offline on the network and seen over BLE, connect and write one
  `snapshot` per card every 60 s, built from the last nearby list.
- [ ] **Step 3: Verify:** turn the wearable's Wi-Fi off; within 2 minutes its cards update from the phone.
- [ ] **Step 4: Commit:** `feat: phone relays arrivals over Bluetooth when the wearable has no Wi-Fi`.

### Task 20: Extras, only if the full showcase run passes twice (Tracks D, H, A)

- [ ] **The dock follows your train:** dock `POST /trip` (the follow body plus the pet name, parsed by `parseFollow`
  plus `"pet"`); the dock draws Trackie on that vehicle on its map with "Trackie · Red Line · at Fullerton";
  waiting at the stop before the vehicle reaches it; ends on `DELETE /trip` (sent by the wearable when its follow
  ends), at the end of the line, when the vehicle drops out, or after 90 minutes.
- [ ] **Trackie hops:** the wearable `POST /event {"docked":true|false}` to the dock; the dock plays a hop-in on its
  status strip as the wearable falls asleep, and the reverse.
- [ ] Each gets its own verify-on-hardware step and commit.

### Task 21: Docs and the demo script (Track X)

**Files:** Modify base spec §8.3, §9.5, §10 (point to the ecosystem spec), `firmware/handheld/mock/README.md`,
`firmware/dock/` README section (the wearable README's Bluetooth section is done, e4efe9d); create
`docs/ecosystem/demo-script.md` (showcase steps 1–8, step 7 only if its 11/20 go/no-go passed, with the exact taps,
the reset step before pairing, and what to do if a step fails; no network names). Drafted 11/18
with BLE plan X2 Step 2.

- [ ] **Step 1:** Write them. Check `git diff` for network names, home paths, tokens. *Demo script drafted 2026-11-17 (Bluetooth-only Trackie); the READMEs and base spec pointers are still open.*
- [ ] **Step 2: Verify:** run the script end to end on the showcase network (11/22), twice.
- [ ] **Step 3: Commit:** `docs: ecosystem demo script, base spec points to the ecosystem design`.

---

### Task 22: Dock touch extras (Track D)

Ecosystem spec 5.10 (Leo, 2026-11-16). Dock firmware only; one worker on the dock. Not a showcase step: drop it first
if Thursday's full run struggles.

**Files:** Modify `firmware/dock/src/dock_ui.cpp` (touch targets, the overlay), `firmware/dock/src/net.cpp` (the
on-tap requests), `firmware/lib/cta` (parsers, with native tests in `firmware/native/test`).

- [x] **Step 1: The stops ahead.** Tap an arrival time → overlay with the vehicle's next stops and times
  (`ttfollow` by run number for trains, `getpredictions?vid=` for buses). Parser tests from saved real responses (no
  keys in fixtures). Closes on tap, after 30 s, or when the vehicle drops out.
- [x] **Step 2: The other direction.** Tap a stop card → 30 s peek at the other direction (trains: the opposite
  platform from the dock's tables; buses: the closest stop on the same route going the other way). Saved pick, LED
  row and `picks_ver` unchanged; hold still opens the picker.
- [x] **Step 3: Verify** on the dock: both peeks on a train and a bus card; RAM low-water mark no worse than 5 KB
  below today's (83 KB); frame time unchanged when idle.
  *Done 2026-11-16 (Leo, on the showcase network): every tap worked (stops ahead for a train and a bus, other way
  on both, hold still picks); internal RAM low-water 82.5 KB during the taps.*
- [x] **Step 4: Commit:** `feat(dock): tap a time for the stops ahead, tap a card for the other direction`.

**As built (worker notes):**
- Parsers and URLs in `firmware/lib/cta`: `ahead.h` (`parseTrainAhead`, `parseBusAhead`, `oppositeTrainPlatform`,
  `oppositeBusStop`), `trainFollowUrl` and `busAheadUrl` in `urls.h`. Tests in `firmware/native/test/test_ahead`, from
  real replies saved with the keys stripped: `fixtures/cta/train_follow*.json`, `bus_follow*.json`. Arrivals already
  kept `rn` / `vid` (`Arrival::vehicle`).
- "Gone": Train Tracker answers `errCd` 501 ("No trains with runnumber … were found"), Bus Tracker an error naming the
  `vid`; both parse to 0 stops, and the overlay closes. Other errors show "Couldn't load the stops ahead".
- Trains' other way: the closest platform (within 300 m, by `kPlatforms`) on the same line with another direction
  label. Buses' other way: worked out once when `getpatterns` answers for the LED row (the closest stop within 500 m
  on a pattern with another `rtdir`), so a tap is one `getpredictions?stpid=`. A route without one shows "Couldn't
  load the other way".
- Requests: `net::ahead` / `net::peek` queue one each (a newer tap replaces one not yet served); the network task
  serves them before its next poll, on its one connection, and the answer rides in the snapshot (`Data::ahead`,
  `Data::peek`) under the tap's id. No new buffers: `body` and `busRecs` as the polls use them.
- Touch: a tile is its own target (tap only); the rest of a filled card taps to peek and holds for the picker. A
  peeking card shows an "OTHER WAY" chip and taps anywhere to flip back. The stops ahead take every tap while up and
  close when the screen leaves the stops. `tools/preview.cpp` renders both (`home_ahead*`, `home_peek*`) and checks
  the gestures (`extrasFlow`).
- Flashed 2026-11-16, idle, no taps (there is no serial touch command): internal free min 82.9 KB, frames 18–22 ms.
  The taps on the dock are Step 3.

---

### Task 23: Settings per device (Track A, one dock line in Track D)

Ecosystem spec 5.2, "Settings layout" (Leo, 2026-11-16). Replaces Task 13's single Device settings screen.

**Files:** Modify `ios/CTATracker/Settings/SettingsScreen.swift`, `DeviceSettingsScreen.swift` (becomes Quiet hours),
`ios/CTATracker/Devices/DevicePage.swift`, `App/AppModel.swift`; `firmware/dock/src/main.cpp` and `dock_ui.cpp` (screen
brightness as a software dim).

- [x] **Step 1:** App: top-level Trackie and Quiet hours; per-device Screen brightness (both), Route LEDs (dock:
  brightness, Off); wearable Wi-Fi row with "Add a network" (the setup flow's Wi-Fi step). Settings saved per device
  id; each device gets its own `settings`, with the shared quiet hours. A pure function builds one device's settings,
  with a test. *As built:* `AppModel.quietHours` (on, start, end; off is sent as start == end, which both boards
  read as no night) and `devicePrefs` by device id (screen, led, ledOn), each in the App Group; a device with none
  takes Task 13's saved `deviceSettings`, which is left in place. `AppModel.settings(_:quiet:)` builds one device's
  `settings` (`DeviceSettingsTests`). A per-device change goes to that device alone, debounced as before.
  `QuietHoursScreen` replaces `DeviceSettingsScreen`; `DevicePage` has Screen brightness, Route LEDs (dock) and
  Wi-Fi (wearable). "Add a network" opens `SetupScreen` on a `SetupFlow(addingNetworkTo:)` parked on the Wi-Fi step,
  enabled only over Bluetooth; it writes the slots after the ones the app has counted (`Device.wifiSlots`, set to 0 on
  pairing; a wearable set up before this starts at 0 too). Limit, accepted: the app can't read the board's slots, so
  after Pair again (which keeps Wi-Fi) the first network added becomes home and replaces the saved one; the screen
  says so. A code-paired wearable has no Bluetooth link, so the page says to Pair again over Bluetooth.
- [x] **Step 2:** Dock: screen brightness dims the frame with the night veil (darker wins, floor about 20%); `led` 0
  turns the route LEDs off. Native test if logic is added. *As built:* `cta::dockVeil(night, screen)` in
  `dock_status` (test `test_dock_veil`): the darker of night's 0.62 and 1 − screen, screen floored at 20%; `dock::Clock`
  carries `screen` and `dock::draw` lays the one veil. `led` 0 already gave black through `ledScaled`, unchanged.
  The preview's `home_dim` matches band by band; the board's frames stayed 17–20 ms.
- [x] **Step 3: Verify** on both boards: dock brightness and LEDs change within 3 s and the wearable's screen doesn't;
  wearable brightness changes only the wearable; quiet hours dim both.
  *Done 2026-11-16 (Leo: "seems to work").*
- [x] **Step 4: Commit:** `feat: settings per device; the dock dims in software`.

---

### Task 24: The same settings on each device's own Settings screen (Tracks D, H, A)

Ecosystem spec 5.2, "Settings layout", last bullet (Leo, 2026-11-16).

**Files:** `firmware/dock/src/dock_ui.cpp`, `main.cpp`, `link.cpp` (Settings rows; `settings` in `info`);
`firmware/handheld/mock/src/ui.cpp` and the live build (a brightness row); `ios/CTATracker/App/AppModel.swift` (adopt a
device's own change from `info.settings`).

- [x] **Step 1:** Dock Settings: Screen brightness and Route LEDs (steps with − / +, LEDs down to Off), and a Quiet
  hours line ("Quiet 10 PM – 7 AM · set in the app", or "Off"). Saved like the app's write; `info` carries `settings`.
  As built: two step rows under APP (10% steps: screen 20–100, LEDs 0–100 with 0 "Off"; a step at its end is dim),
  then the quiet line, then Pair again (moved down to y 692). The line reads "Quiet 10 PM to 7 AM · set in the app"
  or "Quiet hours off": the fonts have no en dash. A tap applies at once (`applySettings`, `ledScaled`), goes into
  `info.settings` and GET /config, and is saved 2 s after the last tap; an app write in between saves and cancels it.
  Shared helpers `cta::stepPct` and `cta::quietLine` (`link_msg`), with native tests; `tools/preview.cpp`'s settings
  flow taps the steps.
- [x] **Step 2:** Wearable Settings: a Screen brightness row (− / +), saved the same way; its `info.settings` already
  goes out. As built: a "Brightness − 80% +" row under Bluetooth (rows now every 64 px, Pair again 240 × 56 at y 356),
  20–100 by 10. The UI sets `Model::screenPct`/`screenChanged`; `main.cpp` applies it, sets `infoDirty` (a Bluetooth
  `info` notify within a second) and saves 2 s after the last tap. `tools/touch_test.cpp` taps the steps.
- [x] **Step 3:** App: when a device's `info.settings` shows a screen or LED value the app didn't send (changed on the
  device), adopt it into that device's settings, unless a change of the app's own is waiting to go out. Test the
  pure rule. As built: `DeviceLink.onInfo` (before the actions, so a `.resendState` sends the adopted value);
  `AppModel.adopted(...)` adopts when the report differs from what the app last sent (kept across launches) and from
  the device's previous report (a stale report from before the app's write landed is ignored); pending is the 400 ms
  debounce or a `/config` still in flight. Quiet hours are never adopted. `DeviceSettingsTests`.
- [x] **Step 4: Verify** on both boards: change brightness on the device and the app's device page shows it within
  3 s; change it in the app and the device's Settings row follows; a reboot keeps it.
  *Done 2026-11-16 (Leo: "Works").*
- [x] **Step 5: Commit:** `feat: device settings on each device's own Settings screen, kept in step with the app`.

---

### Task 25: Remove a pin from the app, and the app sees a pin cleared on the wearable (Tracks A, H)

Ecosystem spec 5.6, "Removing it from the app" (Leo, 2026-11-16). Fixes `todo.md` 2026-11-15 items (c) and (d).

**Files:** `ios/CTATracker/Stop/StopDetail.swift` (the Send menu), `Devices/DevicePage.swift`, `App/AppModel.swift`;
`firmware/lib/cta` (`info` encode/decode), `firmware/handheld/mock/src` (fill `pin`); CTAKit `InfoMessage`.

- [x] **Step 1:** Wearable `info` gains `pin` (stop id or null; native test). CTAKit decodes it (optional).
  As built: `InfoMsg.pin` ("" encodes as null, so the dock always sends null); the wearable fills it from the live
  snapshot's pin each second, and a change is part of `publishInfo`'s compare, so it goes out in a Bluetooth notify.
  Native test `info_has_pin`; the worst-case `info` with a pin is 546 bytes (kMaxInfo 640).
- [ ] **Step 2:** App: "Remove from Trackie" in the stop page's Send menu while that stop is pinned; the wearable page
  shows the pinned stop with Remove; both `DELETE /pin` and clear `AppModel.pinned`/`followed`. When `info.pin` is null
  but the app holds a pin, the app clears its own (no resend). Pure rule, tested.
  As built: the Send plate is a menu with "Remove from Trackie" while the shown card is the pinned one (one tap
  otherwise, as before); the device page has a "Pinned stop" section (name, line · direction, Following) with Remove.
  Both call `AppModel.unpin()` (`DELETE /pin`, which ends a follow on the wearable too). `AppModel.pinCleared` clears
  the app's pin only when the wearable's report went from a stop to null since its previous one at the same or a
  higher uptime: a fresh pin not in `info` yet, a reboot, or a late old report never clears. Clearing also drops
  `DeviceLink.sentPin`, so `WearableRelay` stops relaying that stop.
- [ ] **Step 3: Verify:** pin, remove from the app → the wearable goes back to the closest stops; pin, swipe it away
  on the wearable → the app's pin clears, and it doesn't come back after Bluetooth off/on.
- [x] **Step 4: Commit:** `feat: remove a pin from the app; a pin cleared on the wearable clears in the app`.

---

## Appendix A: if Task 1 says Bluetooth doesn't fit

**Now the dock's design only.** Chosen by Leo on 2026-11-14 for both boards, then the same day the wearable went
Bluetooth first (the second decision box); on the wearable this code pairing is the fallback (Task 9 / W6d). The
original note, history:

Pairing moves to the network. Wi-Fi comes from `secrets.h` (both networks compiled in). A fresh board shows a 6-digit
code on screen and accepts `POST /pair {"code":"123456","token":"<hex>"}` once, within 5 minutes of boot, with the
right code (constant-time compare; 5 wrong tries lock it until reboot). Tasks 7 and 9 become this endpoint plus the
code on screen; Task 12's flow becomes Pair (type the code shown) → Pet → Ready, found by Bonjour instead of BLE;
Task 19 is dropped. Everything else is unchanged.

---

### Task 26: Trackie docks without Wi-Fi; battery percent on every screen (Track H)

Leo, 2026-11-17: Trackie stays on Bluetooth at the showcase, and its screens show the battery percent when a battery
is fitted (todo F2). Wearable firmware only; one worker.

- [ ] **Step 1:** Docked = USB power and a battery, and either the home network or no Wi-Fi joined at all (a joined
  non-home network still isn't docked). Native tests for each case. Spec 8.4 note.
- [ ] **Step 2:** A small battery and percent at the top of the main screens (green while charging, amber at 20% or
  less, nothing with no battery), redrawn only on a change. `docs/handheld-ui.md`, todo F2.
- [ ] **Step 3: Verify (Leo):** on the dock with no Wi-Fi joined, Trackie goes to sleep (Docked); off it, the percent
  shows on each screen.

### Task 27: iPhone app polish (Track A), done 2026-11-17

Leo, 2026-11-17. Each change reviewed, on `main`, installed on Leo's phone after the simulator:

- [x] Search by words in any order, no "&" needed; route and line words mix with street names.
- [x] Search by place (MapKit, Chicago area): the map flies there and lists the stops near it.
- [x] Typo tolerance ("belmnt", "van burn"), shown as "Showing close matches".
- [x] "Trackie" everywhere the user sees it; the wearable advertises `Trackie-xxxx` (the app accepts `CTA-Pet-` too).
- [x] "Send" label on the Send to Trackie button, with its states.
- [x] Setup's Wi-Fi step says why Trackie couldn't join (`info.wifi.fail`); no network names in the wearable's logs.
- [x] F6: the line's trains and buses on the map as round tokens; tap a tile or vehicle to focus it.
- [ ] **Verify (Leo):** taps on map vehicles, the Send states with Trackie, a Blue Line token beside the location dot.
