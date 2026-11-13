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

---

F3–F10 came from the 2026-11-11 dock brainstorm. They all come after the core dock (spec §7) works, and
dock firmware isn't started yet. Each one needs a short design approved before it's built.

### F3. "Leave in N min" on the dock, with weather

Show when to walk out the door, not when the vehicle arrives.

- Give each picked stop a walk time, set on the dock or in the app and saved in flash with the pick.
- Show only arrivals you can still catch. The headline for each section is "Leave in N min", then
  "Leave now". Drop arrivals you can no longer make.
- The stop's LED row pulses when it's time to leave.
- Rain or snow adds a minute of walk time (one constant). Below a cold threshold, show "Wait inside"
  until it's time to leave.

**Why third:** it answers the question people actually have when they look at the dock, using data the
dock already pulls. F7 and F10 build on it.

### F4. Disruption takeover

- When a picked line has a major alert (shuttle buses, reroute, suspended service), that stop's
  section turns into a plain-language card plus the nearest alternative from the bundled GTFS data,
  e.g. "Red Line down at Belmont. The 77 bus is 4 min away."
- Decide which alerts count as major (CTA alerts carry severity and impact fields).

**Why fourth:** the moment the dock matters most is when your usual line breaks.

### F5. Bunching alerts

- When 2 or more vehicles on the same route and direction reach a picked stop within about 2 min of
  each other and are followed by a long gap, say so in one line: "3 buses together, then a 22 min gap."
- The gap and spacing thresholds are constants.

**Why fifth:** cheap, but less urgent than a broken line. It only reads the predictions the dock already
has, and the LED row already shows the bunch.

### F6. Divvy fallback

- Read Divvy's open GBFS feeds (station information + station status; no API key) and find the
  station nearest each picked stop.
- When the next catchable arrival is far off (a constant), show "3 e-bikes at the corner".
- Poll slowly (every couple of minutes). Check Divvy's data license for the attribution it needs.

**Why sixth:** a real alternative for very little work.

### F7. Countdown on the handheld (spec §2 stretch)

- Follow one arrival and count down to it on the handheld.
- Start it from the handheld's stop detail view first. Starting it by tapping an arrival on the dock
  comes later, since that needs a new dock-to-handheld message.
- Count down to the leave time when F3 exists, otherwise to the arrival.
- Track the same vehicle; if its prediction vanishes, say so (ties into F8).

**Why seventh:** it hands the dock's answer to your pocket, but it needs design work and the handheld's
core flow comes first.

### F8. Ghost-bus detection

- Track vehicle IDs (bus `vid`, train run number) in the predictions for each picked stop. A vehicle
  that drops out before it's due counts as a ghost.
- Mark arrivals as solid or shaky by the route's ghost rate today, and show "lost N buses today".
- Keep counts in RAM with a daily summary in flash.

**Why eighth:** the most novel item, but it only looks good after days of data. Start logging as soon as
the dock polls live, even before the screen shows any of it.

### F9. Best time to leave, learned on the dock

- For each picked stop and each 5-minute leave slot, log the wait you'd have had. After about a week,
  show e.g. "Leave 8:10: about 2 min wait. Leave 8:15: about 9."
- Count a vehicle as arrived when its prediction reaches due or vanishes within a minute of it.
- Share the log with F8 and with the pet journal stretch goal. Check the flash budget.

**Why ninth:** needs a week of data and the same logging as F8.

### F10. LED-only ambient mode

- The screen sleeps after a few idle minutes (or on a schedule); a touch wakes it.
- The LEDs carry the information alone, getting brighter as leave time nears (needs F3).
- Night hours still turn the LEDs off.

**Why tenth:** saves power and is calmer, but it's polish on top of the rest.
