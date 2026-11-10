# Round 2 worker brief: handheld power AND performance

You are the round 2 worker in a multi-day power analysis of the CTA Tracker **handheld** (Waveshare ESP32-S3-Touch-AMOLED-1.75). Your job: change the firmware so it **uses less power and feels faster**, without changing what it does. Leo's words: "It's crucial the actual functionality is not changed," and this round "really prioritize performance too, which includes responsiveness and load times. Obviously along with efficiency."

Read first:
- `docs/superpowers/specs/2026-11-09-handheld-power-analysis-design.md`: the contract for the whole analysis (rules, logger, stress test, model, gate). Section 3 now describes **stress-v2**.
- `docs/power/log.md`: every run so far and its findings.
- `docs/power/reports/baseline.md` and `docs/power/reports/r1.md`.
- `docs/Agent-Workflow.md`: how you work (you + fresh Opus reviewers at xhigh, at most 3 review rounds, stop early when findings are small).
- `firmware/handheld/mock/README.md` and the main spec it points to, for what each screen and feature must do.

## The hardware and firmware, briefly

- ESP32-S3, 8 MB octal PSRAM, 16 MB flash. CO5300 AMOLED over QSPI at 80 MHz (466×466). CST9217 touch on I2C, shared with the AXP2101 power chip. Arduino-ESP32 core 3.3.11 (pioarduino): **CONFIG_PM_ENABLE is off**, so no DFS and no automatic light sleep; FreeRTOS run-time stats are on.
- Firmware: `firmware/handheld/mock/` (envs `live`, `power` = live + `-DPOWER_LOG`, `mock`). Shared logic and its native tests: `firmware/lib/cta`, `firmware/lib/pet`, `firmware/native` (run `pio test` there).
- Frame loop on core 1 (`main.cpp` loop: draw strips with changed-row hashing, queue to the panel sender). Panel sender task, touch task (8 ms poll) and network task on core 0. One TLS connection is kept open for train requests (`live.cpp`).
- Round 1 (commit `1c39d26`) added `src/cpu_clock.h/.cpp`: 240 MHz is a hold any task takes; with no hold the CPU drops to 80 MHz. The frame loop drops its hold once the dim always-on screen is drawn and no finger is down; touch, polls, the HTTP clock fallback, joins and scans hold 240.
- `src/power_log.*` (POWER_LOG only) prints one `P,` sample a second (columns on the `H,` line, listed in spec section 2, plus round 1's `clk_ms_*`/`clk_busy_*`). `src/stress.*` drives the stress test. `tools/power_model.py` + `power_model.json` turn samples into estimated mA.

## What the numbers say (baseline, stress-v1, 3 runs, model estimates)

| Phase | Est. mA | Noise | CPU busy c0/c1 % | FPS moving | Slowest frame ms | Tap→screen ms | Request ms |
|---|---|---|---|---|---|---|---|
| 1 Dim idle | 52.6 | 1.5 | 9 / 2 | – | – | – | 866 |
| 2 Awake, still | 57.7 | 1.5 | 10 / 4 | – | – | – | 700 |
| 3 Scroll | 107.9 | 4.4 | 61 / 100 | 37.3 | 86 | – | 1837 |
| 4 Screen churn | 65.5 | 1.7 | 18 / 23 | 37.1 | 66 | 23 (max 65) | 779 |
| 5 Network burst | 85.6 | 0.6 | 38 / 3 | – | – | – | 858 |
| 6 Everything | 123.1 | 1.5 | 65 / 70 | 47.2 | 84 | 30 (max 76) | 1343 |
| 7 Cool-down | 51.3 | 1.7 | 8 / 2 | – | – | – | 805 |

Projected 36.9 h (typical day: 90% dim idle, 7% awake still, 2% scroll, 1% churn). Lowest free internal RAM 21 KB.

Other facts from the raw runs:
- **Dim idle draws 2 frames a second** (about 8.8 k pixels each, 3% of pixels lit, brightness 77). Awake still also 2 fps. Wi-Fi power save is mode 1 (min modem) throughout.
- **Core 0 is 8–10% busy even in dim idle** (touch polling every 8 ms, panel sender, network task). In the burst phase it's 35–38%.
- **Scrolling pegs core 1 at 99–100%** at about 37 fps, sending about 4.2 Mpx/s. That's both the most expensive state the user drives and the biggest performance limit: anything that cuts drawing cost per frame raises fps *and* cuts power.
- **Failed requests after a fresh boot.** In both freshly flashed runs (baseline 1 and round 1's first run), one train station's requests failed every 22 s for about 4.5 minutes after boot: each one holds the radio (and, since round 1, 240 MHz) for 22 s before timing out. Runs that weren't reflashed don't show it. It wastes power and delays when every stop is loaded (load time). Find the cause (`live.cpp`: TLS session, DNS, socket reuse, timeouts, the keep-alive connection's state after the first handshake, or something else) and fix it.
- **Load time on a good boot** (tonight, on the second network): Wi-Fi up at once, "clock set from HTTP (NTP gave nothing)" at 9 s, first train response at 12 s (2.5 s, the TLS handshake), every stop, buses, weather and alerts by 18 s. Check whether data fetching waits on the clock, whether NTP is failing for a fixable reason, and whether the order or concurrency of first fetches can bring "everything loaded" earlier.
- Round 1's first run on the old network was inconclusive (Wi-Fi failed). It's being rerun on a second network tonight; Leo's orchestrator session will send you the result if it lands while you work.

## Goals, in order

1. **Power down** in at least one stress phase and up in none (beyond 2× noise), measured by the existing model.
2. **Performance up or equal everywhere**: tap→screen latency (avg and max), fps while moving, slowest frame, time until every stop is loaded after boot, request durations, data age. Where you can make something faster, do. A power win that makes anything slower fails the gate.
3. Lowest free internal RAM no more than 2 KB lower than now (21 KB); higher is better.

Ideas, roughly by expected value (you decide; measure your reasoning against the code):
1. The post-boot request failures and the 22 s timeout: fix the cause; make a failure give up the radio and 240 MHz quickly without retrying any less often than now.
2. Cheaper drawing per frame while scrolling and in churn (fewer rows redrawn, cheaper strip rendering, better overlap of drawing on core 1 with sending on core 0, less work in the hash pass). Raises fps and lowers power.
3. Faster first load after boot (clock dependence, fetch order, connection reuse across hosts).
4. Dim idle: skip sending frames that are identical to what the panel already shows; draw the dim screen only when something on it changes; cut core 0's idle wake-ups (the 8 ms touch poll could use the touch controller's interrupt line if the board wires it, or poll slower only while dim and untouched, provided a tap on the dim screen still wakes it just as fast).
5. Wi-Fi: anything that shortens time with the radio active per request (connection reuse for buses, weather and alerts, not just trains). Don't change power-save mode or listen interval in ways that delay requests or responses.

Ideas that need care: running awake-still at a lower clock (only if no frame, tap or animation gets slower); custom sdkconfig for light sleep (big change for this core; only if you're confident and it's reviewed hard).

## What must not change (functionality)

- Every screen, card, tab, detail view, animation, pet behaviour, mood, colour, layout and transition looks and behaves the same, frame for frame where it's drawn. Dim always-on screen shows the same content.
- Poll intervals and data freshness: no longer than now (shorter is allowed only if power still goes down). Night hours and docked behaviour as now.
- Touch: every gesture (tap, swipe, fling, catch, swipe home, back) works the same and is no slower to respond.
- Every network feature (trains, buses, alerts, weather, clock, nearby list, battery log, Wi-Fi join and rejoin with backoff) works the same.
- No secrets, Wi-Fi names, passwords, API keys, coordinates or stop IDs in logs, serial output, commits or docs.

## Hard rules

- **Don't touch the board** (no flashing, no opening `/dev/cu.usbmodem*`). Leo's orchestrator session runs every test.
- **Don't change** the stress test (`stress.*`), `tools/power_run.py`, `tools/power_report.py`, or the numbers in `power_model.json`. If your change needs a new logged fact for the model to see it fairly (round 1 added per-clock time), add a column at the **end** of the `P,` line and a model term, keep old runs re-scoring identically, and add a test in `tools/test_power_model.py`.
- Builds `pio run -e live`, `-e power` and `-e mock` (in `firmware/handheld/mock`) must succeed, and `pio test` in `firmware/native` must pass. Add native tests for new pure logic.
- You work in your own git worktree. `firmware/secrets.h` is git-ignored, so the worktree lacks it: copy it from the main checkout's `firmware/secrets.h` to build, never print it, never commit it.
- Commit on your branch with clear messages; don't push. **No `Co-Authored-By` or "Generated with" lines** in any commit. Check `git status` before each commit; never stage `docs/superpowers/specs/2026-11-03-cta-tracker-design.md`.
- Update `firmware/handheld/mock/README.md`'s "Power" section for what you changed.

## What to report back

1. Commits (hashes) and a plain description of each change.
2. For each stress phase: expected effect on estimated mA and on each performance metric, with reasoning.
3. Anything that could change behaviour, and how you checked it doesn't.
4. What to look for in the run (which columns, which events).
5. Your best ideas for round 3, ranked.
