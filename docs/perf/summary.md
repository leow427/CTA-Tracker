# Dock performance analysis: summary (2026-11-12 to 13)

The dock's speed and heat, measured on the dock itself with `env:perf` and the `dock-stress-v1` test, one round at a time, every comparison interleaved with its reference. Rules, logger and gate: `docs/superpowers/specs/2026-11-12-dock-performance-analysis-design.md`. Every run, reason and finding: `log.md`.

## Where it ended

**The firmware on the branch is round 1 + round 3** (`b6f68bc`, `8c022ad`, `01348bb`, `42ae6ce`, `4756d33`), with round 2 reverted (`5aee211`, kept on branch `dock-perf-r2`). Rounds 1 and 3 passed the gate; round 2 didn't. Leo still needs to look at the panel (below).

| What you feel | Baseline | Now | Change |
|---|---|---|---|
| Map pan | 7.1 fps (137 ms frames) | **12.9 fps** (76 ms) | **+82%** |
| Picker scroll | 11.4 fps (83 ms frames) | **17.1 fps** (56 ms) | **+50%** |
| Drag to screen, map | 139 ms | **78 ms** | −44% |
| Drag to screen, picker | 85 ms | **58 ms** | −32% |
| Tap to screen, switching screens | 88 ms (worst 144) | **60 ms** (worst 94) | −32% |
| Tap to screen, during a network burst | 105 ms (worst 130) | **29 ms** (worst 46) | −72% |
| A frame at rest (bob, blink) | 91 ms, 70 of it drawing | **21 ms**, 6 of it drawing | −77% |
| Core 1 busy at idle | 22% | **7.4%** | −66% |
| Requests | 535–920 ms | **368–625 ms** | −19 to −36% |
| Core 0 busy in the network burst | 30% | **18%** | −39% |
| Chip temperature | 36–39 °C | the same | no phase changed beyond noise |
| Every stop loaded after boot | about 10 s | about 10 s | unchanged |

"Now" chains two comparisons: baseline → round 1 (`reports/r1.*`) and round 1 → round 3 (`reports/r3.*`), each interleaved at the same hour. No run had a late panel frame, a panel restart or a reset, and the lowest free internal RAM stayed at 49–51 KB.

## The rounds

| Round | What changed | Result | Report |
|---|---|---|---|
| Baseline (`68778ad`) | First working dock firmware + logger | Every frame redraws the whole screen (70 ms, 94 on the map) | `reports/baseline.*` |
| 1 (`b6f68bc`) | Each frame recorded once and played band by band; bands the buffer already holds aren't drawn or copied; faster plates and lines | **Passes.** 53 scores better. Map 7 → 11.4 fps, picker 11 → 17 fps, taps 31–77% sooner, idle core 1 22 → 7.5% | `reports/r1.*` |
| 2 (`0a200aa`, branch `dock-perf-r2`) | Half bands copied on core 0 while core 1 draws | **Fails.** Drawing got slower (both cores on PSRAM), fps barely moved, worst frames +30 ms, core 0 +20–30 points | `reports/r2.*` |
| 3 (`8c022ad`, `01348bb`, `42ae6ce`, `4756d33`) | Cheaper map and fade drawing; word-store copy; no sleep after a frame; resumed TLS | **Passes.** Requests 19–36% faster, map 11.4 → 12.9 fps, core 0 cooler | `reports/r3.*` |

## Leo's look at the panel (not yet done)

Rounds 1 and 3 are byte-identical to the baseline on the laptop, but nobody has looked at the real panel since. On the dock, check: Trackie's bob, the clock's minute, the blink glyphs, the poke bubble appearing and leaving, cards greying when Wi-Fi drops, the map (pan, recenter, the faded unpicked lines, vehicles), the picker (scroll, the bottom fade, a real pick), the night veil, and no tearing or 32-row strip that lags.

## What's left

1. **The copy into PSRAM (18 ms a moving frame) is now the wall.** Neither a second core (round 2) nor a tighter loop (round 3) helped: the time is in PSRAM itself, probably each 64-byte line being read before it's written. The likely way through is GDMA into the frame buffer with careful cache handling, which needs a small descriptor ring to fit the internal RAM budget. It's also the only way the picker reaches 34 fps.
2. **The panel's `bb_invalidate_cache` flag** might speed up all drawing (the refill evicts the cache every 1.2 ms), but it's a panel-setup change: Leo's call, and a tearing risk.
3. **The map at 17 fps** needs about 18 ms more off its 76 ms frame: profile a pan on the board (serial `g`), then one recorded call per line shape instead of per segment.
4. **Bus API TLS sessions** don't fit a 4 KB slot; bigger slots only after checking internal RAM.
5. **Heat never moved** beyond noise: the chip's temperature follows the board's warming more than CPU load. If heat matters, measure it at the enclosure, not the chip.
