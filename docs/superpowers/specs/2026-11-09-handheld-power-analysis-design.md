# Handheld power analysis

- **Date:** 2026-11-09
- **Owner:** Leo
- **Status:** Draft
- **Relates to:** `2026-11-03-cta-tracker-design.md` (the main spec), section 8.5 (battery)

Over the next few days we measure how much power the handheld uses, then cut it as far as we can, one change at a time. Every change is measured before and after with the same stress test, and every reading is kept so it can be graphed later, along with what changed and why.

## 1. Rules

- **Readings come from the ESP32.** Everything is logged by the handheld's own firmware while it runs the wearable firmware. No external meters.
- **No battery.** The board runs on USB power with no battery attached, so there is no fuel gauge. Current is estimated by a model (section 4). Battery life is a projection: how long a 2000 mAh battery would last at the modeled current.
- **No feature gets worse or goes away.** An optimization changes how the firmware does something, never what the user sees. The main spec's numbers stay fixed: dim after 15 s, the always-on dim screen, the poll intervals (6.6), brightness levels, and the animation. Sending only the part of the screen that changed is allowed. Dropping the frame rate is not. Section 6 is the check.
- **Baseline.** The live build as of 2026-11-09 (`firmware/handheld/mock`, `env:live`). It already sends only the screen rows that changed and keeps one TLS connection open for train requests, so those savings are part of the baseline, not improvements.

## 2. What the firmware logs

A build flag, `POWER_LOG`, turns the logger on. It lives in its own environment, `env:power` (extends `env:live`), so the demo build never carries it.

Once a second the firmware prints one CSV line starting with `P,` over USB serial. It holds raw facts only, never estimated milliamps, so the model can change later and every past run can be re-scored.

| Group | Columns |
|---|---|
| Time | ms since boot, stress phase |
| Power | USB voltage, system voltage, power-chip temperature, ESP32 temperature; battery voltage and % if a battery is ever attached |
| CPU | clock MHz at the sample, busy % on core 0, busy % on core 1; ms spent at each clock (80, 160, 240 MHz) and busy core-ms at each, since the last sample |
| Screen | dim or awake, brightness (0–255), frames this second, pixels sent this second, lit fraction of the last frame |
| Wi-Fi | state (off, joining, connected), RSSI, transmit power, requests started, requests finished, ms spent in requests, bytes received |
| BLE | state and connection interval (empty until BLE exists) |
| Touch | touch events this second |
| User-visible | frame rate while moving, slowest frame ms, tap-to-screen ms (last tap), age of the oldest card's data |
| Health | free internal RAM, lowest free internal RAM since boot, free PSRAM, reset reason (first line only) |
| Load | train cards, bus cards |
| Logger | µs the logger itself spent this second |

Notes:

- **CPU busy %** is 100% minus the share of the second each core's idle task ran (`ulTaskGetIdleRunTimeCounterForCore`, against `esp_timer`). FreeRTOS run-time stats are on in Arduino core 3.3.11. The counter is 32-bit µs and wraps every 71 min, so deltas use unsigned math.
- **Time at each clock:** the clock can change several times a second (80 MHz on the dim screen, 240 MHz while drawing, touching, or in a request; `cpu_clock.h`), so the clock module adds up the ms spent at each clock and the busy core-ms at each (both cores together), and the model costs each at its own clock. A once-a-second clock snapshot alone would misreport it.
- **Lit fraction** is measured on one frame a second, from every 16th pixel of each strip as it is sent to the panel (about 1 pixel in 16 of the screen). Each pixel counts red, green, and blue in proportion to their levels (each scaled to about 0–62), and the sum is divided by its maximum, so all black is 0 and all white is 1. Black AMOLED pixels draw almost nothing, so this tracks what the panel draws better than a pixel count would. A second with no new frame carries the last frame's value, since the screen still shows it.
- **Never logged:** Wi-Fi names, passwords, API keys, location, stop IDs. The logs are committed to the public repo.
- **Events** go on their own lines starting with `E,`: wake, dim, phase start, request start and end (with the endpoint kind: train, bus, alerts, weather), Wi-Fi up and down.
- **Light sleep and USB serial don't mix** (the USB link drops while asleep). The logger buffers lines in a PSRAM ring of at least 10 minutes and sends them when the link is back, so a light-sleep optimization loses no data.

## 3. The stress test

One serial command, `S`, starts it. The firmware runs every input itself on exact timing, prints a phase marker at each phase start, and prints `E,done` at the end. The test is versioned (`stress-v1`); runs are only compared within the same version.

| # | Phase | Length | What happens |
|---|---|---|---|
| 0 | Warm-up (not scored) | until every stop has been fetched once, at most 3 min | Wi-Fi joined, nearby list loaded, chip temperature settling |
| 1 | Dim idle | 3 min | No input. Normal polling continues |
| 2 | Awake, still | 2 min | Home at full brightness; a scripted tap on empty space every 10 s keeps it from dimming |
| 3 | Scroll | 2 min | The Stops list scrolls non-stop (the `a` auto-scroll, kept running) |
| 4 | Screen churn | 2 min | Home → Stops → other tab → a Detail → back, one step every 1.5 s, on a loop |
| 5 | Network burst | 3 min | Screen dim; the network task fetches whatever is next (stations, buses, alerts, weather in rotation) every 2 s |
| 6 | Everything | 3 min | At full brightness with the network burst on: 10 s of scrolling, then one churn loop, repeated |
| 7 | Cool-down | 2 min | Dim idle again; should match phase 1. If not, something stayed on |

- **Network burst is test-only.** It's a mode of the network task that only the stress test turns on. It sends at most 1 request every 2 s and at most 180 per run, so several runs a day stay well under the CTA keys' daily limits.
- **Same conditions every run:** the nearby list is sent from fixed coordinates (`send_nearby.py --lat 41.94 --lon -87.65` unless Leo picks others), the same Wi-Fi network, and the board in the same spot. RSSI is logged to catch drift. Runs stay out of night hours (1–5 AM), when polling slows down.

**stress-v2** (from round 2, 2026-11-09, Leo's call): same phases and timing, with three differences.
- While scrolling (phase 3, and phase 6's scroll windows), the test taps the other tab every 5 s, so it goes back and forth between Trains and Buses.
- Runs use real data for where Leo is: `power_run.py --here` sends the nearby list for the Mac's location (CoreLocation). The coordinates are never printed or saved.
- During a run, the test build keeps daytime polling and moods at any hour, so runs can go overnight. Night hours still work as before outside a run and in every non-test build.

stress-v1 and stress-v2 runs are never compared. Round 1's firmware is rerun under stress-v2 as the reference for rounds 2 and 3, so the chain is baseline → round 1 (stress-v1), then round 1 → round 2 → round 3 (stress-v2).
- **Scoring:** the first 10 s of each phase is dropped. Each phase gets the mean and spread of every logged and modeled value.

## 4. The power model

The model runs on the laptop, in `tools/power_model.py`, with its numbers in `tools/power_model.json`. Each number has a source (datasheet table or a stated estimate). The model's version goes into every report.

Estimated current each second is a sum of parts:

- **CPU:** for each clock (80, 160, 240 MHz), the time at that clock × its idle current plus the busy core-time at that clock × its current per busy core, averaged over the second. Figures are approximate readings of the ESP32-S3 datasheet's modem-sleep table (CPU running, RF off). Older runs without the per-clock columns use the clock snapshot and the two cores' busy %. A light-sleep rate is in the JSON but unused, since the firmware doesn't light-sleep.
- **Screen:** a constant driver current, plus full-white-at-full-brightness current × lit fraction × brightness/255 (linear, no gamma), plus a cost per million pixels sent each second. The panel numbers are the weakest estimates, since no CO5300 panel power data was found; they're marked as estimates.
- **Wi-Fi:** off is zero and joining is a flat current. Connected, the second is split between idle and active by the ms spent in requests: idle at the current for the power-save mode in use, active at a request current raised by a fixed share for each dB the RSSI is below −60 dBm.
- **BLE:** advertising and connected currents (zero until BLE exists).
- **Memory:** PSRAM and flash standby as one constant.
- **Fixed:** touch controller, power chip, and other board parts as one constant.

The parts are summed on the 3.3 V rail and multiplied by `rail_to_battery` to give current from a 3.7 V cell through the power chip's buck at about 90%.

Outputs per run: estimated mA over time split by part, the average for each phase, and mAh for the whole run.

**Projected battery life** comes from the phase averages weighted by a typical day, set in `power_model.json`: 16 h off the dock, of which about 90% dim, 7% awake and still, 2% scrolling, 1% switching screens. Normal polling is already inside the dim and awake phases. The weights stay fixed across runs so projections compare fairly.

## 5. Running a test

`tools/power_run.py` does a whole run from the laptop, using only the Python standard library:

1. Flash `env:power` (`pio run -e power -t upload`).
2. Send the fixed nearby list.
3. Send `S`, record every `P,` and `E,` line to a CSV until `E,done`, and stop with an error if the board resets mid-run.
4. Write a header of `#` lines at the top of the CSV: firmware commit (with `-dirty` if uncommitted), change ID, stress version, model version, date.

`tools/power_report.py` reads one or more runs and writes a self-contained HTML report with SVG charts (no libraries): current over time by part, per-phase bars compared with the previous change, CPU busy %, the user-visible numbers, and projected battery life. It also prints a per-phase table for the change log.

Claude runs every test end to end. Leo keeps the board plugged in and in the same spot.

## 6. Noise and the no-regression check

- **Noise:** the baseline runs 3 times. The spread between them, per phase, is the noise. A change whose saving is less than twice the noise gets a second run before it counts.
- **A change passes only if all of these hold:**
  - estimated average power goes down in at least one phase and up in none, beyond noise
  - frame rate while scrolling, slowest frame, tap-to-screen time, and request time are no worse than the previous run, beyond noise
  - lowest free internal RAM is no lower than the previous run's minus 2 KB
  - no resets during the run
  - the native tests pass (`firmware/native`, `pio test`)
  - an on-device check: Home, Stops (both tabs), a Detail, dim and wake, and live data arriving
- A change that fails is reverted, and its entry stays in the log marked as failed with the reason.

## 7. Files

```
firmware/handheld/mock/src/power_log.*   the logger (POWER_LOG builds only)
firmware/handheld/mock/src/stress.*      the stress test script (POWER_LOG builds only)
firmware/handheld/mock/platformio.ini    env:power
tools/power_run.py                       flash, run, record
tools/power_model.py, power_model.json   the model
tools/power_report.py                    charts and tables
docs/power/log.md                        one entry per change
docs/power/runs/                         every run's CSV
docs/power/reports/                      the HTML reports
```

Each `log.md` entry has: change ID, date, what changed, why (the idea and the expected saving), the runs before and after, the per-phase table, the user-visible numbers, pass or fail, and the commit.

## 8. Order of work

1. Logger, stress test, `env:power`, and the two scripts, tried on the board.
2. Baseline: 3 runs, the noise figure, and the first report.
3. Optimizations, one at a time, each with its log entry. Which ones come first is decided from the baseline report, biggest estimated share first.

## 9. Risks

| Risk | What could go wrong | Mitigation |
|---|---|---|
| Model accuracy | With no battery or meter, absolute numbers can be well off | Every number is labeled an estimate. Comparisons between runs use the same model, so savings are still meaningful. If a battery appears, the logger records it and one drain run checks the model |
| The model can't see some changes | A saving in something the model doesn't track scores as zero | The model tracks what the logger measures; a change that targets something new adds a column and a model term first |
| Logger overhead | Logging changes what it measures | It logs its own cost each second; it's the same in every run |
| Live data varies | Different numbers of cards or alerts change the load | Card counts are logged; odd runs get a second run |
| Internal RAM | Already dips to about 30 KB during a TLS handshake; the logger's buffers could push it lower | Logger buffers go in PSRAM; lowest free RAM is part of the check |
| API limits | Back-to-back fetching uses up the daily request limits | At most 180 burst requests per run; requests per run are logged |
