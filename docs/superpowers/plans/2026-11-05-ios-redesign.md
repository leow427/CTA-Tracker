# iOS redesign implementation plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Restyle the working milestone 2 app into the redesign: a Mapbox diorama map, a card list that scrolls over the map instead of a sheet, thick line-colored tiles, and a Transit-style stop detail.

**Architecture:** The map becomes a Mapbox SwiftUI `Map` in its own view. The sheet is removed. The list, and the stop detail pushed from it, live in one `NavigationStack` layered over the map, inside a host view that lets touches above the list's top edge fall through to the map. Data, polling, and CTAKit logic are untouched, except two small pure additions (sun times, tile colors).

**Tech Stack:** SwiftUI on iOS 26, Mapbox Maps SDK 11.34.1 (`MapboxMaps` via Swift Package Manager), Swift Testing for CTAKit.

**Spec:** `docs/superpowers/specs/2026-11-05-ios-redesign-design.md`, amending `docs/superpowers/specs/2026-11-04-ios-app-design.md`. Read both.

## Global Constraints

- The iOS plan's global constraints (`docs/superpowers/plans/2026-11-03-cta-ios-app.md`) still hold, with two exceptions: the deployment target is iOS 26.0, and `mapbox-maps-ios` is the one allowed third-party package.
- The Mapbox token is read from `ios/CTATracker/Secrets.plist` key `MAPBOX_TOKEN` and set with `MapboxOptions.accessToken` at launch. It is never in `Info.plist`, code, logs, or git.
- Colors: line colors from `LineColors`; Ink `0x1C2A4A`; Paper `0xF6F2EA`; Amber `0xFFB000`; Signal green `0x34C759`. No system blue.
- Type: SF Pro Rounded (`design: .rounded`). Route numbers and minutes heavy, destinations bold, secondary lines medium.
- Pixel art: whole-number scales, `.interpolation(.none)`, never rotated.
- The iOS plan's polish bar applies to every task (light and dark, Reduce Motion, haptics, checked on the phone with screenshots). Accessibility work stays on hold as that plan says.
- Out of scope here, left to iOS milestone 4: line taps and the line filter, station dots, live vehicles, the pet in the label. This plan only makes sure they'll draw on Mapbox.

## Review Focus

1. **Touches in the open map area** must pan the map at rest, after scrolling, and on stop detail. A touch on a card must still scroll the list.
2. **No token, or a bad one:** the app must still launch and the list must still work over a blank map. Mapbox can't render without it, so log nothing and show no crash.
3. **Location off:** the map starts on the Loop, the "Turn on location" state shows in the list, and nothing tries to follow a missing puck.
4. **Search focus:** focusing the search field scrolls the list up so the field pins at the top and results show above the keyboard.
5. **Night:** at night the L lines and tiles stay readable over the dark map, and the scrolled-up background is Ink in dark mode.

---

### Task 1: Mapbox diorama map (checkpoint A)

The sheet stays for now. Only the map changes.

**Files:**
- Modify: `ios/CTATracker.xcodeproj/project.pbxproj` (deployment target `26.0` in both configs; add the remote package `https://github.com/mapbox/mapbox-maps-ios.git`, exact `11.34.1`, product `MapboxMaps` linked to the app target)
- Modify: `ios/Secrets.example.plist` (add `MAPBOX_TOKEN` = `YOUR_MAPBOX_PUBLIC_TOKEN`)
- Modify: `ios/CTATracker/Data/ArrivalsStore.swift` (`Secrets`)
- Modify: `ios/CTATracker/App/CTATrackerApp.swift`
- Create: `ios/CTATracker/Resources/train_shapes.json` (a copy of `data/train_shapes.json`)
- Create: `ios/CTAKit/Sources/CTAKit/TrainShapes.swift`
- Create: `ios/CTATracker/Map/DioramaMap.swift`
- Modify: `ios/CTATracker/Map/MapScreen.swift`
- Test: `ios/CTAKit/Tests/CTAKitTests/MainScreenTests.swift`

**Interfaces:**
- Produces: `Secrets.mapbox: String?`; `public struct TrainShape: Decodable, Sendable { public let route: String; public let points: [[Double]] }` (each point `[lat, lon]`); `public enum TrainShapes { public static func decode(_ data: Data) throws -> [TrainShape] }`; `struct DioramaMap: View`, which takes `following: Binding<Bool>`, `recenterTick: Int`, `focus: CLLocationCoordinate2D?` (the open stop), `bottomInset: CGFloat`, and `preset: LightPreset` (`.day` until Task 2), and reports the settled visible center through `onCenter: (CLLocationCoordinate2D) -> Void`.

- [ ] **Step 1: Write the failing test** `trainShapesDecodeTheBundledFile`: decode `data/train_shapes.json` (read through the repo-root path that `Fixture.swift` already uses), then expect 10 shapes, 8 distinct routes equal to `LineColors.trains.map(\.route)`, and every point inside lat 41.6...42.1 and lon -88.0...-87.5.
- [ ] **Step 2: Run it and confirm it fails** with `swift test --package-path ios/CTAKit --filter trainShapes`. Expected: does not compile, because `TrainShapes` is missing.
- [ ] **Step 3: Implement `TrainShapes.decode`** as a plain `JSONDecoder` call.
- [ ] **Step 4: Run the CTAKit suite** with `swift test --package-path ios/CTAKit`. Expected: all pass.
- [ ] **Step 5: Wire the project.** Set the deployment target and add the package. Add `mapbox` to `Secrets` using the same `key(_:)` rule as the CTA keys. In `CTATrackerApp.init`, set `MapboxOptions.accessToken` only when `Secrets().mapbox` is non-nil. Copy `train_shapes.json` into Resources. The folder is a synchronized group, so it's included automatically.
- [ ] **Step 6: Build `DioramaMap`.** Use `Map(viewport:)` with `.mapStyle(.standard(theme: .faded, lightPreset: preset, showPointOfInterestLabels: false, show3dObjects: true))`. Hide the scale bar and compass. Place the logo and attribution at bottom-left and bottom-right, `bottomInset` above the bottom.
  - Draw two lines from one `GeoJSONSource` of `TrainShapes` (a `route` property per feature): a white casing (`l-case`) and the line (`l`, colored by a `match` on `route` to `LineColors`). Put both in slot `.middle`, with `lineEmissiveStrength(1)` and `lineOcclusionOpacity(0.35)`. Width interpolates by zoom, about 3 pt at z12 to 7 pt at z16, and the casing is 2 pt wider.
  - Bus pins are a `CircleLayer` with the same rule as the old `busPins`: bus color, white 2 pt stroke. The user is `Puck2D` with bearing.
  - Following uses `.followPuck(zoom: 15.5, pitch: 55)`. Otherwise use the Loop (`AppModel.loop`), zoom 14.5, pitch 55. `focus` flies to the stop at zoom 15.5, and clearing it restores the camera from before. Both animate with `.smooth`, or jump under Reduce Motion.
  - The viewport padding's bottom is `bottomInset`. A user drag clears `following`. When the camera settles after a drag, report the center of the padded area through `onCenter`.
- [ ] **Step 7: Swap `MapScreen`'s MapKit `Map` for `DioramaMap`.** Keep the sheet, the controls, and the existing follow, recenter, and detail logic, moved to the new view's inputs. Pass `bottomInset` as the old `covered` value. In `NearLabel.name`, replace `CLGeocoder` with `MKReverseGeocodingRequest` and keep the same fallback order. That is MapKit's only remaining use.
- [ ] **Step 8: Check it on the simulator and the phone.** With a token, the map is muted 3D Chicago, all 8 lines are colored and show through buildings, the puck is followed, panning moves the list, and recenter works. Without a token, the app launches, the list works, and the map is blank. Take light and dark screenshots.
- [ ] **Step 9: Commit:** `iOS redesign checkpoint A: Mapbox diorama map`.

### Task 2: Light follows the sun

**Files:**
- Create: `ios/CTAKit/Sources/CTAKit/SunTimes.swift`
- Modify: `ios/CTAKit/Sources/CTAKit/Config.swift` (`twilightMin = 45`)
- Modify: `ios/CTATracker/Map/MapScreen.swift`
- Modify: `docs/superpowers/specs/2026-11-05-ios-redesign-design.md` §6 Light
- Test: `ios/CTAKit/Tests/CTAKitTests/SunTimesTests.swift`

**Interfaces:**
- Produces: `public enum LightPreset: String, Sendable { case dawn, day, dusk, night }`; `public enum SunTimes { public static func riseSet(on day: Date, lat: Double, lon: Double) -> (rise: Date, set: Date)?; public static func preset(at now: Date, rise: Date, set: Date) -> LightPreset }`.
- Spec change: sunrise and sunset are computed on the phone (the NOAA solar equations), not taken from Open-Meteo. The app doesn't fetch weather until milestone 4, and this needs no network. Update §6 to say so, and drop the 7-to-19 fallback.

- [ ] **Step 1: Write the failing tests.**
  - `chicagoSummerSolstice`: `riseSet` for 2026-06-21 at (41.8819, -87.6278) gives rise 10:15 UTC and set 01:29 UTC the next day, each within ±3 min.
  - `chicagoWinterSolstice`: 2026-12-21 gives rise 13:15 UTC and set 22:22 UTC, each within ±3 min.
  - `presetEdges`: with rise R and set S:
    - R−46 min is night, and R−45 min is dawn.
    - R+45 min is dawn, and R+46 min is day.
    - S−46 min is day, and S−45 min is dusk.
    - S+45 min is dusk, and S+46 min is night.
  - `polarDayReturnsNil`: `riseSet` at lat 89.9 on 2026-06-21 is nil, and the caller treats nil as day.
- [ ] **Step 2: Run them and confirm they fail** with `swift test --package-path ios/CTAKit --filter SunTimes`.
- [ ] **Step 3: Implement both functions.** Use NOAA's sunrise equation with a zenith of 90.833°, computed in UTC. Use `Config.twilightMin` for the windows.
- [ ] **Step 4: Run the suite.** Expected: all pass.
- [ ] **Step 5: Wire the preset in `MapScreen`.** A `TimelineView(.everyMinute)` computes the preset for today at the map center and passes it to `DioramaMap`. Then update spec §6.
- [ ] **Step 6: Check it.** Temporarily set the date to dusk and to night on the simulator. The lines and puck stay bright at night. Take screenshots.
- [ ] **Step 7: Commit:** `iOS redesign: map light follows the sun`.

### Task 3: The list over the map (checkpoint B, the main risk)

**Files:**
- Create: `ios/CTATracker/Map/PassThroughHost.swift`
- Rename: `ios/CTATracker/Sheet/RouteSheet.swift` to `ios/CTATracker/Sheet/CardList.swift` (`struct CardList`)
- Modify: `ios/CTATracker/Map/MapScreen.swift`, `ios/CTATracker/App/AppModel.swift`

**Interfaces:**
- Consumes: `DioramaMap(bottomInset:)` from Task 1.
- Produces: `struct PassThroughHost<Content: View>: UIViewRepresentable` with `init(openAbove: CGFloat, @ViewBuilder content:)`. It hosts `content` in a `UIHostingController` whose view sits inside a container `UIView`. The container's `hitTest` returns nil for points with `y < openAbove`, so the map below gets them, and otherwise defers to `super`. `AppModel.listTop: CGFloat` is the on-screen y of the list's top edge, and it replaces `sheetTop`.

- [ ] **Step 1: Spike the pass-through.** Do this before anything else. Put a plain `ScrollView` of colored rectangles in `PassThroughHost` over `DioramaMap`. Set `openAbove` from `onScrollGeometryChange` as `restTop − contentOffset.y`. On the phone, confirm three things:
  - the map pans above the list, at rest and after scrolling;
  - cards scroll;
  - the edge back-swipe still works inside a `NavigationStack`.
  If a host container can't pass touches, stop and report what failed. Don't fall back to a sheet.
- [ ] **Step 2: Build `CardList`.** It's a `NavigationStack` with `.containerBackground(.clear, for: .navigation)` and the existing content, search, polling, and refresh logic, minus everything about detents.
  - Set `restTop = 0.55 × screen height`. The scroll view gets `.safeAreaPadding(.top, controlsBottom)` and `.contentMargins(.top, restTop − controlsBottom, for: .scrollContent)`, so it can't rest lower than that.
  - The search capsule and the switch are the pinned header of a `LazyVStack(pinnedViews: [.sectionHeaders])` section.
  - The background (Paper in light, Ink in dark) fades in as `listTop` moves from `controlsBottom + 120` to `controlsBottom`.
  - Focusing search scrolls to the top using a `ScrollPosition`.
- [ ] **Step 3: Update `MapScreen`.**
  - Remove `.sheet` and `detent`.
  - Layer it as `ZStack { DioramaMap; PassThroughHost(openAbove: model.listTop) { CardList() }; controls }`.
  - Pass `DioramaMap` a constant `bottomInset = screen height − restTop`.
  - Settings is now a `.sheet` on `MapScreen` itself, since the slot is free.
- [ ] **Step 4: Check every Review Focus item on the phone:** 1 (touches), 3 (location off), and 4 (search focus). Also confirm pull-to-refresh works at rest. Take screenshots.
- [ ] **Step 5: Commit:** `iOS redesign checkpoint B: card list over the map`.

### Task 4: Tiles, glass, and type (checkpoint C)

**Files:**
- Modify: `ios/CTAKit/Sources/CTAKit/Config.swift` (`LineColors`)
- Modify: `ios/CTATracker/Design/Theme.swift`, `ios/CTATracker/Design/RouteCard.swift`
- Modify: `ios/CTATracker/Map/MapScreen.swift` (`MapButton`, `NearLabel`)
- Test: `ios/CTAKit/Tests/CTAKitTests/BasicsTests.swift`

**Interfaces:**
- Produces:
  - `LineColors.busTile: UInt32 = 0x1C2A4A`, plus `LineColors.tile(_ route: String) -> UInt32` (a train's color, else `busTile`). `LineColors.bus` stays `0x565A5C` for the devices.
  - `LineColors.edge(_ rgb: UInt32) -> UInt32`, which multiplies each channel by 0.75 and rounds down.
  - `Theme.ink`, `Theme.paper`, `Theme.signal`, `Theme.cardRadius = 22`, `Theme.cardGap = 12`, `Theme.tileEdge = 4`, `Theme.pressDepth = 3`.
  - `struct TileStyle: ButtonStyle`, which takes `rgb: UInt32` and replaces `PressableCard` for cards and detail tiles.
  - `func glassBackground<S: Shape>(_ shape: S) -> some View`, which replaces `materialBackground`.

- [ ] **Step 1: Write the failing tests.**
  - `edgeDarkensByAQuarter`: `edge(0xFFFFFF) == 0xBFBFBF` and `edge(0x00A1DE) == 0x0078A6`.
  - `busTileContrast`: `contrast(textColor(on: busTile), busTile) >= 4.5`, and the text color is white.
  - Add `busTile` and each `tile(route)` to the existing every-color contrast loop at `BasicsTests.swift:24`.
- [ ] **Step 2: Run them and confirm they fail.** Then implement, then run the suite. Expected: all pass.
- [ ] **Step 3: Build `TileStyle`.** Layer the shapes back to front:
  - a soft shadow (black 12%, radius 16, y 10);
  - the edge shape, in `edge(rgb)`, offset `tileEdge` down;
  - the face, in `rgb`, with a contact shadow (black 18%, radius 2, y 2);
  - a 1 pt top highlight (white 25%, fading out by about 30% of the height).
  
  When pressed, the face and label move down `pressDepth` on `.snappy`, and the contact shadow tightens. Add `.sensoryFeedback(.impact(weight: .light), trigger:)` on press.
- [ ] **Step 4: Restyle `RouteCard` to spec §3.**
  - Use the tile fill from `LineColors.tile`, keeping the dimming on stale cards.
  - The route number and the line name are both rounded heavy text. The line name replaces the outlined capsule.
  - Minutes are rounded heavy, with `PixelImage` of the live glyph, which replaces the pixel-font minutes. Keep the two-frame flip as a vertical push transition, skipped under Reduce Motion.
  - Chips are white at 20%, and Delayed is amber.
- [ ] **Step 5: Move map controls and the Near capsule to glass.** Use `.glassEffect(.regular.interactive(), in: shape)`. Under Reduce Transparency, use solid Paper or Ink. Text and symbols use the primary label color.
- [ ] **Step 6: Check it.** Look at every card state in previews (live, sched, delayed, alert, due, no arrivals, stale) in light and dark, and look at the phone at night. Take screenshots.
- [ ] **Step 7: Commit:** `iOS redesign checkpoint C: tiles, glass, and rounded type`.

### Task 5: Stop detail (checkpoint D)

**Files:**
- Modify: `ios/CTATracker/Stop/StopDetail.swift`, `ios/CTATracker/Sheet/CardList.swift`

**Interfaces:**
- Consumes: `TileStyle`, `LineColors.tile`, `LineColors.edge` (Task 4), and `PassThroughHost` (Task 3). `DetailGroups.groups` and `DetailState` are unchanged.

- [ ] **Step 1: Add the zoom transition.** Cards get `.matchedTransitionSource(id: slot.id, in: ns)`, and detail gets `.navigationTransition(.zoom(sourceID:in:))`. Search rows use the stop ID.
- [ ] **Step 2: Lay out the detail to spec §4.**
  - The content starts at 40% of the screen height, and `model.listTop` is set to that, so the map above stays live.
  - The header holds the route number or line name and "→ destination".
  - Departure tiles sit in a horizontal `ScrollView`. The first is 1.4× the size of the rest, and the later ones use `edge(tile)` as their face. Each shows minutes, the live glyph or "Sched", and Due or a clock time exactly as cards do.
  - The chip row holds Delayed, ⚠ (tap to expand the alert text in place), ★, and "Live · updated 12 s ago".
  - "Also at this stop" shows the other groups as compact tiles. Tapping one makes it the selected group, as the current `selected` state does.
- [ ] **Step 3: Remove the code the redesign replaced:** `TileFit` and the pixel-font minutes path, if nothing else uses them.
- [ ] **Step 4: Check it.** Open detail from a card and from search, then swipe back. The camera returns, and polling switches back to the list. Check a station with several lines, a bus stop, and an alert. Take light and dark screenshots.
- [ ] **Step 5: Commit:** `iOS redesign checkpoint D: stop detail`.
