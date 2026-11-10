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
