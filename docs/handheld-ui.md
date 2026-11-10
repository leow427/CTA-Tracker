# Handheld UI: how the wearable should look

The written guide to the handheld's screens (Waveshare 1.75" round AMOLED, 466 × 466), as Leo decided them on 2026-11-06 and 2026-11-07. It says what each screen shows and where the code behind each piece lives.

- **The contract** is spec §8.1 (`docs/superpowers/specs/2026-11-03-cta-tracker-design.md`). If this guide and the spec disagree, the spec wins; fix whichever is wrong.
- **To see it moving,** open `docs/ideas/2026-11-04-handheld-ui-mockup.html` in a browser. It was updated to these decisions on 2026-11-07, and its panel jumps between screens, moods, weather, and dim/docked states.
- **Status:** the shared code is built (Trackie's sprites and renderer, moods, parsers). The screens themselves are not; they're Plan 3 milestones 6 and 8 (`docs/superpowers/plans/2026-11-03-cta-handheld.md`). Layout details here are starting points and can change, the way the plans say.

## The look

- **Black on every screen.** Only what's drawn lights up on the AMOLED, which saves battery.
- **The iPhone app's pixel style.** Cards and tabs have stepped pixel corners, a thin rim, light from the top, and a hard edge under them, so they sit up like game tiles.
- **Line colors.** Every card is in its line's CTA color: a darker shade of the line as the fill, a lighter shade for all its text (the app's `LineColors.card` and `cardText` math). All buses share one color, the light blue `0x00A1DE`; the Blue Line uses the deeper `0x2F7BFF` (swapped 2026-11-09, when the Blue Line read too light).
- **Pixel minutes.** Arrival minutes use the app's 5×7 pixel digits, like a departure board, at whole scales only so they stay crisp.
- **Type.** Heavy, rounded type for route names. SF Pro Rounded can't ship on a non-Apple device, so the mockup uses Nunito Black; convert the chosen font with `lv_font_conv` at the sizes used.
- **Colors in small doses:** headlight yellow for the active control, mist for secondary text, cream for Trackie's speech bubble, amber only for Delayed.

## Trackie

- **The sprite:** the app's pet, Leo's blue L car with a face on its door, 32×32 pixels, drawn at 4× (128 px) on the handheld.
- **No tile.** He's drawn straight on black, with nothing behind him.
- **Bob:** he moves one art pixel up and down twice a second. His floating extra moves with him, not opposite.
- **Faces and extras by mood:**

  | Mood | Face | Floating extra | Bubble word | Bubble reason (on tap) |
  |---|---|---|---|---|
  | Happy | closed smiling eyes, cheeks | heart | "All on time" | "Brown in 4 min" |
  | Hyped | `> <` eyes, open mouth | excitement lines | "Go now!" | "Red in 2 min!" |
  | Worried | brows, wavy mouth | sweat drop | "Running late" | "22 is 4 min late" |
  | Sad | T_T with tears | none | "Uh oh…" | "Red Line alert" |
  | Sleepy | closed eyes | Zz | "Sleepy…" | "Night hours" |
  | Asleep (docked) | as Sleepy | Zz | "Zzz…" | "Charging, 64%" |

  The reasons are examples; the real one names the stop or alert behind the mood.
- **Speech bubble:** a cream pixel bubble beside him says the mood word. Tapping Trackie switches it to the reason for a few seconds.
- **Weather:** an umbrella over the roof in rain, a striped scarf over the bumper in snow or below 0 °C, from Open-Meteo. Never while Asleep.
- **Pet colors:** set from the app. They replace two palette slots (body and body light), so recoloring needs no new art.
- **Debug:** a two-finger long-press on Trackie cycles every mood and state, for testing and the demo.

## Screens

### Home

From the top:
- the time and weather icon
- Trackie with his speech bubble
- one card for the most urgent stop, so "should I hurry?" needs no swipe

Swipe up for Stops. Swipe left for the Map (a stretch goal).

### Stops: Trains | Buses

A tab pill at the top, then a scrolling list of cards, nearest stop first.

- **Trains:** one card per line and direction at each of the 5 nearest stations, soonest first within a station. Belmont shows six: Red, Brown, and Purple, each way.
- **Buses:** one card per route and direction, at the nearest of the 5 listed stops that serves it (spec 8.3: each route and direction's nearest stop, the 5 nearest of those), soonest first within a stop. A stop served by the 22 and the 36 shows both.
- **On a round screen,** cards away from the list's focus shrink, fade, and narrow to stay inside the circle.
- **Stale cards** grey out when their data is more than 5 min old.

Swipe down to go back to Home.

### The card

Every card shows:
- **The route, for buses:** "22", on top. Train cards don't name the line: the card's color does (2026-11-09).
- **Where it ends and the way it goes.** A bus says both: "South to Harrison". A train's direction is the end of its line, so "To Kimball" says both. Where a platform's direction and a train's destination differ (Green's shared trunk), the card shows the train's destination.
- **The stop name:** "Belmont", "Clark & Belmont". On a train card it's the largest text, on top (24 px, stepping down to 21 or 19 before it truncates), with small squares after it for the station's other lines.
- **The next arrival:** big pixel minutes, "Due" at 0–1 min, with a live mark. A timetable-only time shows as a clock time instead.
- **Badges:** ⚠ for an alert on that route, an amber "Delayed" chip.

### Stop detail

Tap a card. The tapped line or route comes first (a train's title is the stop name in its line's color, with where it goes under it; a bus's is its route number), then every other line and direction at that stop, each with where it ends and its next 3–4 arrivals. Alerts for the stop's routes show below. Swipe right to go back.

### Dim (always-on)

After 15 s with no touch, the screen dims to black with Trackie alone, and he shifts a few pixels every 10 s so he doesn't burn in. A tap wakes it back to Home.

### Docked

On the dock and home Wi-Fi, Trackie is Asleep. The screen is extra dim, with a charging ring around the edge and "Charging · 64%".

### First run and pairing

- **First run,** before the first nearby list: "Open Trackie on your iPhone".
- **Pairing:** Trackie with the 6-digit passkey in pixel digits, to type into the iPhone.

### Map (stretch goal)

Swipe left from Home. It shows:
- the L lines drawn on the device from `data/train_shapes.json`
- nearby stops and live trains
- a recenter button
- Trackie saying the nearest station

It needs BLE additions first (stop coordinates and train positions); see Plan 3 milestone 10.

## Where the code is

| Piece | Where | Status |
|---|---|---|
| Trackie's art (body, faces, extras, umbrella, scarf, palette) | `art/*.txt`, one character per pixel | done |
| Turning the art into firmware data and preview PNGs | `tools/sprite_build.py` (writes `firmware/lib/pet/src/pet_sprites.cpp` and `art/build/*.png`) | done |
| Drawing Trackie at any scale, palette swap, weather overlay rule | `firmware/lib/pet/src/pet_render.{h,cpp}` (`renderSprite`, `petStateFor`, `overlayFor`), `pet_sprites.h` | done |
| Which mood Trackie is in | `firmware/lib/cta/src/mood.{h,cpp}` (`stopMood`, `deviceMood`) | done |
| Umbrella or scarf from the weather | `firmware/lib/cta/src/alerts_weather.{h,cpp}` (`accessoryFor`) | done |
| Line colors | `firmware/lib/cta/src/line_colors.{h,cpp}` (mirrored in the app's `Config.swift`) | done; card shade math still to port |
| Bus cards (one record per stop, route, direction) | `firmware/lib/cta/src/bus_parse.{h,cpp}` | done |
| Train cards (one record per station, line, direction) | `firmware/lib/cta/src/train_cards.{h,cpp}` (`parseTrainCards`, `trainDirLabel`) | done |
| Screen dim timing and other values | `firmware/lib/cta/src/cta_config.h` (`kScreenDimSec`) | done |
| Screens, card drawing, pixel digits, fonts | Plan 3 milestones 6 and 8 (`ui_kit`, `ui_home`, `ui_stops`, `ui_detail`) | to build |
| The look, interactive | `docs/ideas/2026-11-04-handheld-ui-mockup.html` | updated 2026-11-07 |

## Changing the look

- **Trackie's art:** edit a grid in `art/` (a face, his body, an extra, a color in `art/palette.txt`), then run `python3 tools/sprite_build.py`. It regenerates the firmware art and the previews in `art/build/`, and names the file and line of any mistake. Run `python3 -m unittest discover -s tools` and `cd firmware/native && pio test -e native` after. The app still draws its own copy of the art until the iOS plan switches it to `art/`, so for now change `Glyphs` in `ios/CTATracker/Design/Pixel.swift` to match.
- **Values:** timings and thresholds are in `firmware/lib/cta/src/cta_config.h`. Each has a test, so update that test line too.
- **Layout and copy:** not built yet. Change spec §8.1 and this guide, then the mockup if you want to see it first.
