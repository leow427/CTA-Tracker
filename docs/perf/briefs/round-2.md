# Round 2 worker brief: dock speed and responsiveness

You are the round 2 worker in a performance analysis of the CTA Tracker **dock** (Waveshare ESP32-S3-Touch-LCD-4.3). Your job: change the dock firmware so it **feels faster and runs cooler**, without changing what it does. Leo's words: "Focus more on speed and responsiveness, since this won't be on a battery, power consumption won't matter as much, besides heat." From the handheld round: "It's crucial the actual functionality is not changed."

Read first:
- `docs/superpowers/specs/2026-11-12-dock-performance-analysis-design.md`: the contract (rules, logger, stress test, gate). Section 9 is the first look at the code.
- `docs/perf/log.md` (read it all: the baseline and round 1, with findings) and `docs/perf/reports/r1.md`.
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

## What the numbers say (round 1 against the baseline, interleaved, 2 runs each)

| Phase | Frame ms (draw / copy / wait) | FPS dragging | Tap ms (max) | Drag ms | Busy c0/c1 % | Temp °C | Request ms | List ms |
|---|---|---|---|---|---|---|---|---|
| 1 Idle | 20 (4.8 / 0.9 / 14.4) | – | – | – | 9 / 7.5 | 36.4 | 531 | – |
| 2 Screen churn | 37 (13.1 / 8.7 / 14.9) | – | 61 (92) | – | 11 / 14 | 36.6 | 544 | 1271 |
| 3 Map pan | **84 (45.7 / 18.4 / 19.4)** | **11.4** | 85 | 85 | 11 / 71 | 37.7 | 617 | – |
| 4 Picker scroll | **54 (22.7 / 18.5 / 12.8)** | **17.1** | 56 | 56 | 9 / 69 | 38.4 | 559 | – |
| 5 Network burst | 28 (9.5 / 3.4 / 14.6) | – | 25 (38) | – | 29 / 11 | 38.2 | 822 | – |
| 6 Mixed | 60 (29.2 / 17.0 / 14.0) | 14.3 | 66 (95) | 70 | 34 / 47 | 38.9 | 874 | 516 |
| 7 Cool-down | 20 (5.0 / 1.1 / 14.2) | – | – | – | 9 / 7.6 | 37.4 | 505 | – |

Baseline for comparison: map 7.1 fps (137 ms frames), picker 11.4 fps (83 ms), taps 85–145 ms. No late panel frames, lowest free internal RAM 51 KB, every stop loaded 10.9 s after boot.

What this says:
- **Frame rate moves in panel steps.** A frame ends at the first panel frame (29.3 ms) after its switch, so fps is 34, 17, 11.4, 8.5. The map pan's 84 ms frame sits just above the **58.6 ms needed for 17 fps**; the picker's 54 ms sits above the **29.3 ms for 34 fps**, which is far but not impossible (it was 83 ms in the baseline).
- **The copy into PSRAM (17–18 ms) and the panel wait (13–19 ms) are now as large as drawing** on moving frames. The copy is about 0.7–0.8 ms a band: 480 short rows of 64 bytes, each into a frame-buffer row 1600 bytes away, through the cache. The wait is core 1 doing nothing while the panel finishes a frame.
- **Map drawing is still 46 ms** (tracks, casings, zones, labels, vehicles: most of it is the same every pan frame, shifted).
- **Core 0 is mostly idle** (9–11% outside the burst).
- **Requests** take 500–870 ms, and a bus route list 500–1300 ms (the network is untouched so far).
- **Heat:** the chip's temperature didn't move beyond noise in round 1 even with core 1 two-thirds less busy at idle; it follows the board's own warming more than load. Keep cores no busier.

## Ideas, roughly by expected value (you decide; check each against the code and measure)

1. **Hide the copy behind drawing.** Draw band N+1 while band N is copied, by async DMA (`esp_async_memcpy`, GDMA, which can write PSRAM on the S3; mind cache write-back of the destination rows so the panel's refill never reads stale lines, and alignment) or by a copier on core 0 at a priority that can't starve the network task (the handheld's round 3 failed exactly by starving it). Two smaller bands (2 × 16 rows) keep internal RAM where it is; a second 30 KB band would drop the 51 KB floor to about 21 KB (TLS needs about 45 KB during a handshake: don't). Or make the CPU copy itself cheaper first (measure what 0.75 ms a band is spent on: cache misses on the destination rows?).
2. **Use the panel wait.** While core 1 waits for the switch to complete, record the next frame and draw what doesn't need the old buffer; only copying into the old front buffer must wait. With a drag, the newest touch sample should go into the frame that's drawn, not one 30 ms stale: check that the frame's input is taken as late as possible (drag-to-screen counts from the oldest unshown sample).
3. **The map's still layer.** Tracks, casings and zones depend on the pan only by an offset: a cached layer (PSRAM, or per-band) blitted at the pan offset, with vehicles, pins and labels drawn on top, if it's byte-identical. Recording currently refuses blits: you'd extend it. Getting the map's frame under 58.6 ms gives 17 fps.
4. **Text**: walk glyph pixels down the band's contiguous direction; opaque and alpha paths apart (round 1's worker ranked it the next-largest primitive). Faded paths (cards at 0.55 alpha when Wi-Fi drops, the night veil): blend colour once outside the pixel loop.
5. **Recording cost**: two 32-bit hash lanes instead of 64-bit FNV, a smaller call record than 96 B; the map's vehicles as one pixel-art call instead of 56 rectangles (about 1 ms a frame).
6. **Requests and list loads**: resumed TLS sessions and the CPU idling through DNS and TCP waits, as the handheld's round 2 did (`firmware/handheld/mock/src/live.cpp`, commit `5b09be1`: 14–34% faster requests). Watch internal RAM (51 KB floor now).

Round 2 should go after ideas 1–3 first (they decide the frame rate), then 6 if it's safe.

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
