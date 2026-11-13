# To do

## Bugs

Labelled B1, B2, … in order of importance.

### B1. Handheld cards look jagged while scrolling (open investigation)

On the handheld (wearable), scrolling the card list doesn't look smooth: the cards' edges look mildly
jagged, and one side of the screen seems to move at a different rate from the other, or a little before it.

Not diagnosed yet. Leads to check first:

- **Tearing.** Frames go out in strips of changed rows from a task on core 0 while core 1 draws the next strip
  (`firmware/handheld/mock/src/main.cpp`), with no sync to the panel's refresh. The panel is mounted a quarter
  turn off, so its scan runs across the screen's columns: a frame caught mid-scan would show one side of the
  screen newer than the other, which matches what's seen. Check whether the CO5300 exposes a tearing-effect
  (TE) signal on this board, and whether sending only whole frames, or sending in step with TE, fixes it.
- **Frame time.** Measure draw and send time per frame while scrolling (the serial `p` profile, `plog`) to see
  whether scroll frames miss the refresh, and whether sends lag the draws.
- **CPU clock.** Make sure scrolling holds full speed for the whole gesture and its fling (`cpu_clock.h`).

Check Waveshare's and Espressif's examples and FAQs for this panel before custom fixes. Done when scrolling
looks even on the board; record what the cause was here.

### B2. Dock screen is still choppy: map 13 fps, picker 17 fps, aim for 24–30 (open)

The dock's screen was at 5–10 frames a second; the goal is at least 24–30 fps if the board allows it.

The first perf pass is on main (merged `68f177c`, 2026-11-13; rounds 1 and 3 kept, round 2 reverted and kept on
branch `dock-perf-r2`). It took the map pan from 7.1 to 12.9 fps and the picker scroll from 11.4 to 17.1 fps;
taps reach the screen 32–72% sooner and a frame at rest costs 21 ms instead of 91. Every number and run is in
`docs/perf/summary.md` and `docs/perf/log.md`.

Still to do:

- **Look at the real panel.** The firmware matches the old frames byte for byte on the laptop, but nobody has
  watched the panel since: Trackie's bob, the clock, the blink glyphs, the poke bubble, cards greying when Wi-Fi
  drops, the map (pan, recenter, faded lines, vehicles), the picker (scroll, fade, a real pick), the night veil,
  and no tearing or 32-row strip that lags. The summary has the checklist.
- **The copy into the PSRAM frame buffer (18 ms a moving frame) is the wall now.** A second core (round 2) and a
  tighter loop (round 3) didn't help; the time is in PSRAM itself. The likely way through is GDMA into the frame
  buffer with careful cache handling, and it's the only way the picker reaches 34 fps.
- **The map to 17 fps** needs about 18 ms off its 76 ms frame: profile a pan on the board (serial `g`), then one
  recorded call per line shape instead of per segment.
- **Panel setup** (the `bb_invalidate_cache` flag, pixel clock, bounce buffers) may speed up all drawing, but it
  was left alone because it fixed tearing: Leo's call, and any change must stay tear-free.

Frame rate moves in panel steps of 29.3 ms (34, 17, 11.4 fps), so 24 fps really means making the 34 fps step
(about 29 ms a frame) or settling for 17. Done when map pans and list scrolls measure at least 24 fps with
`tools/dock_perf_run.py`, or when it's clear why not and what the best rate is.

## Features

Ranked by importance, most important first (do F1 first). Labels: F = feature, B = bug.

### F1. Save at least 5 Wi-Fi networks on the handheld; join the strongest

Today the handheld knows one network (`WIFI_SSID` / `WIFI_PASS` in `firmware/secrets.h`), so it only gets
live data in one place.

- Hold a list of at least 5 SSID + password pairs (still in the git-ignored `secrets.h`, until the app sets
  them over BLE). Update `secrets.example.h` and the handheld README to show the list form.
- Scan, and of the saved networks in range, join the one with the strongest signal (RSSI).
- If the connection drops, scan again and move to the next strongest.
- Keep the first entry as home: docked detection (spec 8.4) depends on being on the home network.

**Why first:** without it the handheld is offline anywhere but one network, which matters more than how
it looks.

### F2. Tap Trackie to see why he feels that way

The user can tap Trackie and see the reason for his current mood (Sleepy, Sad, Worried, Happy, Hyped).
Right now it isn't clear why he's happy or sad.

- Show a short, plain reason, e.g. "Your bus is 12 min late" or "Train arriving in 2 min".
- The mood logic needs to return its reason along with the mood (`cta::Mood` in
  `firmware/lib/cta/src/cta_types.h`; sprites in `firmware/lib/pet/`).
- Decide how the tap is made on the handheld and how the reason is shown and dismissed.

**Why second:** it needs some design work first (input, layout, wording). Build it in the handheld's
current theme, which matches the app since 2026-11-11 (white bus cards, light green live glyph; see
`docs/handheld-ui.md`).

### F3. Trains and buses on the iOS map: a stop shows its line's vehicles, a tap focuses one

Today, tapping a card (e.g. the Red Line at a station) opens stop detail with the next few arrivals as minute
tiles, and the map shows no vehicles.

- Opening a stop shows every train (or bus) on that line on the map, where each is now: tap the Red Line at
  Belmont and the map shows all the Red Line trains.
- Tapping a departure tile, or a vehicle on the map, focuses that one: the map zooms to it, it stands out from
  the rest, and the page says where it is in plain words, e.g. "At Fullerton" or "Between Belmont and
  Wellington, 3 stops away".
- Keep them updated while the stop is open (the positions feeds refresh about every minute); tapping the focused
  one again clears the focus and shows the whole line, and tapping another moves the focus.
- Data: `Prediction` doesn't carry the vehicle yet. Keep the run number (`rn`) from Train Tracker arrivals and
  the vehicle ID (`vid`) from Bus Tracker predictions, then find that vehicle in `ttpositions` / `getvehicles`.
  CTAKit already parses both feeds into `Vehicle` (`TrainParse`, `BusParse`); the app doesn't fetch them yet.
- If the vehicle drops out of the feed, say so instead of showing an old position.

**Why third:** it answers "where is my train right now?" with data the app can already parse.

### F4. Pixel versions of the cards' train and bus symbols on the iOS map

Every card in the iOS app shows which kind it is in its bottom-right corner: a subway in its tunnel
(`tram.fill.tunnel`) on train cards and a bus (`bus.fill`) on bus cards (SF Symbols, `RouteCard.swift`). Draw
pixel-art versions of those two symbols and use them as the map's stop icons, replacing the pixel train front
and pixel bus in `ios/CTATracker/Map/StopIcons.swift`, so the map matches the cards but stays pixel art like
the rest of the map. F3's vehicles on the map use them too.

- Draw them as grids like the current icons (whole art pixels, nearest-neighbor scaling, app spec 4.6), in the
  line's card colors, with the one-pixel white halo so they read on the navy map.
- Decide how a station on several lines shows its lines (today a cream icon with a strip of line colors).
- Keep icons the same size as today, and keep the open stop ranked first when icons collide.

**Why fourth:** small, and it makes the map read like the rest of the app.

---

F5–F12 came from the 2026-11-11 dock brainstorm. They all come after the core dock (spec §7) works, and
the first dock firmware runs as of 2026-11-12 (`58c8b60`). Each one needs a short design approved before
it's built.

### F5. "Leave in N min" on the dock, with weather

Show when to walk out the door, not when the vehicle arrives.

- Give each picked stop a walk time, set on the dock or in the app and saved in flash with the pick.
- Show only arrivals you can still catch. The headline for each section is "Leave in N min", then
  "Leave now". Drop arrivals you can no longer make.
- The stop's LED row pulses when it's time to leave.
- Rain or snow adds a minute of walk time (one constant). Below a cold threshold, show "Wait inside"
  until it's time to leave.

**Why fifth:** it answers the question people actually have when they look at the dock, using data the
dock already pulls. F9 and F12 build on it.

### F6. Disruption takeover

- When a picked line has a major alert (shuttle buses, reroute, suspended service), that stop's
  section turns into a plain-language card plus the nearest alternative from the bundled GTFS data,
  e.g. "Red Line down at Belmont. The 77 bus is 4 min away."
- Decide which alerts count as major (CTA alerts carry severity and impact fields).

**Why sixth:** the moment the dock matters most is when your usual line breaks.

### F7. Bunching alerts

- When 2 or more vehicles on the same route and direction reach a picked stop within about 2 min of
  each other and are followed by a long gap, say so in one line: "3 buses together, then a 22 min gap."
- The gap and spacing thresholds are constants.

**Why seventh:** cheap, but less urgent than a broken line. It only reads the predictions the dock already
has, and the LED row already shows the bunch.

### F8. Divvy fallback

- Read Divvy's open GBFS feeds (station information + station status; no API key) and find the
  station nearest each picked stop.
- When the next catchable arrival is far off (a constant), show "3 e-bikes at the corner".
- Poll slowly (every couple of minutes). Check Divvy's data license for the attribution it needs.

**Why eighth:** a real alternative for very little work.

### F9. Countdown on the handheld (spec §2 stretch)

- Follow one arrival and count down to it on the handheld.
- Start it from the handheld's stop detail view first. Starting it by tapping an arrival on the dock
  comes later, since that needs a new dock-to-handheld message.
- Count down to the leave time when F5 exists, otherwise to the arrival.
- Track the same vehicle; if its prediction vanishes, say so (ties into F10).

**Why ninth:** it hands the dock's answer to your pocket, but it needs design work and the handheld's
core flow comes first.

### F10. Ghost-bus detection

- Track vehicle IDs (bus `vid`, train run number) in the predictions for each picked stop. A vehicle
  that drops out before it's due counts as a ghost.
- Mark arrivals as solid or shaky by the route's ghost rate today, and show "lost N buses today".
- Keep counts in RAM with a daily summary in flash.

**Why tenth:** the most novel item, but it only looks good after days of data. Start logging as soon as
the dock polls live, even before the screen shows any of it.

### F11. Best time to leave, learned on the dock

- For each picked stop and each 5-minute leave slot, log the wait you'd have had. After about a week,
  show e.g. "Leave 8:10: about 2 min wait. Leave 8:15: about 9."
- Count a vehicle as arrived when its prediction reaches due or vanishes within a minute of it.
- Share the log with F10 and with the pet journal stretch goal. Check the flash budget.

**Why eleventh:** needs a week of data and the same logging as F10.

### F12. LED-only ambient mode

- The screen sleeps after a few idle minutes (or on a schedule); a touch wakes it.
- The LEDs carry the information alone, getting brighter as leave time nears (needs F5).
- Night hours still turn the LEDs off.

**Why twelfth:** saves power and is calmer, but it's polish on top of the rest.
