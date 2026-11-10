# Handheld power analysis: summary (2026-11-09 to 2026-11-10)

Every current figure is an **estimate** from the power model (`tools/power_model.py`, `power_model.json`, model-v2). The board ran on USB with no battery; nothing measured current directly. The full record of runs, reasons and findings is `log.md`. The rules and stress test are in `docs/superpowers/specs/2026-11-09-handheld-power-analysis-design.md`.

## Where it ended

**The firmware on main is round 2** (`dd3a0af`, with round 3 reverted in `0c60ad2`). Two rounds passed the gate. Round 3 didn't, and is kept on branches `power-r3` and `power-r3-dma-only`.

| Phase | Baseline mA | Now mA (chained) | Change |
|---|---|---|---|
| Dim idle (90% of a day) | 52.6 | about 41.6 | **−21%** |
| Awake, still | 57.7 | about 48.4 | **−16%** |
| Network burst | 85.6 | about 70.6 | **−18%** |
| Everything at once | 123.1 | about 106.3 | **−14%** |
| Scroll, churn | 107.9, 65.5 | see below | |

**Projected battery life (typical day, 2000 mAh): 36.9 h → about 45.8 h (+24%).**

The "now" column chains two comparisons, because the stress test changed between rounds 1 and 2:
- Baseline → round 1 was measured under stress-v1.
- Round 1 → round 2 was measured under stress-v2, as round 2 against an interleaved run of round 1's firmware.

Each "now" figure is baseline × (round 1 / baseline) × (round 2 / round 1 under v2). Scroll and churn depend on how many cards the live data has on screen, which changed through the night, so they're given per round below rather than chained.

**Responsiveness and load times (better or equal everywhere):**
- **First trains after Wi-Fi up:** about 12 s → 2.3 s. Everything loaded about 18 s → 8.5 s after Wi-Fi up.
- **Requests:** 14–34% faster in every phase (resumed TLS, the CPU idles through DNS and TCP waits).
- **Worst tap in screen churn:** 141 → 40 ms. Slowest frame in churn 63 → 48 ms.
- **The 22 s post-boot request failures:** cause found and fixed. A failed DNS lookup went on to connect to 0.0.0.0.
- **Waking the dim screen:** drawn at once, not at the next half-second.
- **Lowest free internal RAM:** about 21 KB throughout (within the 2 KB allowance).

## The rounds

| Round | What changed | Result | Report |
|---|---|---|---|
| Baseline | First working live firmware (`1aad096`) | 36.9 h projected; dim idle 52.6 mA, CPU at 240 MHz all the time | `reports/baseline.*` |
| 1 (`1c39d26`) | 80 MHz on the dim always-on screen; 240 MHz is a hold taken by touch, network and drawing | **Passes.** Dim idle −17%, cool-down −15%, 43.4 h; fps and taps equal or better | `reports/r1.*` |
| 2 (`5b09be1`, `dd3a0af`) | DNS fix; resumed TLS; clock from the first station; 80 MHz in DNS/TCP waits and between still frames; skip unchanged always-on frames; 32-card cache; cheaper row hash | **Passes.** Awake still −16%, churn −13%, burst −13%, everything −12%; requests 14–34% faster; load about 10 s sooner | `reports/r2.*` |
| 3 (branch `power-r3`) | DMA panel sends; row hashing moved to the sender on core 0 | **Fails.** Frames 40% faster while moving and taps sooner, but core 0 got busier and starved the network task: everything +10.7 mA, requests up to 65% slower while scrolling | `reports/r3.*` |
| 3, DMA only (branch `power-r3-dma-only`) | DMA panel sends alone | **Borderline, not kept.** Core 0 40 → 24% busy while scrolling and scroll −3 mA, but a few user-visible numbers slightly worse, and not checked by eye | `reports/r3a.*` |

All stress-v2 groups side by side: `reports/final-v2.html`.

## What's left

1. **Dim idle (about 42 mA) is now mostly the CPU idling at 80 MHz.** The next big step is light sleep, which needs power management in the sdkconfig. It would also cut the USB link this analysis logs through, so it needs another way to log (a battery and the fuel gauge, or logging to flash).
2. **Round 3, done right:** DMA sends with the hashing kept on the drawing core, and the network task given priority over the sender during requests. Check the panel by eye, then rerun interleaved.
3. **Bus TLS sessions don't fit their slot.** Moving mbedTLS's big buffers to PSRAM would let them resume and let the train connection stay open through the bus poll.
4. **Run conditions:** real data and server latency vary through the night, so every comparison must run interleaved with its reference (A, B, A) at the same hour.
