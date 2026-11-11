# iOS performance pass and bus restyle: handoff (2026-11-10)

This is where the work stands and what to do next, so a fresh session can pick it up. Everything below is
**uncommitted** in the working tree on `main`, on top of commit `5354b33`.

## Goal and rules (Leo)

- The goal: make the app fast and stop it heating the phone.
- "It is crucial that no functionality is lost when optimizing. This should only improve the app."
- Architecture changes need Leo's OK first. He approved three: a single location source, a 5° heading filter and a 60 fps cap.
- The workflow is [Agent-Workflow.md](../../Agent-Workflow.md) with a worker and fresh Opus xhigh reviewers per
  [reviewer.md](../../reviewer.md), at most 3 rounds. Report each round's Blockers and Should-fixes to Leo.
- Commits: name the paths, never `git commit -a`, and no attribution lines. Never stage
  `docs/superpowers/specs/2026-11-03-cta-tracker-design.md` (Leo's own edit) or `docs/ideas/2026-11-10-dock-screen-mockup.*`
  (not part of this work).

## Where it stands

| Step | State |
|---|---|
| Worker pass 1: rendering and data fixes | Done |
| Worker pass 2: location, heading, 60 fps, list schedule | Done |
| Leo's UI changes: kind icons, white bus cards, bus stop page | Done (made in the main session) |
| Review round 1 (Opus xhigh) | Done: 1 Blocker, 5 Should-fix, nits (below) |
| Worker: round 1 fixes plus Leo's 3 additions | Done, and verified in the simulator (see "Evening update") |
| Review round 2 (Opus xhigh) | Done: no blockers, 3 should-fixes. **All round 1 findings fixed.** |
| Spec Revision 4 | **Approved by Leo**; written in `2026-11-05-ios-redesign-design.md`, uncommitted |
| Round 2 should-fixes and nits | **Done 2026-11-11** in the main session; 134 tests pass, ⇄ verified at Jackson & Austin Terminal (same stop) |
| Install on iPhone | Done with the round 2 fixes (2026-11-11) |
| Commit | Not done |

At handoff, the tree builds (Release, simulator) and `swift test --package-path ios/CTAKit` passes **134 tests**.

## Evening update (2026-11-10, second pause)

**Verified in the simulator**
- Resting list CPU: **5.2% averaged over 60 s** (median 1.2%), against pass 1's 5.3%.
- The blink survives backgrounding.
- The bus ⇅ switch works (29 State & Jackson SB ⇄ State & Adams NB).
- The green live glyph and the kind icons render.

**Round 2 should-fixes** (all done 2026-11-11, along with the cheap nits below; the two questions for Leo stay open)
1. **⇅ at stops that serve both directions** (610 stop-and-route pairs, for example Jackson & Austin Terminal, route 126).
   - *Done:* `OppositeStop.find` now returns the same stop (0 m) when it already serves the opposite line, with a test.
   - *Not done:* the app side. `StopDetail`'s `DirectionSwitch` action still sets `model.openStop` to that same ID, and
     `openTapped` ignores a same-stop open, so **the button currently does nothing at those stops**.
   - Fix: when `opposite.stop.id == stop.id`, do `withAnimation(Theme.snappy) { selected = opposite.line }` instead. Then
     check that ★ saves the shown line.
2. **`ScheduledTag` uses `.fixedSize()`** (`Design/RouteCard.swift` about line 233). At AX2 and up it overflows the later
   tiles and squeezes LineCards ("→ Howard" truncates). Use `.lineLimit(1).minimumScaleFactor(0.5)`, or stack the
   LineCard's right column at AX sizes. Check at AX5 with a scheduled arrival.
3. **VoiceOver on LineCards** (`Stop/StopDetail.swift` about lines 267-269) never says "scheduled". Append it when `!next.live`.

**Cheap nits sent with them (not done)**
- FreshnessLine ticks every second forever. After 60 s of age, step to the next minute or stale boundary.
- The list's live glyphs keep blinking under detail. Use `blinks: fresh && !paused` with `cardClocksPaused`.
- Correct two contrast comments in `Theme.swift`: busText is 5.29:1 on the card bottom, and Pink's top gives 4.49:1 for the
  scheduled blue.
- AlertsRow on a bus page: outline its chevron (`Text(Image)`) and its pixel ⚠ (`pixelOutline`).

**Questions for Leo** (answered 2026-11-11: yes to both, done; spec Revision 4 updated)
- **CPU spikes** (20–36% for about 1 s per minute change, simulator) come from the spring on the minutes' push transition
  (`RouteCard.swift` about line 181, `Theme.snappy`). A short non-spring ease would cut them about 3–4×, but the minutes
  would roll over a little more plainly.
- **Small text outline:** gray text on the 2 pt light-blue outline is about 3.3:1 for small text ("updated 12 s ago",
  stop names). A 1 pt outline under about 20 pt would fix it. The spec says 2 pt, so change both if Leo agrees.

**Other notes**
- The round 2 reviewer couldn't tell whether the CA blink keeps the render server busy; measure that on the device.
- The pet's 2 Hz bob is the ~1.2% baseline (pre-existing).
- The physical iPhone's UDID was removed from this doc (public repo); it's `<your-iphone-udid>` below. Find it with
  `xcrun devicectl list devices`.

**Then:** finish the round 2 fixes through the worker, verify, install on the iPhone, and commit named paths. Include the
approved spec Revision 4 and this doc. A round 3 review is optional: the workflow allows it, but round 2 had no blockers.

## What changed, by file

**Performance (passes 1 and 2)**

- `Map/MapScreen.swift`: the full-screen wash is a `CAGradientLayer` (`Wash`, `GradientLayer`). The controls and the pet mood
  live in their own `MapControls` view, so arrivals don't rebuild the map. Location runs only while the scene is not in
  the background (`setActive(scenePhase != .background)`).
- `Design/Theme.swift`: `Plate` is Core Animation layers rasterized once (`PlateLayers`). `CardGradient` is the fill type.
- `Design/RouteCard.swift`: `LiveGlyph` blinks with a `CAKeyframeAnimation` (`BlinkLayer`). It now restarts on returning to the
  foreground and keeps `isRemovedOnCompletion = false` (round 1 Blocker).
- `Design/Pixel.swift`: `PixelText` caches its images.
- `Sheet/CardList.swift`:
  - Rectangular clips replace the alpha masks.
  - Each `Card` has its own `TimelineView(ChangeSchedule)`. A list-level `orderSchedule` (`CardOrder.nextChange`) only
    reorders, and is `.never` while detail, search or a location denial is up.
  - Card clocks pause while detail is open (`cardClocksPaused`).
- `Stop/StopDetail.swift`: the page runs on a `ChangeSchedule` instead of a 1 s clock. `FreshnessLine` keeps its own 1 s clock.
- `CTAKit/ArrivalFormat.swift`, `StopDetail.swift`, `CardBuilder.swift`:
  - `ArrivalFormat.nextChange` and `DetailGroups.nextChange` give exact tick times.
  - `CardBuilder.slots` does one distance pass and a partial sort, with identical results.
  - Tests: `ListNextChangeTests`, `DetailNextChangeTests`, `CardBuilderSpeedupTests`.
- `Map/DioramaMap.swift`: cached map styles (`navy3D`, `navy2D`) and station art and features. `.frameRate(range: 30...60,
  preferred: 60)`. The puck comes from `.locationDataModel(puck)`.
- `App/LocationService.swift` (**new, untracked: must be added in the commit**):
  - The app's only `CLLocationManager`, at NearestTenMeters with a 5 m distance filter.
  - `headingFilter` 5 and portrait orientation.
  - It feeds Mapbox's puck through one `LocationDataModel`.
  - `coordinate`, which the list uses, moves only after 30 m.
  - Moved out of `AppModel.swift` because importing MapboxMaps there made `Slot` ambiguous.

**Leo's UI changes (all of 2026-11-10)**

- Every list card has a kind icon at the bottom right: `tram.fill.tunnel` for trains, `bus.fill` for buses. It stacks under
  the footer at accessibility sizes.
- **Buses are white, gray and light blue everywhere.**
  - `Color.card`, `cardText` and `cardGradient` return `Theme.busCard` / `busText` for any non-train route.
  - `Color.cardOutline(route)` returns `Theme.busOutline` (`#7FCBF2`) for buses and nil for trains.
  - Text is outlined by the `OutlinedText` TextRenderer through `.outlinedText(_:)`.
  - Pixel art is outlined through `.pixelOutline(_:pixel:)`.
  - `PlainText` / `.plainText()` exempts amber Delayed badges, SCHEDULED tags, `UnavailableBanner` and alert paragraphs.
  - This covers cards, the stop page (near-white wash, header on a white plate for buses, tiles, chips), the hero
    number and the search direction buttons. Train values are unchanged.
- **Live glyph:** light green `Theme.live` (`#7CF29A`) everywhere, blinking only while live and fresh. On white it gets a gray halo.
- **Scheduled arrivals:** a still blue glyph (`Theme.scheduled`, `#9AD0FF`) plus a `ScheduledTag` ("SCHEDULED") under the
  clock time, on cards, tiles and LineCards.
- **Bus direction toggle:**
  - A `DirectionSwitch` button beside the bus stop page header (⇅ or ⇄) jumps to the nearest stop within **400 m** serving the
    same route the opposite way.
  - The logic is `OppositeStop.find` in CTAKit, with tests. The button is hidden when there's no such stop.

## Next steps, in order

1. **Verify the worker's last batch**, which was stopped before its own checks:
   - Release simulator build.
   - Resting list CPU: it should be at or below pass 1 (about 1.4% between ticks, 5.3% averaged over 60 s). Pass 2 measured
     5.7% before the per-card clocks.
   - Bus stop page CPU.
   - Screenshots: a live train card, a live bus card, a scheduled card, a bus stop page with the toggle and after toggling,
     a train stop page (unchanged), an amber Delayed badge, and AX5.
   - The blink after backgrounding the app for about 4 s.
2. **Review round 2.** Use a fresh Opus xhigh reviewer with this file, the two workflow docs, and base `5354b33`. Give it the
   round 1 findings so it can check they're fixed, not the worker's summary. Tell Leo the findings, then fix them through a
   worker. Round 3 only if needed.
3. **Propose spec text to Leo before committing.** The redesign spec
   (`docs/superpowers/specs/2026-11-05-ios-redesign-design.md`) still says buses use the bus blue card and the stop page
   wash is the line's dark shade. Add a revision for:
   - the white bus style with outlines
   - kind icons
   - the green and blue glyphs with the SCHEDULED tag
   - the bus direction toggle (400 m)
   - single location source, 5° heading and 60 fps
   - exact-change clocks instead of 15 s and 1 s ticks
4. **Install on Leo's iPhone (Release),** then **commit named paths** with no attribution. Suggested split:
   `perf(ios): …` for the performance work and `feat(ios): …` for the UI. Include `ios/CTATracker/App/LocationService.swift`.

## Round 1 review findings, for round 2 to check

- **Blocker:** the blink stopped after backgrounding. *Fixed in code, not yet verified.*
- Should-fix 1: the list clock ticked far more than 15 s (about 530 per 10 min at Belmont, about 2,300 in the Loop) and kept
  ticking under detail. *Restructured (per-card clocks, order-only list clock, paused under detail); not yet measured.*
- Should-fix 2: the bus stop page header was about 2:1 on the faded wash. *Header plate added; check it.*
- Should-fix 3: the light-blue live glyph was 1.8:1 on white. *Superseded by Leo's green with a gray halo; check contrast.*
- Should-fix 4: the outline reached badges and banners. *`.plainText()` added; check.*
- Should-fix 5: the kind icon squeezed the footer at AX sizes. *Uses `row(...)` now; check at AX5.*
- Should-fix 6: spec update (step 3 above).
- Nits:
  - The freshness line's own 1 s clock can disagree with the tiles for up to 1 s.
  - Exact-800 m radius test: *case added*.
  - `MapScreen` comment overstated the savings: *comment corrected; the map still rebuilds on pans through the bus pins*.
  - The outline renderer draws each text 9 times (event-driven, so acceptable).
  - Search buttons' arrows: *now `Text(Image)`*.
  - Measure on the device whether the blink animation keeps the render server busy.

## Open questions for Leo

- On the phone: does the app still run warm? Look at Settings → Battery after about 10 minutes on the map.
- If the list's live-minute ticks still cost too much, they could snap to a 15 s grid. Minutes would again show up to 15 s
  late, as before the pass.

## Commands

- **Tests:** `swift test --package-path ios/CTAKit`
- **Simulator build** (iPhone 18 Pro): `cd ios && xcodebuild -project CTATracker.xcodeproj -scheme CTATracker -configuration
  Release -destination 'id=3274EF24-B6AA-47F0-8B82-4434F8369013' -derivedDataPath ../build/sim build`
- **Device build:** same command with `-destination 'id=<your-iphone-udid>' -derivedDataPath ../build/dd
  -allowProvisioningUpdates`
- **Install:** `xcrun devicectl device install app --device <your-iphone-udid>
  build/dd/Build/Products/Release-iphoneos/CTATracker.app`
- **Launch:** `xcrun devicectl device process launch --terminate-existing --device <your-iphone-udid> io.github.leow427.CTATracker`
- **CPU** while the app rests in the simulator: `ps -o %cpu= -p $(pgrep -f CTATracker.app/CTATracker)` sampled every
  second for 60 s.
- **Backup:** a local copy of the whole uncommitted diff, plus the new `LocationService.swift`, is in `build/handoff/`
  (gitignored).
