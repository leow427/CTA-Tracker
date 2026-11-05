# CTA Tracker iPhone app: design spec

- **Date:** 2026-11-04
- **Owner:** Leo
- **Status:** Draft for review
- **Extends:** `2026-11-03-cta-tracker-design.md` (the base spec), section 9

This adds detail to the base spec's iPhone app section: screens, navigation, look, and the main flows. Once approved, it wins over the base spec for the app wherever the two disagree. Section 11 lists the base spec edits that fold these decisions back in.

## 1. Goals

- Pass demo items 3, 4, 5, 8, and 9 of the base spec on 11/23.
- Be an app Leo would actually use every day to check a train or bus.
- The current pet art is a placeholder. The app draws whatever `tools/sprite_build.py` produces, so final art needs no app changes.

## 2. Decisions

| Topic | Decision |
|---|---|
| Opens to | A full-screen map with a sheet of route cards over it. No tab bar. |
| Main screen build | The native iOS sheet (`presentationDetents`) over a full-screen SwiftUI `Map`. |
| Search | A bar at the top of the sheet. It filters by line, route, or stop name. No Home button and no trip planning. |
| Settings | A gear button on the map, top left. |
| List | One mixed list, nearest stop first, with an All · Trains · Buses switch. |
| Favorites | A ★ section pinned on top of the list, up to 3 stops, live from anywhere. The rest show under ★ in search. |
| Map | Stop pins, train station dots, and all 8 train lines in CTA colors. |
| Tapping a card | The sheet becomes that stop's detail, and the map zooms to that line and shows its vehicles live. |
| Tapping a drawn line | The list filters to that line's cards. |
| Pet | A small pet in the "Near *place*" label, using the handheld's mood rules. Tapping it opens Pet settings. |
| Device setup | An AirPods-style "Dock found · Set up" card for unpaired devices, plus the same steps under Settings → Devices. |
| Style | San Francisco everywhere, except the big arrival minutes and the pet label, which use the devices' pixel font. Light and dark follow the phone. |

## 3. Screens and navigation

```
Map (full screen, always there)
 ├─ on the map: gear (top left) · "Near place" label with pet (top) · recenter button
 └─ sheet (always up; three heights: peek, half, full)
     └─ NavigationStack inside the sheet
         ├─ List:    search bar · All · Trains · Buses · ★ favorites (≤3) · nearby cards
         │   └─ Stop detail:  arrivals by line and direction · alert text · ★
         └─ Search:  focusing the bar raises the sheet to full; favorites first, then matches
 Presented over everything:
  ├─ Settings:  Devices · Pet · Device settings · Find my handheld · About
  └─ "Dock found · Set up" card → setup steps (section 6)
```

- **Sheet:** opens at half height and can't be dismissed (`interactiveDismissDisabled`). At peek and half, the map stays usable behind it (`presentationBackgroundInteraction(.enabled(upThrough: .medium))`).
- **The map follows the sheet:**
  - On the list: pins for the listed stops, plus the train lines and station dots.
  - On stop detail: it zooms to that line, dims the other lines, and shows the line's vehicles live (section 5).
  - Going back to the list restores the previous camera and stops live polling.
- **Line filter:** tapping a drawn line filters the list to that line's cards. The filter shows as a chip next to the switch, and its × clears it.
- **First launch:**
  1. The map appears and iOS asks for When In Use location.
  2. One line appears: "Bluetooth finds your dock and handheld." Then iOS asks for Bluetooth.
  3. Nothing else blocks the map.
- **Presentation:** a view can present only one sheet, and the always-up sheet holds the base view's slot. Settings and the setup card are therefore presented from inside the sheet's content. The gear on the map sets a flag on `AppModel` that the sheet reacts to.

## 4. Main screen

### 4.1 Map

- **Train lines:** all 8, drawn as `MapPolyline` in their CTA colors from the bundled `train_shapes.json` (section 7.3).
- **Station dots:** a small dot for every train station (about 145), from the parent stations in `stops.json`.
- **Bus stops:** pins only for stops that are in the list.
- **"Near *place*" label:** the pet, then the street or neighborhood at the map center ("Near Belmont & Sheffield"). It comes from `CLGeocoder` reverse geocoding, run 1 s after the camera stops moving.
- **Panning** moves the list to the new map center. **Recenter** snaps the map and the list back to the user.
- **Line taps:** MapKit doesn't report taps on polylines. A `MapReader` converts the tap to a coordinate, and the app picks the nearest drawn line within 20 pt.

### 4.2 List

- **One card per route and direction, at its nearest stop.** Six stops on one street served by route 3 northbound make one card, at the closest of them.
- **Order:** by distance to the card's stop, then by soonest arrival.
- **Radius:** stops within 800 m of the map center. If none are that close, the nearest 5 stops at any distance.
- **Switch:** All (default) · Trains · Buses filters the cards. The radius and fallback apply to the current setting: under Trains with no station within 800 m, the nearest 5 stations show; under All, the fallback runs only when no stop of either kind is within 800 m.
- **Favorites:** the ★ section sits above the nearby cards, up to 3, fetched live wherever the user is. A favorite that's also nearby shows only in ★.

### 4.3 Card

- **Background:** the line's CTA color, full width. All buses share one color (base spec 6.5).
- **Text color:** white, or a dark color on any line color where white fails WCAG 4.5:1 (Yellow at least). A test checks every pair (section 9).
- **Left:** the big route number for buses ("22") or a line badge for trains ("Red Line"); an arrow and the destination ("→ Howard"); the stop name. No CTA logo.
- **Right:** big minutes in the pixel font with a live icon. Under 1 minute shows "Due". A timetable-only arrival (`live: false`) shows a clock time ("10:05") with no live icon.
- **Below:** later departures in one small line ("also 12, 20 min"), then badges: amber Delayed and ⚠ for an active alert.

### 4.4 Refresh

- Every 30 s while the list is on screen and the app is in the foreground, plus pull-to-refresh.
- Only stops that produced a visible card (plus favorites) are fetched.
- Polling stops in the background and pauses while stop detail is open.

### 4.5 Pet

- About 28 pt tall in the "Near *place*" label, drawn by `PetSprites` with the user's color preset.
- Its mood uses the base spec's device-mood rules (6.3) over the listed stops. Only the 3 closest stops of either kind count for Hyped, the same as the handheld's `close` list. No location yet means Sleepy.
- Tapping it opens Settings → Pet.

## 5. Stop detail and live tracking

### 5.1 Detail

- Tapping a card pushes stop detail inside the sheet and keeps the sheet at half height.
- **Header:** stop name, ★, and "Live · updated 12 s ago".
- **Contents:** everything at the stop, grouped by line or route, then direction. The tapped card's group comes first. Each group shows up to 4 arrivals: minutes, destination, Live or Sched, and the badges.
  - **Trains:** the whole station, all lines and both directions.
  - **Buses:** every route at that direction-specific stop.
- **Alerts:** the full Customer Alerts text for the stop's routes, collapsed under a ⚠ row.
- Tapping a different group points the live map at that line.

### 5.2 Live map

- **Trains:** `ttpositions` for the line every 30 s. Each train is a marker in the line color with a heading arrow.
- **Buses:** `getvehicles` for the route every 30 s. The route path is drawn from `getpatterns`, cached for the session.
- **Framing:** fit the stop and the next 3 vehicles headed toward it. Vehicles going the other way are dimmed.
- **Motion:** markers animate to each new position over about 1 s.
- **Vehicle tap:** a small bubble with the destination and next stop ("→ Howard · next stop Belmont").
- **Stops polling** when the user goes back, the app leaves the foreground, or the phone locks.
- **Cost:** 2 calls every 30 s (positions, plus this stop's arrivals). List polling is paused meanwhile.

## 6. Devices, setup, and settings

### 6.1 "Found" card

- While the app is in the foreground, `BLEManager` scans for the CTA service UUID (base spec 10).
- A `CTA-Dock-*` or `CTA-Pet-*` the app hasn't set up slides up a small card: "Dock found · Set up" or "Handheld found · Set up".
- "Not now" hides that device until the next launch.
- "Known" means its `CBPeripheral.identifier` is in the app's device list.

### 6.2 Setup steps

Full screen, one step per screen. The same flow runs from Settings → Devices → Add device.

1. **Pair:** "Type the code shown on your dock." The app connects, reads `info`, and its first encrypted write triggers iOS's pairing prompt.
2. **Wi-Fi:**
   - The network name is pre-filled from `NEHotspotNetwork.fetchCurrent`. That needs the Access Wi-Fi Information entitlement and location permission. The field stays editable.
   - The user types the password. The app writes `wifi`, then reads `info` until `wifi.connected` is true, for up to 20 s.
   - On failure: "Couldn't join. Check the password."
   - On the handheld, this network is slot 0, labeled Home. Slots 1–2 are added later in Settings.
   - The password is never logged or stored on the phone.
3. **Dock stops (dock only):** pick 3 stops with the main search UI, favorites suggested first. Writes `config.stops`.
4. **Pet (first device only):** name and color preset. Devices added later get the existing pet.
5. **Done:** "Your dock is showing live arrivals." or "Your handheld is ready."

### 6.3 Settings

- **Devices:** each known device with Online or Not connected, last update, Wi-Fi network, and (handheld) battery and charging, all from `info`. A device's page has its Wi-Fi networks, its stops (dock), and Forget.
  - iOS doesn't let apps remove a Bluetooth bond. Forget removes the device from the app, then tells the user to finish in iOS Settings → Bluetooth.
- **Pet:** name and 6 color presets, with a live preview. Presets are palette swaps defined next to the art in `art/`.
- **Device settings:** screen brightness, LED brightness (dock only), and night hours.
- **Find my handheld:** a map with the last-seen pin and time, or "Connected now" (base spec 9.3).
- **About:** the app version and the CTA's data attribution line.

### 6.4 Config sync

- **Dock stops:** the dock is the source. The app reads `config.stops` on every connect, so a pick on the touchscreen shows up in the app (base spec 7.2, most recent pick wins).
- **Pet and device settings:** the app is the source. It writes them to each device's `config` on every connect and on every change.

## 7. Architecture and data flow

### 7.1 Modules

Follows the iOS plan's layout. New pieces are marked ✚.

```
CTAKit (local Swift package, shared by app + widget, tested against fixtures/cta)
  Models · parsers · MoodRules · StopRecordCodec · CTAClient · StopIndex · TrainLines
  ✚ TrainShapes (loads train_shapes.json)
  ✚ CardBuilder (radius, one card per route + direction, favorites dedupe)
  PetSprites
App
  AppModel (@Observable root: location, map center, filter, selection, favorites)
  ArrivalsStore   polls visible stops, keeps the last good results and their age
  ✚ LiveLineStore polls positions for the selected line only
  BLEManager · HandheldRelay · Settings screens
Widgets: read the App Group, fetch the first favorite themselves (base spec 9.4)
```

Views stay thin. Logic with branches (dedupe, moods, parsing, encoding) lives in CTAKit, where tests reach it.

### 7.2 Main screen flow

1. The map center (user location or panned point) goes to `StopIndex`, which returns stops within 800 m.
2. `CardBuilder` keeps the nearest stop per route and direction.
3. `ArrivalsStore` fetches those stops and the favorites: bus stops 10 per `getpredictions` call, train stations one `ttarrivals` call each.
4. Responses become stop records (base spec 6.2), then cards.
5. After each fetch, the first favorite's record is written to the App Group and `WidgetCenter.shared.reloadAllTimelines()` runs.

### 7.3 Build data

`tools/gtfs_build.py` also writes `train_shapes.json`: one simplified path per train line, using the most common trip pattern (the same rule as base spec 5). Target size: under 200 KB.

### 7.4 Storage

Small JSON in App Group `UserDefaults`: favorites, known devices, pet, device settings, the handheld's last-seen location, and the widget snapshot. No database.

### 7.5 Request budget

- An open list makes about 6–10 calls every 30 s (roughly 1,200 an hour), only while it's on screen.
- Stop detail makes 2 calls every 30 s.
- That's small next to the dock, which polls around the clock on the same keys.
- Intervals are constants in CTAKit's config, mirrored from the shared library, so they can be tuned once the key emails give the real daily limits.

### 7.6 Handheld relay

Unchanged from base spec 9.5 and milestone 7 of the iOS plan.

## 8. Errors

Adds to base spec 9.6.

| Situation | What the user sees |
|---|---|
| Location off | The map starts on the Loop. The sheet says "Turn on location to see stops near you," with a button to Settings. Search and ★ still work. |
| Far from Chicago (nearest stop over 30 km) | "No CTA stops near here" plus favorites. This overrides the nearest-5 fallback. |
| CTA error, bad key, or offline | A "CTA data unavailable" banner. The last cards stay, dimmed, with "updated N min ago." A train failure doesn't blank bus cards, and the other way around. |
| Stale data | Cards dim after 2 min and show their age after 5 min. |
| No predictions | "No arrivals", or "No service now" during the night hours set in Device settings. |
| Live map fails | The last positions stay with their age. Vehicles disappear after 2 min. |
| Reverse geocode fails | The label says "Near you". |
| Bluetooth off or denied | No found cards. Devices says "Bluetooth is off" or shows how to allow it. |
| Pairing fails | "Pairing didn't finish. Try again." |
| Wi-Fi join fails | "Couldn't join. Check the password." |
| Device disconnects | Automatic reconnect. Devices shows "Not connected" until it's back. |

## 9. Testing

- **CTAKit (Swift Testing):** the fixture tests that mirror the C++ suite (iOS plan milestone 1), plus:
  - Card dedupe: 6 stops on one route and direction give 1 card; a nearby favorite isn't duplicated.
  - Radius: the 800 m cutoff, the nearest-5 fallback under each switch setting, and the 30 km empty state.
  - Contrast: every line color with its chosen text color passes WCAG 4.5:1.
  - Pet mood with the 3-closest rule for Hyped.
  - Every BLE message the app writes encodes to 512 bytes or less.
- **`gtfs_build.py` self-check:** `train_shapes.json` has 8 lines, each with points inside Chicago's bounding box.
- **Previews:** SwiftUI previews fed from fixtures for every card state (live, sched, delayed, alert, due, no arrivals), in light and dark. No automated UI test suite for the alpha.
- **On hardware:** a TestFlight build on Leo's phone, walking demo items 3, 4, 5, 8, and 9.

## 10. Build order

The base spec's rule still holds: the core path comes first (map and cards, stop detail, favorites, BLE setup, dock stops, relay). The drawn train lines and live tracking are built with the other app extras (pet in the label, widgets, find my handheld) and slip first if time runs short.

## 11. Base spec edits

To fold in once this doc is approved:

- **§5 build-time data:** add `train_shapes.json`.
- **§6.6 polling:** add an app column: list every 30 s while on screen; selected line's positions every 30 s while stop detail is open; nothing in the background except the relay.
- **§9.1 main screen:** replace the Trains | Buses toggle with the mixed list and All · Trains · Buses switch. Add the pinned favorites, the one-card-per-route-and-direction rule, the 800 m radius, the drawn train lines, line taps, and the pet in the label.
- **§9.2 stop detail:** shown inside the sheet, covers the whole station or all routes at the bus stop, and drives the live map.
- **§9.3 gear menu:** add the found card and setup steps, pet color presets, About, and how Forget works.
- **§9.6 errors:** point to this doc's section 8.
- **§13 testing:** add the CTAKit cases from section 9.

## 12. Open items

- **Bundle ID and App Group ID:** Leo's choice, needed before the Xcode project exists.
- **Pixel font:** the same one as the dock and handheld, chosen with the final art. It must have a license (such as OFL) that allows bundling in the app and the firmware.
- **CTA attribution:** confirm the exact wording the CTA's developer terms require when the keys arrive.
- **`ttarrivals` per call:** confirm in the Train Tracker docs whether one call can take more than one station. If it can, the request budget drops.
- **Daily limits:** from the key emails (base spec 15).

## 13. Out of scope

Trip planning and a Home button, live vehicles without a selected line, bus routes drawn by default, free color pickers, a tab bar, and an automated UI test suite.
