# To do

## Bugs

Labelled B1, B2, … in order of importance.

### B1. Handheld cards look jagged while scrolling (fixed on the board 2026-11-13, on ESP-IDF in b659af5; card pop open)

On the handheld (wearable), scrolling the card list didn't look smooth: the cards' edges looked mildly
jagged, one side of the screen seemed to move a little before the other, and slow scrolls froze now and then.
It had two causes, both fixed:

- **Tearing.** The panel scans its own rows, which are the screen's columns (it's mounted a quarter turn off),
  at 59.2 Hz (16.9 ms a scan). Frames went out in strips of screen rows, across the scan, with no sync to it,
  so every moving frame showed with a slanted seam. The panel's TE line is wired to GPIO13 (Waveshare's
  schematic; their examples and BSP never use it). The firmware now turns TE on (`0x35 00`), draws ten strips of
  48 screen columns (whole panel rows) and sends each just behind the scan, so a frame shows whole on one scan
  (`paceSend` in `main.cpp`). While the list moves, each frame starts 2.5 ms before a scan, for a steady two
  scans a frame. Two gfx fixes came with it: `blitNearest`/`blitScaled` step their source column from the
  image's own edge (from the clip's edge, a column strip picked different pixels), and `arc` skips strips its
  sweep misses (7 ms a frame for the scrollbar otherwise). `tools/frame_test.cpp` checks column strips draw
  exactly the row strips' pixels.
- **Freezes on slow scrolls.** Not frame time: a touch log of Leo scrolling showed the list only followed a
  drag while it was more vertical than horizontal since touch-down. Diagonal thumb drags kept crossing 45
  degrees, and the list stopped dead for 88–656 ms (7 of 26 drags). A drag now takes the axis it first moves
  along (past 9 px) and keeps it; one that scrolled doesn't count as a tab swipe on release
  (`pointerMove`/`pointerUp` in `ui.cpp`, checked by `testDiagonalDrag` in `frame_test.cpp`). The old
  firmware froze the same way.

**Cost.** A scroll frame draws in about 21.5 ms in column strips against 15.3 ms in row strips (per-strip
overhead in the card blits and plates), so scrolling runs at a steady ~28 fps against ~38–49 fps torn before.
The stats line now shows `scan` and `late sends`; the late sends left come from the one frame where the list
comes to rest (60–80 ms, either strip order).

Still open:

- **The card pop.** While moving, cards draw from whatever width is cached; at rest each re-renders at its exact
  width with bilinear scaling, so the card visibly grows and that frame takes 60–80 ms. Fix: render exact widths
  one card a frame while the list moves.
- **Not yet confirmed with a finger:** the axis lock (the laptop check passes), and whether a quick sideways flick
  that starts mostly vertical now scrolls instead of switching tabs.
- **59 fps scrolling** would need scroll frames under 16.9 ms, about 25% faster drawing in column strips.

### B2. Dock screen frame rate: picker 34 fps, map 15.7, aim for 24+ on the map (partly done)

The dock's screen was at 5–10 frames a second; the goal is at least 24–30 fps if the board allows it.

**Status (2026-11-15):** since the DMA band copies (merged `aa6b705`, 2026-11-14) and the ESP-IDF port, the picker
scroll runs at 34 fps, the map pan at 15.7 and the mixed phase at 25 (`docs/perf/log.md`). **Open:** the map to
24 fps or more (the next panel step is 34; it needs about 18 ms off its frame), and the look on the real panel
(below). Scheduled only if time on 11/20. The history below is from before those changes.

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

## Found 2026-11-15

Small items from the Send to Trackie and Follow checks on Leo's phone and wearable:

- **"Send to Trackie" is a bare paper-plane icon.** Leo couldn't find it at first. Consider a label or a clearer icon.
  Partly done (2026-11-16): with a dock online it opens a menu with text ("Send to Trackie", "Send to dock"); the
  button itself is still the icon.
- **The wearable prints the Wi-Fi network name on serial at join** (`net: connecting to …`). Remove it: network
  names stay out of logs.
- ~~**The phone keeps relaying a stop after the wearable clears its pin on its own** (swipe, dock, passed follow).~~
  Done 2026-11-16 (Task 25): `info.pin` going null clears the app's pin and the relay's stop.
- ~~**A swiped pin comes back on the next reconnect.**~~ Done 2026-11-16 (Task 25): `info` carries `pin`, and the
  app drops a pin the wearable cleared, so it isn't resent.
- **Rewrite git history for the network name in old commit f9ed1e2?** The repo is public. Leo's call.

## Found 2026-11-16

- **The setup flow's Wi-Fi step says "Couldn't join. Check the password." for every failure.** On a lab network the
  access point refused the wearable before any password (reason 2). Report the board's reason instead (it would need
  the disconnect reason in `info.wifi`).

## Features

Ranked by importance, most important first (do F1 first). Labels: F = feature, B = bug.

### F1. Save at least 5 Wi-Fi networks on the handheld; join the strongest

Today the handheld knows one network (`WIFI_SSID` / `WIFI_PASS` in `firmware/secrets.h`), so it only gets
live data in one place.

**Status (2026-11-15): done on the wearable, not on the dock.** The wearable keeps up to 5 networks in NVS, sent from
the app over Bluetooth (setup's Wi-Fi step), with the `secrets.h` list as the fallback, and joins the strongest;
a board check with two saved networks is still open (BLE plan W6a Step 7). The dock still joins one network from
`secrets.h`, so moving it to another network means editing `secrets.h` and reflashing.

- Hold a list of at least 5 SSID + password pairs (in the git-ignored `secrets.h`; setting them from the app
  over Bluetooth is built, BLE plan W6a/AB2). Update `secrets.example.h` and the handheld README to show the list form.
- Scan, and of the saved networks in range, join the one with the strongest signal (RSSI).
- If the connection drops, scan again and move to the next strongest.
- Keep the first entry as home: docked detection (spec 8.4) depends on being on the home network.

**Why first:** without it the handheld is offline anywhere but one network, which matters more than how
it looks.

### F2. Battery level at the top of the handheld

**Status (2026-11-15): partly done.** The battery and charging show on the wearable's Settings page (S-W); not yet at
the top of every screen.

Today the handheld only shows its charge while it's docked (the ring and "Charging · 64%" on the home screen).
Off the dock there's no way to tell how much battery is left.

- Show the battery at the top of every screen off the dock: a small bar, the percent, or both, in the round
  screen's top edge so it doesn't crowd the cards.
- The AXP2101's fuel gauge already gives the percent (`firmware/handheld/mock/src/power.h`). Show "charging"
  when plugged in, and turn it warning-colored when low (a constant, e.g. 15%).
- Redraw it only when the percent changes, so it costs no power between frames (see `docs/power/`).

**Why second:** small, and you need it every day you carry the handheld.

### F3. A real settings page on the handheld

**Status (2026-11-15): built as S-W** (`bdd9e1f`, BLE plan): swipe left from Home; name, Wi-Fi with bars, battery and
charging, Bluetooth, Pair again behind a confirm. Not built: brightness, networks in range, forget / move up,
firmware version. Board check open (BLE plan S-W Step 4).

The handheld has no settings screen; brightness is fixed in code (`brightness()` in `ui.cpp`), and Wi-Fi comes
from `secrets.h`.

- A settings screen reached from the home screen, in the handheld's theme, with at least:
  - default brightness (and the dimmed level), saved in flash
  - Wi-Fi: the saved networks from F1, the networks in range with their signal, which one it's on, and
    forget / move up. Typing a new password on the watch is impractical, so networks come from the app over
    Bluetooth (built) or `secrets.h`.
  - battery and charging (F2), firmware version, and pairing / unpair
- Decide the list of settings and how you get there (a gesture or a button on the home screen) in a short design
  first.

**Why third:** it goes with F1 (the network list) and F2, and it ends hard-coded settings in firmware.

### F4. A more premium font in the app, the handheld and the dock (done 2026-11-14 in b181da8; dock draw cost open)

**Done:** Leo picked from renders on both boards (Nunito, Inter, Geist, Figtree, Manrope). The boards use
Manrope (SIL OFL 1.1), built by `font_build.py` from Google Fonts; Manrope stops at 800, so the atlases the UI
asks for as 900 are drawn at 800 and the 800 ones at 700. The app uses SF Pro, the system font (`Theme.rounded`
keeps its name). The pixel digits stay. Flash: the dock drops 29,088 B (2,738,094 to 2,709,006; perf env
2,749,758 to 2,720,686) and the wearable 29,072 B (2,381,234 to 2,352,162), with the added ‘ glyph. The atlas
carries the OFL notice; the license is `firmware/lib/gfx/tools/OFL.txt`. Plan:
`docs/superpowers/plans/2026-11-14-f4-font.md`. **Still open:** the draw cost on the dock board (B2), measured
with `tools/dock_perf_run.py`, and a look on the real screens.

Original request:

The text in all three looks close to Comic Sans: the iOS app uses SF Rounded (`Theme.rounded`, `design:
.rounded`) and the handheld and dock use Nunito (`firmware/lib/gfx/src/fonts.*`).

- Pick one non-rounded typeface family that looks premium on all three, try it on real screens first, and keep
  the pixel digits for the minutes (they're the departure-board look, not body text).
- iOS: switch `Theme.rounded` to the new face (SF Pro is the system default; a custom font needs bundling).
- Handheld and dock: regenerate the glyph atlases with `firmware/lib/gfx/tools/font_build.py` from a font
  whose license allows embedding in firmware (SF Pro's doesn't; Inter and other OFL fonts do). Check the flash
  size of the new atlases and the draw cost on the dock (B2).
- Update the font in `docs/handheld-ui.md` and the iOS redesign spec.

**Why fourth:** it lifts the look of everything at once, but it touches all three devices and needs a choice
made on real screens first.

### F5. Tap Trackie to see why he feels that way

**Status: built** on the wearable (`294ec27`) and the dock (`58c8b60`): tapping Trackie switches his bubble to the
reason for a few seconds (`docs/handheld-ui.md`).

The user can tap Trackie and see the reason for his current mood (Sleepy, Sad, Worried, Happy, Hyped).
Right now it isn't clear why he's happy or sad.

- Show a short, plain reason, e.g. "Your bus is 12 min late" or "Train arriving in 2 min".
- The mood logic needs to return its reason along with the mood (`cta::Mood` in
  `firmware/lib/cta/src/cta_types.h`; sprites in `firmware/lib/pet/`).
- Decide how the tap is made on the handheld and how the reason is shown and dismissed.

**Why fifth:** it needs some design work first (input, layout, wording). Build it in the handheld's
current theme, which matches the app since 2026-11-11 (white bus cards, light green live glyph; see
`docs/handheld-ui.md`).

### F6. Trains and buses on the iOS map: a stop shows its line's vehicles, a tap focuses one

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

**Why sixth:** it answers "where is my train right now?" with data the app can already parse.

### F7. Pixel versions of the cards' train and bus symbols on the iOS map

**Done (2026-11-14):** tunnel A stations, M3 multi-line arches, bus A; grids in CTAKit `StopArt`, plan in
`docs/superpowers/plans/2026-11-14-f7-pixel-icons.md`.

Every card in the iOS app shows which kind it is in its bottom-right corner: a subway in its tunnel
(`tram.fill.tunnel`) on train cards and a bus (`bus.fill`) on bus cards (SF Symbols, `RouteCard.swift`). Draw
pixel-art versions of those two symbols and use them as the map's stop icons, replacing the pixel train front
and pixel bus in `ios/CTATracker/Map/StopIcons.swift`, so the map matches the cards but stays pixel art like
the rest of the map. F6's vehicles on the map use them too.

- Draw them as grids like the current icons (whole art pixels, nearest-neighbor scaling, app spec 4.6), in the
  line's card colors, with the one-pixel white halo so they read on the navy map.
- Decide how a station on several lines shows its lines (today a cream icon with a strip of line colors).
- Keep icons the same size as today, and keep the open stop ranked first when icons collide.

**Why seventh:** small, and it makes the map read like the rest of the app.

---

F8–F15 came from the 2026-11-11 dock brainstorm. They all come after the core dock (spec §7) works, and
the first dock firmware runs as of 2026-11-12 (`58c8b60`). Each one needs a short design approved before
it's built.

### F8. "Leave in N min" on the dock, with weather

Show when to walk out the door, not when the vehicle arrives.

- Give each picked stop a walk time, set on the dock or in the app and saved in flash with the pick.
- Show only arrivals you can still catch. The headline for each section is "Leave in N min", then
  "Leave now". Drop arrivals you can no longer make.
- The stop's LED row pulses when it's time to leave.
- Rain or snow adds a minute of walk time (one constant). Below a cold threshold, show "Wait inside"
  until it's time to leave.

**Why eighth:** it answers the question people actually have when they look at the dock, using data the
dock already pulls. F12 and F15 build on it.

### F9. Disruption takeover

- When a picked line has a major alert (shuttle buses, reroute, suspended service), that stop's
  section turns into a plain-language card plus the nearest alternative from the bundled GTFS data,
  e.g. "Red Line down at Belmont. The 77 bus is 4 min away."
- Decide which alerts count as major (CTA alerts carry severity and impact fields).

**Why ninth:** the moment the dock matters most is when your usual line breaks.

### F10. Bunching alerts

- When 2 or more vehicles on the same route and direction reach a picked stop within about 2 min of
  each other and are followed by a long gap, say so in one line: "3 buses together, then a 22 min gap."
- The gap and spacing thresholds are constants.

**Why tenth:** cheap, but less urgent than a broken line. It only reads the predictions the dock already
has, and the LED row already shows the bunch.

### F11. Divvy fallback

- Read Divvy's open GBFS feeds (station information + station status; no API key) and find the
  station nearest each picked stop.
- When the next catchable arrival is far off (a constant), show "3 e-bikes at the corner".
- Poll slowly (every couple of minutes). Check Divvy's data license for the attribution it needs.

**Why eleventh:** a real alternative for very little work.

### F12. Countdown on the handheld (spec §2 stretch)

**Status (2026-11-15): mostly superseded** by ecosystem Tasks 16 and 18: "Follow on Trackie" from a departure in the
app, counted down on the wearable's Home card. Left from this item: starting a follow on the wearable itself, and
leave time (F8).

- Follow one arrival and count down to it on the handheld.
- Start it from the handheld's stop detail view first. Starting it by tapping an arrival on the dock
  comes later, since that needs a new dock-to-handheld message.
- Count down to the leave time when F8 exists, otherwise to the arrival.
- Track the same vehicle; if its prediction vanishes, say so (ties into F13).

**Why twelfth:** it hands the dock's answer to your pocket, but it needs design work and the handheld's
core flow comes first.

### F13. Ghost-bus detection

- Track vehicle IDs (bus `vid`, train run number) in the predictions for each picked stop. A vehicle
  that drops out before it's due counts as a ghost.
- Mark arrivals as solid or shaky by the route's ghost rate today, and show "lost N buses today".
- Keep counts in RAM with a daily summary in flash.

**Why thirteenth:** the most novel item, but it only looks good after days of data. Start logging as soon as
the dock polls live, even before the screen shows any of it.

### F14. Best time to leave, learned on the dock

- For each picked stop and each 5-minute leave slot, log the wait you'd have had. After about a week,
  show e.g. "Leave 8:10: about 2 min wait. Leave 8:15: about 9."
- Count a vehicle as arrived when its prediction reaches due or vanishes within a minute of it.
- Share the log with F13 and with the pet journal stretch goal. Check the flash budget.

**Why fourteenth:** needs a week of data and the same logging as F13.

### F15. LED-only ambient mode

- The screen sleeps after a few idle minutes (or on a schedule); a touch wakes it.
- The LEDs carry the information alone, getting brighter as leave time nears (needs F8).
- Night hours still turn the LEDs off.

**Why fifteenth:** saves power and is calmer, but it's polish on top of the rest.

### F16. Dock option: cycle through the 6 nearest stops

The dock shows the 3 stops the user picked (default, unchanged). As an option, it can instead show the 6 stops
nearest the dock, 3 at a time, flipping between the two sets every 30 s or so (Leo, 2026-11-14).

- Off by default; a setting on the dock (and in the app's device settings once Task 13 exists).
- "Nearest" is measured from the dock's location, set once (the dock doesn't move).
- The LED rows follow whichever 3 are on screen.

**Why sixteenth:** an idea from the ecosystem build; not committed for the showcase unless Leo says so.

### F17. After the showcase: ESP-IDF build, then Bluetooth setup again (superseded)

**Superseded (2026-11-14):** done before the showcase as pure ESP-IDF, with no Arduino at all (Leo: "NO Arduino whatsoever. Only ESP-IDF."); no hybrid `arduino, espidf` step. Plan: `docs/superpowers/plans/2026-11-14-esp-idf-port.md`. Bluetooth is back for the wearable (2026-11-14, Leo): Bluetooth first with the relay, its Wi-Fi path kept as the fallback, and the dock Wi-Fi only (`docs/superpowers/plans/2026-11-14-wearable-ble-link.md`). The text below is the original item, kept as history.

Both boards build with `framework = arduino` on Espressif's prebuilt high_perf SDK libraries, whose memory settings
are fixed. That is why Bluetooth (~50 KB of internal RAM, measured in `docs/ecosystem/day1-link.md`) didn't fit and
the showcase pairs over Wi-Fi (plan Appendix A).

- First step: pioarduino's `framework = arduino, espidf`, Arduino as an ESP-IDF component with our own `sdkconfig`.
  Arduino code stays; Bluetooth and Wi-Fi buffers and TLS buffers can move to PSRAM and the controller pools shrink.
- Re-measure internal RAM on both boards, then bring back BLE setup (spec section 12, "Future work: Bluetooth").
- Fully native ESP-IDF, or a separate radio chip (e.g. ESP32-P4 + C6), only if the hybrid build isn't enough.

**Why seventeenth:** changes build, memory layout and timing on two working boards; not before the showcase.
