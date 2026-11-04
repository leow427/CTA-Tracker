# CTA Tracker: design spec

- **Date:** 2026-11-03
- **Owner:** Leo
- **Due:** 2026-11-23 (class final project demo)
- **Status:** Draft for review
- **Repo:** https://github.com/leow427/CTA-Tracker

This is the base design for the CTA Tracker: a desk dock, a handheld "pet," and a companion iPhone app that show live Chicago Transit Authority (CTA) bus and train arrivals. It replaces the older "Transit dock design notes" (kept in `docs/archive/` for history) wherever the two disagree.

## 1. Goals and demo success

All three pieces must work end to end by 11/23. Alpha quality is acceptable: rough edges are fine, broken flows are not.

The demo passes when all of these work:

1. The dock shows live arrivals for 3 picked stops, including at least one train stop and one bus stop.
2. Each dock LED row lights where real vehicles are approaching that stop.
3. A stop can be picked both on the dock's touchscreen and from the app.
4. A fresh dock and handheld can be put on Wi-Fi from the app.
5. With the handheld on Wi-Fi, a touch wakes it, it asks the app for its location, and the Trains and Buses tabs fill with nearby stops. Tapping a stop opens its detail view.
6. The pet's mood follows the rules in section 6.3, and a debug gesture cycles through every mood (real delays can't be scheduled for a demo).
7. Putting the handheld on the dock makes the pet fall asleep.
8. With the handheld's Wi-Fi turned off, the app relays arrivals to it over BLE.
9. The app shows the map with route cards, stop detail, favorites, a widget, pet name and color sync, device settings, and "find my handheld."

## 2. Scope

| Alpha (due 11/23) | Stretch | Cut |
|---|---|---|
| Dock: 3 stop sections, LED approach rows, pet, stop picker | Live Activity "leave now" countdown | Partner sharing / relay server |
| Handheld: pet, nearby Trains/Buses tabs, stop detail | Pet journal (weekly moods, "trains caught") | Tamagotchi care (feeding, neglect) |
| App: map + route cards, stop detail, favorites | Siri / Shortcuts ("When's my train?") | Countdown or approach view on the handheld |
| App: Wi-Fi setup, dock stops, BLE relay | Firmware updates over BLE | Any backend server |
| App extras: pet name + colors, device settings, widgets, find my handheld | Live navigation | Dead-front OLED and custom ESP32 PCB from the old notes |
| | GPS module on the handheld as a backup location source | Device-hosted Wi-Fi setup page |
| | Phone-hotspot fallback for the handheld | |
| | Raise-to-wake on the handheld (IMU) | |
| | 3 handhelds paired at once | |

The app extras are built after the core path works (dock live, handheld live, app setup and relay). If time runs short they slip first.

No feature requires a login or an account. All data stays on the phone and the devices.

## 3. Hardware baseline (fixed)

The hardware is decided. This section only lists what the software depends on.

**Dock**

- Board: Waveshare ESP32-S3-Touch-LCD-4.3 (800×480, 5-point capacitive touch). Used in portrait (480×800).
- Route board: 3 rows × 7 NeoPixels = 21 LEDs, driven from one free GPIO through an SN74AHCT125 level shifter. The pin is defined as `LED_DATA_PIN` in `firmware/dock/include/pins.h` once wired.
- Power: 5 V ≥3 A from a USB-C PD module. LED brightness is capped in firmware by one constant (`LED_MAX_BRIGHTNESS`).
- 3 upright USB-C plugs charge handhelds. They carry VBUS and GND only, so no data passes through the dock.
- Front panel: laser-cut acrylic at 60°, in a 3D-printed body.

**Handheld (one built for the demo)**

- Board: Waveshare ESP32-S3-Touch-AMOLED-1.75 (466×466 round AMOLED, CST9217 touch, AXP2101 power chip, QMI8658 IMU).
- Battery: 3.7 V 2000 mAh LiPo on the MX1.25 socket.
- The AXP2101 reports USB power, which the firmware uses to detect charging.
- Custom 3D-printed shell.

## 4. Architecture

There is no server. Each of the three clients calls CTA and Open-Meteo directly over HTTPS. The app talks to both ESP32s over BLE.

```
     CTA Train Tracker · CTA Bus Tracker · CTA Customer Alerts · Open-Meteo
          ▲ HTTPS                 ▲ HTTPS                  ▲ HTTPS
          │                       │                        │
     ┌────┴────┐  BLE (setup)  ┌──┴────────┐  BLE (setup,  ┌────┴─────┐
     │  Dock   │◄─────────────►│ iPhone app│◄─────────────►│ Handheld │
     │ (4.3")  │               │ (SwiftUI) │  nearby list, │ (1.75")  │
     └─────────┘               └───────────┘  relay)       └──────────┘
```

- **Dock:** stores its 3 picked stops in flash, polls their arrivals and vehicle positions, and drives the screen and LED rows.
- **Handheld:** holds a list of nearby stop IDs sent by the app, polls their arrivals on Wi-Fi, and accepts the same data from the app over BLE when it has no Wi-Fi.
- **App:** shows live arrivals, finds nearby stops from a bundled stop list, and acts as the BLE central for setup, the nearby list, and the relay.

The dock and handheld share one C++ library (`firmware/lib/cta`) for the CTA clients, parsing, mood rules, approach-row logic, and the stop record. The app reimplements the same logic in Swift. Shared test fixtures (section 13) keep the two in agreement.

**Why no server:** Leo chose direct calls over a recommended small backend. The trade-offs accepted:

- The transit logic exists twice (C++ and Swift) instead of once.
- API keys live on every device.
- Every device's polling counts against CTA's daily request limits, so the poll intervals in section 6.6 are tuned for that.

## 5. Data sources

| Source | Key | Used for |
|---|---|---|
| CTA Train Tracker API | Train Tracker key | `ttarrivals` (arrivals by station or platform), `ttpositions` (train positions by line) |
| CTA Bus Tracker API | Bus Tracker key | `getpredictions` (several stops per call), `getvehicles` (positions by route), `getpatterns` (ordered stops with distance along route), `getroutes` / `getdirections` / `getstops` (dock stop picker) |
| CTA Customer Alerts API | none | Active alerts per route (the ⚠ badge, the Sad mood) |
| Open-Meteo | none | Current weather code and temperature for pet accessories |
| CTA GTFS static feed | none | Build-time only: stop list for the app, station order per train line and direction for the dock |

**Keys:** Leo has no CTA keys yet. Applying for both is task one. Until they arrive, parsing is developed against sample responses from CTA's API docs. Real responses get recorded into `fixtures/cta/` as soon as keys work.

**Build-time data** (`tools/gtfs_build.py`):

- `stops.json` for the app: every bus stop and train platform with ID, name, lat/lon, routes, and direction. Train platforms carry their parent station ID.
- `train_sequences.h` for the dock: for each train line and direction, the ordered list of platform IDs. Where a line has branches or runs the Loop, use the most common trip pattern.
- The script checks that GTFS bus stop IDs match Bus Tracker stop IDs, and fails loudly if a sample doesn't match.

## 6. Shared concepts

### 6.1 Picked stop

A picked stop is always a stop **plus direction**: a train platform ID or a direction-specific bus stop ID, with its route. Example: Red Line, Belmont, toward 95th/Dan Ryan.

### 6.2 Stop record

The one data shape every device uses for "what's happening at a stop," whether it was fetched over Wi-Fi or relayed over BLE (values below are illustrative):

```json
{
  "id": "30255",
  "type": "train",
  "route": "Red",
  "dir": "95th/Dan Ryan",
  "name": "Belmont",
  "arrivals": [
    {"min": 3, "dest": "95th/Dan Ryan", "live": true, "delayed": false}
  ],
  "alert": false,
  "ts": 1763000000
}
```

- At most 4 arrivals per record. Names are truncated to 24 characters so a record fits in one 512-byte BLE write.
- `live: false` means a timetable time, not a live prediction (shown as a clock time in the app and dimmed with a "Sched" badge on the devices).
- `ts` is Unix time of the fetch, used to judge staleness.
- A train *station* record (the handheld's train rows) has an empty `dir` and arrivals for both directions; each arrival's `dest` tells them apart.

### 6.3 Pet moods

Each stop gets a mood from its next arrival:

| Stop mood | Condition |
|---|---|
| Sleepy | No data, or data older than 5 min, or within night hours |
| Sad | Active alert on the route, or no predictions outside night hours |
| Worried | Next arrival flagged delayed by CTA |
| Happy | Live prediction, not delayed |

The device mood combines the stop moods:

1. **Asleep** (separate state, not a mood): the handheld is docked (section 8.4). The dock never sleeps.
2. **Sleepy:** no location yet (handheld), no data at all, or within night hours.
3. **Hyped:** a live arrival is due in 3 min or less at a *qualifying* stop. On the dock, any of its 3 stops qualifies. On the handheld, only the 3 closest stops (either tab) qualify, as listed in the `nearby` message's `close` field; with about 20 stops nearby, some vehicle is almost always 3 min away, so counting all of them would make the pet Hyped nonstop.
4. **Otherwise:** the majority stop mood among stops that have data. Ties go to the more serious mood: Sad, then Worried, then Happy.

**Weather** changes the pet's look, not its mood. Open-Meteo WMO codes 51–67, 80–82, and 95–99 (rain, showers, thunder) add an umbrella. Codes 71–77 and 85–86 (snow), or a temperature below 0 °C, add a scarf.

**Debug:** a long-press with two fingers on the pet cycles through every mood and state, for testing and the demo.

### 6.4 Approach row (dock LEDs)

Each picked stop has a row of 7 LEDs. LEDs 1–6 are the 6 stops before the picked stop in its direction, farthest first. LED 7 is the picked stop.

- **Trains:** the 6 preceding platform IDs come from `train_sequences.h`. `ttpositions` for the line gives each train's next stop. A train lights the LED of its next stop if that stop is in the row.
- **Buses:** `getpatterns` for the route gives the ordered stops with distance along the route. For each pattern that contains the picked stop, the 6 preceding stops are taken. `getvehicles` gives each bus's pattern and distance. A bus lights the LED of the next stop ahead of it. Patterns are cached until the stop is changed.
- **Color:** the line's CTA color for trains and a single bus color for buses.
- **LED 7** pulses while a vehicle is due in 3 min or less.
- **Staleness:** all LEDs in a row turn off when its data is more than 2 min old.

### 6.5 Line colors

The CTA's published line colors are used for train cards, LEDs, and badges: Red, Blue, Brown, Green, Orange, Pink, Purple, Yellow. All buses share one color. The values live in one table in the shared library and its Swift mirror.

### 6.6 Polling and time

| What | Dock | Handheld (on Wi-Fi) |
|---|---|---|
| Arrivals | every 30 s | 3 closest train stations every 60 s; other train stations every 3 min; all bus stops in one batched call every 60 s |
| Vehicle positions | every 30 s | not used |
| Alerts | every 5 min | every 5 min |
| Weather | every 15 min | every 15 min |
| Night hours or docked | everything every 5 min | everything every 5 min |

- Intervals are constants in the shared library, so they can be tuned once the daily limits from the key emails are known. If the Bus Tracker limit is tight, dock bus polling moves to 60 s first.
- Night hours default to 1:00–5:00 and can be changed from the app.
- Both ESP32s get time over NTP with the Chicago time zone (`CST6CDT,M3.2.0,M11.1.0`).

## 7. Dock

### 7.1 Screen (portrait, 480×800)

- **Status strip (top, ~80 px):** pet, time, weather icon, Wi-Fi icon.
- **Three stop sections (~240 px each):** section N matches LED row N. Each shows:
  - a bar in the line's color, stop name, and direction ("Belmont → 95th")
  - the next 3 arrivals in minutes with destinations
  - badges: amber **Delayed**, dimmed **Sched**, and ⚠ for an active alert
- An empty section shows "Tap to pick a stop."

### 7.2 Stop picker

Long-press a section, then choose Train or Bus → line or route → direction → stop from a scrolling list.

- Train lines and stations come from the bundled tables.
- Bus routes, directions, and stops come live from Bus Tracker.
- The pick is saved in flash. Picking in the app writes to the same place over BLE, and the most recent pick wins.

### 7.3 Pet

Shown in the status strip at 64 px, using the mood rules in 6.3 over the 3 picked stops.

### 7.4 Offline and setup

- **Wi-Fi lost:** the Wi-Fi icon turns red, arrivals stay on screen greyed out with "updated N min ago," and the LEDs go dark after 2 min.
- **No saved Wi-Fi:** the screen shows "Open the CTA app to set up Wi-Fi."
- **No API key compiled in:** the screen shows "No API key."
- **Night hours:** the screen dims and the LEDs turn off.

## 8. Handheld

### 8.1 Screens (round, 466×466)

- **Home:** the pet centered on black, with the time and weather icon above it. Swipe up for stops.
- **Stops:** a Trains | Buses tab pill at the top, then a scrolling list of up to 10 stops per tab, sorted by distance. Each row shows a line-color dot, name and direction, and the next arrival in minutes. Train rows are per station (both directions); bus rows are per direction-specific stop. Swipe down to go back to Home.
- **Stop detail:** tap a row to see the next 3–4 arrivals per line or route, with the same badges as the dock. Swipe right to go back.

### 8.2 Screen behavior

The screen is always on, dimmed. After 15 s with no touch it drops to low brightness and the animation slows to a few frames per second. A tap returns it to full brightness and counts as a **wake**.

### 8.3 Location and data

1. On every wake, the handheld sends a `request` to the app over BLE with whether it has Wi-Fi.
2. The app takes one location fix and writes `nearby`: the location, the 10 nearest train station IDs, the 10 nearest bus stop IDs, and `close`, the 3 nearest stops of either kind. The handheld can't rank stations against bus stops itself because it has no stop coordinates.
3. **On Wi-Fi:** the handheld polls those stops itself (section 6.6).
4. **Off Wi-Fi:** the app also fetches arrivals and writes one `snapshot` per stop every 60 s while connected.
5. **Neither:** the last data stays on screen greyed out, and the pet turns Sleepy once data is more than 5 min old.

The handheld keeps its last nearby list in flash. A brand-new handheld shows "Open the CTA app" until it receives its first list.

The handheld saves up to 3 Wi-Fi networks, all set from the app.

### 8.4 Docked

Docked means the AXP2101 reports USB power **and** the handheld is on its first saved network (home). While docked, the pet is Asleep, the screen dims further, and polling slows to every 5 min.

### 8.5 Battery

Target: about one day off the dock with the dim always-on screen. The firmware logs battery percentage every 15 min to the serial port. A full-discharge test before the demo measures the real number.

### 8.6 Location source interface

The firmware reads location through one interface with a single alpha implementation: the phone over BLE. A GPS module (Leo owns one) is the stretch second source.

## 9. iPhone app

SwiftUI, iOS 17 or later (needed for MapKit in SwiftUI). No login or account.

### 9.1 Main screen

The layout follows the Transit app pattern Leo shared, with CTA colors and the pet instead of that app's branding.

- **Top half:** map with the location dot and CTA stop pins. Gear button at the top left.
- **"Near *place*" pill:** the place name comes from Apple's reverse geocoding. The list follows the map center when the map is panned, and a recenter button snaps back to the user.
- **Bottom sheet (draggable):**
  - Trains | Buses toggle and a search field (line, route, or stop name).
  - Route cards: line or route number on its CTA color, direction and destination, stop name, big live minutes on the right with a "live" icon, later departures stacked below. Timetable-only times show as clock times (10:05 AM).
- Arrivals refresh every 30 s while visible, plus pull-to-refresh.

### 9.2 Stop detail

All arrivals grouped by line and direction, the badges, the full alert text from Customer Alerts, and a ★ to save the stop as a favorite. Favorites are stored on the phone.

### 9.3 Gear menu

- **Devices:** scan for and pair the dock and handheld; Wi-Fi setup (1 network on the dock; up to 3 on the handheld, the first labeled Home because docking checks for it); pick the dock's 3 stops with the same picker as search; status (online, last update, handheld battery).
- **Pet:** name and color scheme, written to both devices' `config`.
- **Device settings:** screen brightness, LED brightness, night hours.
- **Find my handheld:** when the handheld disconnects, the app saves the phone's location and time and shows them as a map pin.

### 9.4 Widgets

Home Screen and Lock Screen widgets (WidgetKit) show the pet and the next 2 arrivals at the first favorite stop. The pet's mood is computed from that one stop with the rules in 6.3. The widget reads shared data from an App Group and fetches CTA itself on its timeline. iOS limits refreshes to roughly every 15 min, so the app also reloads widget timelines whenever it fetches.

### 9.5 Handheld relay

- CoreBluetooth central, with the Bluetooth and location background modes and state restoration, so iOS can relaunch the app for BLE events after the system closes it.
- **Nearby search:** the nearest stops come from the bundled `stops.json`, searched by distance from one location fix.
- **Permissions:** answering wake requests while the app is in the background needs "Always" location permission. With "While Using," the relay works only while the app is open, which is enough for the demo.
- **Known limit:** if the user force-quits the app, iOS won't relaunch it, and the relay stops until the app is opened again.

### 9.6 Errors

- **No location permission:** Nearby shows a prompt to enable it, and search still works.
- **CTA error or bad key:** a "CTA data unavailable" banner, with the last results kept and their age shown.
- **BLE disconnect:** automatic reconnect, and the Devices list shows "Not connected."

## 10. BLE protocol

One custom GATT service, implemented with NimBLE-Arduino 2.x on both ESP32s. Devices advertise as `CTA-Dock-xxxx` and `CTA-Pet-xxxx`, where `xxxx` is the end of the MAC address.

**Pairing:** the device shows a 6-digit passkey on screen and the user types it into iOS's pairing prompt (NimBLE's display-passkey flow). This encrypts and authenticates the link, which `wifi` requires because passwords cross it. Bonds are stored, so pairing happens once.

All payloads are UTF-8 JSON, at most 512 bytes per write.

| Characteristic | UUID | Properties | Payload |
|---|---|---|---|
| Service | `d8d4388c-c8e3-4ff0-b269-015d0979c266` | | |
| `info` | `fb360799-3925-45c3-90be-c32d504bbc99` | read | `{type: "dock"\|"handheld", fw, battery, charging, wifi: {connected, ssid}, last_update}` |
| `wifi` | `8d0820b1-1735-4f23-b1be-17b5d867632e` | write (encrypted) | `{slot: 0–2, ssid, pass}`; the dock only accepts slot 0 |
| `config` | `d8427173-107a-4c28-97ba-ab3b12d1500f` | read, write | `{stops: [{type, route, dir, id, name} ×3], pet: {name, colors}, settings: {screen, led, night_start, night_end}}`; `stops` is dock-only, and a write may include any subset of keys |
| `request` | `3c4dd75c-a019-4171-b7f1-b8dd87eda07c` | notify | handheld → app: `{want: "nearby", wifi: bool}` |
| `nearby` | `e8b866e4-18bf-4ab4-bd7c-07b52cb2a057` | write | `{lat, lon, trains: [station IDs], buses: [stop IDs], close: [3 IDs]}`; each list nearest first |
| `snapshot` | `7d05cd5a-5784-431f-b051-deb65bc0c3ee` | write | one stop record (6.2) |

The UUIDs are defined once in `firmware/lib/cta/ble_ids.h` and mirrored in the app.

## 11. Toolchain and repo

**Firmware**

- Arduino framework on Arduino-ESP32 core 3.x, built with **pioarduino** (the community fork of PlatformIO's ESP32 platform; official PlatformIO only ships core 2.x). Both boards are pinned to the same core version.
- BLE: NimBLE-Arduino 2.x.
- UI: LVGL 9 on both boards if the dock's display stack supports it. Otherwise the dock stays on LVGL 8.4 (what Waveshare's 4.3" Arduino demos use) and the sprite script outputs both image formats.
- Display drivers start from Waveshare's demos: ESP32_Display_Panel + ESP32_IO_Expander for the dock, and the 1.75" repo's examples (including XPowersLib for the AXP2101) for the handheld.
- JSON parsing: ArduinoJson with filters, so only the needed fields of CTA's responses are kept in memory.
- HTTPS uses the ESP32's built-in CA certificate bundle. TLS verification is never disabled.

**Day-1 toolchain test** (Nov 3–4). Before any feature work, prove these on the pinned setup:

1. Waveshare's display demo builds and runs on each board under pioarduino.
2. An iPhone pairs with each board using the on-screen passkey and writes a characteristic.
3. An HTTPS request to CTA runs while BLE is connected, without crashing or running out of memory.

If pioarduino fails, fall back to `arduino-cli` with the same core version. The shared library's laptop tests then move to a plain CMake + Unity setup.

**iPhone app:** Xcode, SwiftUI, Swift Testing. App and widget extension share an App Group. Leo's paid developer account allows TestFlight.

**Pet art.** Claude draws it: an original pixel character based on the shape of the CTA 7000-series railcar, with no CTA logo.

- Each frame is a 32×32 text grid in `art/`, one character per pixel, each character mapped to a palette slot.
- The app's pet colors are a palette swap, so recoloring needs no new art.
- `tools/sprite_build.py` turns the grids into PNG previews and LVGL image arrays: 5× scale (160 px) for the handheld, 2× (64 px) for the dock.
- Each of the 5 moods and Asleep gets 2–4 frames, plus umbrella and scarf overlays.

**Repo layout**

```
docs/                 specs, plans, notes (archive/ holds superseded notes)
firmware/lib/cta/     shared C++: CTA clients, parsing, mood rules, approach row, stop record, BLE IDs
firmware/dock/        PlatformIO project
firmware/handheld/    PlatformIO project
ios/                  Xcode project: app + widget extension
art/                  pet sprite grids + palette
tools/                gtfs_build.py, sprite_build.py
fixtures/cta/         recorded CTA responses used by both test suites
```

## 12. Security and secrets

- The repo is public. API keys live only in untracked `firmware/secrets.h` and `ios/Secrets.xcconfig`. Committed `*.example` versions show the format. `.gitignore` covers the real files.
- Wi-Fi passwords cross BLE only on an encrypted, passkey-authenticated link, and are stored in each ESP32's flash (NVS).
- No accounts, no analytics, and no data leaves the phone except requests to CTA and Open-Meteo. Find-my-handheld locations stay on the phone.

## 13. Testing

- **Shared C++ library:** unit tests with PlatformIO's native environment (Unity), run on the laptop. They cover parsing every fixture, the stop moods and device mood (every row of 6.3, including ties and the Hyped rule), the approach-row mapping for a train and a bus, and the stop record's size limit.
- **iOS:** Swift Testing over the same fixtures and the same mood and approach cases, plus the nearest-stop search.
- **Fixtures:** `fixtures/cta/` holds recorded responses for arrivals, positions, predictions, vehicles, patterns, and alerts, including a delayed arrival, an alert, and an empty prediction list. Both suites read the same files, so the C++ and Swift logic must agree.
- **Tools:** `gtfs_build.py` and `sprite_build.py` each carry an assert-based self-check (for example: a known station appears in the right order, a sprite grid has 32 rows).
- **On hardware:** the demo checklist in section 1, plus the battery discharge test in 8.5.

## 14. Milestones

| Dates | Work |
|---|---|
| Nov 3–4 | Apply for both CTA keys. Day-1 toolchain test. Repo scaffold. |
| Nov 5–9 | `gtfs_build.py`, `sprite_build.py`, first pet sprites. Shared library + fixtures. Dock screen showing live arrivals. |
| Nov 10–16 | Dock LED rows and stop picker. Handheld screens, pet, moods. App main screen, BLE pairing, Wi-Fi setup, dock stops. |
| Nov 17–21 | Handheld nearby flow and BLE relay. App extras: pet name and colors, device settings, widgets, find my handheld. Battery test. |
| Nov 22 | Full demo dry run against the section 1 checklist. Feature freeze. |
| Nov 23 | Due. |

## 15. Risks and open items

| Item | Risk | Mitigation |
|---|---|---|
| CTA API keys | Approval may take days and blocks real data | Apply Nov 3. Develop parsing against CTA's documented sample responses until then. |
| Daily request limits | Dock + handheld + app polling may exceed a key's daily limit | Limits read from the key emails on arrival. Poll intervals are constants (6.6). Night-hours slowdown. |
| pioarduino | Small community team; may break with a board's libraries | Day-1 test; `arduino-cli` fallback (section 11). |
| LVGL version | Dock demos use 8.4, handheld examples use 9 | Try 9 on both in the day-1 test; otherwise the sprite script outputs both formats. |
| NimBLE-Arduino vs. core 3.3+ | Core 3.3+ bundles its own NimBLE for the built-in BLE library; whether the two conflict is unverified | Pin one core version in the day-1 test and confirm pairing there. |
| iOS background relay | Force-quit stops the relay; "Always" location permission is needed in the background | Demo runs with the app open. Stretch: GPS backup and hotspot fallback. |
| Branching train lines | "6 stops before" is ambiguous after a branch merge or on the Loop | Use the most common trip pattern from GTFS (5). |
| Battery life | Dim always-on may not last a day | Measured in the discharge test; dim level and frame rate are constants. |
| Scope | Four app extras on top of the core path in 20 days | Extras are built last and slip first (section 2). |
