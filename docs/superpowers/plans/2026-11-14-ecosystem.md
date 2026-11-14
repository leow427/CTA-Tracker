# One Ecosystem Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** The iPhone app, the dock, and the wearable work as one system for the 2026-11-23 showcase: pairing and
Wi-Fi from the app, one Trackie, dock stops both ways, nearby and mirrored stops on the wrist, Send to Trackie,
Follow a train, the no-Wi-Fi relay, and docking with find my handheld. The rest of `todo.md` runs alongside as its
own tracks.

**Architecture:** Bluetooth only for setup (passkey pairing, the Wi-Fi password, a household token); after that each
board serves a small HTTP API on the shared network (ESP-IDF `esp_http_server`, announced over mDNS as
`_ctatracker._tcp`), and the app polls `GET /info` every 2 s while it's open. Message parsing lives in the shared C++
library and CTAKit, both tested on the laptop; board code only wires handlers to it.

**Tech Stack:** arduino-esp32 3.2.0 high_perf on both boards (pioarduino), `esp_http_server` and `ESPmDNS` from the
core, NimBLE-Arduino 2.x (or the core's BLE, decided in Task 1), ArduinoJson 7, Unity native tests; SwiftUI iOS 17,
CoreBluetooth, Network.framework (`NWBrowser`), Swift Testing in CTAKit.

**Spec:** `docs/superpowers/specs/2026-11-14-ecosystem-design.md` (approved 2026-11-14). Base spec
`2026-11-03-cta-tracker-design.md`, app spec `2026-11-04-ios-app-design.md`.

## Global Constraints

- Showcase on one network: the campus IoT network (slot 0, home, on the wearable); development on Leo's home network.
  **Never write either network's name into a tracked file**, a test, a fixture, or a log line that gets committed.
- Internal RAM floor: 40 KB free at the worst moment, logged with `heap_caps_get_minimum_free_size(MALLOC_CAP_INTERNAL)`.
- All new board work runs off the dock's frame loop (core 1): HTTP handlers on the httpd task, BLE on its host task,
  hand-offs to the loop through a mutex-guarded inbox. The dock perf runner must not show worse frames than round 4
  (`d0769a0`: picker 34 fps, map 16 fps, mixed 25 fps).
- Writes need `Authorization: Bearer <token>`; the token is 32 lowercase hex characters (128 bits), compared in
  constant time, never logged, kept in NVS on the boards and in the Keychain on the phone. `GET /info` needs no token.
- Wi-Fi passwords cross only the encrypted, passkey-bonded BLE link. Never over HTTP, never logged, never stored on the
  phone.
- BLE runs only while a board has no working Wi-Fi (none saved, or none joined for 60 s) or for 120 s after `POST /ble`.
  Otherwise the stack is shut down and its memory freed.
- Pin and follow bodies for **trains** carry the parent **station** ID (4xxxx, the app's `Slot.fetchID`) and the
  platform's direction label as in `stops.json` (the wearable's train cards are keyed that way, base spec 6.2); for
  **buses** the stop ID and `rtdir` ("Southbound"). Dock `config.stops` keep platform IDs (3xxxx), as `cta::PickedStop`
  does today.
- Sizes from the base spec: IDs and routes `char[8]` (7 bytes), names/dirs/dests 24 bytes, pet name 16 bytes,
  at most 4 arrivals a record, a `snapshot` at most 512 bytes. Vehicle IDs (`rn`, `vid`) `char[8]`.
- Wearable: up to 5 saved networks (`kWifiSlots = 5`), slot 0 is home. Dock: 1.
- mDNS names `cta-dock-xxxx` / `cta-pet-xxxx` (`xxxx` = last 2 MAC bytes in hex, the same as the BLE name), service
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
| **L — shared library** | `firmware/lib/cta/src/link_msg.*`, `follow.*`, `wifi_list.*`, `cta_types.h`, `train_parse.*`, `bus_parse.*`, `stop_record.*`, `firmware/lib/pet/src/pet_palette.*`, `firmware/native/test/test_link*`, `tools/link_send.py` | 2, 3 | nothing |
| **K — CTAKit** | `ios/CTAKit/**` | 4, 5 | nothing |
| **S — link spike** | a throwaway `spike/link/` PlatformIO project | 1 | nothing |
| **D — dock link** | `firmware/dock/src/link.*`, `store.*`, `net.cpp` (Wi-Fi join only), `main.cpp`, setup messages in `dock_ui.cpp` | 6, 7 | S (decision), L (Task 2) |
| **H — wearable link** | `firmware/handheld/mock/src/link.*`, `live.cpp`, `live_store.*`, `main.cpp`, `power.*` | 8, 9, 19a | S, L (Tasks 2, 3) |
| **A — app** | `ios/CTATracker/Devices/**`, `Settings/**`, `App/AppModel.swift`, `Stop/StopDetail.swift`, project build settings | 10–17, 19b | K (Task 4), then D/H for on-device checks |
| **H-UI — wearable screens** | `firmware/handheld/mock/src/ui.*`, `docs/handheld-ui.md` | 18, then F2, F3, F5 | L (Task 3) for 18 |
| **F4 — font** | `firmware/lib/gfx/src/fonts.*`, `firmware/lib/gfx/tools/font_build.py`, `ios/CTATracker/Design/Theme.swift` | its own design + plan | nothing |
| **F6 — app map vehicles** | `ios/CTATracker/Map/**` except `StopIcons.swift`, `Data/` vehicle polling | its own plan | K (Task 4: `Prediction.vehicle`) |
| **F7 — pixel icons** | `ios/CTATracker/Map/StopIcons.swift` | its own plan | nothing |
| **B1 — wearable scroll tearing** | the handheld's display send path (`main.cpp` frame send, display driver setup) | investigation | coordinate `main.cpp` edits with H |
| **B2 — dock frame rate** | `firmware/dock/src/bands.*`, `display.*`, `dock_ui.cpp` drawing | investigation + rounds | shares the dock board with D |
| **X — finish** | `docs/superpowers/specs/2026-11-03-cta-tracker-design.md` §8.3/9.5/10, READMEs, `docs/ecosystem/demo-script.md` | 20, 21 | everything |

**F2, F3, F4, F6, F7, B1, B2 are not planned task-by-task here.** Each starts in its own session with a short design
(F3 and F4 say so in `todo.md`), then its own plan. The hooks they share with this plan are in spec section 6.1.

### Order, day by day

Today is 2026-11-14; the showcase is 2026-11-23. "Start" means open the track's session.

- **Day 1 (11/15).** Start **S** (Task 1, needs both boards), **L** (Task 2), **K** (Task 4), **F7**, **F4** (design:
  try fonts in the laptop previews), **B1** on the laptop (Waveshare's and Espressif's notes on the CO5300's tearing signal, the frame-time probe for
  the serial profile); it gets the wearable once S lets it go. Six sessions at once; S is the critical path.
- **Day 2 (11/16).** S reports the BLE-or-fallback decision. **L** does Task 3. **K** does Task 5. Start **D**
  (Task 6) and **H** (Task 8) once Task 2 is merged. Start **A** (Task 10, discovery against `tools/link_send.py
  --serve`, which fakes a board on the Mac). Start **F6** (needs Task 4 merged).
- **Day 3 (11/17).** **D** Task 7, **H** Task 9 (BLE setup; the dock board goes to D, so B2 works on the laptop
  today). **A** Tasks 11–12 (device list, setup flow) against the real boards by evening. **H-UI** Task 18. Start
  **B2** on the laptop (profile captures from day 1).
- **Day 4 (11/18).** **A** Tasks 13–14 (pet and settings, dock stops). **H-UI** F2 (battery level). **B2** gets the
  dock board in the morning, D in the afternoon.
- **Day 5 (11/19).** **A** Tasks 15–16 (nearby and mirror, pin and follow). **H-UI** F5 (tap Trackie). First full run
  of showcase steps 1–6 on the home network.
- **Day 6 (11/20).** **A** Task 17 (find my handheld). **H** + **A** Task 19 (relay, step 7). **H-UI** F3 (settings
  page; moves step 7's Wi-Fi switch and the reset into it). F4 lands its atlases if its design is picked.
- **Day 7 (11/21).** Spec section 9 extras (Task 20) only if steps 1–8 pass twice. **X** Task 21. Freeze new
  features at 18:00.
- **Day 8 (11/22).** Dry run on the **campus** network, twice in a row, from `docs/ecosystem/demo-script.md`. Fix
  only what breaks.

Merge order onto `main`: L and K first (everyone builds on them), then D and H, then A, then the UI tracks. Each
track rebases on `main` before merging; the merge itself is Leo's call (ask before every commit-and-push).

---

### Task 1: Prove the link on both boards (Track S)

**Files:**
- Create: `spike/link/platformio.ini` (envs `dock` and `handheld`, copied from each board's pins and core)
- Create: `spike/link/src/main.cpp`
- Create: `docs/ecosystem/day1-link.md` (results; no network names)

**Interfaces:**
- Produces: a decision recorded in `docs/ecosystem/day1-link.md`: **BLE** (Tasks 7, 9, 12 as written) or
  **LAN pairing fallback** (Appendix A), and which BLE library.

- [ ] **Step 1:** Write `main.cpp`: join Wi-Fi from `secrets.h`; start BLE with the base spec service UUID
  (`ble_ids.h`), one readable `info` and one encrypted-write characteristic, passkey display pairing (print the 6
  digits to serial); start `esp_http_server` with `GET /info` returning `{"ok":true}`; `MDNS.begin("cta-spike-xxxx")`
  and `MDNS.addService("ctatracker","tcp",80)`; every 30 s run one HTTPS GET to the train API with the existing
  `Tls` setup; every 5 s print `heap_caps_get_minimum_free_size(MALLOC_CAP_INTERNAL)` and the current free internal.
  Serial `b` toggles BLE off (`deinit(true)`) and on.
- [ ] **Step 2:** Flash to the dock and a wearable. Pair from the iPhone with LightBlue (or nRF Connect): the
  passkey prompt appears and an encrypted write lands.
- [ ] **Step 3:** With BLE on and a TLS request every 30 s for 10 minutes, record the lowest free internal RAM per
  board. Then BLE off: record that it returns to within 2 KB of the no-BLE figure.
  Pass: ≥ 40 KB on both with BLE on. If NimBLE-Arduino fails to build or pair on core 3.2, try the core's BLE.
- [ ] **Step 4:** On the home network: `dns-sd -B _ctatracker._tcp` on the Mac lists both boards; the iPhone reaches
  `http://<ip>/info` in Safari; the dock fetches the wearable's `/info` (serial `n <ip>`). Repeat on the campus
  network when on campus; record pass/fail only.
- [ ] **Step 5:** Write `docs/ecosystem/day1-link.md` (RAM numbers, library, mDNS and reachability per network as
  "home" / "campus", the decision). Commit `spike/link` and the doc.

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
- [x] **Step 3: Implement** the declarations above. Parsers use ArduinoJson with the same "fill a temporary, copy
  on success" pattern as `parseNearby`; `parseConfig` reuses `decodePicks` for `stops`.
- [x] **Step 4: Run** the same command plus the whole suite (`pio test -e native`). Expected: all PASS.
- [x] **Step 5: Write `tools/link_send.py`**: `link_send.py <host> info|config|pet|nearby|pin|follow|unpin|unfollow|ble|reset
  [json] --token <hex>`, `--burst` (20 pins in 2 s, prints each status and time), `--garbage` (bad JSON, 2 KB body,
  no token, wrong token: expects 400, 413, 401, 401), and `--serve [--type dock|handheld]` (a fake board on the Mac
  answering the same endpoints from memory and advertising `_ctatracker._tcp` with `dns-sd -R`, for Track A).
  The token comes from `--token` or `LINK_TOKEN`, never printed. `tools/test_link_send.py` checks the fake server's
  answers to each `--garbage` case.
- [ ] **Step 6: Commit** the files above: `feat(lib): link messages, Wi-Fi list, pet palette swap, link_send tool`.

### Task 3: Vehicle IDs on arrivals and the follow rules (Track L)

**Files:**
- Modify: `firmware/lib/cta/src/cta_types.h` (`Arrival`), `train_parse.cpp`, `bus_parse.cpp`, `stop_record.cpp`
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

- [ ] **Step 1: Write the failing tests:** `train_keeps_rn` (a fixture arrival's `vehicle` equals its `rn`),
  `bus_keeps_vid`, `record_with_vehicles_fits_512` (4 arrivals, 24-byte names and dests, 7-byte vehicles: encoded
  ≤ 512), and in `test_link_follow`: `follow_waiting_4_min`, `follow_due_at_0`,
  `follow_gone_after_min_1_is_arrived`, `follow_gone_at_min_9_is_lost` (only once a record with a newer `ts` lacks
  it), `follow_failed_poll_keeps_waiting` (same `ts` → still Waiting with the last minutes),
  `follow_expires_after_60_s`.
- [ ] **Step 2: Run** `pio test -e native -f test_link_follow -f test_train -f test_bus -f test_stop_record`.
  Expected: FAIL.
- [ ] **Step 3: Implement.** Keep `rn` (`ttarrivals`) and `vid` (`getpredictions`) in the parse filters;
  `stop_record` encodes a vehicle as `"veh"` only when non-empty.
- [ ] **Step 4: Run** the full native suite. Expected: PASS.
- [ ] **Step 5: Commit:** `feat(lib): vehicle IDs on arrivals, follow rules`.

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
- [ ] **Step 5: Commit:** `feat(ctakit): link messages, vehicle on predictions, device watch`.

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
- [ ] **Step 5: Commit:** `feat(ctakit): nearby builder for location and mirror, 10 s throttle`.

### Task 6: The dock on the network (Track D)

**Files:**
- Create: `firmware/dock/src/link.h`, `firmware/dock/src/link.cpp`
- Modify: `firmware/dock/src/store.h/.cpp` (Wi-Fi slot 0, token, pet, settings), `net.cpp` (join from the store,
  `secrets.h` as fallback), `main.cpp` (apply inbox each frame; `picks_ver`), `firmware/dock/platformio.ini`
  (`build_src_filter` unchanged; no new lib for httpd/mDNS)

**Interfaces:**
- Consumes: Task 2 (`parseConfig`, `encodeConfig`, `encodeInfo`, `tokenMatches`, `pet::applyColors`).
- Produces:

```cpp
namespace link {
struct Inbox { bool config = false; cta::ConfigMsg cfg; bool reset = false; bool openBle = false; };
void start(const char* hostLabel);        // httpd on port 80 + mDNS; call once Wi-Fi is up
void setInfo(const cta::InfoMsg& info);   // the loop publishes; GET /info serves the latest copy
void setConfig(const cta::ConfigMsg& c);  // what GET /config returns
bool take(Inbox& out);                    // the loop drains writes; false if none
}
namespace store {
bool loadWifi(cta::SavedNet& out); bool saveWifi(const cta::SavedNet& n);
bool loadToken(char out[cta::kTokenLen + 1]); bool saveToken(const char* t);
bool loadPet(cta::PetConfig& out); bool savePet(const cta::PetConfig& p);
bool loadSettings(cta::DeviceSettings& out); bool saveSettings(const cta::DeviceSettings& s);
void wipeLink();   // Wi-Fi, token, bonds (NimBLE store), pet and settings stay
}
```

- [ ] **Step 1:** Implement `link.cpp`: endpoints `GET /info`, `GET /config`, `POST /config`, `POST /ble`,
  `POST /reset`; bodies over `cta::kMaxBody` → 413, bad JSON → 400, missing or wrong token → 401 (no token set
  yet → 401 for every write). Handlers copy into the inbox under a mutex and answer 200 at once.
- [ ] **Step 2:** Wire it: `main.cpp` drains `link::take` every frame and applies `config.stops` through
  `setPicks()` (which now bumps `picksVer`), `config.pet` through `pet::applyColors` on the dock's palette copy plus
  `savePet`, `config.settings` to backlight, `LED_MAX_BRIGHTNESS` scaling, and night hours; `reset` →
  `store::wipeLink()` then `ESP.restart()`. Serial `R` and the hidden reset gesture (three fingers held 10 s
  anywhere on screen) do the same.
- [ ] **Step 3: Verify on the board** (Wi-Fi from `secrets.h`, token set with serial `T <hex>` for now):
  `python3 tools/link_send.py cta-dock-xxxx.local info` shows the fields; `config` with new stops changes the dock
  and its LED rows; a pick on the dock raises `picks_ver`; `pet` recolors Trackie; `--garbage` gives 400/413/401/401.
- [ ] **Step 4: Verify no regression:** `python3 tools/dock_perf_run.py` (3 runs): picker ≥ 34 fps, map ≥ 16 fps,
  lowest internal RAM ≥ 40 KB, no late panel frames.
- [ ] **Step 5: Commit:** `feat(dock): HTTP link on the home network (info, config, reset)`.

### Task 7: Dock BLE setup (Track D)

**Files:**
- Create: `firmware/dock/src/ble_setup.h`, `firmware/dock/src/ble_setup.cpp`
- Modify: `firmware/dock/src/main.cpp`, `dock_ui.cpp` (the passkey and "Joining Wi-Fi…" setup messages),
  `firmware/lib/cta/src/ble_ids.h` (add the token characteristic UUID), `firmware/dock/platformio.ini` (BLE lib if
  Task 1 chose NimBLE-Arduino)

**Interfaces:**
- Consumes: `parseWifi(…, 1, …)`, `parseToken` (Task 2); `store::saveWifi`, `saveToken` (Task 6).
- Produces: `namespace ble_setup { void start(const char* name); void stop(); bool running(); uint32_t passkey(); }`;
  the new UUID `cta::kTokenUuid = "5b1e6f0a-3c2d-4e8f-9a7b-1d2c3e4f5a60"` (mirrored in CTAKit `BLEID.token`).

- [ ] **Step 1:** Implement: advertise `CTA-Dock-xxxx` with the service; `info` (read), `wifi` (encrypted write:
  save, then join), `token` (encrypted write: save). Show the 6-digit passkey on screen while pairing. Run only
  under the Global Constraints rule (no working Wi-Fi, or 120 s after `POST /ble`); `stop()` deinitializes and
  frees memory. After a successful join, start `link`.
- [ ] **Step 2: Verify with LightBlue:** fresh dock (`R`) advertises, passkey pairing works, writing
  `{"slot":0,"ssid":"…","pass":"…"}` joins the home network, writing a token makes `link_send.py config` accepted,
  and BLE stops itself afterwards (serial log shows internal RAM back to the Task 6 level).
- [ ] **Step 3: Commit:** `feat(dock): BLE setup (passkey, Wi-Fi, token), only while unconfigured`.

### Task 8: The wearable on the network (Track H)

**Files:**
- Create: `firmware/handheld/mock/src/link.h`, `firmware/handheld/mock/src/link.cpp`
- Modify: `live.h/.cpp` (pin and follow polled with the close stations; Wi-Fi list from NVS with
  `strongestSaved`), `live_store.*` (pinned/followed records), `main.cpp` (inbox, wake counter, docked, apply pet and
  settings, serial `R` and the three-finger 10 s hold reset)

**Interfaces:**
- Consumes: Task 2 (`parseConfig`, `parseStop`, `parseFollow`, `encodeInfo`, `tokenMatches`, `strongestSaved`,
  `pet::applyColors`), Task 3 (`followView`, `followExpired`), `live::setNearby`.
- Produces:

```cpp
namespace link {   // same shape as the dock's, plus:
struct Inbox { bool config; cta::ConfigMsg cfg; bool nearby; char nearbyJson[cta::kMaxBody + 1]; size_t nearbyLen;
               int pin; cta::PickedStop pinStop; int follow; cta::FollowMsg followMsg;   // +1 set, -1 clear, 0 none
               bool reset; bool openBle; };
}
namespace live {
void setPin(const cta::PickedStop* stop);        // nullptr clears
void setFollow(const cta::FollowMsg* f);         // nullptr clears; also pins f->stop
}
// live::Snapshot gains: bool hasPin; Rec pin; bool hasFollow; cta::FollowView follow;
```

- [ ] **Step 1:** Implement `link.cpp`: endpoints `GET /info`, `GET/POST /config` (pet, settings), `POST /nearby`,
  `POST/DELETE /pin`, `POST/DELETE /follow`, `POST /ble`, `POST /reset`, same error rules as the dock.
- [ ] **Step 2:** Wire `live`: the pinned stop is polled at the 3 closest stations' rate even when not nearby and
  counts as close for Hyped; the follow is evaluated with `followView` after every poll of its stop and cleared by
  `followExpired`. The Wi-Fi join picks `strongestSaved` and, on a drop, rescans skipping the failed slot.
  `info.wake` goes up on every wake (`live::wake()` call site), `info.docked` mirrors `model.docked`.
- [ ] **Step 3: Verify on the board:** `link_send.py cta-pet-xxxx.local nearby '<json>'` fills the cards (same as
  `send_nearby.py`); `pin` then `info`; `follow` with a live `rn` from a current arrival counts down in the serial
  log; `--burst` during a station poll: every request answers within 500 ms; `--garbage` gives 400/413/401/401.
- [ ] **Step 4: Verify power:** `tools/power_run.py` dim, app closed: within 5% of `docs/power/summary.md`.
- [ ] **Step 5: Commit:** `feat(handheld): HTTP link (nearby, pin, follow, config), Wi-Fi list joins the strongest`.

### Task 9: Wearable BLE setup (Track H)

**Files:**
- Create: `firmware/handheld/mock/src/ble_setup.h/.cpp` (same interface as the dock's, Task 7)
- Modify: `main.cpp`, `ui.cpp` setup states only (passkey and "Joining Wi-Fi…" in Trackie's bubble), `platformio.ini`

**Interfaces:**
- Consumes: `parseWifi(…, cta::kWifiSlots, …)`, `parseToken`, Task 8's NVS Wi-Fi list.
- Produces: `CTA-Pet-xxxx` advertising; slots 0–4 written over `wifi`.

- [ ] **Step 1:** Implement as Task 7, with 5 slots and the passkey in Trackie's bubble.
- [ ] **Step 2: Verify with LightBlue:** fresh wearable pairs, two networks written to slots 0 and 1, it joins the
  stronger, BLE stops, `POST /ble` reopens it for 120 s.
- [ ] **Step 3: Commit:** `feat(handheld): BLE setup with up to 5 networks`.

### Task 10: App device discovery and the link client (Track A)

**Files:**
- Create: `ios/CTATracker/Devices/DeviceLink.swift`, `ios/CTATracker/Devices/KnownDevices.swift`,
  `ios/CTATracker/Devices/TokenStore.swift`, `ios/CTATracker/Info.plist` (only `NSBonjourServices`)
- Modify: `ios/CTATracker.xcodeproj/project.pbxproj` (`INFOPLIST_FILE = CTATracker/Info.plist` merged with the
  generated one; `INFOPLIST_KEY_NSLocalNetworkUsageDescription = "Finds your Trackie dock and wearable on this Wi-Fi."`;
  `INFOPLIST_KEY_NSBluetoothAlwaysUsageDescription = "Sets up your Trackie dock and wearable."`)

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
- [ ] **Step 3: Commit:** `feat(ios): find boards on the network and poll them`.

### Task 11: Devices list (Track A)

**Files:** Create `ios/CTATracker/Devices/DeviceList.swift`, `DevicePage.swift`; modify `Settings/SettingsScreen.swift`.

**Interfaces:** Consumes `DeviceLink` (Task 10). Produces the Settings → Devices section and a device page
(online state, last update, Wi-Fi, battery and charging for the wearable, its stops for the dock, Forget).

- [ ] **Step 1:** Build the list and page from `DeviceLink.online` (app spec §6.3 wording; Forget removes it from
  `KnownDevices` and says to finish in iOS Settings → Bluetooth).
- [ ] **Step 2: Verify** against both real boards (token set over serial `T` until Task 12): both online, battery
  shows, Forget works.
- [ ] **Step 3: Commit:** `feat(ios): devices list and device page`.

### Task 12: BLE setup flow (Track A)

**Files:** Create `ios/CTATracker/Devices/BLESetup.swift`, `FoundCard.swift`, `SetupFlow.swift`; modify
`ios/CTAKit/Sources/CTAKit/Config.swift` (`BLEID.token`, the UUID from Task 7) in coordination with Track K.

**Interfaces:**
- Consumes: `WifiMessage`, `TokenMessage`, `Token.random()`, `TokenStore`, `KnownDevices`.
- Produces: `BLESetup.scan()` (foreground only), `BLESetup.setUp(_ peripheral:, wifi: WifiMessage) async throws`;
  the found card and the full-screen flow Pair → Wi-Fi → Pet (first device only) → Ready.

- [ ] **Step 1:** Implement: scan for the service, "Dock found · Set up" / "Trackie found · Set up" card, connect,
  read `info`, write `wifi` (the encrypted write brings up iOS's passkey prompt), write `token` (the household token:
  `TokenStore.token() ?? Token.random()`), then wait up to 20 s for the board on the network before "Ready".
  The Wi-Fi name pre-fills from `NEHotspotNetwork.fetchCurrent` when available (Access Wi-Fi Information
  entitlement), editable; the password field is never logged or kept.
- [ ] **Step 2: Verify:** reset a board (`R`), remove it in iOS Settings → Bluetooth, set it up from the card on the
  home network; both boards end up online with the same token.
- [ ] **Step 3: Commit:** `feat(ios): set up the dock and wearable over Bluetooth`.

### Task 13: One Trackie and device settings (Track A)

**Files:** Create `art/presets.txt`, `ios/CTATracker/Settings/PetSettings.swift`, `DeviceSettingsScreen.swift`;
modify `ios/CTATracker/Design/Pixel.swift` (palette slots 2–3 from the chosen preset), `App/AppModel.swift`.

**Interfaces:** Consumes `ConfigMessage`, `PetConfig`, `DeviceSettings`, `DeviceLink.post`. Produces
`AppModel.pet: PetConfig`, `AppModel.deviceSettings: DeviceSettings`, saved in the App Group; on change and on
`.cameOnline`/`.resendState`, `POST /config` to every online device.

- [ ] **Step 1:** Write `art/presets.txt`: 6 presets, two `0xRRGGBB` each for slots 2 and 3, the first being today's
  blue (`0x2D6BE6 0x7DB4FF`). Add `PetPresetsTests.matchesArtFile` in CTAKit tests (a `Pet.presets` table in CTAKit,
  Track K reviews) that reads `art/presets.txt` and compares.
- [ ] **Step 2:** Build the Pet screen (name, 6 swatches with a 128 pt Trackie preview) and Device settings (screen
  brightness, LED brightness, night hours).
- [ ] **Step 3: Verify:** change the color and name in the app; Trackie changes on the dock and the wearable within
  3 s; reboot the wearable and it comes back recolored.
- [ ] **Step 4: Commit:** `feat(ios): one Trackie and device settings, synced to both boards`.

### Task 14: Dock stops from the app (Track A)

**Files:** Modify `ios/CTATracker/Devices/DevicePage.swift`, `App/AppModel.swift`; create
`ios/CTATracker/Devices/DockStopPicker.swift`.

**Interfaces:** Consumes `ConfigStop`, `GET/POST /config`, `.fetchConfig`. Produces the dock page's 3 sections; tapping
one opens the search picker; a chosen card sends all 3 sections (platform IDs for trains).

- [ ] **Step 1:** Build it; on `.fetchConfig` read `GET /config` and refresh the sections.
- [ ] **Step 2: Verify:** pick in the app → dock section and LED row change; pick on the dock → the app's page shows
  it within 3 s.
- [ ] **Step 3: Commit:** `feat(ios): pick the dock's stops from the app, and see picks made on the dock`.

### Task 15: Nearby and the mirror (Track A)

**Files:** Modify `ios/CTATracker/App/AppModel.swift`, `App/LocationService.swift`.

**Interfaces:** Consumes `NearbyBuilder`, `NearbyThrottle` (Task 5), `.sendNearby`, `.cameOnline`, `DeviceLink.post`.

- [ ] **Step 1:** On `.sendNearby` or `.cameOnline`: while the map is at the user's location, send
  `fromLocation`; while panned, `fromList(slots, center)`. When the list settles after a pan, offer
  `fromList` to the throttle; a timer calls `flush`. The recenter button sends `fromLocation`.
- [ ] **Step 2: Verify:** wake the wearable → nearby stops fill (no Mac script); pan the map to the Loop → within
  10 s the wearable shows the phone's Loop stops; recenter → back to home stops.
- [ ] **Step 3: Commit:** `feat(ios): nearby on the wrist, mirrored from the map`.

### Task 16: Send to Trackie and Follow (Track A)

**Files:** Modify `ios/CTATracker/Stop/StopDetail.swift` (`StarButton` row and `DepartureTile`), `App/AppModel.swift`.

**Interfaces:** Consumes `ConfigStop`, `FollowMessage`, `Prediction.vehicle`, `.resendState`. Produces
`AppModel.pinned: ConfigStop?`, `AppModel.followed: FollowMessage?`, re-sent on `.resendState`.

- [ ] **Step 1:** "Send to Trackie" next to the ★ (only while the wearable is online) posts `/pin` with the station
  ID for trains (`Slot.fetchID`) and the card's direction; a long press on a departure tile with a `vehicle`
  shows "Follow on Trackie" and posts `/follow`. Tapping elsewhere sends nothing.
- [ ] **Step 2: Verify:** pin Belmont Red → the wearable's Home card is Belmont with ★ (after Task 18); follow the
  next train → the wearable counts down to it; reboot the wearable → pin and follow come back.
- [ ] **Step 3: Commit:** `feat(ios): send a stop to Trackie and follow a departure`.

### Task 17: Find my handheld (Track A)

**Files:** Create `ios/CTATracker/Settings/FindHandheld.swift`; modify `App/AppModel.swift`.

**Interfaces:** Consumes `.wentOffline` for the wearable. Produces `AppModel.lastSeen: (CLLocationCoordinate2D, Date)?`
in the App Group.

- [ ] **Step 1:** On `.wentOffline` take one location fix and save it; the screen shows the pin and time, or
  "Connected now".
- [ ] **Step 2: Verify:** power the wearable off → within 6 s the screen shows the pin and the time.
- [ ] **Step 3: Commit:** `feat(ios): find my handheld`.

### Task 18: The pinned and followed card on the wearable (Track H-UI)

**Files:** Modify `firmware/handheld/mock/src/ui.cpp/.h`, `docs/handheld-ui.md`.

**Interfaces:** Consumes `live::Snapshot::hasPin`, `pin`, `hasFollow`, `follow` (Task 8) and `cta::FollowState`
(Task 3). Until Task 8 merges, drive it from the mock build (`mock_data.cpp` sets a pin and a follow).

- [ ] **Step 1:** Home card: a pinned stop takes the card with a ★ before the stop name; a follow shows
  "Red · 4 min · to 95th", then "Due", then "Arrived" or "Lost the train" (bus: "Lost the bus") for 60 s.
  A swipe left on the pinned card clears it (calls `live::setPin(nullptr)`).
- [ ] **Step 2: Verify** in the mock build on the board and in `tools/preview.cpp` renders; update
  `docs/handheld-ui.md`.
- [ ] **Step 3: Commit:** `feat(handheld): pinned stop and follow countdown on Home`.

### Task 19: Relay with no Wi-Fi (Tracks H and A; last, drops first)

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

### Task 20: Extras, only if showcase steps 1–8 pass twice (Tracks D, H, A)

- [ ] **The dock follows your train:** dock `POST /trip` (the follow body plus the pet name, parsed by `parseFollow`
  plus `"pet"`); the dock draws Trackie on that vehicle on its map with "Trackie · Red Line · at Fullerton";
  waiting at the stop before the vehicle reaches it; ends on `DELETE /trip` (sent by the wearable when its follow
  ends), at the end of the line, when the vehicle drops out, or after 90 minutes.
- [ ] **Trackie hops:** the wearable `POST /event {"docked":true|false}` to the dock; the dock plays a hop-in on its
  status strip as the wearable falls asleep, and the reverse.
- [ ] Each gets its own verify-on-hardware step and commit.

### Task 21: Docs and the demo script (Track X)

**Files:** Modify base spec §8.3, §9.5, §10 (point to the ecosystem spec), `firmware/handheld/mock/README.md`,
`firmware/dock/` README section; create `docs/ecosystem/demo-script.md` (showcase steps 1–8 with the exact taps, the
reset-and-forget-in-iOS step before pairing, and what to do if a step fails; no network names).

- [ ] **Step 1:** Write them. Check `git diff` for network names, home paths, tokens.
- [ ] **Step 2: Verify:** run the script end to end on the home network, then on campus (11/22), twice each.
- [ ] **Step 3: Commit:** `docs: ecosystem demo script, base spec points to the ecosystem design`.

---

## Appendix A: if Task 1 says Bluetooth doesn't fit

Pairing moves to the network. Wi-Fi comes from `secrets.h` (both networks compiled in). A fresh board shows a 6-digit
code on screen and accepts `POST /pair {"code":"123456","token":"<hex>"}` once, within 5 minutes of boot, with the
right code (constant-time compare; 5 wrong tries lock it until reboot). Tasks 7 and 9 become this endpoint plus the
code on screen; Task 12's flow becomes Pair (type the code shown) → Pet → Ready, found by Bonjour instead of BLE;
Task 19 is dropped. Everything else is unchanged.
