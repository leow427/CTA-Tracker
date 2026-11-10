# Handheld power log

One entry per change, newest last. How it works is in the spec: `docs/superpowers/specs/2026-11-09-handheld-power-analysis-design.md`.

- Every current figure is an **estimate** from the power model (`tools/power_model.py`, numbers in `tools/power_model.json`). The board runs on USB with no battery, so nothing checks the absolute numbers. Comparisons between runs use the same model, so savings still mean something.
- Raw runs are in `runs/` (every line the logger printed: `H` column names, `P` one sample a second, `E` events). Reports are in `reports/`.
- A change passes only if power goes down in at least one phase and up in none, beyond noise, and nothing the user would notice gets worse (spec section 6).

How to run a test:

```bash
python3 tools/power_run.py --change c01 --note "what changed"
python3 tools/power_report.py docs/power/runs/*-baseline-*.csv docs/power/runs/*-c01-*.csv --out docs/power/reports/c01.html
```

---

## Baseline, run 1 of 3 (2026-11-09)

- **Firmware:** `1aad096`, the live build as it was on 2026-11-09, plus the logger (`env:power`).
- **Run:** `runs/2026-11-09-baseline-1.csv` (1,201 samples, no resets). Report: `reports/baseline-after-run-1.html`, table: `reports/baseline-after-run-1.md`.
- **Conditions:** USB power, no battery, Wi-Fi RSSI about −57 dBm, fixed nearby list (5 stations, 5 bus stops).

| Phase | Est. mA | CPU | Screen | Wi-Fi | CPU busy c0/c1 % | FPS moving | Slowest frame ms | Tap→screen ms |
|---|---|---|---|---|---|---|---|---|
| 1 Dim idle | 81.8* | 34.2 | 4.5 | 36.9* | 12 / 2 | – | – | – |
| 2 Awake, still | 57.4 | 34.2 | 9.4 | 7.3 | 9 / 4 | – | – | – |
| 3 Scroll | 110.1 | 57.9 | 29.7 | 16.6 | 62 / 100 | 37.4 | 58 | – |
| 4 Screen churn | 66.1 | 38.9 | 13.0 | 7.9 | 18 / 25 | 38.1 | 53 | 23 (max 36) |
| 5 Network burst | 85.9 | 38.6 | 4.6 | 36.6 | 38 / 3 | – | – | – |
| 6 Everything | 124.1 | 53.8 | 19.8 | 44.7 | 66 / 71 | 46.3 | 83 | 30 (max 73) |
| 7 Cool-down | 50.2 | 33.5 | 4.6 | 5.6 | 7 / 2 | – | – | – |

Memory and fixed parts add 7 mA to every phase. All figures are model-v1 estimates.

**Findings**

1. **\*Phase 1 is not clean in this run.** The warm-up timed out after 3 minutes, because one train station's requests kept failing (12 failures, each holding the radio for 22 s before timing out). Phase 1 started while those retries were still going, so its Wi-Fi share is inflated. Cool-down (phase 7, 50.2 mA) is the clean dim-idle number for this run. The 24.8 h projection uses phase 1, so it's too low; with phase 7's dim number it would be about 38 h.
2. **The CPU is the biggest cost while idle.** In dim idle the CPU is about 34 of 50 mA, though the cores are only 2–12% busy. The chip runs at 240 MHz the whole time and never light-sleeps (power management is off in this Arduino core).
3. **Scrolling maxes out core 1** (100% busy drawing, 37 fps), and sends about 4.1 million pixels a second to the panel. That's the most expensive screen state.
4. **A failed request is expensive.** A hung train request keeps the radio busy for 22 s before it gives up.
5. **Free internal RAM got as low as 21 KB** during the run (25 KB before phase 6). That's below the known 30 KB dip and a stability risk to watch, especially once BLE is added.
6. **No resets, every phase ran on time, and every request in phases 2–7 succeeded** (87 of 87 in the network burst).

## Baseline, run 2 of 3: stopped (2026-11-09)

Stopped by hand at about 8:22 PM, roughly 13 minutes in (Leo's call). Run 3 was cancelled. No data was saved: the run script only wrote its CSV when a run ended. It now saves whatever it recorded when it's stopped, marked `# aborted: stopped by hand`.

## Baseline, runs 2 and 3, and the baseline result (2026-11-09)

- **Runs:** `runs/2026-11-09-baseline-2.csv` and `-3.csv` (1,025 samples each, no resets, every request succeeded: 226 and 221 of them). Both ran on firmware `1aad096` without reflashing. Run 2 of the first attempt was stopped by hand and isn't among these.
- **Report:** `reports/baseline.html`, table: `reports/baseline.md`. Per-run reports: `reports/baseline-after-run-2.*`, `-3.*`.
- **Two phases excluded** (`--exclude`), since something other than the firmware disturbed them:
  - Run 1, phase 1: one station's requests were failing at the start (see run 1).
  - Run 3, phase 3: the stress test began on the screen run 2 had left, the Buses tab, so it scrolled the 5-card bus list instead of the 12-card train list. It drew up to 117 frames a second, many with nothing changed, and sent half the pixels. That's a flaw in the stress test, now fixed: `stress::start` resets to Home, the Trains tab, and both lists at the top, as a fresh boot shows (`stress.cpp`). Every measured run after this one is also flashed first, which resets it too.

| Phase | Est. mA | Noise (range) | CPU busy c0/c1 % | FPS moving | Slowest frame ms | Tap→screen ms | Request ms (ok/all) |
|---|---|---|---|---|---|---|---|
| 1 Dim idle | 52.6 | 1.5 | 9 / 2 | – | – | – | 866 (30/30) |
| 2 Awake, still | 57.7 | 1.5 | 10 / 4 | – | – | – | 700 (26/26) |
| 3 Scroll | 107.9 | 4.4 | 61 / 100 | 37.3 | 86 | – | 1837 (19/19) |
| 4 Screen churn | 65.5 | 1.7 | 18 / 23 | 37.1 | 66 | 23 (max 65) | 779 (29/29) |
| 5 Network burst | 85.6 | 0.6 | 38 / 3 | – | – | – | 858 (261/261) |
| 6 Everything | 123.1 | 1.5 | 65 / 70 | 47.2 | 84 | 30 (max 76) | 1343 (220/220) |
| 7 Cool-down | 51.3 | 1.7 | 8 / 2 | – | – | – | 805 (25/25) |

**Baseline projected battery life: 36.9 h** (typical day, 2000 mAh) at 54.2 mA average. Lowest free internal RAM: 21 KB.

**Findings**

1. **Noise is small**: within 1.7 mA for every phase except scrolling (4.4 mA). So a saving above about 3 mA in a quiet phase (about 9 mA while scrolling) is real by the spec's rule (twice the noise).
2. **Dim idle is about 52 mA, and the CPU is about 34 of it** while 2–9% busy, confirming run 1's main finding. That's round 1's target.
3. **Scrolling is the most expensive state the user drives** (108 mA): the drawing core is pegged at 100%.
4. The model's numbers are unchanged by the round 1 worker's model-v2 code for these runs: re-scored, they match model-v1 exactly (they have no per-clock columns).

## Round 1: 80 MHz CPU on the dim always-on screen (2026-11-09)

- **Change:** commit `1c39d26` (worker 1, reviewed by one fresh Opus reviewer at xhigh: no Blockers or Should fixes). Full speed (240 MHz) becomes a hold any task can take; with no hold the CPU drops to 80 MHz. The frame loop holds it except once the dim always-on screen is drawn and no finger is down; the touch task holds it through every press; the network task holds it through every poll, the HTTP clock fallback, Wi-Fi joins, and scans. The power log gained six columns (`clk_ms_80/160/240`, `clk_busy_80/160/240`) and the model costs each part of a second at its own clock (model-v2; the baseline re-scores identically).
- **Why:** in dim idle (about 90% of a day) the CPU was about 34 of 52 mA while 2–9% busy, because it ran at 240 MHz all the time. Expected: dim idle 52.6 → about 44 mA, burst 85.6 → about 80, cool-down 51.3 → about 42; projection 36.9 → about 43 h.
- **Run:** `runs/2026-11-09-r1-1.csv` (1,201 samples, no resets). Report: `reports/r1.html`, table: `reports/r1.md`.

**Result: inconclusive. The network failed during the run, so its numbers can't be compared with the baseline.**

| Phase | Baseline mA | Round 1 mA | Why |
|---|---|---|---|
| 1 Dim idle | 52.6 | 88.0 | a train station's requests failed every 22 s until 4.6 min after boot (13 failures); each holds the radio and 240 MHz |
| 2 Awake, still | 57.7 | 57.3 | clean; always 240 MHz by design |
| 3 Scroll | 107.9 | 109.6 | clean; within noise (4.4) |
| 4 Screen churn | 65.5 | 135.4 | Wi-Fi dropped at 10:34 into the run (status 6, then 0) |
| 5 Network burst | 85.6 | 125.8 | no Wi-Fi: no requests at all; the model charges a lost connection as continuous joining (90 mA) |
| 6 Everything | 123.1 | 160.3 | no Wi-Fi |
| 7 Cool-down | 51.3 | 125.3 | no Wi-Fi |

**Findings**

1. **The clock change works on the board.** The CPU spent 72 s of dim idle, 139 s of the network burst phase, and 99 s of cool-down at 80 MHz, and switched back to 240 for requests and joins. The USB link kept streaming through every switch, and there were no resets.
2. **Nothing the user sees got worse where the run was clean:** scrolling 37.6 fps moving (baseline 37.3), slowest scroll frame 64 ms (86), tap→screen 23 ms average in churn (23), phase 6 49.5 fps (47.2). Phase 6's tap→screen (37 ms avg, max 86, against 30 and 76) and slowest frame (96 against 84) are slightly worse, but that phase had no Wi-Fi, so the network task was rejoining instead of polling; not comparable.
3. **Lowest free internal RAM rose from 21 to 25 KB,** probably because there were fewer TLS connections in the second half (no Wi-Fi). Not credited to the change.
4. **Failed requests right after boot are a pre-existing problem.** Baseline run 1 (also freshly flashed) shows the same pattern: one station's requests failing every 22 s for about 4.5 minutes. The runs that weren't reflashed (baseline 2 and 3) don't. That wastes radio time and keeps the CPU at 240 MHz, and it outlasts the warm-up's 3-minute limit, so it spoils phase 1. Worth fixing as its own round.
5. **The Wi-Fi drop happened at 240 MHz** (phase 4 is awake, so no clock switches were near it), and the board kept failing to rejoin for over 10 minutes. The same network refused the plain live build earlier tonight (disconnect reason 2), so the network is the likely cause, not round 1; being checked before a rerun.
