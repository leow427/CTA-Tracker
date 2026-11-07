# CTA Tracker Handheld Firmware Plan (Plan 3 of 4)

> **Reference plan, not a script.** This lays out milestones, module boundaries, and the decisions made so far. Expect it to change as the hardware and libraries teach us things. When the code shows a better way, take it, say so, and update this file in the same commit. The spec is the contract. Execute natively, one milestone at a time, following `docs/Agent-Workflow.md`.
>
> **The UI is subject to change, and likely will.** Screen layouts, sizes, colors, fonts, copy, and pet art here are starting points, not final designs. Build them so they're easy to change; don't treat a UI detail in this plan as fixed.

**Goal:** Handheld firmware for the Waveshare 1.75" round AMOLED: the pet on a dim always-on home screen, Trains and Buses tabs of nearby stops with a detail view, location and data from the app over BLE, its own polling on Wi-Fi, and sleeping when docked.

**Spec:** `docs/superpowers/specs/2026-11-03-cta-tracker-design.md` (sections 3, 6, 8, 10, 12)

**Depends on Plan 1:** toolchain notes, the `cta` library (parsers, `stopMood`, `deviceMood`, stop record codec, URL builders, `cta_config.h`, `ble_ids.h`), `cta_net::ctaGet` (Plan 1 Task 9b, built right after Task 1), and the `pet` library.

**Depends on Plan 2:** `device_config` (DeviceConfig, `applyConfigJson`, `encodeInfoJson`, `decodeWifiJson`) and `scheduler`. If the handheld is built first, move those two shared pieces here.

## Module map (expected; adjust freely)

```
firmware/handheld/platformio.ini     board settings from toolchain notes; lib_extra_dirs = ../lib; -I..
firmware/handheld/src/main.cpp       setup/loop wiring only
firmware/handheld/src/display.*      QSPI AMOLED + CST9217 touch + LVGL (from the Waveshare examples)
firmware/handheld/src/power.*        AXP2101 via XPowersLib: USB present, battery %, charging
firmware/handheld/src/screen.*       brightness states: full ↔ dim after kScreenDimSec; tap = wake event
firmware/handheld/src/ui_kit.*       black background, line-colored pixel cards, 5×7 pixel digits, pixel corners
firmware/handheld/src/ui_home.*      time + weather, Trackie (32×32 at 4×) + speech bubble, the most urgent card; swipe up → stops
firmware/handheld/src/ui_stops.*     Trains | Buses tab pill, card list (train cards per line and direction, bus cards per route)
firmware/handheld/src/ui_detail.*    next 3–4 arrivals per line and direction at one stop
firmware/handheld/src/net.*          up to 3 saved networks, connect/backoff, NTP + time zone
firmware/handheld/src/poller.*       Wi-Fi fetch cadence for the nearby list
firmware/handheld/src/data.*         HandheldState: nearby list, records, source (Wi-Fi or BLE), freshness
firmware/handheld/src/location.*     LocationSource interface; BlePhoneLocation is the only alpha impl
firmware/handheld/src/ble_server.*   info, wifi (slots 0–2), config, request (notify), nearby, snapshot
firmware/handheld/src/store.*        NVS: Wi-Fi ×3, DeviceConfig, last nearby list
firmware/lib/cta/src/ble_messages.*  NEW shared: request encode, nearby decode (native-tested)
firmware/lib/cta/src/train_cards.*   NEW shared: split one station response into card records (native-tested)
```

## Shared additions (native-tested)

**`ble_messages.h`**

```cpp
struct NearbyList {
  double lat, lon; bool hasLocation;
  char trains[10][8]; uint8_t nTrains;    // station IDs (4xxxx), nearest first
  char buses[10][8];  uint8_t nBuses;     // bus stop IDs, nearest first
  char close[3][8];   uint8_t nClose;     // the 3 closest of either kind (spec §10)
};
bool decodeNearby(const char* json, size_t len, NearbyList& out);   // >10 per tab → keep first 10
size_t encodeRequest(bool wifiUp, char* buf, size_t cap);           // {"want":"nearby","wifi":<bool>}
```

Tests to include: 12 trains → 10 kept; a missing `close` → `nClose = 0`; malformed JSON → false; the encoded request matches the spec's shape.

**`train_cards.h`** (spec §6.2, train card records)

```cpp
// One record per (platform stpId, rt) in a ttarrivals?mapid= response, soonest card first.
// id = stationId, route = rt, dir = the platform's dirLabel from kTrainLines (falls back to the
// soonest arrival's destNm if the platform isn't in any sequence), up to 4 arrivals each.
int parseTrainCards(const char* json, size_t len, uint32_t now, const char* stationId,
                    StopRecord* out, size_t max);   // returns cards written, −1 on error
```

Tests to include: `fixtures/cta/train_arrivals_station.json` (Belmont) gives 4 cards: Brown toward Kimball (1 min) first, then Red toward 95th/Dan Ryan (3, 7 min), Red toward Howard (12), and Purple toward Linden (20); every card's `id` is `41320`; `max = 2` keeps the 2 soonest; the error fixture returns −1. Reuse Task 4's per-eta parsing rather than parsing twice.

**Fixture fix first:** the synthetic Belmont fixture gives its 95th-bound Red trains `stpId` 30255 and its Howard-bound one 30256, but in GTFS (`data/stops.json`) 30255 is the Howard platform and 30256 the 95th/Dan Ryan one. Swap them in the fixture, and update the stop ID order the iOS `TrainParseTests` expects to match, in the same commit; otherwise the labels from `kTrainLines` contradict each arrival's `destNm`. Ask Leo before changing the shared fixture.

## Milestones

1. **Screen, touch, power.** Port the display, touch, and AXP2101 code from the spike. The home screen shows a placeholder pet and the time. Serial logs USB present and battery % every 15 min (this is also spec §8.5's battery log). *Check (Leo):* the round screen renders, touch works, and USB plug/unplug shows up in the log.
2. **Brightness and wake.** `screen.*` dims after `kScreenDimSec` with no touch, drops the animation to a few fps, and returns to full on tap. Each dim → full transition raises one wake event. The dim level is a constant so it can be tuned against the battery test.
3. **BLE server and Wi-Fi setup.**
   - NimBLE service with all six characteristics, advertising as `CTA-Pet-xxxx`, with the passkey shown during pairing.
   - `wifi` accepts slots 0–2; slot 0 is Home.
   - `net.*` tries saved networks in slot order.
   - With no nearby list yet, the home screen says "Open the CTA app".
4. **Nearby flow.**
   - On wake, if a phone is connected, notify `request` with the Wi-Fi state.
   - A `nearby` write is decoded, saved to NVS, and replaces the list.
   - With no phone connected, keep the last list.
   - The location lives behind `LocationSource`, so a GPS module can be a second source later.
5. **Polling on Wi-Fi.**
   - The first 3 train stations are polled every `kHandheldClosePollSec`, the rest every `kHandheldFarPollSec`, one `ttarrivals?mapid=` each.
   - All bus stops go in one `getpredictions` call every `kHandheldBusPollSec`, parsed with `max` sized for the worst case: one card per route and direction, up to about 80 across 10 downtown stops (State & Lake alone is ~60), about 16 KB of records, so in PSRAM. Then order the cards by the nearby list (nearest stop first), since a multi-stop response comes in time order. Add the cap as a `cta_config.h` constant (`kMaxBusCards`) and propose it as a spec value.
   - Alerts every 5 min for the routes seen in the records.
   - Weather every 15 min at the phone's last location.
   - Night hours or docked: everything every `kSlowPollSec`.
   - Each station's response becomes card records through `parseTrainCards` (spec §6.2).
6. **Stops and detail screens** (spec §8.1; the mockup in `docs/ideas/` shows the look).
   - `ui_kit.*` first: black background, line-colored cards with the app's `LineColors.card`/`cardText` math, pixel corners, the 5×7 pixel digits as a bitmap font at whole scales.
   - Tabs and a card list suited to a round screen: center-weighted, cards away from the focus shrink, fade, and narrow inside the circle.
   - Trains: cards for the 10 nearest stations, nearest station first, soonest first within a station. Buses: one card per route and direction at each stop (Plan 1 Task 5 records), soonest first within a stop.
   - Every card shows where it ends (`arrivals[0].dest`) and the direction it goes (`dir`: a bus's "Southbound", a train's platform label shown as "toward Loop"), plus the stop name and next minutes (spec §8.1).
   - Tap a card for detail (the stop's lines and directions, tapped one first); swipe right to go back; swipe down returns home.
   - Stale cards grey out.
7. **BLE relay intake.** `snapshot` writes decode into the record with the matching (`id`, `route`, `dir`); the app sends one per card, so a downtown relay burst is up to about 80 bus snapshots plus the train cards a minute. Size the loop's snapshot queue for a full burst, or have the queue keep only the newest write per (`id`, `route`, `dir`). For each record the newer `ts` wins, whether it came from Wi-Fi or BLE. With no Wi-Fi and no snapshots, the data ages out and the pet turns Sleepy after `kStaleSec`.
8. **Pet and moods.**
   - The home pet is Trackie (Plan 1 Task 11's sprites, 4×, straight on black with no tile) and shows `deviceMood` over all cards, with `hypedEligible` set for cards whose `id` is in `close`.
   - The speech bubble shows the mood word; a tap on Trackie shows the reason for a few seconds (spec §8.1 copy). Home also shows the most urgent card.
   - Dim always-on: black with Trackie alone, shifted a few pixels every 10 s.
   - Weather accessory from Open-Meteo, drawn with `pet::overlayFor(state, accessoryFor(...))`, which draws nothing while Asleep.
   - `petStateFor(mood, docked)`, where docked means USB present **and** connected to the slot 0 SSID.
   - Asleep while docked, with the screen extra-dim and slow polling.
   - The two-finger long-press debug cycle.
   - Palette from `cfg.petColors`.
9. **Battery test and demo check.** Full charge, off the dock, dim always-on, normal app use. Log until shutdown and record the hours in `docs/notes/battery.md`. Walk the handheld items of the spec §1 checklist (5–8).
10. **Stretch, after the core path works: the map screen** (spec §2). Swipe left from Home. Draw the L lines from `data/train_shapes.json` built into the firmware (15 KB), in whole pixels at half resolution, doubled; nearby stops, live trains, recenter, tap through to detail. Needs BLE additions to spec §10 first: the nearby stops' coordinates in `nearby` (about 160 bytes) and a train-positions message (about 10 bytes a train, every 60 s). The mockup has the details.

## Decisions so far

- **The handheld never needs CTA's full stop table.** The app sends IDs, and names arrive with the predictions. Until the first fetch, cards show "Loading…".
- **Bus cards (Leo, 2026-11-07):** one card per route at a stop, like train cards; `parseBusPredictions` makes one record per (stop, route, direction), so the few stops served both ways get a card per direction. Every card shows where it ends and its direction.
- **Look (Leo, 2026-11-06):** the app's pixel style on black, not navy; Trackie is drawn straight on black, with no tile; his bubble shows the mood word and the reason on tap; train cards are one per line and direction at each station; the map is a stretch goal.
- **One state object** (`HandheldState`) is written only on the loop task. BLE callbacks copy the payload into a small queue for the loop.
- **Keep internal RAM free** for Wi-Fi, TLS, and BLE; put LVGL buffers and response buffers in PSRAM.

## Open questions

- **Dim level and frame rate** that still look alive on AMOLED: tune against the battery test.
- **Raise-to-wake** with the QMI8658 IMU: stretch.
- **Wake vs. phone not connected:** keep the old list (current plan). Should cards show their age?
- **GPS module** as a second `LocationSource`: stretch; the interface is ready for it.
