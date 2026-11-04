# CTA Tracker iPhone App Plan (Plan 4 of 4)

> **Reference plan, not a script.** This lays out milestones, module boundaries, and the decisions made so far. Expect it to change as the code teaches us things. When the code shows a better way, take it, say so, and update this file in the same commit. The spec is the contract. Execute natively, one milestone at a time, following `docs/Agent-Workflow.md`.

**Goal:** The required companion app: a Transit-style map with live CTA arrivals, stop detail and favorites, setup and control of the dock and handheld over BLE, the handheld relay, widgets, and find-my-handheld. No logins.

**Spec:** `docs/superpowers/specs/2026-11-03-cta-tracker-design.md` (sections 1, 2, 5, 6, 9, 10, 12)

**Depends on Plan 1:**
- `fixtures/cta/*.json`: the Swift tests read the same files as the C++ tests
- `data/stops.json` and `data/train_lines.json`, bundled as resources
- The spec values mirrored in Swift: thresholds, intervals, UUIDs, line colors
- `tools/sprite_build.py`, extended here to also write `data/pet_sprites.json` (index grids + default palette) so the app can draw the pet

**Depends on Plans 2 and 3:** the BLE message shapes (`config`, `info`, `wifi`, `request`, `nearby` including the proposed `close` field, `snapshot`).

## Project layout (expected; adjust freely)

```
ios/CTATracker.xcodeproj
ios/Secrets.example.xcconfig        CTA_TRAIN_KEY, CTA_BUS_KEY → Info.plist; real Secrets.xcconfig is gitignored
ios/CTAKit/                         local Swift package shared by app + widget
  Models.swift                      StopRecord, Arrival, Kind, Mood (mirror of cta_types.h)
  Config.swift                      spec constants (mirror of cta_config.h, ble_ids.h, line_colors)
  TrainParse.swift, BusParse.swift, AlertsWeather.swift   lenient Codable: object-or-array, number-or-string
  MoodRules.swift                   stopMood / deviceMood mirror
  StopRecordCodec.swift             JSON ≤ 512 bytes, 24-byte UTF-8-safe names
  CTAClient.swift                   async URLSession; URL builders
  StopIndex.swift                   loads stops.json; nearest(k, to:) by haversine
  TrainLines.swift                  loads train_lines.json (picker)
  PetSprites.swift                  draws a sprite grid + palette into a CGImage
ios/CTAKit/Tests/                   Swift Testing; reads ../../fixtures/cta
ios/CTATracker/                     app target
  App/            CTATrackerApp, AppModel (observable root state)
  Map/            MapScreen, NearPill, RouteSheet, RouteCard
  Stop/           StopDetail, Favorites
  Devices/        BLEManager, DeviceList, WiFiSetup, DockStopsPicker, PetSettings, DeviceSettings, FindHandheld
  Relay/          HandheldRelay (request → nearby → snapshots)
ios/CTAWidgets/                     WidgetKit extension (Home + Lock Screen)
```

**Rule of thumb:** parsing, mood rules, the codec, and the nearest-stop search live in `CTAKit` with tests against the shared fixtures. Views stay thin.

## Milestones

1. **CTAKit core.** Models, parsers, the stop record codec, mood rules, constants, and URL builders, with Swift Testing over `fixtures/cta`. The suite should mirror the C++ cases one for one: the same fixture, the same expected minutes, moods, and truncation. *Check:* `swift test` in `ios/CTAKit` passes, and a deliberately broken mood rule fails a test.
2. **Stop index.** Load `stops.json` and `train_lines.json`, and implement `nearest(k:kind:to:)`. *Test:* a point next to a known station returns it first. A linear scan over about 11k stops should be fast enough on device; only build a spatial index if profiling says so.
3. **Map and route cards.**
   - Xcode project, iOS 17+, App Group, and `Secrets.xcconfig` wiring.
   - `MapScreen`: SwiftUI `Map` with the user location, pins for nearby stops only, and a recenter button.
   - `NearPill`: reverse-geocodes the map center with `CLGeocoder`, debounced on camera changes.
   - `RouteSheet`: a bottom sheet (`presentationDetents`) with the Trains | Buses toggle, a search field, and route cards per spec §9.1 (line or route number on its color, direction and destination, stop, big live minutes, later departures stacked, timetable-only times as clock times).
   - The list follows the map center, refreshes every 30 s while visible, and supports pull-to-refresh.
   - Errors: a location-permission prompt, and a "CTA data unavailable" banner that keeps the last results.
4. **Stop detail and favorites.** Arrivals grouped by line and direction, the full alert text, and a ★ that saves the stop in App Group `UserDefaults`. Search by line, route, or stop name.
5. **BLE devices.**
   - `BLEManager` (CoreBluetooth central with a state-restoration identifier) scans for the service UUID, lists `CTA-Dock-*` and `CTA-Pet-*`, and connects.
   - The first access to an encrypted characteristic triggers iOS's passkey prompt.
   - Devices screen: status from `info`.
   - `WiFiSetup` writes `wifi`: one network on the dock; up to 3 on the handheld, with slot 0 labeled Home.
   - Reconnects automatically after a disconnect, and the Devices list shows "Not connected" until it does (spec §9.6).
   - Never logs passwords.
6. **Dock and pet control.**
   - `DockStopsPicker`: trains from `train_lines.json`; buses through `getroutes`, `getdirections`, and `getstops`.
   - It writes `config.stops`.
   - `PetSettings` (name + body and stripe colors) and `DeviceSettings` (screen and LED brightness, night hours) write partial `config` to both devices.
   - Pet colors preview with `PetSprites`.
7. **Handheld relay.**
   - Subscribe to `request`. On each request, take one `CLLocationManager.requestLocation()` fix and write `nearby` with the 10 nearest stations, the 10 nearest bus stops, and `close` (the 3 nearest of either kind).
   - If the request says `wifi:false`, fetch arrivals for those stops and write one `snapshot` per stop every 60 s while connected.
   - Background modes: Bluetooth central and location; `NSLocationAlwaysAndWhenInUseUsageDescription`. "While Using" permission still works with the app open.
   - *Check (Leo):* with the handheld's Wi-Fi off, its stops fill from the phone.
8. **Widgets.** A timeline provider reads the first favorite from the App Group and fetches its arrivals. It shows the pet (mood from that one stop, via `MoodRules`) and the next 2 arrivals. The app calls `WidgetCenter.shared.reloadAllTimelines()` after each fetch.
9. **Find my handheld.** On a handheld disconnect, save the phone's location and the time to the App Group, and show a map pin with "Last seen at 8:42 AM". The location never leaves the phone.
10. **TestFlight and demo check.** Archive, upload, and install on Leo's phone. Walk spec §1 items 3, 4, 5, 8, and 9.

## Decisions so far

- **Logic mirrors C++:** the Swift logic mirrors the C++ logic and is held to it by the shared fixtures. When one side changes, the other changes in the same task, or the plan notes the gap.
- **No third-party Swift packages.** URLSession, Codable, MapKit, CoreBluetooth, CoreLocation, and WidgetKit cover everything.
- **Keys:** CTA keys come from `Info.plist` via the xcconfig. They're readable in the app binary, which the spec accepts for a no-server design.
- **Resources:** `stops.json` and `train_lines.json` are bundled resources, refreshed by re-running `gtfs_build.py`.

## Open questions

- **Bundle ID and App Group ID:** Leo's choice. They're needed at milestone 3.
- **Pet on the main screen:** the spec puts it in widgets and settings. Should a small pet also sit in the "Near" pill? Ask Leo once the map works.
- **Map pin density:** nearby stops only (current plan), or every stop in view when zoomed in.
- **Live Activity, Siri, OTA updates, live navigation:** stretch goals, not planned here.
