# Round 1 worker brief: dock speed and responsiveness

You are the round 1 worker in a performance analysis of the CTA Tracker **dock** (Waveshare ESP32-S3-Touch-LCD-4.3). Your job: change the dock firmware so it **feels faster and runs cooler**, without changing what it does. Leo's words: "Focus more on speed and responsiveness, since this won't be on a battery, power consumption won't matter as much, besides heat." From the handheld round: "It's crucial the actual functionality is not changed."

Read first:
- `docs/superpowers/specs/2026-11-12-dock-performance-analysis-design.md`: the contract (rules, logger, stress test, gate). Section 9 is the first look at the code.
- `docs/perf/log.md` and `docs/perf/reports/baseline.md`: the baseline numbers.
- `docs/Agent-Workflow.md`: how you work (you + fresh Opus reviewers at xhigh, at most 3 review rounds, stop early when findings are small).
- `firmware/dock/platformio.ini` (why the core and SDK are pinned), `firmware/dock/src/*` and the main spec's section 7 for what each screen must do. `docs/power/summary.md` shows what worked and what didn't on the handheld.

## The hardware and firmware, briefly

- ESP32-S3-WROOM-1-N8R8 (8 MB octal PSRAM), 800 × 480 RGB panel used in portrait 480 × 800, GT911 touch on I2C, 21 WS2812 LEDs on RMT. arduino-esp32 3.2.0 with Espressif's prebuilt high_perf SDK (XIP from PSRAM, 64 B cache lines). Wall powered.
- Frame loop on core 1 (`main.cpp` `loop()`): touch in, `dock::tick`, the network snapshot, then if anything changed (data, touch, the clock's minute, the half-second bob and blink, a drag), `display::render` draws the whole screen in 25 bands of 32 portrait rows into a 30 KB internal-RAM band (`dock_ui.cpp` over `firmware/lib/gfx` `gfx2d`), hashes each band, copies only changed bands into the hidden PSRAM frame buffer (rotated into the panel's landscape order, `bands.cpp`), then switches buffers and waits for a frame read wholly after the switch.
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

## What the numbers say (check run `runs/2026-11-12-try-1.csv`, firmware `68778ad`; the 3 baseline runs are recording now)

| Phase | Temp °C | Busy c0/c1 % | Frames/s | FPS dragging | Frame ms (draw / copy / wait) | Slowest ms | Tap ms (max) | Drag ms | List ms | Request ms |
|---|---|---|---|---|---|---|---|---|---|---|
| 1 Idle | 29.0 | 9 / 21 | 2.2 | – | 88 (68 / 6 / 15) | – | – | – | – | 537 |
| 2 Screen churn | 30.9 | 12 / 26 | 3.6 | – | 73 (46 / 12 / 15) | – | 84 (144) | – | 579 | 579 |
| 3 Map pan | 32.7 | 12 / 84 | 7.0 | **7.0** | **139 (95 / 25 / 19)** | 176 | 144 | 140 | – | 644 |
| 4 Picker scroll | 33.8 | 10 / 66 | 11.0 | **11.4** | 83 (41 / 17 / 24) | 88 | 85 | 85 | – | 567 |
| 5 Network burst | 34.6 | 31 / 32 | 3.1 | – | 100 (78 / 8 / 14) | – | 97 (115) | – | – | 826 |
| 6 Mixed | 35.6 | 32 / 55 | 6.7 | 9.3 | 96 (59 / 19 / 18) | 176 | 98 (144) | 111 | 987 | 1038 |
| 7 Cool-down | 34.9 | 10 / 22 | 2.2 | – | 92 (71 / 6 / 15) | – | – | – | – | 523 |

After boot: Wi-Fi up 4.2 s, clock set 7.1 s, every stop loaded 11.5 s. No late panel frames, no panel restarts, lowest free internal RAM 50 KB.

What this says:
- **Drawing is the whole story.** A full redraw of the stops screen costs about 68 ms on core 1, even when only Trackie's bob or a blink changed (the band hash then skips the copy, but every pixel is still drawn). The map costs about 95 ms and pans at **7 fps**; the picker list scrolls at **11 fps**. A tap takes 85–145 ms to reach the screen: one frame.
- **Copying into the PSRAM frame buffer costs 6–25 ms** a frame (the rotated copy, `bands.cpp`, 32-pixel runs into rows 1600 bytes apart), and **waiting for the panel 15–24 ms**.
- **Idle heat comes from redrawing**: core 1 is 21% busy at 2 frames a second doing full redraws. The chip warms through the run (29 → 35 °C), so compare temperatures phase for phase between interleaved runs.
- **Core 0 is mostly idle** (9–12%, 31% during the burst): spare capacity.
- Requests take 520–1040 ms; a picker list 500–1000 ms.

## Ideas, roughly by expected value (you decide; check each against the code and measure)

1. **Don't draw what can't have changed.** Most frames at rest change only Trackie's bob, a blink, the clock: track which regions (or bands) a frame can change and draw only the bands they touch, keeping the rest of the hidden buffer as it is (each buffer must get the same bands: there are two). Data, screen, or touch changes still redraw fully until proven safe. This should cut idle and churn drawing several times over, and heat with it. It must stay pixel-identical: prove it with the laptop preview (render each screen and the bob/blink states both ways, compare bytes).
2. **Make a full redraw cheaper**, since dragging redraws everything: profile the primitives (serial `g` on the board prints per-primitive time; on the laptop, time `dock::draw` per screen), then speed up the hot ones in `firmware/lib/gfx` (text, plates, rims, gradients, anti-aliased lines, fills) without changing a pixel. The map at 95 ms is the worst.
3. **The map's still parts** (tracks, casings, zones) are the same every pan frame, only offset: a cached layer in PSRAM blitted at the pan offset, with vehicles and labels on top, could make panning several times faster, if it's pixel-identical.
4. **Overlap the copy and the panel wait with drawing**: e.g. copying a band by async DMA (`esp_async_memcpy`, PSRAM-capable on the S3) while the next one draws, or drawing the next frame's first bands while waiting for the switch. Watch internal RAM (a second 30 KB band would take lowest free from 50 to about 20 KB; TLS needs about 45 KB during a handshake, so smaller bands are safer).
5. **Use core 0** for part of the drawing, if the network task (priority 1) and the touch task still get their time; the handheld's round 3 failed exactly because moving work onto core 0 slowed requests.
6. **Requests**: the handheld's round 2 cut request time 14–34% with resumed TLS sessions and the CPU idling through DNS and TCP waits (`firmware/handheld/mock/src/live.cpp`, commit `5b09be1`); see whether the dock's `net.cpp` can do the same.

Round 1 should go after ideas 1 and 2 first: they're the biggest and safest.

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
