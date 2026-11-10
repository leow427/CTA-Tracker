# Round 3 worker brief: handheld power AND performance

You are the round 3 (final) worker in the CTA Tracker handheld power analysis. Same job as round 2: make the firmware **use less power and feel faster** without changing what it does. Leo: "It's crucial the actual functionality is not changed," and "really prioritize performance too, which includes responsiveness and load times. Obviously along with efficiency."

Read first, in this order:
1. `docs/power/briefs/round-2.md`: the round 2 brief. Everything in its sections "The hardware and firmware", "What must not change" and "Hard rules" applies to you unchanged. Its numbers are older; this brief has newer ones.
2. `docs/superpowers/specs/2026-11-09-handheld-power-analysis-design.md` (section 3 describes stress-v2, which judges you).
3. `docs/power/log.md` (every run so far) and `docs/Agent-Workflow.md` (you + fresh Opus reviewers at xhigh, at most 3 review rounds).
4. `git show 5b09be1 dd3a0af`: round 2's two commits, so you know what's already been done and don't redo or undo it.

## Where things stand

The reference is round 1's firmware under stress-v2 (two runs; your round is compared with round 2 under stress-v2, which is being measured now; the orchestrator will message you the result):

| Phase | Est. mA | CPU busy c0/c1 % | FPS moving | Slowest frame ms | Tap→screen ms | Request ms |
|---|---|---|---|---|---|---|
| 1 Dim idle | 43.3 | 13 / 6 | – | – | – | 841 |
| 2 Awake, still | 59.3 | 10 / 4 | – | – | – | 978 |
| 3 Scroll (Trains ↔ Buses) | 93.6 | 44 / 99 | 62.6 | 76 | 20 (max 30) | 1252 |
| 4 Screen churn | 65.6 | 18 / 23 | 38.7 | 61 | 23 (max 49) | 804 |
| 5 Network burst | 84.5 | 38 / 5 | – | – | – | 1005 |
| 6 Everything | 122.4 | 60 / 70 | 51.0 | 76 | 28 (max 73) | 1367 |
| 7 Cool-down | 43.4 | 13 / 6 | – | – | – | 1245 |

Projected 43.8 h. Lowest free internal RAM 21 KB.

Round 2 (`5b09be1`, `dd3a0af`) added:
- resumed TLS sessions per host;
- a DNS path without the 0.0.0.0 connect;
- the clock from the first station's `Date` header;
- 80 MHz during DNS/TCP waits and between the frames of a still screen;
- always-on frames skipped when unchanged;
- a 32-card cache;
- a two-lane row hash.

On the board, the first trains now arrive 2.3 s after Wi-Fi is up (was about 12 s), and everything is loaded 8.5 s after (was about 18 s). Bus, weather and alerts handshakes are still full.

## Ideas, ranked by round 2's worker (you decide, and check each against the code)

1. **Interrupt-driven DMA panel sends.** Replace Arduino_GFX's polled 2 KB transactions with queued DMA transactions of up to 16 KB, so core 0 isn't busy-waiting on the panel. Expected:
   - Core 0 in scroll: from about 44–61% busy to about 35%.
   - About −4 mA in scroll and −2 mA in everything.
   - TLS during scrolling gets faster.

   The SPI handle is private in the GFX library, so you'd need the shared-bus mode with your own device, or your own databus class. It's the biggest performance and power lever left, and the riskiest. The frames must stay pixel-identical (the preview tool and `tools/frame_test.cpp` help) and tearing-free.
2. **mbedTLS's large allocations (≥4 KB) in PSRAM**, via `mbedtls_platform_set_calloc_free`. That frees about 33 KB of internal RAM per connection, so:
   - the train connection could stay open through the bus poll (no reconnect at all);
   - bus sessions would fit their slot;
   - BLE gets headroom.

   Check first that AES DMA works with PSRAM buffers and that handshakes don't get slower.
3. **Touch on the CST9217 INT line (GPIO 11)** instead of the 8 ms poll while no finger is down. This cuts core 0's idle wake-ups in dim idle (core 0 is 13% busy there at 80 MHz).
   - Check the wiring in the Waveshare docs and code.
   - A tap on the dim screen must still wake it at least as fast.
   - You can't test on the board, so keep the 8 ms poll as a fallback if INT doesn't arrive (for example, poll at 8 ms for N ms after an INT, and slowly otherwise only if INT has been seen to work).
   - If you can't make it provably safe without the board, skip it.
4. **Faster Wi-Fi joins** with a cached BSSID and channel, for load time and rejoins. The cache must fall back to a normal scan the moment it fails.
5. **Per-strip drawing waste:** the scroll bar's arc set-up and the card key `snprintf` run in every strip. Do them once per frame (about 0.2–0.5 ms per frame).
6. **The train TLS session in RTC memory**, so it resumes after a soft reset or reflash.
7. `rowHash` (18.6 KB internal) to PSRAM: +18.6 KB lowest RAM for about 0.3–0.5 ms per frame. Only if it costs no fps or frame time, which is unlikely; performance comes first.

**Don't do light sleep or anything that stops USB serial.** The power log streams over the USB-Serial-JTAG port, and auto light sleep would cut it, so no run could measure it.

## Rules (same as round 2, plus these)

- Start by bringing your worktree to main: `git merge --ff-only main` (main is at `dd3a0af` or later). Your commits must apply cleanly on top of main with `git cherry-pick`.
- Never write a Wi-Fi network name, password, key, coordinate, stop ID or home path into any file or commit. Don't edit `docs/power/log.md` or the briefs.
- Copy the git-ignored `firmware/secrets.h` from the main checkout to build; never print or commit it.
- Don't touch the board, `stress.*`, `tools/power_run.py`, `tools/power_report.py`, or the numbers in `power_model.json`.
- Builds `pio run -e live/-e power/-e mock` and `pio test` in `firmware/native` must pass. So must `tools/frame_test.cpp`, `tools/live_test.cpp` and the preview tool's byte-identical PNG check (see round 2's commits for how they're run).
- Commits: no `Co-Authored-By` or "Generated with" lines, no `git commit -a`, never stage `docs/superpowers/specs/2026-11-03-cta-tracker-design.md`. Don't push.
- Update `firmware/handheld/mock/README.md`'s "Power" section.
- **Time box:** the orchestrator needs your commits by about 4:30 AM local time (Leo wants the whole analysis done by morning). Prefer two or three solid, reviewed changes over many risky ones. If the DMA rewrite can't be finished and reviewed by then, do the others.

## Report back

1. Branch and commit hashes, and what each change does.
2. The expected per-phase effect on estimated mA and on each performance metric, against round 2.
3. Behaviour risks and how you ruled them out.
4. What to look for in the board run: columns, events and serial lines.
