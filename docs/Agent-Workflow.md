# Agent workflow

Every code change is built by a worker and audited by fresh reviewer agents. Leo talks only to the worker. One plan task at a time.

## Workers

1. Read this file, your task in its plan (`docs/superpowers/plans/`), and the spec it names.
2. Build it test-first: watch each test fail, then pass.
3. Start a fresh reviewer with the Agent tool, `model: "opus"` and `effort: "xhigh"`. Give it this file, the task, `docs/reviewer.md`, and the base commit, not your own summary.
4. When a review comes back, tell Leo explicitly what it found: each Blocker and Should fix, in a sentence or two, before or while you fix it. Do this after every round, not only in the final report. Then fix every Blocker and Should fix, and:
   - **At most 3 rounds.**
   - **After any round:** if every finding was small (a wording change, a missing test, a one-line correction, nothing that changes the code or logic substantially), fix and test it yourself and stop. Skip the remaining rounds.
   - **Otherwise:** run the next round with a fresh reviewer.
   - **After round 3:** if a Blocker remains, ask Leo before committing.
   - If you think a finding is wrong, ask Leo rather than skipping it.
5. Run every affected suite once in full (below), read the whole diff, check `git status` for strays and private data, then make one commit and push. No attribution lines.
6. Report to Leo: the commit, each review round and what it found, anything outside the task, and anything left for Leo (for example, steps that need the boards or the iPhone).

Tasks that change only docs or notes (like the day-1 toolchain spike's write-up) skip review.

**Suites:**

| Area | While working | Before committing |
|---|---|---|
| Shared C++ (`firmware/lib`) | `cd firmware/native && pio test -e native -f test_<area>` | `pio test -e native` |
| Python tools | `python3 -m unittest tools.test_<name> -v` | `python3 -m unittest discover -s tools -v` |
| Firmware builds | `pio run` in the project you touched | `pio run` in `firmware/dock` and `firmware/handheld` |
| iPhone app | the affected test class only | the full app test suite |

**Rules:**

- **Git:** one commit per task, at the end. No `stash`, `checkout`, `reset`, `clean`, or `rebase`. Stay inside the task.
- **Plan vs. spec:** if the task and the spec disagree, or the code doesn't match the task, stop and ask Leo. Never edit a spec or a plan; propose changes in your report.
- **Tests:** never delete, skip, or weaken a test. Tests stay offline and deterministic: CTA and weather data come from `fixtures/`, time is passed in (`now`), and nothing in a test touches the network, Wi-Fi, or BLE.
- **Hardware:** steps that need the boards, the iPhone, or real API keys are Leo's. Prepare them, say exactly what to run and what passing looks like, and wait.
- **Dependencies:** none beyond what the spec and `docs/notes/toolchain.md` name. Use the pinned versions.
- **Secrets:** API keys and Wi-Fi passwords never go in git, logs, serial output, or test fixtures. Recorded CTA responses have the key stripped from any URL they contain.
- **Privacy:** the phone's location stays on the phone and the handheld; it's never logged or sent anywhere except Open-Meteo and the handheld.
- **Spec values:** use the spec's exact values (thresholds, intervals, UUIDs, colors, copy). They live in `cta_config.h`, `ble_ids.h`, and `line_colors.*`, and their Swift mirrors. Don't repeat them elsewhere.

## Reviewers

Never edit a file or touch git state. Never trim a review to save usage.

- **Scope:** the diff since the base commit, plus new files.
- **Check, in order:**
  1. Correctness: bugs, crashes, buffer bounds and fixed-size copies, integer overflow, time math, timeouts, reconnects (Wi-Fi, BLE), and memory use on the ESP32 (PSRAM vs. internal RAM, heap left with Wi-Fi + TLS + BLE + LVGL running).
  2. Security and privacy: no keys or passwords in git, logs, or fixtures; TLS verification on; BLE writes of Wi-Fi credentials require an encrypted, authenticated link; location never logged or sent anywhere the spec doesn't allow.
  3. The spec's exact values and copy.
  4. Every part of the task.
  5. Tests, including mutation tests in a scratch copy: break the code and confirm a test fails.
  6. Build registration: new sources are picked up (`library.json`, `platformio.ini`, the Xcode project), and the affected `pio test` / `pio run` / Xcode build succeeds.
  7. No stray files, private paths, credentials, or attribution lines.
- **Run** only the affected tests. Kill processes by PID, never with a broad `pkill -f`.
- **Report** only Blocker and Should fix findings, ranked, each with file:line, what's wrong, a failure scenario, and a fix. No nits. If nothing needs fixing, say so.
