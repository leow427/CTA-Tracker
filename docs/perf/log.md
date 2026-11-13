# Dock performance log

One entry per change, newest last. How it works is in the spec: `docs/superpowers/specs/2026-11-12-dock-performance-analysis-design.md`.

- Speed first, heat second. The dock runs from the wall, so there's no current model: heat is the chip's own temperature sensor.
- Raw runs are in `runs/` (every line the logger printed: `H` column names, `P` one sample a second, `E` events). Reports are in `reports/`; worker briefs in `briefs/`.
- A change passes only if a speed score gets better beyond noise, none gets worse, the chip runs no hotter, and the panel shows no new late frames (spec section 6).

How to run a test:

```bash
python3 tools/dock_perf_run.py --change r1 --note "what changed"
python3 tools/dock_perf_report.py docs/perf/runs/*-baseline-*.csv docs/perf/runs/*-r1-*.csv --out docs/perf/reports/r1.html
```

---

## Tooling check (2026-11-12)

- **Run:** `runs/2026-11-12-try-1.csv` on `58c8b60` plus the logger (uncommitted then, committed as `68778ad`). Every phase ran on time, no resets, no late panel frames. Its numbers match the baseline's below, so round 1's brief was written from it while the baseline recorded.
- The dock's USB serial port dropped once, about 10 s into an earlier attempt, and came back after a replug. `dock_perf_run.py` now saves what it recorded if the port goes away.

## Baseline (2026-11-12 to 13)

- **Firmware:** `68778ad`: the first working dock firmware (`58c8b60`) plus the logger (`env:perf`). Wi-Fi: the home network (Leo, 2026-11-12).
- **Runs:** `runs/2026-11-12-baseline-1.csv`, `runs/2026-11-13-baseline-1.csv`, `-2.csv`, back to back (about 900 samples each, no resets, every request in phases 1–7 timed). Report: `reports/baseline.html`, table: `reports/baseline.md`.

| Phase | Temp °C | Busy c0/c1 % | Frames/s | FPS dragging | Frame ms (draw / copy / wait) | Slowest ms | Tap ms (max) | Drag ms | List ms | Request ms |
|---|---|---|---|---|---|---|---|---|---|---|
| 1 Idle | 35.7 | 9 / 22 | 2.2 | – | 91 (70 / 6 / 15) | – | – | – | – | 535 |
| 2 Screen churn | 36.0 | 11 / 26 | 3.6 | – | 74 (47 / 12 / 15) | – | 86 (154) | – | 517 | 540 |
| 3 Map pan | 36.9 | 13 / 85 | 7.1 | 7.3 | 135 (93 / 25 / 17) | 166 | 141 (154) | 136 | – | 617 |
| 4 Picker scroll | 37.2 | 10 / 66 | 11.0 | 11.4 | 83 (41 / 17 / 24) | 88 | 85 (86) | 85 | – | 556 |
| 5 Network burst | 37.5 | 30 / 31 | 3.0 | – | 105 (82 / 8 / 15) | – | 108 (134) | – | – | 908 |
| 6 Mixed | 38.3 | 33 / 55 | 6.7 | 9.3 | 97 (59 / 19 / 19) | 176 | 100 (173) | 113 | 905 | 921 |
| 7 Cool-down | 37.1 | 10 / 22 | 2.2 | – | 93 (72 / 6 / 15) | – | – | – | – | 528 |

After boot: Wi-Fi up 4.2 s, clock set 5.3 s, every stop loaded 9.7 s. Lowest free internal RAM 50 KB. No late panel frames, no panel restarts.

**Findings**

1. **Drawing is almost all of a frame.** A full redraw of the stops screen costs about 70 ms on core 1, every time anything changes, even just Trackie's half-second bob (the band hash then skips the copy, but every pixel is drawn). The map costs about 93 ms and pans at **7 fps**; the picker list scrolls at **11 fps**. A tap reaches the screen one frame later: 85–140 ms.
2. **Copying into the PSRAM frame buffer** costs 6–25 ms a frame, and **waiting for the panel** 15–24 ms.
3. **Idle isn't idle**: core 1 is 22% busy redrawing the whole screen twice a second.
4. **Frame times are steady between runs** (within about 6 ms in every phase; map pan within 11 ms), so a real speed-up shows clearly.
5. **Temperature drifts run to run** as the board warms: idle 34.2, 36.3, 36.6 °C in the three runs, back to back. Its noise is about 2.4 °C, so every comparison from here runs interleaved (reference, change, reference, change), and heat is judged phase for phase.
6. Requests take 530–920 ms, and a bus route list 500–1170 ms.
