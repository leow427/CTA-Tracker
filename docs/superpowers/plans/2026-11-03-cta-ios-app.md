# CTA Tracker iPhone App Plan (Plan 4 of 4)

> **Reference plan, not a script.** This lays out milestones, module boundaries, interfaces, and the decisions made so far. Expect it to change as the code teaches us things. When the code shows a better way, take it, say so, and update this file in the same commit. The specs are the contract.
>
> **For agentic workers:** execute natively with superpowers:executing-plans, one milestone at a time, following `docs/Agent-Workflow.md` (worker plus fresh reviewers). Don't start until Leo says go.
>
> **The UI is subject to change, and likely will.** Screen layouts, sizes, colors, fonts, copy, and pet art here are starting points, not final designs. Build them so they're easy to change; don't treat a UI detail in this plan as fixed.

**Goal:** The required companion app: a Transit-style map with live CTA arrivals, stop detail and favorites, setup and control of the dock and handheld over BLE, the handheld relay, and the app extras (drawn train lines, live tracking, pet in the label, widgets, find my handheld). No logins.

**Architecture:** A local Swift package, `CTAKit`, holds every piece of logic (parsing, mood rules, the card list rules, formatting, encoding) and is tested against the shared fixtures. The app is a full-screen SwiftUI `Map` under a native, always-up sheet. Thin views read from a few `@Observable` stores.

**Tech stack:** Xcode, Swift, SwiftUI, MapKit for SwiftUI, CoreLocation, CoreBluetooth, NetworkExtension (current SSID only), WidgetKit, Swift Testing.

**Specs:**
- `docs/superpowers/specs/2026-11-04-ios-app-design.md` (the app design; wins for the app)
- `docs/superpowers/specs/2026-11-03-cta-tracker-design.md` (base spec: sections 1, 2, 5, 6, 9, 10, 12)

## Global constraints

- iOS 17.0 or later. Swift Testing for tests.
- No third-party Swift packages.
- No login, accounts, or analytics.
- App and widget share one App Group. Storage is small JSON in App Group `UserDefaults`, nothing else.
- CTA keys come from `ios/Secrets.xcconfig` through `Info.plist`. The real file is gitignored; `ios/Secrets.example.xcconfig` is committed.
- Wi-Fi passwords are never logged or stored on the phone. The phone's location goes only to Open-Meteo and the handheld.
- Spec values (thresholds, intervals, UUIDs, line colors, radii) live once, in `CTAKit/Config.swift`, mirroring `cta_config.h`, `ble_ids.h`, and `line_colors.*`.
- No CTA logo anywhere. User-facing copy is exactly as written in the app design spec.
- Tests are offline and deterministic: CTA data from `fixtures/cta`, time passed in as `now`.

## Depends on Plan 1

- `fixtures/cta/*.json`: the Swift tests read the same files as the C++ tests.
- `data/stops.json` (with `lines` per bus stop and `platforms` per station), `data/train_lines.json`, and `data/train_shapes.json` (Plan 1, Task 10), bundled as resources.
- `tools/sprite_build.py`, extended here to also write `data/pet_sprites.json`: index grids, the default palette, and the 6 color presets from `art/presets.txt` (one line per preset: name, slot 2 color, slot 3 color).

**Depends on Plans 2 and 3:** the BLE message shapes (`config`, `info`, `wifi`, `request`, `nearby` with `close`, `snapshot`).

## Project layout (expected; adjust freely)

```
ios/CTATracker.xcodeproj
ios/Secrets.example.xcconfig        CTA_TRAIN_KEY, CTA_BUS_KEY → Info.plist
ios/CTAKit/                         local Swift package shared by app + widget
  Models.swift          Stop, Platform, Line, Prediction, Alert, StopRecord, Arrival, Kind, Mood, Favorite
  Config.swift          spec constants + line colors (with text color and contrast)
  TrainParse.swift, BusParse.swift, AlertsWeather.swift, VehicleParse.swift
  MoodRules.swift       stopMood / deviceMood mirror
  StopRecordCodec.swift JSON ≤ 512 bytes, 24-byte UTF-8-safe names
  CTAClient.swift       async URLSession; URL builders
  StopIndex.swift       loads stops.json; nearest and within-radius by haversine
  TrainLines.swift, TrainShapes.swift
  CardBuilder.swift     radius, fallbacks, one slot per line, favorites dedupe
  ArrivalFormat.swift   "Due" / "3 min" / "10:05", staleness
  DetailGroups.swift    stop detail grouping
  LineHitTest.swift     nearest drawn line to a tap
  Framing.swift         which vehicles to fit on the live map
  PetSprites.swift      sprite grid + palette → CGImage
ios/CTAKit/Tests/                   Swift Testing; reads ../../fixtures/cta
ios/CTATracker/
  App/       CTATrackerApp, AppModel
  Map/       MapScreen, NearLabel, LineLayer, VehicleLayer
  Sheet/     RouteSheet, SearchBar, KindSwitch, RouteCard, FavoritesSection
  Stop/      StopDetail
  Data/      ArrivalsStore, LiveLineStore
  Devices/   BLEManager, FoundCard, SetupFlow (+ steps), DeviceList, DevicePage
  Settings/  SettingsScreen, PetSettings, DeviceSettings, FindHandheld, About
  Relay/     HandheldRelay
ios/CTAWidgets/                     WidgetKit extension (Home + Lock Screen)
```

## Interfaces

The names milestones share. Bodies are the implementer's.

```swift
// Models.swift — mirrors data/stops.json
enum Kind: String, Codable { case bus, station }
enum KindFilter { case all, trains, buses }
struct Line: Codable, Hashable { let route: String; let dir: String }
struct Platform: Codable { let id: String; let lines: [Line] }
struct Stop: Codable, Identifiable {
  let id: String; let name: String; let lat: Double; let lon: Double; let kind: Kind
  let lines: [Line]?          // bus stops
  let platforms: [Platform]?  // stations
}
struct Favorite: Codable, Hashable { let stopID: String; let line: Line }  // the card that was starred

// What the Swift parsers return: one per eta (trains) or prd (buses), no 4-arrival cap
struct Prediction: Hashable {
  let stopID: String   // platform ID (ttarrivals stpId) or bus stop ID (stpid)
  let route: String    // rt
  let dir: String      // rtdir for buses; empty for trains (the platform implies the direction)
  let dest: String; let at: Date; let live: Bool; let delayed: Bool
}
extension StopRecord {
  // The device-shaped record (base spec 6.2), built with the C++ rules: ≤4 arrivals, 24-byte names
  static func make(id: String, kind: Kind, name: String, from: [Prediction], alert: Bool, now: Date) -> StopRecord
}

// CardBuilder.swift
struct Slot: Hashable {
  let line: Line
  let stopID: String   // bus stop ID or platform ID
  let fetchID: String  // bus stop ID, or the parent station ID for trains
  let kind: Kind
  let meters: Double
}
struct SlotList { let favorites: [Slot]; let nearby: [Slot]; let farFromChicago: Bool }
enum CardBuilder {
  static func slots(center: CLLocationCoordinate2D, filter: KindFilter,
                    favorites: [Favorite], index: StopIndex) -> SlotList
}

// ArrivalFormat.swift
enum Freshness: Equatable { case fresh, dim, aged(minutes: Int) }
enum ArrivalFormat {
  static func label(_ p: Prediction, now: Date) -> String   // "Due", "3 min", "10:05"
  static func freshness(fetched: Date, now: Date) -> Freshness
}

// Stores (app target)
@Observable final class ArrivalsStore {
  var predictions: [String: [Prediction]]   // keyed by fetchID
  var fetched: [String: Date]               // keyed by fetchID, for freshness
  var alerts: [String: [Alert]]             // keyed by route, refreshed every 5 min
  var lastError: Error?
  func track(_ fetchIDs: Set<String>) // starts/stops the 30 s polling for exactly these
  func refresh() async
}
@Observable final class LiveLineStore {
  var vehicles: [Vehicle]; var path: [CLLocationCoordinate2D]?; var updated: Date?
  func select(_ line: Line?, at stop: Stop)  // nil stops polling
}
struct Vehicle: Identifiable { let id: String; let coord: CLLocationCoordinate2D
  let heading: Double; let dest: String; let nextStop: String; let towardStop: Bool }
```

## Decisions beyond the spec

Each is small and reversible.

1. **Favorites are cards, not stations.** ★ saves the tapped card's stop and line (route + direction), like a dock pick (base spec 6.1). So the ★ section is always at most 3 cards, and the widget's "first favorite" is one line at one stop.
2. **Favorite dedupe is by line.** A nearby card whose route + direction matches a favorite is hidden.
3. **Unknown favorites are dropped.** A favorite whose stop vanished in a GTFS rebuild is left out of the list silently and kept in storage.
4. **Fetch IDs:** train cards fetch their parent station (`ttarrivals?mapid=`); bus cards fetch their stop (`getpredictions?stpid=`, up to 10 per call).
5. **Clock times** for scheduled arrivals are formatted in `America/Chicago`, whatever the phone's time zone.
6. **Card text** is white when white has a contrast of at least 4.5:1 on the line color, and black otherwise.
7. **Default map center** with no location: State and Madison (41.8819, −87.6278).
8. **"Not now"** on the found card lives in memory only, so it resets on the next launch.
9. **Sheet heights:** peek is `.height(140)` (search bar, switch, and the top of the first card), then `.medium` and `.large`. Tune peek on the phone.
10. **The app parses into `Prediction`s, not `StopRecord`s.** Plan 1's record holds one route and 4 arrivals, which can't feed a Clark/Lake card or a detail group. A card shows the predictions whose `stopID` and `route` match its slot. `StopRecord.make` builds the device-shaped record only for relay snapshots and mood rules, and the C++ fixture cases are mirrored through it.

## Review focus

Inputs the specs imply but don't spell out, most likely to bite first. Each has a test in the milestone named.

1. **Multi-line stations** (Clark/Lake has 6 lines): every line and direction gets its own card, and detail groups them all. Test in milestone 2.
2. **Favorites that vanished** after a GTFS rebuild: dropped quietly, never an empty or crashing card. Test in milestone 2.
3. **Arrival labels at the edges:** 0 min is "Due", 1 min is "1 min", and scheduled times show Chicago time even on a phone set to another zone. Test in milestone 1.
4. **Coming back after an hour in the background:** cards show dimmed with their age right away, then refresh. Never fresh-looking stale data. Test in milestone 1.
5. **Largest text sizes and long names:** at accessibility text sizes, with 24-character names, cards wrap instead of clipping the minutes. Preview in milestone 3.

## Milestones

The core path is milestones 1–7. Milestones 8–11 are the app extras: built after the core path works, and the first to slip (app spec section 10).

1. **CTAKit core.**
   - Models, parsers into `Prediction`s (including vehicles and patterns), `StopRecord.make`, the stop record codec, mood rules, constants, URL builders, `ArrivalFormat`, and line colors.
   - *Tests:*
     - The C++ fixture cases mirrored one for one through parse → `StopRecord.make`: same fixture, same expected minutes, moods, and truncation.
     - A multi-line train fixture parses with each prediction's own `route` and platform `stopID`.
     - `ArrivalFormat.label` at 0, 1, and 12 min, and a scheduled 10:05 Chicago arrival with the test's time zone set to Los Angeles (review focus 3).
     - `freshness` at 119 s (fresh), 121 s (dim), 301 s (aged, 5 min), and 3,600 s (aged, 60 min) (review focus 4).
     - Every line color with its text color passes 4.5:1, bus color included.
     - Every BLE message the app writes is 512 bytes or less, including `wifi` with a 32-character SSID and a 63-character password, and a full `nearby`.
   - *Check:* `swift test` in `ios/CTAKit` passes, and a deliberately broken mood rule fails a test.
2. **Stop index and card rules.**
   - Load `stops.json`, `train_lines.json`, and `train_shapes.json`. Implement `nearest` and within-radius search and `CardBuilder.slots`.
   - A linear scan over about 11k stops should be fast enough; build a spatial index only if profiling says so.
   - *Tests (`CardBuilderTests`):*
     - 6 stops on one route and direction give 1 slot, at the nearest stop.
     - A station with 2 lines and 2 platforms gives 4 slots (review focus 1).
     - The 800 m cutoff.
     - Under Trains with no station within 800 m, the 5 nearest stations show.
     - Under All, the fallback runs only when no stop of either kind is within 800 m.
     - Over 30 km from the nearest stop sets `farFromChicago`.
     - A favorite hides the nearby slot with the same line.
     - An unknown favorite ID is dropped (review focus 2).
     - At most 3 favorites.
3. **Xcode project and main screen.**
   - Project, App Group, and `Secrets.xcconfig` wiring.
   - `MapScreen` with the persistent `RouteSheet`: detents per decision 9, `interactiveDismissDisabled`, and background interaction up through `.medium`. A `NavigationStack` inside the sheet.
   - `SearchBar`, `KindSwitch`, `RouteCard`, and an empty `FavoritesSection`.
   - `ArrivalsStore` polls arrivals every 30 s while the list is visible and the app is in the foreground, plus pull-to-refresh, and alerts for the listed routes every 5 min.
   - `NearLabel` shows text only for now, with a 1 s debounce and "Near you" on failure. Recenter, the list following panning, and bus pins for listed stops.
   - A `SettingsScreen` shell. The gear on the map sets a flag on `AppModel`, and the sheet's content presents Settings (app spec section 3).
   - Errors from app spec section 8: location off, far from Chicago, "CTA data unavailable", stale dimming, and "No arrivals" / "No service now".
   - *Previews:* every card state (live, sched, delayed, alert, due, no arrivals) in light and dark, plus one at the largest accessibility text size with a 24-character name (review focus 5).
   - *Check (Leo):* on the phone in Chicago, cards fill with live times and refresh.
4. **Stop detail, favorites, and search.**
   - Detail pushes inside the sheet at half height and pauses list polling. Its header shows the stop name, ★, and "Live · updated 12 s ago".
   - The map centers on the stop (milestone 9 adds line framing and vehicles). Going back restores the previous camera.
   - The whole station, or all routes at the bus stop, grouped by `DetailGroups`, with the tapped group first and up to 4 arrivals per group. Alerts collapsed under ⚠.
   - ★ saves a `Favorite` and fills the ★ section.
   - Search raises the sheet to full and shows favorites first, then stops and routes matching the name or route number.
   - *Test:* `DetailGroups` on a station record with 2 lines × 2 directions puts the tapped group first.
5. **BLE devices and setup.**
   - `BLEManager`: a CoreBluetooth central with a state-restoration identifier. It scans for the service UUID while in the foreground and reconnects after a disconnect.
   - First launch: the location prompt, then the one-line explainer, then the Bluetooth prompt.
   - `FoundCard` for identifiers not in the device list.
   - `SetupFlow`: Pair → Wi-Fi → Done.
     - The SSID is pre-filled with `NEHotspotNetwork.fetchCurrent`, which needs the Access Wi-Fi Information entitlement.
     - It writes `wifi`, then reads `info` until `wifi.connected`, for up to 20 s.
   - `DeviceList` and `DevicePage` show status from `info`. Forget shows the iOS Settings instruction.
   - Error copy exactly per app spec section 8. Passwords are never logged.
   - *Check (Leo):* a fresh dock gets onto Wi-Fi from the found card (demo item 4).
6. **Dock stops, pet, and device settings.**
   - Dock stops step and dock page: favorites first; trains from `train_lines.json`; buses through `getroutes`, `getdirections`, and `getstops`. Writes `config.stops`.
   - The dock's stops are read back on every connect.
   - Pet step and `PetSettings`: name and the 6 presets from `pet_sprites.json`, with a `PetSprites` preview.
   - `DeviceSettings`: screen and LED brightness, and night hours.
   - Pet and settings are written to every device on connect and on change.
   - *Check (Leo):* a pick on the dock's touchscreen shows up in the app, and a preset change shows on both devices (demo items 3 and 9).
7. **Handheld relay.**
   - Subscribe to `request`. On each request, take one `CLLocationManager.requestLocation()` fix and write `nearby` with the 10 nearest stations, the 10 nearest bus stops, and `close` (the 3 nearest of either kind).
   - If the request says `wifi:false`, fetch arrivals for those stops and write one `snapshot` per stop every 60 s while connected.
   - Background modes: Bluetooth central and location; `NSLocationAlwaysAndWhenInUseUsageDescription`. "While Using" permission still works with the app open.
   - *Check (Leo):* with the handheld's Wi-Fi off, its stops fill from the phone (demo item 8).
8. **Extra: train lines and line taps.**
   - `LineLayer` draws `TrainShapes` as `MapPolyline`s in line colors, plus a dot per station.
   - A `MapReader` tap goes through `LineHitTest`: the nearest line in screen points, within 20 pt. The result sets a filter chip with ×.
   - *Test:* `LineHitTest` picks the nearer of two lines and returns nil beyond 20 pt.
9. **Extra: live tracking.**
   - `LiveLineStore` polls `ttpositions` or `getvehicles` every 30 s for the selected line. The bus path comes from `getpatterns`, cached for the session.
   - `Framing` fits the stop plus the next 3 vehicles toward it. Vehicles going the other way are dimmed.
   - Markers animate over 1 s. A vehicle bubble shows destination and next stop.
   - Polling stops on back, background, or lock. On failure, the last positions stay with their age and are hidden after 120 s.
   - *Tests:* `Framing` picks the 3 nearest approaching vehicles from the positions and vehicles fixtures; the parsers read both fixtures.
10. **Extra: pet in the label, and widgets.**
    - `NearLabel` gets the 28 pt pet: device mood over the listed stops, with only the 3 closest counting for Hyped. Weather accessories from Open-Meteo every 15 min in the foreground. Tapping it opens Pet settings.
    - Widgets: a timeline provider reads the first favorite from the App Group and fetches its arrivals. It shows the pet (mood from that one stop) and the next 2 arrivals. The app calls `WidgetCenter.shared.reloadAllTimelines()` after each fetch.
    - *Test:* device mood over 5 listed stops where only the 4th is due in 2 min is not Hyped.
11. **Extra: find my handheld and About.**
    - On a handheld disconnect, save the phone's location and the time to the App Group. Show a map pin with "Last seen at 8:42 AM", or "Connected now".
    - About shows the version and the CTA attribution line.
12. **TestFlight and demo check.** Archive, upload, and install on Leo's phone. Walk base spec §1 items 3, 4, 5, 8, and 9.

## Open questions

- **Bundle ID and App Group ID:** Leo's choice, needed at milestone 3.
- **Pixel font:** chosen with the final art, under a license (such as OFL) that allows bundling in the app and the firmware. Until then the minutes use a system monospaced font.
- **CTA attribution wording:** confirm from the developer terms when the keys arrive (milestone 11).
- **`ttarrivals` per call:** if one call can take more than one station, `ArrivalsStore` batches trains too.
- **Daily limits:** from the key emails. The intervals are constants in `Config.swift`.
- **Stretch, not planned here:** Live Activity, Siri, OTA updates, live navigation.
