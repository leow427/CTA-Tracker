# To do

## Bugs

_None open._

## Features

Ranked by priority of implementation (do 1 first).

### 1. Save at least 5 Wi-Fi networks on the handheld; join the strongest

Today the handheld knows one network (`WIFI_SSID` / `WIFI_PASS` in `firmware/secrets.h`), so it only gets
live data in one place.

- Hold a list of at least 5 SSID + password pairs (still in the git-ignored `secrets.h`, until the app sets
  them over BLE). Update `secrets.example.h` and the handheld README to show the list form.
- Scan, and of the saved networks in range, join the one with the strongest signal (RSSI).
- If the connection drops, scan again and move to the next strongest.
- Keep the first entry as home: docked detection (spec 8.4) depends on being on the home network.

**Why first:** without it the handheld is offline anywhere but one network, which matters more than how
it looks.

### 2. Handheld UI matches the app's theme

Some handheld colors don't match the iOS app:

- **Bus cards** are still the old bus blue (`kBusCard = 0x00A1DE` in
  `firmware/handheld/mock/src/ui.cpp`). The app now draws buses white (redesign revision 4).
- **The blinking live mark** (`liveGlyph` in `ui.cpp`) uses a different color from the app's live glyph.
- Go through the rest of the handheld screens against the app and list any other mismatches.

**Why second:** it's a small, well-scoped change. Feature 3 adds new handheld UI, so the palette should be
settled before that UI is built, or it will need restyling.

### 3. Tap Trackie to see why he feels that way

The user can tap Trackie and see the reason for his current mood (Sleepy, Sad, Worried, Happy, Hyped).
Right now it isn't clear why he's happy or sad.

- Show a short, plain reason, e.g. "Your bus is 12 min late" or "Train arriving in 2 min".
- The mood logic needs to return its reason along with the mood (`cta::Mood` in
  `firmware/lib/cta/src/cta_types.h`; sprites in `firmware/lib/pet/`).
- Decide how the tap is made on the handheld and how the reason is shown and dismissed.

**Why third:** it needs some design work first (input, layout, wording), and it should be built in the
final theme from feature 2.
