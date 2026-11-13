# Round 3 worker brief: dock speed and responsiveness

You are the round 3 worker in a performance analysis of the CTA Tracker **dock** (Waveshare ESP32-S3-Touch-LCD-4.3). Your job: change the dock firmware so it **feels faster and runs cooler**, without changing what it does. Leo's words: "Focus more on speed and responsiveness, since this won't be on a battery, power consumption won't matter as much, besides heat." From the handheld round: "It's crucial the actual functionality is not changed."

Read first:
- `docs/superpowers/specs/2026-11-12-dock-performance-analysis-design.md`: the contract (rules, logger, stress test, gate). Section 9 is the first look at the code.
- `docs/perf/log.md` (read it all: the baseline, round 1 which passed, and round 2 which failed and was reverted, with findings) and `docs/perf/reports/r1.md`, `r2.md`.
- Round 2's commit `ddeb145` on branch `dock-perf-r2` (`git show ddeb145`): reverted as a whole, but parts of it are worth salvaging (below). Its worker's scratch tools are in `the session scratchpad (not in the repo)/r2/tools/`.
- Round 1's commit `b6f68bc` (`git show b6f68bc`): the recorded-frame path you build on. Its worker left verification tools you may reuse (not committed) in `the session scratchpad (not in the repo)/tools/`: `compare.sh` (byte comparison of raw frames against a base manifest, `scratchpad/raw_base.sha`), `bench.cpp`/`bench_rec.cpp` (laptop timings), `hh.sh` (handheld checks), `mutate.py` (mutation tests). Rebuild the base manifest from round 1's commit before you change anything.
- `docs/Agent-Workflow.md`: how you work (you + fresh Opus reviewers at xhigh, at most 3 review rounds, stop early when findings are small).
- `firmware/dock/platformio.ini` (why the core and SDK are pinned), `firmware/dock/src/*` and the main spec's section 7 for what each screen must do. `docs/power/summary.md` shows what worked and what didn't on the handheld.

## The hardware and firmware, briefly

- ESP32-S3-WROOM-1-N8R8 (8 MB octal PSRAM), 800 × 480 RGB panel used in portrait 480 × 800, GT911 touch on I2C, 21 WS2812 LEDs on RMT. arduino-esp32 3.2.0 with Espressif's prebuilt high_perf SDK (XIP from PSRAM, 64 B cache lines). Wall powered.
- Frame loop on core 1 (`main.cpp` `loop()`): touch in, `dock::tick`, the network snapshot, then if anything changed (data, touch, the clock's minute, the half-second bob and blink, a drag), `display::render` records the frame's drawing calls once (`ui::Recording`), then for each of 25 bands of 32 portrait rows plays only the calls that reach it into a 30 KB internal-RAM band, skipping bands whose call hash the hidden buffer already holds, copies drawn bands into the hidden PSRAM frame buffer (rotated into the panel's landscape order, `bands.cpp`), then switches buffers and waits for a frame read wholly after the switch (round 1).
- The panel's bounce-buffer refill interrupts run on core 1 too (that's the tearing fix).
- Core 0: the network task (priority 1), the LED task (2), the touch task (3, 10 ms I2C poll).

## Hard limits

- **Don't touch the panel's setup**: pixel clock, porches, bounce buffer size, `num_fbs`, the SDK/core pins, which core creates the panel, the frame-done wait's meaning (a buffer is never drawn into while the panel may still read it). Tearing there cost a day.
- **Don't change** `stress.*`, `perf_log.*`'s existing columns, `tools/dock_perf_run.py`, or `tools/dock_perf_report.py`'s scoring. If your change needs a new logged fact, add a column at the **end** of the `P,` line.
- **Don't touch the board** (no flashing, no opening `/dev/cu.*`). The orchestrator runs every test.

## What must not change (functionality)

- Every screen (stops, map, picker steps 1–4, setup messages), card, tile, colour, font, layout, Trackie's bob and moods, blinks, the night veil, and every gesture (tap, long press, map pan, edge swipe back, picker scroll, swipe to the map) look and behave the same, frame for frame where drawn.
- Poll intervals and data freshness no longer than now; the LED rows the same.
- No secrets, Wi-Fi names, stop IDs or locations in logs, serial output, commits or docs.

## Goals, in order

1. **Speed up or equal everywhere, beyond noise**: frame time while dragging and fps (map pan, picker scroll), slowest frame, tap-to-screen, drag-to-screen, picker list load, request times, boot to every stop loaded.
2. **Heat no higher**: chip temperature per phase, core busy % (busy cores make heat).
3. Lowest free internal RAM no more than 2 KB lower than the baseline.

## What the numbers say

Round 1 (`b6f68bc`, the firmware you start from) against the baseline: map pan 7.1 → 11.4 fps, picker scroll 11.4 → 17.1 fps, taps 31–77% sooner, idle core 1 22 → 7.5% busy. Its numbers (interleaved, 4 runs: `ref1-*` in round 2's comparison):

| Phase | Frame ms (draw / copy / wait) | FPS dragging | Worst frame ms | Tap ms | Drag ms | Busy c0/c1 % | Request ms |
|---|---|---|---|---|---|---|---|
| 2 Screen churn | 36 (13 / 8.5 / 15) | – | – | 60 | – | 12 / 14 | 560 |
| 3 Map pan | **83 (44 / 19 / 21)** | **11.4** | 88 | 85 | 85 | 11 / 70 | 620 |
| 4 Picker scroll | **54 (23 / 19 / 13)** | **17.1** | 59 | 56 | 56 | 10 / 69 | 560 |
| 5 Network burst | 27 (9 / 3 / 15) | – | – | 25 | – | 30 / 11 | 850 |
| 6 Mixed | 60 (29 / 17 / 14) | 14.3 | 88 | 66 | 70 | 30 / 47 | 900 |

Round 2 (`ddeb145`) **failed**: a copier task on core 0 copied half bands into PSRAM while core 1 drew. Core 1's copy halved (19 → 10 ms), but its drawing grew 6–7 ms (both cores on PSRAM and the shared cache at once, plus the cost of drawing halves), fps barely moved (map 12.2, picker 17.1), worst frames got 30 ms worse (88 → 118), and core 0 got 20–30 points busier (heat). The handheld's round 3 failed the same way. **Lesson: on this chip, splitting memory-bound work across both cores doesn't pay. Make core 1's own work smaller.**

What decides the frame rate now:
- **Panel steps:** a frame ends at the first panel frame (29.3 ms) after its switch: 34, 17, 11.4 fps. The map pan's 83 ms frame needs to get under **58.6 ms for 17 fps**; the picker's 54 ms under **29.3 ms for 34 fps**.
- **The copy** is 0.75 ms a band (about 40 MB/s): 480 rows of 64 bytes, each into a frame-buffer row 1600 bytes away. Each 64-byte write is one data-cache line in PSRAM, and the S3's cache allocates on write: every line is probably read from PSRAM before it's overwritten, doubling the traffic. Measure before you trust that.
- **The recording lives in PSRAM** (458 KB), so playing it back reads PSRAM too.
- **The map's drawing (44 ms)** is mostly tracks, casings and zones that only shift with the pan.

## Ideas, roughly by expected value (you decide; check each against the code and measure)

1. **Salvage round 2's single-core pieces**, without the copier, the half bands or the priority changes: faded map lines blended once a pixel (`fadedSeg`), a line's first step in 32 bits (no `__divdi3`/`__moddi3`), and no 2 ms loop sleep after a pass that showed a frame. Round 2's worker expected the first two to cut map drawing several ms; they were masked by the copier's contention.
2. **Cache the map's still layer** (round 2's worker's first idea): tracks, casings and zones drawn once into a PSRAM layer (or per band) and blitted at the pan offset, with vehicles, pins and labels on top, byte-identical. First check that `mapProject`'s rounding shifts by whole pixels under a pan. Recording refuses blits today: extend it. Getting map frames under 58.6 ms gives 17 fps.
3. **A cheaper copy, on core 1**: find out what the 0.75 ms a band is spent on (write-allocate fills? cache misses?) and cut it, e.g. writing whole cache lines in a way that avoids the fill if the S3 allows it, or reordering the writes for the cache, or `esp_async_memcpy` (GDMA) of the band into PSRAM with the destination's cache lines written back and invalidated first so nothing stale is ever written over it, overlapping the next band's drawing. Every option must keep the panel from reading a half-written buffer (only the hidden buffer is written, as now).
4. **Picker under 29.3 ms**: an exact inline replacement for `lroundf` (a library call, several per seg), cheaper plate setup, `listFade`'s roughly 23K alpha blends a frame (precompute the faded rows?), text glyphs walked down the band's contiguous direction.
5. **Requests: resumed TLS sessions**, ported from the handheld (`firmware/handheld/mock/src/live.cpp`, commit `5b09be1`; 14–34% faster requests there). In the burst every request changes host, so each is a full handshake (about 2 s of CPU for the train API's ECDSA chain). Watch internal RAM: lowest free is 51 KB and may not drop more than 2 KB; reloading a session parses the peer certificate into small allocations that land in internal RAM. If it can't fit, leave it out and say so.
6. **Use the panel wait** (13–21 ms of idle core 1 per frame): record the next frame during it, so only the copy waits; the newest touch sample must still go into the frame that's drawn.

Don't put drawing or copying work on core 0.

## Rules

- You work in your own git worktree. `firmware/secrets.h` is git-ignored: copy it from the main checkout's `firmware/secrets.h` to build, never print it, never commit it.
- Build with `~/.platformio/penv/bin/pio` (Homebrew's `pio` lacks pip): `pio run -e dock` and `pio run -e perf` in `firmware/dock` must succeed, and `pio test -e native` in `firmware/native` must pass. Add native tests for any new pure logic (in `firmware/lib`).
- The laptop preview (`firmware/dock/tools/preview.cpp`) renders the UI on the Mac: use it to prove pixel-identical output before and after a drawing change (render every screen before and after, compare bytes).
- Commit on your branch with clear messages; don't push. **No `Co-Authored-By` or "Generated with" lines.** Check `git status` before each commit; never stage `secrets.h`.

## What to report back

1. Commits (hashes) and a plain description of each change.
2. For each stress phase, the expected effect on each score, with reasoning.
3. Anything that could change behaviour or bring tearing back, and how you checked it doesn't.
4. What to look for in the run.
5. Your best ideas for round 2, ranked.
