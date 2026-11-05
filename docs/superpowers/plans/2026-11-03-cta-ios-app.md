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
- CTA keys come from `ios/CTATracker/Secrets.plist` (gitignored; template `ios/Secrets.example.plist`), changed from `Secrets.xcconfig` in milestone 2 checkpoint C.
- Wi-Fi passwords are never logged or stored on the phone. The phone's location goes only to Open-Meteo and the handheld.
- Spec values (thresholds, intervals, UUIDs, line colors, radii) live once, in `CTAKit/Config.swift`, mirroring `cta_config.h`, `ble_ids.h`, and `line_colors.*`.
- No CTA logo anywhere. User-facing copy is exactly as written in the app design spec.
- Tests are offline and deterministic: CTA data from `fixtures/cta`, time passed in as `now`.

## Depends on Plan 1

- `fixtures/cta/*.json`: the Swift tests read the same files as the C++ tests. Milestone 1 wrote them, to the exact contents Plan 1 tasks 4–6 list (plus the other fields the real APIs send, and `bus_error_key.json`), so the C++ tests reuse them as they are.
- `data/stops.json` (with `lines` per bus stop and `platforms` per station), `data/train_lines.json`, and `data/train_shapes.json` (Plan 1, Task 10), bundled as resources.
- `tools/sprite_build.py`, extended here to also write `data/pet_sprites.json`: index grids, the default palette, and the 6 color presets from `art/presets.txt` (one line per preset: name, slot 2 color, slot 3 color).

**Depends on Plans 2 and 3:** the BLE message shapes (`config`, `info`, `wifi`, `request`, `nearby` with `close`, `snapshot`).

## Project layout (expected; adjust freely)

```
ios/CTATracker.xcodeproj
ios/Secrets.example.plist           CTA_TRAIN_KEY, CTA_BUS_KEY → copy to ios/CTATracker/Secrets.plist (gitignored)
ios/CTATracker.entitlements         App Group (placeholder group.io.github.leow427.CTATracker)
ios/CTAKit/Sources/CTAKit/          local Swift package shared by app + widget (standard SwiftPM layout)
  Models.swift          Kind, Prediction, Mood, Accessory, ServiceAlert, Vehicle, Pattern, CTAError (milestone 1);
                        Stop, Platform, Line, Favorite, KindFilter arrive with milestone 2
  Config.swift          spec constants, BLE UUIDs, line colors (with text color and contrast)
  Time.swift            CTA time differences (naive, like the firmware), Chicago calendar, night hours
  Decoding.swift        OneOrMany (object-or-array quirk), Loose (numbers as strings; junk becomes nil)
  Text.swift            24-byte UTF-8-safe truncation
  TrainParse.swift      arrivals and positions
  BusParse.swift        predictions, vehicles, and patterns
  AlertsWeather.swift   alerts, weather, accessory rule
  MoodRules.swift       stopMood / deviceMood mirror
  StopRecord.swift      StopRecord, Arrival, make, JSON codec (≤ 512 bytes)
  BLEMessages.swift     wifi, nearby, config encoders (each ≤ 512 bytes)
  CTAClient.swift       URL builders (milestone 1); async URLSession fetch (milestone 2)
  StopIndex.swift       loads stops.json; nearest and within-radius by haversine
  TrainLines.swift, TrainShapes.swift
  CardBuilder.swift     radius, fallbacks, one slot per line, favorites dedupe
  ArrivalFormat.swift   "Due" / "3 min" / "10:05", staleness
  DetailGroups.swift    stop detail grouping
  LineHitTest.swift     nearest drawn line to a tap
  Framing.swift         which vehicles to fit on the live map
  Pixel.swift           PixelScale, PixelFont.snap, PetSprites (grid + palette → CGImage)
  CardSpeech.swift      the card's VoiceOver sentence; ArrivalFormat.also
ios/CTAKit/Tests/CTAKitTests/       Swift Testing; reads fixtures/cta from the repo root via #filePath
ios/CTATracker/
  App/       CTATrackerApp, AppModel
  Map/       MapScreen, NearLabel, LineLayer, VehicleLayer
  Design/    Theme, Pixel (PixelImage, pixelFont, Glyphs), RouteCard
  Sheet/     RouteSheet, SearchBar, KindSwitch, FavoritesSection
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
  static func make(id: String, kind: Kind, name: String, from: [Prediction], alert: Bool, fetched: Date) -> StopRecord
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
enum Freshness: Equatable { case fresh, dim(minutes: Int), aged(minutes: Int) }
enum ArrivalFormat {
  static func label(_ p: Prediction, now: Date) -> String   // "Due", "3 min", "10:05"
  static func freshness(fetched: Date, now: Date) -> Freshness
}

// Stores (app target)
@Observable final class ArrivalsStore {
  var predictions: [String: [Prediction]]   // keyed by fetchID
  var fetched: [String: Date]               // keyed by fetchID, for freshness
  var alerts: [String: [ServiceAlert]]      // keyed by route, refreshed every 5 min (not `Alert`: SwiftUI has one)
  var lastError: Error?
  func track(_ fetchIDs: Set<String>) // starts/stops the 30 s polling for exactly these
  func refresh() async
}
@Observable final class LiveLineStore {
  var vehicles: [Vehicle]; var path: [CLLocationCoordinate2D]?; var updated: Date?
  func select(_ line: Line?, at stop: Stop)  // nil stops polling
}
struct Vehicle: Identifiable { let id: String; let lat: Double; let lon: Double
  let heading: Double; let dest: String
  let nextStopID: String; let nextStop: String   // trains only
  let pid: Int?; let pdist: Double? }            // buses only; Framing (milestone 4) decides "toward the stop"
```

## Decisions beyond the spec

Each is small and reversible.

1. **Favorites are cards, not stations.** ★ saves the tapped card's stop and line (route + direction), like a dock pick (base spec 6.1). So the ★ section is always at most 3 cards, and the widget's "first favorite" is one line at one stop.
2. **Favorite dedupe is by line, for nearby favorites only.** A nearby card whose route + direction matches a favorite is hidden only when the favorite's own stop is in the list (app spec 4.2). A favorite across town hides nothing. (Changed 2026-11-04 from "any favorite", which hid the nearest card for every starred line.)
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
5. **Largest text sizes and long names:** at accessibility text sizes, with 24-character names, cards wrap instead of clipping the minutes. Preview in milestone 2.

## Milestones

Five milestones, regrouped on 2026-11-04 from twelve. Old numbers map as: 1 → 1, 2–4 → 2, 5–7 → 3, 8–11 → 4, 12 → 5.

- Milestones 1–3 are the core path. Milestone 4 holds the app extras: built after the core path works, and the first to slip (app spec section 10).
- Each milestone ends in one reviewed commit. The bigger ones (2 and 3) are split into checkpoints: points where the work builds, its tests pass, and Leo can try it on the phone before the milestone goes on.

**Polish bar, for every UI milestone (2–4).** A milestone is done only when its screens:
- use only the design kit, with no one-off colors, sizes, or fonts
- follow app spec 4.6, except the accessibility items on hold below
- respect Reduce Motion (app spec 4.6 Motion): the camera jumps instead of flying, the pet holds its first frame, the minutes skip the flip, and the live glyph stays lit
- look right in light and dark, and look polished: Transit-level craft is the bar (Leo, 2026-11-04)
- keep text at 4.5:1 on its background (readability, not just accessibility)
- have their haptics
- were checked on Leo's phone, with screenshots in the worker's report

**Accessibility is on hold for the demo (Leo, 2026-11-04).** Workers don't build, and reviewers don't flag, layouts for the largest accessibility text sizes, VoiceOver checks, or Reduce Transparency on new screens. What checkpoint B already built stays: `CardSpeech` and the stacking card at accessibility sizes. Reduce Motion is not on hold. Proposed spec edit for Leo: mark the accessibility items in app spec 4.6 and 9 as post-demo.

Leo can send a screen back for polish before the commit.

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
   - *Done (2026-11-04).* Built as planned, plus a minimal app shell (Leo's call): `ios/CTATracker.xcodeproj` with a placeholder screen of line colors and arrival labels, so the app runs in the simulator from day one. Milestone 2 replaces the screen and adds the App Group and `Secrets.xcconfig`. The project uses synchronized folders, so new files under `ios/CTATracker/` need no project edits.
   - *Rulings made here:*
     - Parsers take `fetched:` (when the response arrived), and a prediction's time is `fetched + (arrT − tmst)`, as differences of naive wall-clock times like the firmware. Parsing Chicago times as absolute instants made every arrival read "Due" during the repeated 1 AM hour on fall-back night, and let phone-vs-server clock drift skew minutes.
     - `StopRecord.make` takes `fetched:`, not `now:`: `ts` is the fetch time (base spec 6.2) and minutes count from it, so a record rebuilt from cached predictions during an outage reads as stale instead of fresh.
     - `config` fields are cut to the dock's sizes on the way in: ids and routes 7 bytes, names and dirs 24, the pet name 16 (`petName[17]`, Plan 2). Real station names reach 41 bytes, which would push a full `config` past 512.
     - `WifiMessage` is encode-only, and its descriptions and mirror leave the password out, so a stray log can't leak it.
     - Bus `prdctdn: "DUE"` sets the prediction's time to `fetched`, so it reads "Due" and 0 min, matching the firmware.
     - A Bus Tracker error that names no stop, route, or pattern (bad key, daily limit) throws, so the app can show "CTA data unavailable". One that names a stop ("No arrival times") is an empty list. The firmware returns 0 records for both.
     - Train and bus vehicles, and pattern points, whose coordinates are missing or don't parse are dropped (they can't be drawn); the rest of the response still parses. Numbers may arrive as numbers or strings.
     - `config.pet.colors` is two 0xRRGGBB numbers, for palette slots 2 and 3 of the chosen preset. The base spec doesn't define the shape yet; Plans 2 and 3 must read the same.
     - `config.settings.screen` and `led` are percents; `night_start` and `night_end` are minutes after midnight.
   - *Tests:* `cd ios/CTAKit && swift test` (macOS, fast), or on the simulator: `xcodebuild test -scheme CTAKit -destination 'platform=iOS Simulator,name=iPhone 18 Pro'` from `ios/CTAKit`.
2. **Main screen.** Covers old milestones 2–4.
   - **Checkpoint A: stop index and card rules.**
     - Load `stops.json`. Implement `nearest` and within-radius search and `CardBuilder.slots`.
     - A linear scan over about 11k stops should be fast enough; build a spatial index only if profiling says so.
     - *Tests (`CardBuilderTests`):*
       - 6 stops on one route and direction give 1 slot, at the nearest stop.
       - A station with 2 lines and 2 platforms gives 4 slots (review focus 1).
       - The 800 m cutoff.
       - Under Trains with no station within 800 m, the 5 nearest stations show.
       - Under All, the fallback runs only when no stop of either kind is within 800 m.
       - Over 30 km from the nearest stop sets `farFromChicago`.
       - A favorite hides the nearby slot with the same line when its stop is listed; a far favorite hides nothing.
       - An unknown favorite ID is dropped (review focus 2).
       - At most 3 favorites.
     - *Done (2026-11-04).* `StopIndex` (decode, lookup by stop or platform ID, `byDistance`) and `CardBuilder` with the tests above, on synthetic stops in Plan 1 Task 10's `stops.json` shape. Plan 1 hasn't run yet, so `data/` doesn't exist; the app bundles the real files in checkpoint C.
     - *Moved:* `TrainLines` to milestone 3 checkpoint B (dock stops, its only user) and `TrainShapes` to milestone 4 (`LineLayer`), so each is written next to the code that reads it.
     - *Rulings made here:*
       - One `byDistance(from:kinds:)` serves both nearest and within-radius; ties break by stop ID so the list never reorders between refreshes.
       - A train favorite's `stopID` is its platform ID (the slot's `stopID`); the index maps platforms to their station for `fetchID`.
       - A favorite is dropped if its stop is gone *or* its line no longer serves that stop (it would be an empty card).
       - The All · Trains · Buses switch filters favorites too ("filters the cards", app spec 4.2).
       - Far from Chicago measures the nearest stop of any kind, whatever the switch.
   - **Checkpoint B: design kit** (app spec 4.6), in `ios/CTATracker/Design/`, before any screen:
     - `Theme.swift`: the 4 pt spacing scale, 16 pt card radius and margins, 8 pt gaps, 44 pt map buttons, materials with the Reduce Transparency fallback, and the springs.
     - `PixelImage`: draws a `PetSprites` image or glyph at a whole-number scale with `.interpolation(.none)`.
     - `PixelFont`: the minutes and pet-label font, snapped per Dynamic Type size. Uses system monospaced until the pixel font is chosen.
     - `Haptics`: selection, light impact, success, and error.
     - `RouteCard` is the first component built on the kit.
     - `tools/sprite_build.py` also writes the 6 glyphs (16×16 grids, `art/glyph_*.txt`) into `data/pet_sprites.json`.
     - *Tests (CTAKit):*
       - `PixelScale.scale(art: 32, points: 32) == 1`, `(32, 64) == 2`, and a non-multiple rounds down, never below 1.
       - `PixelFont` sizes are whole multiples of the font's pixel unit at every Dynamic Type size.
       - `CardSpeech.sentence` gives "Red Line to Howard from Belmont, 3 minutes, live, delayed" for that fixture case.
     - *Done (2026-11-04).* `Theme`, `PixelImage`, `pixelFont()`, and `RouteCard` (every state, flip and blink included, with previews) in `ios/CTATracker/Design/`; `PixelScale`, `PixelFont.snap`, `PetSprites` (grid + palette → `CGImage`), `CardSpeech`, and `ArrivalFormat.also` in CTAKit. The placeholder screen shows the card gallery until checkpoint C.
     - *Changed:*
       - No `Haptics` file: SwiftUI's `.sensoryFeedback` (iOS 17) gives selection, light impact, success, and error at each call site.
       - The glyph export is waiting on Plan 1 Task 11: `art/` and `tools/sprite_build.py` don't exist yet. `Glyphs.live` (8×8) and `Glyphs.alert` (16×16) are placeholder grids marked `shortcut:` until then. Milestone 3 checkpoint A now owns the export and the loading, since its setup steps need the dock and handheld glyphs.
     - *Rulings made here:*
       - Buses read "Route 22 to …" in VoiceOver. No arrivals reads "no arrivals" (or "no service now" at night), a scheduled arrival reads "at 10:05, scheduled", later arrivals add "also 12 minutes and at 10:05", an alert adds "service alert", and an aged card ends "updated 7 minutes ago", so VoiceOver never hears stale data as fresh.
       - A stale card dims its fill instead of its opacity: `LineColors.dimmed(_:dark:)` mixes the line color halfway toward the background's gray (systemGray5 0xE5E5EA in light, systemGray4 0x3A3A3C in dark), so it always recedes. Text color is picked against the dimmed fill. Tests cover all 18 pairs: text passes 4.5:1, and the dimmed fill never contrasts more with the background than the fresh one. A stale card's live glyph stays lit and still.
       - Footer order follows app spec 4.3: later departures, then Delayed and ⚠, then the age. Badge padding and the label gap sit on the 4 pt grid.
       - At accessibility text sizes both the header and the footer stack vertically, badges never wrap, and the live glyph scales with the text (still at whole multiples).
       - "also 12, 20 min" when every later arrival is live and at least 1 min away; otherwise each label ("also 12 min, 10:05").
       - The flip is two stepped frames (old, then new, at half height, 75 ms each), not a tween. One-color glyphs draw as templates in the card's text color, so they pass contrast on Yellow.
       - The pixel-font stand-in snaps to a 2 pt grid.
       - ⚠ on cards is a pixel glyph (Leo, 2026-11-04), a placeholder `Glyphs.alert` until the art lands. Proposed spec edit for Leo: add "the alert badge" to app spec 4.6's list of where pixel art appears.
       - A dimmed card (2–5 min) speaks its age too (Leo, 2026-11-04): `Freshness.dim` now carries its minutes.
       - Card look (Transit as the polish benchmark): trains get an outlined uppercase line badge; buses a black-weight route number; the destination leads with `arrow.right.circle.fill` as the hero line; minutes are departure-board style, a big pixel-font number with a small "min" under it and the live glyph at its corner, held on one line so the left column wraps instead. Cards get a soft shadow in light mode only, and `PressableCard` (0.97 on touch-down, `.snappy`) is ready for checkpoint D's taps.
   - **Checkpoint C: map, sheet, and cards.**
     - Project: App Group and `Secrets.xcconfig` wiring.
     - `MapScreen` with the persistent `RouteSheet`: detents per decision 9, `interactiveDismissDisabled`, and background interaction up through `.medium`. A `NavigationStack` inside the sheet.
     - `SearchBar`, `KindSwitch` (selection haptic), inset `RouteCard`s with the minutes' destination-sign flip and the blinking live glyph, and an empty `FavoritesSection`.
     - Map buttons and the "Near" label float on material, per app spec 4.6.
     - `ArrivalsStore` polls arrivals every 30 s while the list is visible and the app is in the foreground, plus pull-to-refresh, and alerts for the listed routes every 5 min.
     - `NearLabel` shows text only for now, with a 1 s debounce and "Near you" on failure. Recenter, the list following panning, and bus pins for listed stops.
     - A `SettingsScreen` shell. The gear on the map sets a flag on `AppModel`, and the sheet's content presents Settings (app spec section 3).
     - Errors from app spec section 8: location off, far from Chicago, "CTA data unavailable", stale dimming, and "No arrivals" / "No service now".
     - *Previews:* every card state (live, sched, delayed, alert, due, no arrivals) in light and dark, plus one at the largest accessibility text size with a 24-character name (review focus 5).
     - *Done (2026-11-05).* `AppModel` (location, list center, switch, slots), `ArrivalsStore` (30 s polling while active, bus stops 10 per call, one `ttarrivals` per station, alerts every 5 min, last good results kept per stop, trains and buses failing separately), `MapScreen`, `NearLabel`, `RouteSheet` with `SearchBar`, `KindSwitch`, and favorites as a bare `ForEach` above the nearby cards (checkpoint D adds the ★ header; there's no `FavoritesSection` type), the empty states, and a `SettingsScreen` shell with About. In CTAKit: the bitmap pixel digits, `Slot.predictions(from:)`, `CardOrder`, `CTAClient.get`. `data/stops.json` is bundled through a symlink in `ios/CTATracker/Resources/` (Xcode copies the file). Tests: `PixelDigitsTests`, `CardDataTests`.
     - *Changed:*
       - Keys come from a gitignored `ios/CTATracker/Secrets.plist` (template: `ios/Secrets.example.plist`), not `Secrets.xcconfig` through `Info.plist`: the synchronized folder bundles it with no project edits, and a missing file just means "CTA data unavailable".
       - The App Group entitlement is in `ios/CTATracker.entitlements` with the placeholder ID; simulator builds embed it, and Leo's signed-in device build registers the group. Checkpoint D stores favorites in the App Group's `UserDefaults` (app spec 7.4).
       - Train lines and station dots stay in milestone 4, as planned; the map shows bus pins for listed stops and the user.
     - *Rulings made here:*
       - **Pixel digits (the Apple/pixel hybrid, app spec 4.6):** the big minutes are our own 5×7 departure-board glyphs (0–9, ":", "Due") drawn through `PetSprites` at whole-number scales, scaled by height so every label shares one size. They stand in for the devices' pixel font until it's chosen; text without glyphs falls back to the snapped monospaced font.
       - **Empty states** show the 128 pt pixel pet (app spec 4.6) over the spec's copy. The pet is a placeholder sleepy grid from Plan 1 Task 11's art brief until `pet_sprites.json` lands.
       - **The list follows the visible map:** the sheet reports its height, the map pads its bottom by it (up to 55% of the screen), and the list center is the middle of what's left, so recenter puts the user in view instead of behind the sheet.
       - A card whose stop never loaded shows "—" while CTA data is unavailable, instead of "No arrivals" or "No service now". Before the first fetch it's a redacted placeholder.
       - Copy the spec doesn't give: the location prompt "Shows the stops and arrivals near you.", the search placeholder "Search stops and routes", the location-off button "Open Settings", Settings rows "Coming in a later build.", and About's "Data provided by Chicago Transit Authority" with "This app is not made or endorsed by the CTA." (CTA developer guidelines).
       - Map controls: gear top left, the "Near" capsule top center, recenter top right; 44 pt on regular material with a soft shadow.
       - `-demo` (DEBUG builds only) fills the tracked cards with plausible arrivals for simulator screenshots without keys.
       - Review round 1 fixes: departed arrivals drop a minute after their time (`Config.departedGraceSec`), so a stale card never shows trains that left as "Due"; a stale card with nothing left shows "—". `ArrivalBook` (CTAKit, tested) keeps the last good results per stop, splits each 10-stop Bus Tracker answer per stop, and marks never-loaded stops whose request failed as unknown ("—"); a poll cancelled by leaving the list records nothing. The map follows the user through `MapCameraPosition.userLocation` (keeps their zoom; a pan stops following; recenter resumes) with a 30 m distance filter, and pads by where the sheet's top edge actually is, so the Maps logo and Legal link stay visible. Cards keep a stable ID (stop + line), so the minutes flip survives moving. With location off, only favorites are fetched and pinned. The app tint is the label color (no system blue), and `Theme.secondaryText` (0x6C6C70 / 0xAEAEB2) keeps secondary text at 4.5:1. The sheet's content scrolls instead of resizing, so pull to refresh works; resize by the grabber or the search area.
       - Review round 2 fixes: following is the app's own flag, cleared only by a real drag (MapKit marks the camera user-positioned by itself when the sheet changes the map's padding, so the first fix used to be ignored and the list showed the Loop). While following, the list centers on the user's fix (30 m filter), not the camera, so resizing the sheet neither moves the list nor refetches; each new fix puts the camera back on the user if MapKit dropped it. `track` fetches only newly added stops, coalesced over 0.4 s. One 15 s clock wraps the lazy stack, so cards are built only as they scroll in. The label-color tint is scoped to the sheet and Settings, so the map's location dot stays blue. The search field is opaque and `Theme.secondaryText` is 0x636366 in light, so its placeholder keeps 4.5:1 over the translucent sheet.
       - Review round 3 fixes: a started fetch of added stops can't be cancelled by the next list change; a fetch of added stops leaves the other kind's failure flag alone; polling also stops while Settings covers the list.
       - Known: `-demo` only fills cards it was asked about, so new cards at an already-fetched station read "No arrivals" until the next poll; real `ttarrivals` returns every line.
       - Known: SwiftUI `Map` has no annotation z-order, so a bus pin can still overlap the user's dot when standing at a stop.
       - Proposed spec edit for Leo: base spec §12 names `ios/Secrets.xcconfig`; it's now `ios/CTATracker/Secrets.plist`.
   - **Checkpoint D: stop detail, favorites, and search.**
     - Detail pushes inside the sheet at half height and pauses list polling. Its header shows the stop name, ★, and "Live · updated 12 s ago".
     - The map centers on the stop (milestone 4 adds line framing and vehicles). Going back restores the previous camera.
     - The whole station, or all routes at the bus stop, grouped by `DetailGroups`, with the tapped group first and up to 4 arrivals per group. Alerts collapsed under ⚠.
     - ★ saves a `Favorite` (light impact) and fills the ★ section.
     - Search raises the sheet to full and shows favorites first, then stops and routes matching the name or route number.
     - *Test:* `DetailGroups` on a station record with 2 lines × 2 directions puts the tapped group first.
   - *Check (Leo):* on the phone in Chicago, cards fill with live times and refresh, and detail, ★, and search work.
3. **Devices.** Covers old milestones 5–7.
   - **Checkpoint A: BLE and setup.**
     - Glyphs first (from milestone 2 checkpoint B; needs Plan 1 Task 11): draw the 6 one-color 16×16 glyph grids (`art/glyph_{live,train,bus,alert,dock,handheld}.txt`, live as 2 frames) and extend `tools/sprite_build.py` to accept 16×16 for `glyph_*` files and write them, with the pet grids and default palette, into `data/pet_sprites.json`. Load it in CTAKit's `PetSprites`, bundle it, and replace the placeholder `Glyphs.live` and `Glyphs.alert`. The setup steps below need the dock and handheld glyphs.
     - `BLEManager`: a CoreBluetooth central with a state-restoration identifier. It scans for the service UUID while in the foreground and reconnects after a disconnect.
     - First launch: the location prompt, then the one-line explainer, then the Bluetooth prompt.
     - `FoundCard` for identifiers not in the device list.
     - `SetupFlow`: Pair → Wi-Fi → Done. Each step shows the 128 pt dock or handheld glyph. Done plays the success haptic, and a pairing or Wi-Fi failure the error haptic.
       - The SSID is pre-filled with `NEHotspotNetwork.fetchCurrent`, which needs the Access Wi-Fi Information entitlement.
       - It writes `wifi`, then reads `info` until `wifi.connected`, for up to 20 s.
     - `DeviceList` and `DevicePage` show status from `info`. Forget shows the iOS Settings instruction.
     - Error copy exactly per app spec section 8. Passwords are never logged.
   - **Checkpoint B: dock stops, pet, and device settings.**
     - Presets: write `art/presets.txt` (6 lines: name, slot 2 color, slot 3 color) and add the presets to `data/pet_sprites.json` through `sprite_build.py`.
     - Dock stops step and dock page: favorites first; trains from `train_lines.json` (load it here, in `TrainLines`; skip each line's first `lead` stops, which are lead-in, not picks; a (line, direction) can span several sequences, so merge their stations; Plan 1 Task 10 rulings); buses through `getroutes`, `getdirections`, and `getstops`. Writes `config.stops`.
     - The dock's stops are read back on every connect.
     - Pet step and `PetSettings`: name and the 6 presets from `pet_sprites.json`, with a 64 pt `PixelImage` preview of the pet.
     - `DeviceSettings`: screen and LED brightness, and night hours.
     - Pet and settings are written to every device on connect and on change.
   - **Checkpoint C: handheld relay.**
     - Subscribe to `request`. On each request, take one `CLLocationManager.requestLocation()` fix and write `nearby` with the 10 nearest stations, the 10 nearest bus stops, and `close` (the 3 nearest of either kind).
     - If the request says `wifi:false`, fetch arrivals for those stops and write one `snapshot` per stop every 60 s while connected.
     - Background modes: Bluetooth central and location; `NSLocationAlwaysAndWhenInUseUsageDescription`. "While Using" permission still works with the app open.
   - *Checks (Leo):*
     - A fresh dock gets onto Wi-Fi from the found card (demo item 4).
     - A pick on the dock's touchscreen shows up in the app, and a preset change shows on both devices (demo items 3 and 9).
     - With the handheld's Wi-Fi off, its stops fill from the phone (demo item 8).
4. **Extras.** Covers old milestones 8–11. Built in this order, so if time runs short the last ones drop. Each part can be committed on its own if the milestone has to stop partway.
   - **Train lines and line taps.**
     - `LineLayer` draws every `TrainShapes` entry (load `train_shapes.json` here; a route can have more than one: Green's Ashland branch, the Purple Express) as `MapPolyline`s in line colors, plus a dot per station.
     - A `MapReader` tap goes through `LineHitTest`: the nearest line in screen points, within 20 pt. The result sets a filter chip with × and plays the selection haptic.
     - *Test:* `LineHitTest` picks the nearer of two lines and returns nil beyond 20 pt.
   - **Live tracking.**
     - `LiveLineStore` polls `ttpositions` or `getvehicles` every 30 s for the selected line. The bus path comes from `getpatterns`, cached for the session.
     - `Framing` fits the stop plus the next 3 vehicles toward it. Vehicles going the other way are dimmed.
     - A marker is the unrotated pixel train or bus glyph with an SF Symbol heading arrow beside it. Markers glide over 1 s. A vehicle bubble shows destination and next stop.
     - Polling stops on back, background, or lock. On failure, the last positions stay with their age and are hidden after 120 s.
     - *Tests:* `Framing` picks the 3 nearest approaching vehicles from the positions and vehicles fixtures; the parsers read both fixtures.
   - **Pet in the label, and widgets.**
     - `NearLabel` gets the 32 pt pet, animating at 4 fps (first frame only under Reduce Motion), with a VoiceOver label like "Your pet is happy": device mood over the listed stops, with only the 3 closest counting for Hyped. Weather accessories from Open-Meteo every 15 min in the foreground. Tapping it opens Pet settings.
     - Widgets: a timeline provider reads the first favorite from the App Group and fetches its arrivals. It shows the 64 pt pet (mood from that one stop) and the next 2 arrivals. The app calls `WidgetCenter.shared.reloadAllTimelines()` after each fetch.
     - *Test:* device mood over 5 listed stops where only the 4th is due in 2 min is not Hyped.
   - **Find my handheld and About.**
     - On a handheld disconnect, save the phone's location and the time to the App Group. Show a map pin with "Last seen at 8:42 AM", or "Connected now".
     - About shows the version and the CTA attribution line.
5. **Ship.** Covers old milestone 12. Add the app icon first: the pet at a whole-number scale on a line-color ground, rendered from `art/` by `sprite_build.py`. Then archive, upload to TestFlight, and install on Leo's phone. Walk base spec §1 items 3, 4, 5, 8, and 9.

## Open questions

- **Bundle ID and App Group ID:** placeholder `io.github.leow427.CTATracker` until Leo picks (the App Group arrives at milestone 2). Team ID `5JXMXR2W9L` is set, with automatic signing; device builds need Leo signed in to Xcode.
- **Pixel font:** chosen with the final art, under a license (such as OFL) that allows bundling in the app and the firmware. Until then the minutes use a system monospaced font.
- **CTA attribution wording:** confirm from the developer terms when the keys arrive (milestone 4).
- **`ttarrivals` per call:** if one call can take more than one station, `ArrivalsStore` batches trains too.
- **Daily limits:** from the key emails. The intervals are constants in `Config.swift`.
- **Stretch, not planned here:** Live Activity, Siri, OTA updates, live navigation.
