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

## Round 1: record each frame once, draw only the bands that changed (2026-11-13)

- **Change:** `b6f68bc` (worker 1; one review round by a fresh Opus reviewer at xhigh found no Blocker or Should fix).
  - Each frame's drawing calls are recorded once (`ui::Recording`, `firmware/lib/gfx`), then played band by band: only the calls that reach a band run there, so the screens' code runs once a frame instead of 25 times.
  - Each band gets a hash of the calls that reach it, and each of the two frame buffers remembers what it holds: a band whose hash matches isn't drawn or copied. At rest, Trackie's bob and the blinks alternate in step with the two buffers, so most frames at rest draw nothing.
  - Faster plates (only the visible part of the shadow, interiors filled two pixels per store) and line segments (no 64-bit division per step), same pixels.
  - A frame that can't be recorded (a blit, over 4096 calls) is drawn exactly as before. A new last column, `drawn`, counts bands drawn.
  - Checked pixel-identical on the laptop: 1014 raw frames (26 states × 13 variations, both mountings, both paths) byte-identical to the base, a 70-frame two-buffer sequence, the handheld's previews unchanged, 300 random scenes in native tests, mutation tests.
- **Why:** the baseline spent 70 ms of every frame redrawing the whole screen, even for a bob, and 94 ms on the map.
- **Runs, interleaved:** `ref0-1`, `r1-1`, `ref0-2`, `r1-2` (ref0 = the baseline firmware `68778ad`, rebuilt in a scratch worktree). Report: `reports/r1.html`, table: `reports/r1.md`.

| Phase | Frame ms ref0 → r1 | FPS dragging | Tap ms (max) | Drag ms | Busy c1 % | Temp °C |
|---|---|---|---|---|---|---|
| 1 Idle | 91 → **20** | – | – | – | 22 → **7.5** | 36.4 → 36.4 |
| 2 Screen churn | 74 → **37** | – | 88 (144) → **61 (92)** | – | 26 → **14** | 36.9 → 36.6 |
| 3 Map pan | 137 → **84** | 7.1 → **11.4** | 144 → **85** | 139 → **85** | 84 → **71** | 38.0 → 37.7 |
| 4 Picker scroll | 83 → **54** | 11.4 → **17.1** | 85 → **56** | 85 → **56** | 66 → 69 | 38.4 → 38.4 |
| 5 Network burst | 104 → **28** | – | 105 (130) → **25 (38)** | – | 30 → **11** | 38.6 → 38.2 |
| 6 Mixed | 98 → **60** | 9.2 → **14.3** | 100 (158) → **66 (95)** | 113 → **70** | 56 → **47** | 39.2 → 38.9 |
| 7 Cool-down | 93 → **20** | – | – | – | 22 → **7.6** | 37.8 → 37.4 |

**Result: passes** (pending Leo's look at the panel). 53 scores better beyond noise. The only one flagged worse is a part of a frame, not a score: the picker's copy per frame rose 1.4 ms (17.1 → 18.5), while its whole frame got 29 ms shorter. Requests, list loads and boot times within noise; no late panel frames, no restarts, lowest free internal RAM unchanged (51 KB), no resets.

**Findings**

1. **Frames at rest are nearly free now**: about 5 ms of drawing (recording) and 1 ms of copying, so idle core 1 drops from 22% to 7.5% busy.
2. **Temperature didn't move beyond noise** even with core 1 two-thirds less busy at idle: the chip's temperature follows the board warming through the night more than the CPU's load. Heat is judged phase for phase between interleaved runs.
3. **Frame rate moves in panel steps** (a frame ends at the panel frame after the switch, 29.3 ms each: 34, 17, 11.4, 8.5 fps). The map pan now sits at 84 ms, just over the 58.6 ms needed for 17 fps; the picker sits at 54 ms, well over the 29.3 ms needed for 34 fps.
4. **The copy into PSRAM (17–18 ms) and the panel wait (13–19 ms) are now as big as the drawing** on moving frames. That's round 2's target, with the map's 46 ms of drawing.
5. Leo should look at the panel for any 32-row strip that lags or doesn't update (bob, clock minute, blink glyphs, the poke bubble, greyed cards when Wi-Fi drops, map pan and recenter, picker scroll, a real pick, the night veil), and for tearing.

## Round 2: half-band copies on core 0, faded lines blended once (2026-11-13): fails, reverted

- **Change:** `ddeb145` (worker 2; review round 1 found one Should fix, the copier's priority against the idle task, fixed by moving every core 0 task up one; round 2 found nothing).
  - Each band drawn in two halves; a new copier task on core 0 copies one half into the frame buffer while core 1 draws the other, and core 1 copies whatever the copier hasn't reached.
  - Faded map lines blended once a pixel; a line's first step in 32 bits (no 64-bit divisions); no 2 ms sleep after a pass that showed a frame. New last column `copier_ms`.
  - Byte-identical to round 1 on the laptop (1014 frames, with stand-in copiers).
- **Why:** after round 1 the copy into PSRAM (17–18 ms) was as large as the drawing on moving frames, and core 0 was mostly idle.
- **Runs, interleaved:** `ref1-1`, `r2-1`, `ref1-2`, `r2-2` (ref1 = round 1's firmware `b6f68bc`). Report: `reports/r2.html`, table: `reports/r2.md`.

| Phase | Frame ms ref1 → r2 | Draw ms | Copy ms (core 1) | FPS dragging | Worst frame ms | Busy c0 % | Tap ms |
|---|---|---|---|---|---|---|---|
| 3 Map pan | 83 → 79 | 44 → **51** | 19 → 10 | 11.4 → 12.2 | 88 → **118** | 11 → **33** | 85 → 88 |
| 4 Picker scroll | 54 → 56 | 23 → **29** | 19 → 9 | 17.1 → 17.1 | 59 → **88** | 10 → **40** | 56 → 58 |
| 5 Network burst | 27 → 31 | 9 → 15 | 3 → 1 | – | – | 30 → 30 | 25 → **33** |
| 6 Mixed | 60 → 61 | 29 → **35** | 17 → 11 | 14.3 → 14.5 | 88 → **118** | 30 → **45** | 66 → 67 |

**Result: fails** (8 better, 17 worse beyond noise). Reverted in `0653c11`; kept on branch `dock-perf-r2`.

**Findings**

1. **The copy moved but the frame didn't get faster.** Core 1's copy halved (19 → 10 ms), but its drawing grew by 6–7 ms: both cores now hit PSRAM and the shared cache at once, and drawing in halves costs its own setup. The copier also didn't keep up, so core 1 still copied half the rows itself.
2. **Worst frames got 30 ms worse** (88 → 118 ms in map pan and mixed; 59 → 88 in the picker): some frames now miss a panel step they used to make.
3. **Core 0 got 20–30 points busier while dragging**, which is heat for no visible gain.
4. Lesson, same as the handheld's round 3: on this chip, splitting one memory-bound job across both cores doesn't pay; PSRAM bandwidth is shared. Round 3 should make each core's own work smaller.
5. The pieces that don't touch the copy (faded lines blended once, 32-bit line starts, no sleep after a frame) are worth salvaging on their own.
6. Boot came out faster in the r2 runs (every stop loaded 8.3 vs 11.0 s), but nothing in round 2 touched the network: that's the network's own variation.

## Round 3: cheaper map and fade drawing, a tighter copy, no sleep after a frame, resumed TLS (2026-11-13)

- **Change:** four independent commits from worker 3, on round 1's firmware (one review round by a fresh Opus reviewer at xhigh found one Should fix, a missing test at the 32/64-bit line switch, added; nothing else).
  - `675a553` drawing: round 2's single-core pieces (a line's first step in 32 bits; faded lines blended once a pixel; opaque lines write their lead pixels directly), an exact inline rounding in place of `lroundf`, faded fills (`blendRect`) worked out once per rectangle (the picker's bottom fade), each map vehicle one call per colour (map frames 610 → 284 recorded calls), track points projected once and shifted by the pan.
  - `96cafd2` copy: each 64-byte band row written as 16 word stores, no `memcpy` call per row.
  - `807783f` loop: no 2 ms sleep after a pass that showed a frame.
  - `f9bb1be` network: TLS sessions resumed per host (4 PSRAM slots of 4 KB), as the handheld does; bus sessions are bigger than a slot and stay full. A new `E,tls,<kind>,resumed|full` event.
  - Byte-identical on the laptop (1014 preview frames plus a 1246-frame map and picker sweep), fuzzed and mutation-tested.
- **Runs, interleaved:** `ref1b-1`, `r3-1`, `ref1b-2`, `r3-2`, then `r3-3`, `ref1b-3` (ref1b = round 1's firmware `b6f68bc`). Report: `reports/r3.html`, table: `reports/r3.md`. About 36 of 166 TLS connections a run resumed.

| Phase | Frame ms ref1b → r3 | Draw ms | FPS dragging | Drag ms | Request ms | Busy c0 % | Temp °C |
|---|---|---|---|---|---|---|---|
| 1 Idle | 21 → 21 | 5 → 6 | – | – | 546 → **441** | 9 → 7 | 36.1 → 36.2 |
| 2 Screen churn | 37 → 37 | 14 → 14 | – | – | 572 → **462** | 12 → 9 | 36.3 → 36.4 |
| 3 Map pan | 84 → **76** | 48 → **40** | 11.4 → **12.9** | 85 → **78** | 632 → **407** | 11 → **8** | 37.5 → 37.5 |
| 4 Picker scroll | 54 → 56 | 23 → 22 | 17.1 → 17.1 | 56 → 58 | 601 → **432** | 10 → 7 | 38.1 → 38.1 |
| 5 Network burst | 30 → 30 | 12 → 12 | – | – | 894 → **593** | 29 → **18** | 38.0 → 37.6 |
| 6 Mixed | 60 → 60 | 30 → **27** | 14.3 → 15.0 | 70 → 68 | 885 → **625** | 31 → **22** | 38.9 → 38.4 |
| 7 Cool-down | 22 → 22 | 7 → 7 | – | – | 536 → **368** | 9 → 6 | 37.4 → 37.1 |

**Result: passes** (pending Leo's look at the panel). 15 better beyond noise. Three flagged worse, none of them real:
- the panel wait in phases 4 and 6 (+2–3 ms) is a part of a frame, not a score: frames now start without the 2 ms sleep, so they wait longer for the same panel step; the picker's frame time and fps are unchanged
- the picker list load in churn (500 → 835 ms) is one slow run (1406 ms; the others 561 and 537). Round 1's own firmware did the same once (1990 ms in `r1-2`): the bus host's full handshakes vary, and round 3 doesn't change them.
- The burst's tap time looked worse after two runs (one run at 35 ms); a third pair brought it back to 26 vs 26 ms, within noise.

No late panel frames, no restarts, lowest free internal RAM 49 → 50 KB, no resets, failed requests 1 vs 2 over three runs.

**Findings**

1. **Resumed TLS sessions made every request 19–36% faster** and cut core 0's load in the burst from 29% to 18%: a resumed handshake skips the train API's ECDSA chain check.
2. **The map pan gained 13% (11.4 → 12.9 fps)** but didn't reach 17: about a third of its frames now make the 58.6 ms step. Its frames are still 40 ms of drawing plus 18 ms of copying plus the wait.
3. **The copy didn't get cheaper** (18.2 → 18.1 ms): the word stores cost the same as `memcpy`. The time is in PSRAM itself (each 64-byte line is probably read before it's written), not in the loop.
4. **The picker stays at 17 fps**: draw 22 + copy 18 is 40 ms, far from the 29.3 ms needed for 34.
5. **Heat**: no phase is warmer beyond noise; core 0 is cooler in every phase with requests.
