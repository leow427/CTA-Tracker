# CTA Tracker Foundation Implementation Plan (Plan 1 of 4)

> **For agentic workers:** Execute natively with superpowers:executing-plans, one task at a time, following `docs/Agent-Workflow.md` (fresh reviewers, at most 3 rounds, report every Blocker and Should fix to Leo). Steps use checkbox (`- [ ]`) syntax for tracking. Don't start until Leo says go.
>
> **This plan is a reference, not a script.** Expect it to change as the code teaches us things; update it when you deviate (see the workflow's rules). The spec is the contract.
>
> **The UI is subject to change, and likely will.** Screen layouts, sizes, colors, fonts, copy, and pet art here are starting points, not final designs. Build them so they're easy to change; don't treat a UI detail in this plan as fixed.

**Goal:** Prove the firmware toolchain on both boards, and build everything the dock, handheld, and app share: the C++ transit library, the CTA fixtures, the GTFS data tool, and the pet art pipeline.

**Architecture:** A pure-C++ library (`firmware/lib/cta`) holds all transit logic and is unit-tested on the laptop with PlatformIO's native environment. A thin Arduino-only library (`firmware/lib/cta_net`) does HTTPS. Python tools generate the train-line tables and the pet sprite arrays. Plans 2 (dock), 3 (handheld), and 4 (iPhone app) consume these.

**Tech Stack:** C++17, ArduinoJson 7, PlatformIO (pioarduino platform for ESP32, `native` for tests, Unity), Arduino-ESP32 core 3.x, NimBLE-Arduino 2.x (spike only here), Python 3 standard library.

**Spec:** `docs/superpowers/specs/2026-11-03-cta-tracker-design.md`

**Prerequisite (Leo, by hand, Nov 3):** apply for the CTA Train Tracker and Bus Tracker API keys at the CTA Developer Center (transitchicago.com/developers). Tasks 2–8 and 10–11 don't need keys. Task 9's smoke test and Task 10's bus-ID check run once the keys arrive.

## Global Constraints

- Arduino-ESP32 core 3.x, built with pioarduino; both boards pinned to the same core version (recorded by Task 1 in `docs/notes/toolchain.md`).
- BLE: NimBLE-Arduino 2.x. JSON: ArduinoJson with filters, so only needed fields are kept.
- HTTPS uses the ESP32's built-in CA certificate bundle. TLS verification is never disabled.
- API keys live only in untracked `firmware/secrets.h` (and `ios/Secrets.xcconfig`). Commit `firmware/secrets.example.h` only.
- Stop record: at most 4 arrivals, names truncated to 24, one record ≤ 512 bytes.
- Hyped: live arrival due in ≤ 3 min. Stale data: older than 5 min. LED rows go dark after 2 min. Night hours default 1:00–5:00.
- Poll intervals (spec 6.6): dock 30 s; handheld 3 closest train stations 60 s, other train stations 3 min, buses 60 s; alerts 5 min; weather 15 min; night or docked 5 min.
- Time zone string: `CST6CDT,M3.2.0,M11.1.0`.
- BLE UUIDs exactly as listed in spec section 10.
- Line colors live in one table (Task 2), mirrored later by the app.
- `fixtures/cta/` is shared with the app's tests (iOS milestone 1 wrote it). Any edit there must also pass `cd ios/CTAKit && swift test`.

## Decisions Beyond the Spec

These fill gaps in the spec. Each one is small and reversible.

1. **Sprite format:** sprites are stored as 32×32 palette-index arrays and expanded into an RGB565 buffer by `renderSprite` (Task 11), not as prebuilt LVGL image arrays. The same code works under LVGL 8 and 9, and the app's pet-color swap is just a palette change. This removes spec §11's "output both image formats" fallback.
2. **Scheduled arrivals:** a stop whose next arrival is timetable-only (not live) and not delayed counts as Happy. The spec defines Happy only for live predictions and is silent on this case.
3. **Name truncation:** names are cut to 24 **bytes**, never splitting a UTF-8 character.
4. **Hyped needs fresh data:** the qualifying stop's data must be no more than 5 min old.
5. **Rain beats cold:** a rain code with a temperature below 0 °C gives the umbrella, not the scarf.
6. **Tool dependencies:** Python tools use only the standard library.
7. **Generated data location:** `data/stops.json`, `data/train_lines.json`, and `data/train_shapes.json` (for the app) are committed. The downloaded GTFS zip (`data/gtfs/`) is not.

## Review Focus

These are the failure modes most likely to bite a real user that the spec doesn't spell out. Each one has a test in the task named.

1. **Shape quirks:** CTA returns a single object where an array is expected (Alerts `Alert` and `Service`, Train Tracker `train`), or a number as a string (Bus Tracker `pdist`, `pid`). The parser should treat it as one item or one number. Tests in Tasks 4, 5, 6.
2. **Midnight and year-end:** an arrival that crosses midnight or New Year should show the right minutes, not a negative or huge number. Tests in Tasks 2, 4.
3. **Error responses:** a bad key (`errCd` 101) or Bus Tracker's "No arrival times" should be reported as an error or zero records, not as a crash or garbage. Tests in Tasks 4, 5.
4. **Short rows:** a picked stop with fewer than 6 stops before it should give a partially filled LED row. A stop that isn't in the sequence should return `false`. Neither may index out of bounds. Tests in Task 8.
5. **Long and multibyte names:** names over 24 bytes, or containing multibyte characters, should be truncated cleanly, and the record should still encode within 512 bytes. Tests in Task 3.

## File Structure

```
docs/notes/toolchain.md                 Task 1: pinned versions and board settings
firmware/secrets.example.h              Task 2
firmware/lib/cta/library.json           Task 2
firmware/lib/cta/src/cta_types.h        Task 2: all shared structs and enums
firmware/lib/cta/src/cta_config.h       Task 2: every constant from the spec
firmware/lib/cta/src/ble_ids.h          Task 2: BLE UUIDs
firmware/lib/cta/src/line_colors.{h,cpp}   Task 2
firmware/lib/cta/src/local_time.{h,cpp}    Task 2: CTA timestamps, night hours
firmware/lib/cta/src/text.{h,cpp}          Task 3: UTF-8-safe truncating copy
firmware/lib/cta/src/stop_record.{h,cpp}   Task 3
firmware/lib/cta/src/train_parse.{h,cpp}   Task 4
firmware/lib/cta/src/bus_parse.{h,cpp}     Task 5
firmware/lib/cta/src/alerts_weather.{h,cpp} Task 6
firmware/lib/cta/src/mood.{h,cpp}          Task 7
firmware/lib/cta/src/approach.{h,cpp}      Task 8
firmware/lib/cta/src/urls.{h,cpp}          Task 9
firmware/lib/cta/src/train_sequences.h     Task 10 (hand-written declarations)
firmware/lib/cta/src/train_sequences.cpp   Task 10 (generated)
firmware/lib/cta_net/src/cta_fetch.{h,cpp} Task 9 (Arduino only)
firmware/smoke/                            Task 9: on-device fetch smoke test
firmware/lib/pet/src/pet_render.{h,cpp}    Task 11
firmware/lib/pet/src/pet_sprites.h         Task 11 (hand-written declarations)
firmware/lib/pet/src/pet_sprites.cpp       Task 11 (generated)
firmware/native/platformio.ini             Task 2: laptop test runner
firmware/native/test/fixture.h             Task 2: reads fixtures/cta/*
firmware/native/test/test_<area>/test_main.cpp  one per task
fixtures/cta/*.json                        Tasks 4–6 (synthetic until keys arrive; already written by iOS milestone 1)
fixtures/gtfs-mini/*.txt                   Task 10
tools/gtfs_build.py, tools/test_gtfs_build.py      Task 10
tools/sprite_build.py, tools/test_sprite_build.py  Task 11
art/palette.txt, art/*.txt                 Task 11
data/stops.json, data/train_lines.json     Task 10 (generated)
```

All C++ is in `namespace cta`, except `firmware/lib/pet`, which uses `namespace pet`.

**Running the laptop tests:** `cd firmware/native && pio test -e native` runs all native tests, and `-f test_<area>` runs one. **Python tests:** `python3 -m unittest discover -s tools -v`.

---

### Task 1: Day-1 toolchain test (spike, human in the loop)

This is a feasibility test. Its output is `docs/notes/toolchain.md`, not code. The spike code lives in `spike/`, which is never committed, and gets deleted at the end. Steps marked **(Leo)** need the boards or the iPhone in hand.

**Files:**
- Create: `docs/notes/toolchain.md`
- Modify: `.gitignore` (add `spike/`)

**Interfaces:**
- Produces, for Plans 2 and 3: the exact `platform =` URL and version, the Arduino-ESP32 core version, the LVGL version per board, the versions of ESP32_Display_Panel, ESP32_IO_Expander, XPowersLib, NimBLE-Arduino, and ArduinoJson, each board's `board`/`board_build.*` settings, and the working CA-bundle snippet for `WiFiClientSecure` (or `NetworkClientSecure`).

- [ ] **Step 1: Add `spike/` to `.gitignore`.** Install PlatformIO Core (`pipx install platformio`). Verify: `pio --version` prints a version.
- [ ] **Step 2: Build the dock display demo.** Create `spike/dock/platformio.ini` with `platform = https://github.com/pioarduino/platform-espressif32/releases/download/stable/platform-espressif32.zip` and the board settings for the 4.3" board (8 MB octal PSRAM; flash size from the Waveshare wiki). Port Waveshare's Arduino `lvgl_porting` example using ESP32_Display_Panel + ESP32_IO_Expander. **(Leo)** Flash it. Pass: the LVGL demo renders and responds to touch.
- [ ] **Step 3: Try LVGL 9 on the dock.** Swap to LVGL 9 with an ESP32_Display_Panel version that supports it. Pass: the demo renders. If it fails, record "dock stays on LVGL 8.4" and why. Because of Decision 1, nothing else changes.
- [ ] **Step 4: Build the handheld demo.** Create `spike/handheld/platformio.ini` with the same platform and core version. Port `02_lvgl_demo_v9` and `01_AXP2101` from github.com/waveshareteam/ESP32-S3-Touch-AMOLED-1.75. **(Leo)** Flash it. Pass: the demo renders, and the serial log shows the AXP2101 reporting VBUS present when USB is plugged in and absent on battery.
- [ ] **Step 5: BLE passkey pairing.** On each board, add NimBLE-Arduino 2.x. Advertise `CTA-Spike` with one characteristic that requires an encrypted, authenticated write. In `onPassKeyDisplay`, show the 6-digit passkey on screen. **(Leo)** Connect from an iPhone BLE explorer app (e.g. LightBlue), enter the passkey, and write a value. Pass: the value arrives on serial, and reconnecting doesn't ask for the passkey again (bond stored).
- [ ] **Step 6: HTTPS alongside BLE.** While the iPhone stays connected, GET `https://api.open-meteo.com/v1/forecast?latitude=41.8781&longitude=-87.6298&current=temperature_2m,weather_code` every 10 s for 10 min, using the core's built-in CA bundle. Also GET `https://lapi.transitchicago.com/api/1.0/ttarrivals.aspx` with no key once, which should return an error body over a verified TLS connection. Pass: every request returns HTTP 200, there are no resets, and the minimum free heap logged stays above 40 KB. Record the minimum heap.
- [ ] **Step 7: Fallback, only if Steps 2–6 can't pass on pioarduino.** Repeat them with `arduino-cli` on the same core version, and record that the firmware builds use arduino-cli.
- [ ] **Step 8: Write `docs/notes/toolchain.md`.** Include every value listed under Interfaces → Produces, the pass/fail result of each step, the minimum heap, and the LVGL decision. Delete `spike/`.
- [ ] **Step 9: Commit.**

```bash
git add .gitignore docs/notes/toolchain.md
git commit -m "docs: record pinned firmware toolchain from day-1 spike"
```

---

### Task 2: Library scaffold, constants, colors, and time

**Files:**
- Create: `firmware/secrets.example.h`, `firmware/lib/cta/library.json`, `firmware/lib/cta/src/{cta_types.h, cta_config.h, ble_ids.h, line_colors.h, line_colors.cpp, local_time.h, local_time.cpp}`
- Create: `firmware/native/platformio.ini`, `firmware/native/test/fixture.h`, `firmware/native/test/test_basics/test_main.cpp`

**Interfaces:**
- Produces (`cta_types.h`):

```cpp
namespace cta {
constexpr size_t kMaxArrivals = 4;
constexpr size_t kNameLen = 24;   // bytes, excluding NUL
enum class Kind : uint8_t { Train = 0, Bus = 1 };
enum class Mood : uint8_t { Sleepy, Sad, Worried, Happy, Hyped };
enum class Accessory : uint8_t { None, Umbrella, Scarf };
struct Arrival { int16_t min; char dest[kNameLen + 1]; bool live; bool delayed; };
struct StopRecord {
  char id[8]; Kind type; char route[8]; char dir[kNameLen + 1]; char name[kNameLen + 1];
  Arrival arrivals[kMaxArrivals]; uint8_t count; bool alert; uint32_t ts;  // ts: Unix time of fetch, 0 = never
};
struct TrainPos { char nextStpId[8]; };
struct BusVehicle { int32_t pid; int32_t pdist; };
struct ApproachRow { char stops[7][8]; int32_t pdist[7]; int32_t before; int32_t pid; };  // stops[k][0]=='\0' → no stop at LED k
}
```

- Produces (`cta_config.h`, `constexpr` values from the spec): `kDockPollSec=30`, `kHandheldClosePollSec=60`, `kHandheldFarPollSec=180`, `kHandheldBusPollSec=60`, `kAlertsPollSec=300`, `kWeatherPollSec=900`, `kSlowPollSec=300`, `kHypedMaxMin=3`, `kStaleSec=300`, `kLedStaleSec=120`, `kNightStartMin=60`, `kNightEndMin=300`, `kTz="CST6CDT,M3.2.0,M11.1.0"`, `kBleMaxPayload=512`, `kNearbyPerTab=10`, `kHypedClosestStops=3`, `kScreenDimSec=15`.
- Produces (`ble_ids.h`): `kServiceUuid`, `kInfoUuid`, `kWifiUuid`, `kConfigUuid`, `kRequestUuid`, `kNearbyUuid`, `kSnapshotUuid` as `const char*`, using the spec §10 values.
- Produces: `uint32_t lineColor(const char* route)` (0xRRGGBB, case-insensitive); `bool parseCtaTime(const char* s, int64_t& secs)` (naive wall seconds); `bool ctaSecondsBetween(const char* from, const char* to, int64_t& secs)` (real elapsed seconds, Chicago DST rules; added 2026-11-07); `bool inNightHours(int minuteOfDay, int startMin, int endMin)`.
- Produces (`test/fixture.h`): `std::string readFixture(const char* name)`, which reads `../../fixtures/cta/<name>` and fails the test if the file is missing.

- [x] **Step 1: Create the scaffold.** Add `library.json` (name `cta`, depends on `bblanchon/ArduinoJson@^7`), `firmware/secrets.example.h` (`#define CTA_TRAIN_KEY ""` and `#define CTA_BUS_KEY ""`), and `firmware/native/platformio.ini`:

```ini
[env:native]
platform = native
test_framework = unity
lib_extra_dirs = ../lib
lib_deps = bblanchon/ArduinoJson@^7
build_flags = -std=gnu++17
lib_ignore = cta_net
```

- [x] **Step 2: Write the failing tests** in `test_basics/test_main.cpp`:

```cpp
TEST_ASSERT_EQUAL_HEX32(0xC60C30, cta::lineColor("Red"));
TEST_ASSERT_EQUAL_HEX32(0xC60C30, cta::lineColor("red"));   // ttpositions uses lowercase
TEST_ASSERT_EQUAL_HEX32(0x00A1DE, cta::lineColor("Blue"));
TEST_ASSERT_EQUAL_HEX32(0x62361B, cta::lineColor("Brn"));
TEST_ASSERT_EQUAL_HEX32(0x009B3A, cta::lineColor("G"));
TEST_ASSERT_EQUAL_HEX32(0xF9461C, cta::lineColor("Org"));
TEST_ASSERT_EQUAL_HEX32(0xE27EA6, cta::lineColor("Pink"));
TEST_ASSERT_EQUAL_HEX32(0x522398, cta::lineColor("P"));
TEST_ASSERT_EQUAL_HEX32(0xF9E300, cta::lineColor("Y"));
TEST_ASSERT_EQUAL_HEX32(0x565A5C, cta::lineColor("22"));   // every bus route
int64_t a, b;
TEST_ASSERT_TRUE(cta::parseCtaTime("2026-11-05T08:00:00", a));
TEST_ASSERT_TRUE(cta::parseCtaTime("2026-11-05T08:03:00", b));  TEST_ASSERT_EQUAL_INT64(180, b - a);
TEST_ASSERT_TRUE(cta::parseCtaTime("20261105 08:00", a));
TEST_ASSERT_TRUE(cta::parseCtaTime("20261105 08:04:30", b));     TEST_ASSERT_EQUAL_INT64(270, b - a);
cta::parseCtaTime("2026-11-05T23:58:30", a); cta::parseCtaTime("2026-11-06T00:03:30", b); TEST_ASSERT_EQUAL_INT64(300, b - a);
cta::parseCtaTime("2026-12-31T23:59:00", a); cta::parseCtaTime("2027-01-01T00:01:00", b); TEST_ASSERT_EQUAL_INT64(120, b - a);
TEST_ASSERT_FALSE(cta::parseCtaTime("garbage", a));
TEST_ASSERT_TRUE(cta::inNightHours(60, 60, 300));     // 1:00 starts night
TEST_ASSERT_FALSE(cta::inNightHours(300, 60, 300));   // 5:00 ends it (exclusive)
TEST_ASSERT_FALSE(cta::inNightHours(59, 60, 300));
TEST_ASSERT_TRUE(cta::inNightHours(23*60+30, 23*60, 6*60));   // wraps midnight
TEST_ASSERT_FALSE(cta::inNightHours(7*60, 23*60, 6*60));
TEST_ASSERT_FALSE(cta::inNightHours(100, 120, 120));  // start == end means never
```

- [x] **Step 3: Run and confirm failure.** `cd firmware/native && pio test -e native -f test_basics`. Expected: build fails, symbols undefined.
- [x] **Step 4: Implement the headers and functions** listed under Interfaces. `parseCtaTime` accepts `YYYY-MM-DDTHH:MM:SS`, `YYYYMMDD HH:MM`, and `YYYYMMDD HH:MM:SS`. It returns naive seconds (local wall time treated as UTC, using a days-from-civil calculation); only differences are ever used.
- [x] **Step 5: Run and confirm pass.** Same command. Expected: all tests pass.
- [x] **Step 6: Commit.**

```bash
git add firmware/secrets.example.h firmware/lib/cta firmware/native
git commit -m "feat(cta): library scaffold, constants, line colors, CTA time parsing"
```

- *Done (2026-11-06/07).* Built as planned, plus extra tests (leap days, impossible dates, lookalike routes, every guard the review's mutation tests found uncovered). PlatformIO's registry needed allowing in the cloud environment.
- *Daylight saving (Leo, 2026-11-07):* naive differences were off by an hour across a clock change (spring-forward 1:58 → 3:03 read 65 min). `ctaSecondsBetween(from, to)` applies Chicago's rules (US rules since 2007, computed, not a table): CDT from the second Sunday of March at 2:00 to the first Sunday of November at 2:00. In the repeated 1:00–1:59 hour each time has two readings an hour apart, and the pair whose difference is nearest +25 min wins (a tie goes to the later reading), so every true gap from −4 to +55 min is right. (Review round 1 caught the first version, "closest together", reading arrivals 31–60 min out as negative, so "Due".) Only gaps of −5 min or less, or 56 min or more, that touch the repeated hour can come out an hour off; no one-hour window avoids that. A wall time in the skipped spring hour is read as standard time. Checked against the system tz database for 2007–2040, every minute within 3 h of each change: no mismatch except gaps outside −4…+55 min that touch the repeated hour. Tasks 4 and 5 compute minutes with it; `parseCtaTime` stays as the naive primitive.

---

### Task 3: Stop record codec

**Files:**
- Create: `firmware/lib/cta/src/text.{h,cpp}`, `firmware/lib/cta/src/stop_record.{h,cpp}`, `firmware/native/test/test_stop_record/test_main.cpp`

**Interfaces:**
- Consumes: `StopRecord`, `Arrival`, `kNameLen`, `kMaxArrivals`, `kBleMaxPayload` (Task 2).
- Produces:
  - `void copyTruncated(char* dst, const char* src, size_t maxBytes)`: copies at most `maxBytes` bytes, never ending in the middle of a UTF-8 sequence, and always NUL-terminates.
  - `size_t encodeStopRecord(const StopRecord& r, char* buf, size_t cap)`: writes the spec §6.2 JSON (`id, type:"train"|"bus", route, dir, name, arrivals:[{min,dest,live,delayed}], alert, ts`). Returns the length, or 0 if it doesn't fit in `cap`.
  - `bool decodeStopRecord(const char* json, size_t len, StopRecord& out)`: returns false on malformed JSON or a missing `id`, keeps at most 4 arrivals, and truncates names with `copyTruncated`.

- [x] **Step 1: Write the failing tests.**

```cpp
// round trip: a record with 2 arrivals survives encode → decode field for field
// truncation
char out[25]; cta::copyTruncated(out, "Harlem/Lake (via Marketplace)", 24);
TEST_ASSERT_EQUAL_STRING("Harlem/Lake (via Marketp", out);
cta::copyTruncated(out, "AAAAAAAAAAAAAAAAAAAAAAA\xC3\xA9", 24);   // é would straddle byte 24
TEST_ASSERT_EQUAL_STRING("AAAAAAAAAAAAAAAAAAAAAAA", out);
// worst case fits: all names 24 bytes, 4 arrivals with 24-byte dest, min=999
TEST_ASSERT_TRUE(cta::encodeStopRecord(worst, buf, 512) > 0);
TEST_ASSERT_EQUAL_size_t(0, cta::encodeStopRecord(worst, buf, 100));
// decode keeps 4 of 6 arrivals; malformed JSON returns false
TEST_ASSERT_EQUAL_UINT8(4, decoded.count);
TEST_ASSERT_FALSE(cta::decodeStopRecord("{\"id\":", 6, r));
```

- [x] **Step 2: Run and confirm failure.** `pio test -e native -f test_stop_record`. Expected: undefined symbols.
- [x] **Step 3: Implement** `copyTruncated`, `encodeStopRecord`, and `decodeStopRecord` with ArduinoJson.
- [x] **Step 4: Run and confirm pass.**
- [x] **Step 5: Commit.** `git commit -m "feat(cta): stop record JSON codec with UTF-8-safe truncation"`
- *Done (2026-11-07).* Built as planned, mirroring the app's `StopRecordTests` case for case. Rulings:
  - **Control characters are written as spaces.** ArduinoJson writes bytes below 0x20 raw, which isn't valid JSON and would fail the app's decoder; CTA names never contain them. (The app escapes them as `\u0001` instead; both are valid JSON.) The over-512 test uses quotes, which escape to 2 bytes.
  - **Decode also rejects** a non-string `id` and a `type` other than `"train"` or `"bus"`, matching the app's `Codable` decode; other missing fields default to empty, 0, or false.
  - **Ids and routes are cut to 7 bytes** (`char[8]`), like the app's `idLen`; `min` is clamped to 0–32767.
  - **`encodeStopRecord` uses a bounded writer**, so a record of exactly `cap` bytes fits (no room taken for a NUL) and nothing is ever written past `cap`.

---

### Task 4: Train Tracker parsing

**Files:**
- Create: `firmware/lib/cta/src/train_parse.{h,cpp}`, `firmware/native/test/test_train/test_main.cpp`
- Fixtures (already in the repo from iOS milestone 1; check them against the contents below, don't rewrite them) (synthetic, following the Train Tracker API docs' JSON shape, `outputType=JSON`): `fixtures/cta/train_arrivals_station.json`, `train_arrivals_midnight.json`, `train_error.json`, `train_positions_red.json`, `train_positions_single.json`

**Fixture contents** (every value the tests rely on):
- `train_arrivals_station.json`: `ctatt.tmst` `"2026-11-05T08:00:00"`, `errCd` `"0"`, and five `eta` items, all with `staNm` `"Belmont"`:
  1. `rt` Red, `destNm` "95th/Dan Ryan", `arrT` 08:03:00, `isSch` "0", `isDly` "0"
  2. Red, "Howard", 08:12:00, `isSch` "1"
  3. Red, "95th/Dan Ryan", 08:07:30, `isDly` "1"
  4. Brn, "Kimball", 08:01:00
  5. P, "Linden", 08:20:00
- `train_arrivals_midnight.json`: `tmst` "2026-11-05T23:58:30", one eta with `arrT` "2026-11-06T00:03:30".
- `train_error.json`: `errCd` "101", `errNm` "Invalid API key.", `eta` null.
- `train_positions_red.json`: `route[0].train` is an array of trains with `nextStpId` "30005" and "30001".
- `train_positions_single.json`: `route[0].train` is a **single object** with `nextStpId` "30005".

**Interfaces:**
- Consumes: `StopRecord`, `TrainPos`, `parseCtaTime`, `copyTruncated`.
- Produces:
  - `bool parseTrainArrivals(const char* json, size_t len, uint32_t now, StopRecord& out)`: sets `type=Train`, `name` (staNm), `route` (rt of the soonest arrival), and `ts=now`. Fills up to 4 arrivals sorted by `min`, where `min = floor(ctaSecondsBetween(tmst, arrT)/60)` clamped at ≥ 0 (Task 2, DST-aware), `live = isSch=="0"`, `delayed = isDly=="1"`, and `dest = destNm`. Leaves `id`, `dir`, and `alert` as the caller set them. Returns false if `errCd != "0"` or the JSON is malformed; returns true with `count=0` when there are no etas.
  - `int parseTrainPositions(const char* json, size_t len, TrainPos* out, size_t max)`: returns the number of trains written, or −1 on error. Accepts `train` as either an array or a single object.

- [x] **Step 1: Write the failing tests** (the fixtures exist; see above).

```cpp
cta::StopRecord r{}; strcpy(r.id, "41320");
TEST_ASSERT_TRUE(cta::parseTrainArrivals(f.c_str(), f.size(), 1000, r));
TEST_ASSERT_EQUAL_UINT8(4, r.count);   // Linden at 20 min dropped
TEST_ASSERT_EQUAL_INT16(1, r.arrivals[0].min);  TEST_ASSERT_EQUAL_STRING("Kimball", r.arrivals[0].dest);
TEST_ASSERT_EQUAL_INT16(3, r.arrivals[1].min);  TEST_ASSERT_TRUE(r.arrivals[1].live);
TEST_ASSERT_EQUAL_INT16(7, r.arrivals[2].min);  TEST_ASSERT_TRUE(r.arrivals[2].delayed);
TEST_ASSERT_EQUAL_INT16(12, r.arrivals[3].min); TEST_ASSERT_FALSE(r.arrivals[3].live);
TEST_ASSERT_EQUAL_STRING("Belmont", r.name); TEST_ASSERT_EQUAL_STRING("41320", r.id); TEST_ASSERT_EQUAL_UINT32(1000, r.ts);
// midnight: min == 5
// error fixture: returns false
// positions: red → 2 trains, [0].nextStpId "30005"; single → 1 train
```

- [x] **Step 2: Run and confirm failure.** `pio test -e native -f test_train`.
- [x] **Step 3: Implement** both functions. Use a `DeserializationOption::Filter` covering `tmst`, `errCd`, and the eta and train fields that are read.
- [x] **Step 4: Run and confirm pass.**
- [x] **Step 5: Commit.** `git commit -m "feat(cta): Train Tracker arrivals and positions parsing"`
- *Done (2026-11-07).* Built as planned, plus tests for a lone `eta` object, null/empty/missing `eta`, bad `arrT`, past predictions, ordering by seconds, DST nights, and long names. Rulings:
  - **Lone objects vs. the filter:** an ArduinoJson array filter turns a lone object into null. So `json::parseShapes` (`json_util.h`) tries a filter per expected shape (`eta` as a list, then as one object; `route` as a list, then as one object, each with a list of trains) and re-parses unfiltered only when none fits: a lone train, or no arrivals or trains, which are small responses. (Review round 1: the first version went unfiltered for a lone `route` object, which a one-line ttpositions request may return, about 15–20 KB of internal heap on every dock poll.) `json::unfilteredPasses()` counts the fallbacks so a test pins that big responses stay filtered.
  - **Minutes** use `ctaSecondsBetween(tmst, arrT)` (Task 2's DST fix), floored, clamped to 0–32767. Arrivals are ordered by seconds, so 3:10 comes before 3:40 though both read 3 min; equal times keep response order.
  - **An eta whose `arrT` doesn't parse is skipped**, like the app's `compactMap`. Arrivals with no parseable `tmst` fail the whole response; no arrivals at all is `true` with `count = 0`, and `name`/`route` stay as the caller set them.
  - **`errCd`** may be the string `"0"` or the number 0; anything else, or missing, fails.
  - **Positions** keep a train with no `nextStpId` as `""` (it lights no LED), matching the app.

---

### Task 5: Bus Tracker parsing

**Files:**
- Create: `firmware/lib/cta/src/bus_parse.{h,cpp}`, `firmware/native/test/test_bus/test_main.cpp`
- Fixtures (already in the repo from iOS milestone 1; check them against the contents below, don't rewrite them) (synthetic, Bus Tracker v2 `format=json` shape): `fixtures/cta/bus_predictions.json`, `bus_no_arrivals.json`, `bus_vehicles.json`, `bus_patterns.json`

**Fixture contents:**
- `bus_predictions.json`: `bustime-response.prd`, every item with `tmstmp` "20261105 08:00":
  1. `stpid` "1001", `stpnm` "Clark & Belmont", `rt` "22", `rtdir` "Southbound", `des` "Harrison", `prdtm` "20261105 08:04", `prdctdn` "4", `dly` false
  2. stop 1001, rt "36", `prdtm` 08:01, `prdctdn` "DUE" (other fields as item 1)
  3. stop 1001, rt "22", `prdtm` 08:15, `prdctdn` "DLY", `dly` true (other fields as item 1)
  4. `stpid` "1002", `stpnm` "Belmont & Clark", rt "77", `rtdir` "Westbound", `des` "Cumberland", `prdtm` 08:09, `prdctdn` "9"
- `bus_no_arrivals.json`: `bustime-response.error` = `[{"stpid":"1001","msg":"No arrival times"}]`
- `bus_vehicles.json`: `vehicle` = `[{"pid":100,"pdist":4500}, {"pid":"100","pdist":"1500"}, {"pid":200,"pdist":3000}]` (the second has numbers as strings).
- `bus_patterns.json`: `ptr` holds two patterns. (Task 8 consumes this file.)
  - `pid` 100, `rtdir` "Southbound": `pt` has stops (`typ` "S") with `stpid` "2001"…"2010" at `pdist` 0, 1000, …, 9000, with one `typ` "W" waypoint between 2004 and 2005.
  - `pid` 200: stops "2007", "2008", "2009" at pdist 0, 500, 1000.

**Interfaces:**
- Consumes: `StopRecord`, `BusVehicle`, `parseCtaTime`, `copyTruncated`.
- Produces:
  - `int parseBusPredictions(const char* json, size_t len, uint32_t now, const char* routeFilter, StopRecord* out, size_t max)`: one record per distinct (`stpid`, `rt`, `rtdir`), in order of first appearance (spec §6.2; changed 2026-11-07 from one per `stpid`, so each route and direction at a stop is its own card). Sets `type=Bus`, `id`, `name` (stpnm), `route` (rt), `dir` (rtdir), and `ts=now`. Arrivals are sorted by `min`, up to 4, with `min = 0` if `prdctdn=="DUE"`, otherwise `floor(ctaSecondsBetween(tmstmp, prdtm)/60)` clamped at ≥ 0 (Task 2, DST-aware); `live = true`; `delayed = dly || prdctdn=="DLY"`; `dest = des`. `routeFilter` (nullptr means all) drops other routes. Returns the count; with no `prd`, 0 when every error names a stop, route, or pattern ("No arrival times") and −1 for any other error (bad key, daily limit); −1 if malformed. Records are written with `alert = false`: the caller merges the alerts it polls separately.
  - `int parseBusVehicles(const char* json, size_t len, BusVehicle* out, size_t max)`: accepts integers or integer strings for `pid` and `pdist`; a vehicle without both is skipped. Same error rules as `parseBusPredictions`.

- [x] **Step 1: Write the failing tests** (the fixtures exist; see above).

```cpp
int n = cta::parseBusPredictions(f.c_str(), f.size(), 1000, nullptr, recs, 5);
TEST_ASSERT_EQUAL_INT(3, n);   // one card per (stop, route, direction): 1001/22, 1001/36, 1002/77
TEST_ASSERT_EQUAL_STRING("1001", recs[0].id); TEST_ASSERT_EQUAL_STRING("22", recs[0].route); TEST_ASSERT_EQUAL_UINT8(2, recs[0].count);
TEST_ASSERT_EQUAL_INT16(4, recs[0].arrivals[0].min);
TEST_ASSERT_TRUE(recs[0].arrivals[1].delayed);                    // 15 min, DLY
TEST_ASSERT_EQUAL_INT16(0, recs[1].arrivals[0].min);              // 36, DUE
TEST_ASSERT_EQUAL_STRING("Westbound", recs[2].dir);
n = cta::parseBusPredictions(f.c_str(), f.size(), 1000, "22", recs, 5);
TEST_ASSERT_EQUAL_INT(1, n); TEST_ASSERT_EQUAL_UINT8(2, recs[0].count);
TEST_ASSERT_EQUAL_INT(0, cta::parseBusPredictions(noArr.c_str(), noArr.size(), 1000, nullptr, recs, 5));
TEST_ASSERT_EQUAL_INT(3, cta::parseBusVehicles(v.c_str(), v.size(), vs, 10));
TEST_ASSERT_EQUAL_INT32(100, vs[1].pid); TEST_ASSERT_EQUAL_INT32(1500, vs[1].pdist);
```

- [x] **Step 2: Run and confirm failure.** `pio test -e native -f test_bus`.
- [x] **Step 3: Implement** both functions, with filters.
- [x] **Step 4: Run and confirm pass.**
- [x] **Step 5: Commit.** `git commit -m "feat(cta): Bus Tracker predictions and vehicles parsing"`
- *Done (2026-11-07).* Built as planned, plus tests for lone `prd` and `vehicle` objects, mixed predictions and errors, unparseable times, past predictions, the 4 soonest per stop, DST, long names, and vehicles without a usable `pid`/`pdist`. Rulings:
  - **Errors (changes "0 for a `bustime-response.error` with no `prd`"):** with no list, an error that names a stop, route, or pattern ("No arrival times") returns 0, and any other error (bad key, daily limit) returns −1, as the app throws. Otherwise a hit daily limit would read as "no buses": the dock would clear its arrivals and the pet would turn Sad instead of the data ageing to Sleepy.
  - **Ordering:** by raw seconds, as in the app's `StopRecord.make` and the train parser: DUE is 0 s, so an already-passed prediction sorts before it (both read 0 min); ties keep response order. (Review round 1: the first version clamped passed predictions to 0, which could pick a different route than the app.)
  - **Lone objects:** the filter covers the list shape; only a non-list `prd`/`vehicle` (one item, or none with a possibly lone error) triggers the unfiltered re-parse. A multi-stop response with per-stop errors stays filtered.
  - **One record per (stop, route, direction) (Leo, 2026-11-07):** each route at a stop is its own handheld card, like train cards, so every arrival belongs to the card's route and direction. Most stops are direction-specific, so this is one card per route; the ~286 stops that serve a route both ways (mostly terminals, e.g. stop 1, Jackson & Austin, route 126) get one per direction, as the app's cards do (review). The dock picks its record by (`id`, `route`, `dir`). A prediction with no `rt` is skipped; ids and routes are cut to 7 bytes before matching.
  - **Vehicles** without both a numeric `pid` and `pdist` are skipped (they can't be placed on a pattern). Records are capped at `max`; a card past it is dropped (size `max` per Plan 3 milestone 5).
  - Shared parser helpers live in `json_util.h` (internal), used by both parsers.

---

### Task 6: Alerts and weather

**Files:**
- Create: `firmware/lib/cta/src/alerts_weather.{h,cpp}`, `firmware/native/test/test_alerts_weather/test_main.cpp`
- Fixtures (already in the repo from iOS milestone 1; check them against the contents below, don't rewrite them): `fixtures/cta/alerts.json`, `alerts_single.json`, `weather.json`

**Fixture contents:**
- `alerts.json`: `CTAAlerts.Alert` is an array of 2.
  - Alert 1: `ImpactedService.Service` is an array with `{ServiceType:"R", ServiceId:"Red"}` and `{ServiceType:"T", ServiceId:"41320"}`.
  - Alert 2: `Service` is a **single object** `{ServiceType:"B", ServiceId:"22"}`.
- `alerts_single.json`: `Alert` is a single object whose Service is `{ServiceType:"R", ServiceId:"Brn"}`.
- `weather.json`: `{"current":{"temperature_2m":-2.5,"weather_code":61}}`.

**Interfaces:**
- Produces:
  - `int parseAlertRoutes(const char* json, size_t len, char routes[][8], size_t max)`: returns the distinct `ServiceId`s whose `ServiceType` is "R" (train route) or "B" (bus route), or −1 on error. Accepts `Alert` and `Service` as an array or a single object.
  - `bool parseWeather(const char* json, size_t len, int& wmo, float& tempC)`.
  - `Accessory accessoryFor(int wmo, float tempC)`: Umbrella for codes 51–67, 80–82, and 95–99. Otherwise Scarf for codes 71–77, 85–86, or tempC < 0. Otherwise None.

- [x] **Step 1: Write the failing tests** (the fixtures exist; see above).

```cpp
TEST_ASSERT_EQUAL_INT(2, cta::parseAlertRoutes(a.c_str(), a.size(), routes, 8));   // "Red", "22"; station "41320" ignored
TEST_ASSERT_EQUAL_INT(1, cta::parseAlertRoutes(s.c_str(), s.size(), routes, 8));   TEST_ASSERT_EQUAL_STRING("Brn", routes[0]);
int w; float t; TEST_ASSERT_TRUE(cta::parseWeather(wx.c_str(), wx.size(), w, t));  TEST_ASSERT_EQUAL_INT(61, w);
TEST_ASSERT_EQUAL((int)cta::Accessory::Umbrella, (int)cta::accessoryFor(61, 5));
TEST_ASSERT_EQUAL((int)cta::Accessory::Umbrella, (int)cta::accessoryFor(61, -1));   // rain beats cold
TEST_ASSERT_EQUAL((int)cta::Accessory::Umbrella, (int)cta::accessoryFor(95, 20));
TEST_ASSERT_EQUAL((int)cta::Accessory::Scarf,    (int)cta::accessoryFor(71, 1));
TEST_ASSERT_EQUAL((int)cta::Accessory::Scarf,    (int)cta::accessoryFor(85, 0));
TEST_ASSERT_EQUAL((int)cta::Accessory::Scarf,    (int)cta::accessoryFor(3, -1));
TEST_ASSERT_EQUAL((int)cta::Accessory::None,     (int)cta::accessoryFor(2, 10));
```

- [x] **Step 2: Run and confirm failure.** `pio test -e native -f test_alerts_weather`.
- [x] **Step 3: Implement** the three functions.
- [x] **Step 4: Run and confirm pass.**
- [x] **Step 5: Commit.** `git commit -m "feat(cta): alert route and weather parsing, accessory rule"`
- *Done (2026-11-07).* Built as planned, plus tests for duplicate routes, `max`, non-route services, alerts without `ImpactedService`, every accessory range edge and 0 °C, and malformed or partial weather. Rulings:
  - **`ErrorCode` is ignored; a missing `Alert` is 0 routes**, as in the app (CTA's "no alerts" reply can carry a nonzero code). Only malformed JSON, or no `CTAAlerts` object, is −1.
  - **Filter:** each alert keeps only its `ImpactedService` (whole, since CTA mixes a `Service` list in one alert with a lone object in the next), dropping the HTML descriptions; a lone `Alert` object has its own filter. Only a reply with no alerts reaches the unfiltered pass.
  - **Routes are cut to 7 bytes**, like record routes, so `alert` matching compares like with like.
  - **Weather:** `weather_code` must be an integer and `temperature_2m` a number; otherwise false with both outputs untouched.

---

### Task 7: Mood rules

**Files:**
- Create: `firmware/lib/cta/src/mood.{h,cpp}`, `firmware/native/test/test_mood/test_main.cpp`

**Interfaces:**
- Consumes: `StopRecord`, `Mood`, `kStaleSec`, `kHypedMaxMin`.
- Produces:
  - `Mood stopMood(const StopRecord& r, uint32_t now, bool night)`. Checked in this order:
    1. Sleepy if `ts==0`, `now − ts > kStaleSec`, or `night`
    2. Sad if `alert`, or `count==0`
    3. Worried if `arrivals[0].delayed`
    4. Happy otherwise (including timetable-only arrivals; see Decision 2)
  - `Mood deviceMood(const StopRecord* recs, const bool* hypedEligible, size_t n, uint32_t now, bool night, bool hasLocation)`. Checked in this order:
    1. Sleepy if `!hasLocation` or `night`
    2. Hyped if any eligible, non-stale record has `arrivals[0].live` with `min ≤ kHypedMaxMin`
    3. Otherwise the majority of the non-Sleepy stop moods, with ties going to Sad, then Worried, then Happy
    4. Sleepy if every stop is Sleepy, or `n==0`
  - Asleep (docked) is not decided here. It belongs to the pet layer (Task 11).

- [x] **Step 1: Write the failing tests**, one per row. Use `now=10000`, and a helper `rec(count, min0, live0, delayed0, alert, ts)`.

```cpp
EQ(Sleepy, stopMood(rec(1,5,true,false,false,0), 10000, false));        // never fetched
EQ(Sleepy, stopMood(rec(1,5,true,false,false,10000-301), 10000, false)); // stale
EQ(Sleepy, stopMood(fresh_happy, 10000, true));                           // night
EQ(Sad,    stopMood(rec(1,5,true,false,true,9990), 10000, false));       // alert
EQ(Sad,    stopMood(rec(0,0,false,false,false,9990), 10000, false));     // no predictions
EQ(Worried,stopMood(rec(1,5,true,true,false,9990), 10000, false));
EQ(Happy,  stopMood(rec(1,5,true,false,false,9990), 10000, false));
EQ(Happy,  stopMood(rec(1,5,false,false,false,9990), 10000, false));     // scheduled only
EQ(Sleepy, deviceMood(all_happy, elig_all, 3, 10000, false, false));     // no location
EQ(Hyped,  deviceMood({min 3 live, eligible}, ...));
NE(Hyped,  deviceMood({min 4 live, eligible}, ...));
NE(Hyped,  deviceMood({min 1 live, NOT eligible}, ...));                  // far stop can't hype
NE(Hyped,  deviceMood({min 1 live, eligible, stale}, ...));               // Decision 4
EQ(Happy,  deviceMood({Happy, Happy, Worried}, ...));
EQ(Worried,deviceMood({Happy, Worried}, ...));                            // tie → Worried
EQ(Sad,    deviceMood({Sad, Worried}, ...));                              // tie → Sad
EQ(Worried,deviceMood({Sleepy, Sleepy, Worried}, ...));                   // Sleepy stops excluded
EQ(Sleepy, deviceMood({Sleepy, Sleepy}, ...));
```

- [x] **Step 2: Run and confirm failure.** `pio test -e native -f test_mood`.
- [x] **Step 3: Implement** `stopMood` and `deviceMood`.
- [x] **Step 4: Run and confirm pass.**
- [x] **Step 5: Commit.** `git commit -m "feat(cta): stop and device mood rules"`
- *Done (2026-11-07).* Built as planned, mirroring the app's `MoodRules`, plus tests for the 5-min boundary, alert beating delay, stale beating Sad, hype outranking a stop's own Sad, scheduled-only and empty records never hyping, per-stop eligibility, and a three-way tie. Rulings:
  - **`isStale` is public and signed:** `now − ts` is computed in 64 bits, so a `ts` a little ahead of `now` (a relayed snapshot from a phone whose clock runs ahead) is fresh, as in the app, instead of wrapping to stale.
  - **`hypedEligible == nullptr`** means no stop may hype.

---

### Task 8: Approach rows (LED mapping)

> **Moved to Plan 2 (Leo, 2026-11-07):** only the dock has LED rows, so this task is built with the dock, not here. Its text stays as the reference for Plan 2. Note from the Task 5 review: `bus_patterns.json` gives `pdist` as decimals (`1000.0`), so read it as a float, not with `json::toInt32`.

**Files:**
- Create: `firmware/lib/cta/src/approach.{h,cpp}`, `firmware/native/test/test_approach/test_main.cpp`

**Interfaces:**
- Consumes: `ApproachRow`, `TrainPos`, `BusVehicle`; the fixture `bus_patterns.json` (Task 5).
- Produces:
  - `bool buildTrainRow(const char* const* seq, size_t n, const char* stopId, ApproachRow& out)`: sets `stops[6] = stopId` and `stops[0..5]` to the 6 preceding IDs (farthest first). Missing leading slots are left empty (`""`), and `pid = −1`. Returns false if `stopId` isn't in `seq`.
  - `int buildBusRows(const char* patternsJson, size_t len, const char* stopId, ApproachRow* out, size_t max)`: one row per pattern containing `stopId`, using only `typ=="S"` points (`pdist` arrives as a decimal, e.g. `1000.0`, so read it as a float and round, not with Task 5's integer-only `json::toInt32`). Sets `pdist[k]`, `before` (the pdist of the stop preceding `stops[0]`, or −1 if none), and `pid`. Returns the row count, or −1 if malformed.
  - `uint8_t litMaskTrains(const ApproachRow& row, const TrainPos* t, size_t n)`: bit k is set when some train's `nextStpId` equals `stops[k]`.
  - `uint8_t litMaskBuses(const ApproachRow* rows, size_t nRows, const BusVehicle* v, size_t n)`: for a vehicle on the same `pid`, bit k is set when `pdist[k−1] < v.pdist ≤ pdist[k]`. Empty slots are skipped. For the first non-empty slot, the lower bound is `before` (−1 means no lower bound). Vehicles past `pdist[6]` light nothing.

- [ ] **Step 1: Write the failing tests.**

```cpp
const char* seq[] = {"30001","30002","30003","30004","30005","30006","30007","30008","30009","30010"};
TEST_ASSERT_TRUE(cta::buildTrainRow(seq, 10, "30008", row));
TEST_ASSERT_EQUAL_STRING("30002", row.stops[0]); TEST_ASSERT_EQUAL_STRING("30008", row.stops[6]);
cta::TrainPos t[] = {{"30005"}, {"30001"}, {"30008"}};
TEST_ASSERT_EQUAL_HEX8(0b1001000, cta::litMaskTrains(row, t, 3));   // LED 3 and LED 6 lit; 30001 is outside the row
TEST_ASSERT_TRUE(cta::buildTrainRow(seq, 10, "30003", row));        // only 2 stops before it
TEST_ASSERT_EQUAL_STRING("", row.stops[3]); TEST_ASSERT_EQUAL_STRING("30001", row.stops[4]);
TEST_ASSERT_FALSE(cta::buildTrainRow(seq, 10, "39999", row));
int n = cta::buildBusRows(p.c_str(), p.size(), "2009", rows, 4);
TEST_ASSERT_EQUAL_INT(2, n);
TEST_ASSERT_EQUAL_STRING("2003", rows[0].stops[0]); TEST_ASSERT_EQUAL_INT32(1000, rows[0].before);  // waypoint skipped
TEST_ASSERT_EQUAL_STRING("", rows[1].stops[3]);    TEST_ASSERT_EQUAL_INT32(-1, rows[1].before);
cta::BusVehicle v[] = {{100,4500},{100,1500},{100,500},{100,8000},{100,8500},{200,600}};
// rows[0]: 4500→LED3, 1500→LED0, 500→none, 8000→LED6, 8500→none; rows[1] pid 200: 600→ next stop "2009" → LED6
TEST_ASSERT_EQUAL_HEX8(0b1001001, cta::litMaskBuses(rows, 1, v, 6));
TEST_ASSERT_EQUAL_HEX8(0b1000000, cta::litMaskBuses(rows + 1, 1, v, 6));
```

- [ ] **Step 2: Run and confirm failure.** `pio test -e native -f test_approach`.
- [ ] **Step 3: Implement** the four functions.
- [ ] **Step 4: Run and confirm pass.**
- [ ] **Step 5: Commit.** `git commit -m "feat(cta): approach rows and LED masks for trains and buses"`

---

### Task 9: URL builders and HTTPS fetch

**Files:**
- Create: `firmware/lib/cta/src/urls.{h,cpp}`, `firmware/native/test/test_urls/test_main.cpp`
- Create: `firmware/lib/cta_net/library.json`, `firmware/lib/cta_net/src/cta_fetch.{h,cpp}`
- Create: `firmware/smoke/platformio.ini` (the dock board settings from `docs/notes/toolchain.md`, plus `lib_extra_dirs = ../lib` and `build_flags = -I..` so `secrets.h` is found), `firmware/smoke/src/main.cpp`

**Interfaces:**
- Produces (`urls.h`; each returns false if the URL doesn't fit in `cap`):
  - `bool trainArrivalsUrl(char* buf, size_t cap, const char* key, const char* id)`: uses `mapid=` for IDs starting with '4' and `stpid=` for '3'. Base `https://lapi.transitchicago.com/api/1.0/ttarrivals.aspx`, with `outputType=JSON`.
  - `bool trainPositionsUrl(char* buf, size_t cap, const char* key, const char* route)`: `ttpositions.aspx?rt=`.
  - `bool busPredictionsUrl(char* buf, size_t cap, const char* key, const char* const* stpids, size_t n)`: base `https://www.ctabustracker.com/bustime/api/v2/getpredictions`, with `format=json`. Returns false if `n` is 0 or more than 10.
  - `bool busVehiclesUrl(char* buf, size_t cap, const char* key, const char* const* routes, size_t n)` (max 10).
  - `bool busPatternsUrl(char* buf, size_t cap, const char* key, const char* route)`.
  - `bool alertsUrl(char* buf, size_t cap, const char* const* routes, size_t n)`: base `https://www.transitchicago.com/api/1.0/alerts.aspx`, with `outputType=JSON&activeonly=true&routeid=` and a comma-separated list.
  - `bool weatherUrl(char* buf, size_t cap, double lat, double lon)`: `https://api.open-meteo.com/v1/forecast?latitude=%.4f&longitude=%.4f&current=temperature_2m,weather_code`.
- Produces (`cta_fetch.h`, Arduino only): `int ctaGet(const char* url, char* buf, size_t cap, uint32_t timeoutMs = 8000)`. Returns the body length, or −1 for connect/TLS failure, −2 for a non-200 status, −3 if the body exceeds `cap`. Uses the CA-bundle snippet recorded in `docs/notes/toolchain.md`. Callers pass PSRAM buffers for large responses (bus patterns).

- [ ] **Step 1: Write the failing native tests.** `trainArrivalsUrl(…, "K", "41320")` contains `mapid=41320` and `outputType=JSON`; with `"30255"` it contains `stpid=30255`. `busPredictionsUrl` with 11 IDs returns false, and with `{"1001","1002"}` contains `stpid=1001,1002&format=json`. `weatherUrl(…, 41.87812, -87.62980)` contains `latitude=41.8781&longitude=-87.6298`. Any builder with `cap=20` returns false.
- [ ] **Step 2: Run and confirm failure.** `pio test -e native -f test_urls`.
- [ ] **Step 3: Implement** `urls.cpp` with `snprintf`, then `ctaGet`.
- [ ] **Step 4: Run and confirm the native tests pass.**
- [ ] **Step 5: Write the smoke project** `firmware/smoke/src/main.cpp`. It joins the Wi-Fi from `secrets.h` (add `#define WIFI_SSID` and `#define WIFI_PASS` to `secrets.example.h`, for the smoke test only; real devices get Wi-Fi from the app), fetches weather and prints the accessory, then, if `CTA_TRAIN_KEY` is non-empty, fetches arrivals for `41320` and prints `encodeStopRecord` output.
- [ ] **Step 6 (Leo, once keys arrive): run the smoke test on the dock board.** Pass: the serial output shows a weather code and a stop record JSON with at least one arrival. Save the raw arrivals body as `fixtures/cta/recorded_train_arrivals.json`, and re-run `pio test -e native` with a test that parses it (`count ≥ 0`, returns true). Also save one `ttpositions?rt=Red` body and check whether `route` is a list or one object, and whether a line with one train sends `train` as one object (Task 4's filters handle all of these; the recording settles which is real).
- [ ] **Step 7: Commit.** `git commit -m "feat(cta): URL builders, HTTPS fetch, on-device smoke sketch"`

---

### Task 10: GTFS build tool

**Files:**
- Create: `tools/gtfs_build.py`, `tools/test_gtfs_build.py`, `fixtures/gtfs-mini/{routes,stops,trips,stop_times,shapes}.txt`, `firmware/lib/cta/src/train_sequences.h`
- Generate: `firmware/lib/cta/src/train_sequences.cpp`, `data/stops.json`, `data/train_lines.json`, `data/train_shapes.json`
- Modify: `.gitignore` (add `data/gtfs/`)

**Interfaces:**
- Produces (`train_sequences.h`, hand-written):

```cpp
namespace cta {
struct TrainLine { const char* route; const char* dirLabel; const char* const* stopIds; const char* const* names; uint8_t count; uint8_t lead; };
extern const TrainLine kTrainLines[];
extern const size_t kTrainLineCount;
}
```

- Produces: `data/train_lines.json` = `[{"route":"Red","dir":"95th/Dan Ryan","stops":[{"id":"30xxx","name":"Howard"}, …],"lead":0}]` (see the lead-in ruling below).
- Produces: `data/stops.json` = a list of
  - bus stops: `{"id","name","lat","lon","kind":"bus","lines":[{"route":"22","dir":"Northbound"}]}`
  - stations: `{"id","name","lat","lon","kind":"station","platforms":[{"id":"30xxx","lines":[{"route":"Red","dir":"95th/Dan Ryan"}]}]}`

  Platforms appear only inside their station, because the handheld groups its train cards by station. The app's one-card-per-route-and-direction rule (app design spec 4.2) keys on `lines[].route` + `lines[].dir`.
- Produces: `data/train_shapes.json` = `[{"route":"Red","points":[[41.97732,-87.66688], …]}]`, one or more entries per train route (branches and expresses add their own; see the Shapes ruling below), for drawing the lines on the app's map.
- CLI: `python3 tools/gtfs_build.py <gtfs.zip or dir> [--check] [--bustracker-key KEY]` (`--bustracker-key` arrives with Step 6). It writes the four generated files.
- Rules:
  - Train routes are `route_type == 1`.
  - ~~For each (route, direction_id), the sequence is the most common ordered tuple of platform `stop_id`s across trips; `dirLabel` is that pattern's most common `trip_headsign`; names come from each platform's parent station.~~ Superseded by the Sequences, Headsigns, and Names rulings below.
  - ~~A platform's `lines` are every (route, `dirLabel`) whose trips stop there.~~ Superseded by the Platform labels ruling below.
  - Bus stops are `location_type` 0 or empty, ~~with no `parent_station`,~~ and not used by any train trip (see What's a stop below). A bus stop's `lines` are every (route, direction) whose trips stop there. The direction comes from CTA's non-standard `direction` column in `trips.txt` ("North" → "Northbound", matching Bus Tracker). Fail loudly if the column is missing.
  - Stations are `location_type == 1`.
  - ~~A train route's shape is the most common `shape_id` among its direction-0 trips that follow the most common pattern.~~ Superseded by the Shapes ruling below. Simplify each shape with Douglas–Peucker at 5 m and round to 5 decimals.
  - Stream `stop_times.txt` once with `csv`: train trips feed the sequences, and bus trips feed only each bus stop's `lines`.

**Mini fixture contents:**
- Route `Red` (type 1) and route `22` (type 3).
- Stations 40001 Alpha, 40002 Bravo, 40003 Charlie.
- Platforms 30001–30003 (direction 0) and 30011–30013 (direction 1), parented to the matching stations.
- Bus stops 1001 and 1002.
- Trips:
  - T1 and T2: Red, direction 0, headsign "Charlie", shape S1, 30001→30002→30003
  - T3: Red, direction 0, headsign "Bravo", shape S2, 30001→30002
  - T4: Red, direction 1, headsign "Alpha", 30013→30012→30011
  - B1: route 22, `direction` "North", 1001→1002
- Shapes: S1 has 3 points where the middle one lies on the line between the others; S2 has 2 points.

- [ ] **Step 1: Write the mini fixture and the failing tests** in `test_gtfs_build.py`:
  - `test_most_common_pattern`: the Red "Charlie" line's ids are `[30001, 30002, 30003]` and its names are `[Alpha, Bravo, Charlie]`.
  - `test_both_directions`: the Red lines are exactly the "Charlie" and "Alpha" directions.
  - `test_stops_kinds`: `stops.json` has 3 stations and 2 bus stops, and no 3xxxx IDs at the top level.
  - `test_stop_lines`: bus stop 1001's `lines` are `[{"route":"22","dir":"Northbound"}]`; station 40002's platforms are 30002 with `[{"route":"Red","dir":"Charlie"}]` and 30012 with `[{"route":"Red","dir":"Alpha"}]`.
  - `test_train_shapes`: `train_shapes.json` has exactly one entry, Red, built from S1, with 2 points (the collinear middle point simplified away).
  - `test_cpp_output`: the generated `.cpp` contains `kTrainLineCount = 2`.
- [ ] **Step 2: Run and confirm failure.** `python3 -m unittest discover -s tools -v`. Expected: ImportError or failures.
- [ ] **Step 3: Implement** `gtfs_build.py`, standard library only.
- [ ] **Step 4: Run and confirm pass.** Same command.
- [ ] **Step 5: Generate from the real feed.** Download `https://www.transitchicago.com/downloads/sch_data/google_transit.zip` to `data/gtfs/`, then run `python3 tools/gtfs_build.py data/gtfs/google_transit.zip --check`. `--check` asserts:
  - the train route IDs are exactly {Red, Blue, Brn, G, Org, P, Pink, Y}
  - in the Red line direction whose label contains "95th", Belmont comes before Fullerton, and Fullerton before Clark/Division
  - `stops.json` has more than 10,000 bus stops, and every bus stop has at least one line
  - `train_shapes.json` has the same 8 routes, every point is inside Chicago's bounding box (lat 41.6–42.1, lon −88.0 to −87.5), and the file is under 200 KB

  Pass: exit code 0. Check that `pio test -e native` still builds with the generated `.cpp`.
- [ ] **Step 6 (once the Bus Tracker key arrives): check bus IDs.** Run with `--bustracker-key`. It calls `getdirections` and `getstops` for route 22 and asserts every returned `stpid` is in `stops.json` with a 22 line whose `dir` matches the Bus Tracker direction, exiting non-zero on any mismatch.
- *Done (2026-11-05), ahead of Tasks 1–9, so the iPhone app has real stops (Leo's call).* Steps 1–5 done; `--check` passes on the 2026-09-14 feed: 143 stations, 10,683 bus stops, 21 train sequences, 10 shapes (15 KB). The generated `.cpp` compiles with `clang++ -std=c++17 -Wall -Wextra -fsyntax-only`; the `pio test` build check waits for Task 2's native runner. Step 6 waits for the Bus Tracker key.
- *Rulings from the real feed:*
  - **Headsigns:** CTA's `trips.txt` has no `trip_headsign`. A pattern's label is its trips' most common `trip_headsign`, else their most common `stop_headsign` (from `stop_times.txt`).
  - **Platform labels:** a platform's direction is its most common `stop_headsign` for the route; on a trunk shared by branches, every headsign within 80% of the top one, joined (Green's Harlem/Lake–Garfield platforms carry 177 Ashland/63rd and 177 Cottage Grove trains, so they read "Ashland/63rd & Cottage Grove"; worker ruling, review round 3, so a near tie can't flip labels and orphan favorites on the next feed; `--check` fails if any platform sits between 70% and 90%) ("Loop" vs "Kimball" at Belmont, matching Train Tracker's `destNm`), else the label of the pattern that first covered it. Brown, Orange, and Pink run each trip as one round trip in one `direction_id` (Kimball → Loop → Kimball); without per-platform labels both Belmont Brown platforms read "Kimball" and the app merges them into one card.
  - **Sequences (replaces "one most common pattern per direction"):** per (route, `direction_id`), patterns most common first; each contributes only the stretches of platforms no earlier pattern of the route covered. Each stretch is split wherever the platform label changes, so every `TrainLine` has one `dirLabel` that agrees with `stops.json`. This picks up the opposite one-way trips CTA files under one `direction_id` (Purple Linden ↔ Howard, Yellow), Green's second branch (Ashland/63rd), and Purple Express, and adds nothing for Blue short turns. `--check` asserts every route has both directions and every platform direction in `stops.json` lies in some sequence with that label.
  - **Lead-in (Leo, 2026-11-05, for round trips):** a piece keeps up to 6 platforms before it, walking back while at least 90% of each platform's trains reach the piece's first stop (worker ruling, review round 3, replacing "only at a direction change"). Brown, Orange, Pink, and the Purple Express turning at the Loop keep 6; Green's branches, the Purple Express trunk, and the one-way pairs get 0, since the shared track's other trains never arrive, and `TrainLine` gains `uint8_t lead` (`train_lines.json`: `"lead"`). The first `lead` stops feed Task 8's approach rows ("6 stops before mine") but are not offered as picks. A (route, dir) can have more than one sequence (Green → Harlem/Lake is the trunk plus a 2-stop piece from the Ashland branch; Purple → Linden is the shuttle plus the Express): pickers list stations by (route, dir) across sequences, and a platform's row uses the sequence that holds it past its lead. Without that rule, LEDs would light for trains that never reach the stop.
  - **Shapes:** a route's main shape comes from its first stretch: direction 0, or the first direction it has (Orange, Pink, Purple, Yellow run in `direction_id` 1 only). A later stretch whose pattern reaches stations the route's shapes miss adds its own entry (worker ruling, review round 2), so the map draws Green's Ashland/63rd branch and the Purple Express to the Loop: 10 entries for 8 routes, 15 KB.
  - **Proposed spec edit for Leo (base spec §5, app spec §7.3):** "use the most common trip pattern" → "cover every served platform: the most common pattern per direction, plus the stretches other patterns add (branches, expresses, one-way pairs), split by direction; one path per line plus its branches".
  - **Names:** station names drop a trailing list of line colors ("Belmont (Red/Brown/Purple)" → "Belmont"); branch notes stay ("Harlem (Blue - Forest Park Branch)"), since the Blue Line has two Harlems. Sequence names use the same rule.
  - **What's a stop:** stations with no served platform, and bus stops with no trips, are left out. Bus stops are found by their trips, not by having no `parent_station`: CTA parents 20 terminal bays at 95th/Dan Ryan and Jefferson Park to the rail station.
- [x] **Step 7: Commit.**

```bash
git add tools/gtfs_build.py tools/test_gtfs_build.py fixtures/gtfs-mini firmware/lib/cta/src/train_sequences.* data/stops.json data/train_lines.json data/train_shapes.json .gitignore
git commit -m "feat(tools): GTFS build for train sequences and stop list"
```

---

### Task 11: Trackie's art and sprite pipeline

**Files:**
- Create: `art/palette.txt`, `art/body.txt`, `art/face_{happy,hyped,worried,sad,sleepy,asleep}.txt`, `art/extra_{happy,hyped,worried,sleepy}.txt`, `art/overlay_umbrella.txt`, `art/overlay_scarf.txt`
- Create: `tools/sprite_build.py`, `tools/test_sprite_build.py`
- Create: `firmware/lib/pet/library.json`, `firmware/lib/pet/src/pet_render.{h,cpp}`, `firmware/lib/pet/src/pet_sprites.h`
- Generate: `firmware/lib/pet/src/pet_sprites.cpp`; PNG previews go to `art/build/` (gitignored)
- Create: `firmware/native/test/test_pet/test_main.cpp`
- Modify: `.gitignore` (add `art/build/`)

**Art brief: Trackie (Leo, 2026-11-06).** The pet is Trackie, the iPhone app's pet: Leo's train, a blue L car seen from the front with a face on its door, and no CTA logo or wordmark (iOS redesign spec, "The pet"). **The art is already drawn;** don't redesign it. Copy it into `art/` from `Glyphs` in `ios/CTATracker/Design/Pixel.swift` (the same grids are in the handheld mockup, `docs/ideas/2026-11-04-handheld-ui-mockup.html`):
- **Body** (`petBody`): one 32×32 grid shared by every mood.
- **Faces** (`petFaces`): one 10×8 patch per mood, stamped onto the door at columns 11–20, rows 12–19, where `.` keeps the body's pixel. Happy (closed smiling eyes), Hyped (> < eyes, open mouth), Worried (brows, wavy mouth), Sad (T_T with tears), Sleepy (closed eyes). `face_asleep.txt` starts as a copy of Sleepy's, as in the mockup, so it can change on its own later.
- **Extras** (`petExtras`): a 7×7 one floating at his upper right, bobbing opposite him: a heart (Happy), excitement lines (Hyped), a sweat drop (Worried), Zz (Sleepy, and Asleep reuses it). Sad has none; its tears are on its face.
- **Weather overlays** (from the mockup's `drawPet`, Open-Meteo rules in spec §6.3): an umbrella held over the roof (32 wide, drawn in the 6 rows *above* the body) and a striped scarf over the bumper (rows 22–26). Neither shows while Asleep. Turn the mockup's rectangles into grids.
- **Animation:** no extra frames. He bobs one art pixel twice a second (the renderer moves him), as in the app.
- Trackie is drawn straight on the background (black on the handheld, spec §8.1): no tile behind him.

**`art/palette.txt`:** one line per character: `<char> <slot> <#RRGGBB> <role>`, the app's `petChars` and `petPalette` in order, plus the overlay colors:

| Char | Slot | Color | Role |
|---|---|---|---|
| `.` | 0 | — | transparent |
| `K` | 1 | `#0B0D14` | outline, door |
| `B` | 2 | `#2D6BE6` | body (swappable: pet paint) |
| `L` | 3 | `#7DB4FF` | body light (swappable: pet paint) |
| `G` | 4 | `#C3C8CE` | trim |
| `S` | 5 | `#8E949B` | trim shade |
| `W` | 6 | `#FFFFFF` | windows, face |
| `D` | 7 | `#5A6069` | coupler |
| `C` | 8 | `#9BEFF5` | destination sign |
| `R` | 9 | `#E5333D` | marker light, scarf stripe |
| `Y` | 10 | `#FFD43B` | headlight |
| `P` | 11 | `#F27DAE` | cheeks, heart |
| `U` | 12 | `#A9AEB5` | bumper, wheels |
| `T` | 13 | `#7FD3FF` | tears, sweat |
| `F` | 14 | `#F3E6D0` | scarf stripe |
| `M` | 15 | `#FF5F7A` | umbrella |
| `N` | 16 | `#B8324B` | umbrella edge |

Check each role against the art when copying; the names are for readers, the colors are what matter. Slots 2 and 3 are the ones the app's pet paint replaces (the mockup's paints: a line color and that color mixed 45% toward white).

**Interfaces:**
- Produces (`pet_render.h`, `namespace pet`):

```cpp
enum class PetState : uint8_t { Sleepy, Sad, Worried, Happy, Hyped, Asleep };
PetState petStateFor(cta::Mood m, bool docked);           // docked → Asleep, else 1:1 with Mood
uint16_t rgb565(uint32_t rgb);
struct Sprite { const uint8_t* idx; uint8_t w, h; int8_t dx, dy; };   // dx, dy: offset from the body's top left
// Expand an index sprite at (x, y) + (dx, dy), in art pixels × scale: dst pixel = palette[idx] unless
// idx == 0 (left untouched). Pixels outside dstW × dstH are clipped.
void renderSprite(const Sprite& s, int x, int y, const uint16_t* palette565, uint8_t scale,
                  uint16_t* dst, uint16_t dstW, uint16_t dstH);
```

- Produces (`pet_sprites.h`): `extern const Sprite kPet[6];` (indexed by `PetState`: the body with that face already stamped, 32×32), `extern const Sprite* const kPetExtra[6];` (nullptr for Sad), `extern const Sprite kOverlayUmbrella, kOverlayScarf;`, `extern const uint16_t kDefaultPalette565[17];`. The build composes body + face, so the device never stamps faces.
- CLI: `python3 tools/sprite_build.py` validates every grid (body 32×32, faces 10×8, extras 7×7, overlays at their declared sizes; only palette characters), writes `pet_sprites.cpp`, and writes 8× PNG previews of each state, with and without each overlay (stdlib `zlib` + `struct`). It exits non-zero and names the file and line on any invalid grid. The iPhone app plan later extends it to write `data/pet_sprites.json`, so `art/` becomes the one source and the app's hard-coded grids go away.

- [ ] **Step 1: Write the failing tests.**
  - **Python:** `test_rejects_short_grid` (a 31-row body → error naming the file); `test_rejects_unknown_char`; `test_rejects_wrong_face_size`; `test_face_stamp` (a face's non-`.` pixels replace the body's at columns 11–20, rows 12–19, and its `.` pixels keep the body's); `test_cpp_has_all_states` (the output contains `kPet` with 6 entries and `kPetExtra` with nullptr for Sad); `test_png_signature` (the preview starts with `\x89PNG`).
  - **Native** `test_pet`: `rgb565(0xFFFFFF) == 0xFFFF`; `rgb565(0x2D6BE6) == 0x2B5C`. `renderSprite` with a 32×32 sprite whose only non-zero pixel is (0,0) = slot 6, at scale 2 into a 64×64 buffer pre-filled with 0x1234, sets exactly the 4 pixels (0..1, 0..1) to `palette[6]` and leaves everything else at 0x1234. A sprite with `dy = −6` at y = 0 draws only its rows that land inside the buffer and writes nothing out of bounds. `petStateFor(Mood::Happy, true) == PetState::Asleep`.
- [ ] **Step 2: Run and confirm failure.** `python3 -m unittest discover -s tools -v` and `cd firmware/native && pio test -e native -f test_pet`.
- [ ] **Step 3: Implement** `sprite_build.py`, `pet_render.cpp`, and `pet_sprites.h`.
- [ ] **Step 4: Copy Trackie into `art/`** from the app's `Glyphs` (body, 5 faces, 4 extras), add the Asleep face, and draw the umbrella and scarf grids from the mockup's shapes. Run `python3 tools/sprite_build.py`. Pass: exit code 0, and the 6 state previews match the app's pet pixel for pixel (compare against a render of the Swift grids). Show Leo the previews, including the overlays.
- [ ] **Step 5: Run all tests and confirm pass.** Both commands from Step 2, plus `pio test -e native` for everything.
- [ ] **Step 6: Commit.**

```bash
git add art/*.txt tools/sprite_build.py tools/test_sprite_build.py firmware/lib/pet firmware/native/test/test_pet .gitignore
git commit -m "feat(pet): Trackie sprites, build tool, renderer"
```

---

## After This Plan

- **Plan 2 (dock):** screen layout, stop picker, LED rows (using `buildTrainRow`/`buildBusRows` and the masks), polling loop, BLE server (`wifi`, `config`, `info`), night hours.
- **Plan 3 (handheld):** pet screen, Trains and Buses tabs, stop detail, dim always-on, wake → `request`, `nearby` and `snapshot` handling, docked detection through the AXP2101, battery log.
- **Plan 4 (iPhone app):** Swift mirror of the stop record and mood rules, tested against the same `fixtures/cta`; map and route cards using `data/stops.json` and `data/train_lines.json`; BLE central; extras; widgets.
