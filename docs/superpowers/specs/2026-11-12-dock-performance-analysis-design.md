# Dock performance analysis

- **Date:** 2026-11-12
- **Owner:** Leo
- **Status:** Draft
- **Relates to:** `2026-11-03-cta-tracker-design.md` (the main spec), section 7 (the dock); `2026-11-09-handheld-power-analysis-design.md` (the handheld's analysis, which this one follows)

The handheld's analysis cut power one measured change at a time. The dock does the same for **speed and responsiveness**. It runs from the wall, so power only matters as **heat**. Every change is measured before and after with the same stress test, and every reading is kept.

Leo, 2026-11-12: "Follow essentially the same guidelines as we did for the wearable. Focus more on speed and responsiveness, since this won't be on a battery, power consumption won't matter as much, besides heat."

## 1. Rules

- **Readings come from the dock itself**, logged by its own firmware over its UART (the CH343) while it runs the dock firmware. No external tools.
- **No feature gets worse or goes away.** An optimization changes how the firmware does something, never what the user sees: every screen, card, colour, layout, animation (Trackie's bob, the blinks), gesture, poll interval and LED row looks and behaves the same, frame for frame where it's drawn. Skipping work whose result is already on screen is allowed. Showing fewer frames than now while something moves is not.
- **The panel's setup is off-limits**: pixel clock, porches, bounce buffers, the high_perf SDK, and which core runs the panel's interrupts. They are the fix for the tearing (2026-11-12), and a change there can bring it back where the logger can't see it.
- **Baseline:** the first working dock firmware (`58c8b60`) plus the logger.

## 2. What the firmware logs

A build flag, `PERF_LOG`, turns the logger on, in its own environment `env:perf` (extends `env:dock`), so the demo build never carries it. The perf build also gives the UART a 4 KB transmit buffer, so a log line never blocks the loop; that applies to the dock's other prints in that build too, equally in every run.

Once a second the firmware prints one `P,` line of raw facts. Columns (named on the `H,` line; new ones only at the end):

| Group | Columns |
|---|---|
| Time | `ms` since boot, stress `phase`, `screen` (0 stops, 1 map, 2 picker) |
| Heat | `cpu_c`: the ESP32-S3's internal temperature sensor (about 1 °C steps) |
| CPU | `busy0`, `busy1`: 100% minus each core's idle-task share of the second (FreeRTOS run-time stats) |
| Frames | `frames` shown this second, `bands` copied into the frame buffer (of 25 a frame), `draw_ms`, `copy_ms`, `wait_ms` summed over the second's frames (drawing the bands, copying changed bands, waiting for the panel to take the new buffer) |
| Responsiveness | `move_fps` and `slow_frame_ms` (frames while a drag moves the screen: rate and longest gap), `tap_ms`/`tap_max_ms` (a release to the end of the frame that shows it), `drag_ms`/`drag_max_ms` (the oldest unshown drag sample to the end of the frame that shows it), `touches` |
| Picker | `list_ms`: a picker list asked for (the bus routes) until it's in |
| Network | `req_started`, `req_done`, `req_ms` (time with a request in progress), `req_bytes`, `wifi_status`, `rssi` |
| Panel health | `scan_late` (panel frames whose refill ran late: tearing), `recoveries` (panel restarts since boot), `switch_timeouts` (since boot) |
| Memory | `int_free`, `int_min` (lowest free internal RAM since boot), `psram_free` |
| Data | `sections` with arrivals, `picked` sections, `data_age` (oldest picked section's arrivals, s) |
| Logger | `log_us`: the logger's own time last second |

Events go on `E,` lines: `boot`, `start,<version>`, `phase,<n>`, `warmup_done`, `boot,wifi=..,clock=..,loaded=..` (ms since boot when Wi-Fi came up, the clock was set, and every picked section first had arrivals), `tap,<ms>`, `list,<ms>`, `req_start,<kind>`, `req_end,<kind>,<code>,<bytes>,<ms>` (kind: t trains, b buses, a alerts, w weather, g geolocation, c clock), `done`.

**Never logged:** Wi-Fi names, passwords, API keys, stop IDs, location. The runs are committed to the public repo.

## 3. The stress test (`dock-stress-v1`)

Serial `S` starts it, `X` stops it. The firmware feeds synthetic touches into the loop exactly as the touch task's arrive (down, a move every 10 ms, up), so every gesture goes through the real input code. It never picks a stop (no flash writes, picks unchanged).

| # | Phase | Length | What happens |
|---|---|---|---|
| 0 | Warm-up (not scored) | until the clock is set and every picked section has arrivals, at most 3 min | |
| 1 | Idle | 2 min | Stops screen, no input. Trackie's bob and the blinks keep drawing twice a second |
| 2 | Screen churn | 2 min | One step every 1.2 s: tap Trackie; map and back (the map button); long-press section 1 into the picker; Train, Red, first direction (the station list); Back ×3; Bus (the route list, fetched live: `list_ms`); Back ×2 |
| 3 | Map pan | 2 min | The map, dragged in a 120 px circle every 2 s (it comes back to where it started) |
| 4 | Picker scroll | 2 min | The Red Line's station list, dragged 400 px up in 1 s, then down, nonstop |
| 5 | Network burst | 2 min | Stops screen; the network task fetches the next thing in rotation every 2 s (trains, buses, weather, alerts); a tap on Trackie every 3 s |
| 6 | Mixed | 3 min | The burst on, with 5 map circles, one churn loop, 10 scrolls, around again |
| 7 | Cool-down | 2 min | Idle again; should match phase 1. If not, something stayed on |

- **The burst is test-only**, at most 1 request every 2 s and 150 a run, well inside the CTA keys' daily limits.
- **Daytime at any hour:** during a run the night veil and slow polling are off, so runs can go overnight. Outside a run, and in every non-test build, night hours work as before.
- **Same conditions:** the picks saved on the dock, the same Wi-Fi network, the board in the same spot. Opening the dock's serial port reboots it, so every run starts from a fresh boot.
- **Scoring:** the first 10 s of each phase is dropped.

## 4. Scores

Speed first, then heat:

- **Speed:** frame time (and its draw, copy and wait parts), fps while dragging, slowest frame, tap-to-screen and drag-to-screen (average and worst), picker list load, request time, and after boot: Wi-Fi up, clock set, every stop loaded.
- **Heat:** the chip's temperature per phase, and core busy % as its cause.
- **Health:** late panel frames, panel restarts, lowest free internal RAM, failed requests.

`tools/dock_perf_report.py` groups runs by change, prints a per-phase table and a comparison with the reference, and writes an HTML report with charts.

## 5. Running a test

`tools/dock_perf_run.py --change <id> --note "..."` flashes `env:perf`, opens the port (which reboots the dock), sends `S`, and records every `P,` and `E,` line to `docs/perf/runs/<date>-<change>-<n>.csv` until `E,done`. It stops with an error if the board resets mid-run, and saves what it got, marked aborted.

## 6. Noise and the no-regression check

- **Noise:** the baseline runs 3 times; the spread of each phase's value across them is its noise. A difference counts when it's beyond twice the noise (or a fixed floor per metric, in the report script, when there's only one reference run).
- **Runs interleave** (reference, change, reference) at the same hour when live data could differ, as the handheld's later rounds learned.
- **A change passes only if all of these hold:**
  - at least one speed score is better beyond noise and none is worse
  - chip temperature is no higher beyond noise in any phase
  - no late panel frames or panel restarts beyond the reference's
  - lowest free internal RAM no lower than the reference's minus 2 KB
  - no resets, no more failed requests than the reference
  - `pio test -e native` in `firmware/native` passes, `pio run -e dock` and `-e perf` build
  - an on-device look by Leo: the stops screen, the map, the picker, a pick, live data, and **no tearing**
- A change that fails is reverted; its entry stays in the log, marked failed with the reason.

## 7. Files

```
firmware/dock/src/perf_log.*    the logger (PERF_LOG builds only)
firmware/dock/src/stress.*      the stress test (PERF_LOG builds only)
firmware/dock/platformio.ini    env:perf
tools/dock_perf_run.py          flash, run, record
tools/dock_perf_report.py       tables and charts
docs/perf/log.md                one entry per change
docs/perf/runs/                 every run's CSV
docs/perf/reports/              the reports
docs/perf/briefs/               each round's worker brief
```

## 8. Order of work

1. Logger, stress test, `env:perf`, the two scripts, tried on the board.
2. Baseline: 3 runs, the noise, the first report.
3. Three rounds. After each, a worker (in its own worktree, following `docs/Agent-Workflow.md` with fresh Opus reviewers) gets a brief with the data and findings, builds and commits without pushing; I bring the commits in, flash, run interleaved, report, and log.

## 9. What the first look found (2026-11-12, before the baseline)

- **Every frame redraws the whole 480 × 800 screen in software**, about 50 ms of drawing on core 1, even when only Trackie's bob or a blink changed. The band hash only saves the copy into the frame buffer (most frames copy 0–4 of 25 bands at rest).
- **Each frame then waits for the panel** to take the new buffer and finish a frame from it, up to one panel frame (about 29 ms at 14 MHz).
- So while dragging, a frame costs roughly 50 ms of drawing plus copy plus wait, which caps the map and picker at about 12–15 fps.
- The panel's interrupts share core 1 with drawing (the tearing fix), so they take a share of every frame.

## 10. Risks

| Risk | What could go wrong | Mitigation |
|---|---|---|
| Tearing comes back | A change to drawing or buffer timing reintroduces the shaking picture | The panel's setup is off-limits; late frames and restarts are gated; Leo looks at the panel each round |
| Synthetic touches skip the touch chip | Tap time leaves out the GT911's 10 ms poll | The same in every run; the poll itself is unchanged by rule unless a round targets it, and then it says so |
| Temperature resolution | The sensor reads about 1 °C steps and lags | Phase means over 110 s, and the run's whole curve in the report |
| Live data varies | Different numbers of vehicles or alerts change the work | Interleaved runs; card counts are logged |
| Internal RAM | TLS needs about 45 KB; a band in internal RAM is 30 KB | Lowest free internal RAM is gated |
