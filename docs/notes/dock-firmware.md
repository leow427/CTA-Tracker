# Dock firmware

How the dock firmware is put together, and what still needs the board. Spec: base spec sections 3, 6, 7, 10;
plan: `docs/superpowers/plans/2026-11-03-cta-dock.md`.

## Layout

| File | Job |
|---|---|
| `firmware/dock/src/main.cpp` | Wiring only: storage, LEDs, display, UI, BLE, then the network task |
| `display.*` | RGB panel, GT911 touch, CH422G expander, LVGL 9 in portrait |
| `ui.*` | Status strip (Trackie on his cream tile, bubble, clock, weather, Wi-Fi) and the 3 stop cards |
| `picker.*` | Train or Bus → line or route → direction → stop (spec 7.2) |
| `poller.*` | The network task on core 0: Wi-Fi, NTP, and every fetch on the spec 6.6 schedule |
| `leds.*` | The 3 approach rows: masks in line colors, LED 7 breathing when due, brightness cap, staleness, night |
| `ble_server.*` | `info`, `wifi` (encrypted + authenticated, slot 0), `config`; passkey shown full screen |
| `store.*`, `net.*`, `state.*` | NVS, Wi-Fi with backoff, the locked `DockState` |
| `firmware/lib/cta` | Shared, natively tested logic (parsers, moods, approach rows, config messages, colors) |
| `firmware/lib/pet` | Trackie and the glyphs (generated from `art/`), the ARGB renderer, and the pixel plates |

**Threads.** HTTPS calls block for seconds, so they run on their own task; the UI and LEDs run on the loop task.
Both read and write `gState` only under `Lock`, and never hold it across a fetch or a flash write. A new pick (picker,
BLE, or serial) clears that section's old data (`pickChanged`), bumps `cfgVersion`, and sets `cfgDirty`; the loop
task is the only one that saves the config to flash, so the newest pick is always the one stored. BLE callbacks
never write flash (NimBLE's host stack is 5 KB): Wi-Fi credentials are handed to the network task, which saves them
and rejoins. TLS buffers come from PSRAM (`mbedtls_platform_set_calloc_free` in `main.cpp`).

**Serial config.** A line of `config` JSON on the serial monitor applies like a BLE write (bring-up insurance if
touch or BLE misbehaves). Wi-Fi is never accepted over serial.

**Look.** The screen uses the app's identity (iOS redesign revision 2): night navy, one card per stop in its line's
shade (`card`, `cardTop`, `cardText` ported from CTAKit), the next 3 departures as tiles with the pixel minutes,
Delayed/Sched chips and the live glyph, and ⚠ beside the route name. Trackie is the app's 32×32 sprite, drawn
at 2× (64 px, spec 7.3), with the handheld mockup's umbrella and scarf. `tools/test_sprite_build.py` fails if
`art/` drifts from the app's `Pixel.swift`.

**Dimming.** The 4.3" board's backlight is on/off only (CH422G EXIO2), so night hours and the app's screen
brightness dim the screen with a black veil over everything.

## Checks that need the board (Leo)

1. **Screen up.** Flash with `cd firmware/dock && pio run -t upload -t monitor`. Pass: the UI is upright, and the
   empty sections read "Tap to pick a stop". If it's upside down, flip `DISPLAY_ROTATE_CW` in `include/pins.h`.
2. **Touch.** Tap an empty section: the picker opens. Pick Train → Red → 95th/Dan Ryan → Belmont: the section
   shows Belmont. Reboot: the pick is still there.
3. **Wi-Fi and keys.** With `firmware/secrets.h` holding both CTA keys, pair from LightBlue (6-digit code on the
   dock), write `{"slot":0,"ssid":"…","pass":"…"}` to `wifi`. Pass: the Wi-Fi icon turns from red to gray,
   the clock appears, and arrivals fill in within 30 s.
4. **LEDs.** Set `LED_DATA_PIN` (and `LED_INDEX` if the board is wired serpentine) in `include/pins.h`. Pass:
   a real train lights its row over a few minutes, and LED 7 breathes when it's 3 min out.
5. **Config over BLE.** Write `{"stops":[…]}` to `config` from LightBlue. Pass: the section changes at once, and
   the serial log's "ble: host stack free" stays above ~1 KB. The same JSON pasted into the serial monitor works too.
6. **Soak.** Leave it 2 hours. The serial log prints internal and PSRAM free heap every 5 min; internal free should
   stay above ~40 KB.
7. **Pet debug.** Hold two fingers on Trackie for 1.5 s: his mood steps through every state, then back to live.
