# CTA Tracker Handheld Firmware Plan (Plan 3 of 4)

> **Reference plan, not a script.** This lays out milestones, module boundaries, and the decisions made so far. Expect it to change as the hardware and libraries teach us things. When the code shows a better way, take it, say so, and update this file in the same commit. The spec is the contract. Execute natively, one milestone at a time, following `docs/Agent-Workflow.md`.

**Goal:** Handheld firmware for the Waveshare 1.75" round AMOLED: the pet on a dim always-on home screen, Trains and Buses tabs of nearby stops with a detail view, location and data from the app over BLE, its own polling on Wi-Fi, and sleeping when docked.

**Spec:** `docs/superpowers/specs/2026-11-03-cta-tracker-design.md` (sections 3, 6, 8, 10, 12)

**Depends on Plan 1:** toolchain notes, the `cta` library (parsers, `stopMood`, `deviceMood`, stop record codec, URL builders, `cta_config.h`, `ble_ids.h`), `cta_net::ctaGet`, and the `pet` library.

**Depends on Plan 2:** `device_config` (DeviceConfig, `applyConfigJson`, `encodeInfoJson`, `decodeWifiJson`) and `scheduler`. If the handheld is built first, move those two shared pieces here.

## Module map (expected; adjust freely)

```
firmware/handheld/platformio.ini     board settings from toolchain notes; lib_extra_dirs = ../lib; -I..
firmware/handheld/src/main.cpp       setup/loop wiring only
firmware/handheld/src/display.*      QSPI AMOLED + CST9217 touch + LVGL (from the Waveshare examples)
firmware/handheld/src/power.*        AXP2101 via XPowersLib: USB present, battery %, charging
firmware/handheld/src/screen.*       brightness states: full ↔ dim after kScreenDimSec; tap = wake event
firmware/handheld/src/ui_home.*      pet (5× = 160 px), time, weather icon; swipe up → stops
firmware/handheld/src/ui_stops.*     Trains | Buses tab pill, list of up to 10 per tab
firmware/handheld/src/ui_detail.*    next 3–4 arrivals per line/route at one stop
firmware/handheld/src/net.*          up to 3 saved networks, connect/backoff, NTP + time zone
firmware/handheld/src/poller.*       Wi-Fi fetch cadence for the nearby list
firmware/handheld/src/data.*         HandheldState: nearby list, records, source (Wi-Fi or BLE), freshness
firmware/handheld/src/location.*     LocationSource interface; BlePhoneLocation is the only alpha impl
firmware/handheld/src/ble_server.*   info, wifi (slots 0–2), config, request (notify), nearby, snapshot
firmware/handheld/src/store.*        NVS: Wi-Fi ×3, DeviceConfig, last nearby list
firmware/lib/cta/src/ble_messages.*  NEW shared: request encode, nearby decode (native-tested)
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
   - All bus stops go in one `getpredictions` call every `kHandheldBusPollSec`.
   - Alerts every 5 min for the routes seen in the records.
   - Weather every 15 min at the phone's last location.
   - Night hours or docked: everything every `kSlowPollSec`.
   - Records carry `dir = ""` for train stations (spec §6.2).
6. **Stops and detail screens.**
   - Tabs, rows (line-color dot, name, next arrival minutes), and scrolling suited to a round screen (center-weighted list, rows fade at the edges).
   - Tap a row for detail; swipe right to go back; swipe down returns home.
   - Stale rows grey out.
7. **BLE relay intake.** `snapshot` writes decode into the record with the matching `id`. For each record the newer `ts` wins, whether it came from Wi-Fi or BLE. With no Wi-Fi and no snapshots, the data ages out and the pet turns Sleepy after `kStaleSec`.
8. **Pet and moods.**
   - The home pet shows `deviceMood` over all listed stops, with `hypedEligible` set for the stops in `close`.
   - Weather accessory from Open-Meteo.
   - `petStateFor(mood, docked)`, where docked means USB present **and** connected to the slot 0 SSID.
   - Asleep while docked, with the screen extra-dim and slow polling.
   - The two-finger long-press debug cycle.
   - Palette from `cfg.petColors`.
9. **Battery test and demo check.** Full charge, off the dock, dim always-on, normal app use. Log until shutdown and record the hours in `docs/notes/battery.md`. Walk the handheld items of the spec §1 checklist (5–8).

## Decisions so far

- **The handheld never needs CTA's full stop table.** The app sends IDs, and names arrive with the predictions. Until the first fetch, rows show "Loading…".
- **One state object** (`HandheldState`) is written only on the loop task. BLE callbacks copy the payload into a small queue for the loop.
- **Keep internal RAM free** for Wi-Fi, TLS, and BLE; put LVGL buffers and response buffers in PSRAM.

## Open questions

- **Dim level and frame rate** that still look alive on AMOLED: tune against the battery test.
- **Raise-to-wake** with the QMI8658 IMU: stretch.
- **Wake vs. phone not connected:** keep the old list (current plan). Should rows show their age?
- **GPS module** as a second `LocationSource`: stretch; the interface is ready for it.
