// The dock's own pure logic (plan 2): picked stops in flash, the bus picker's lists, the LED rows' colors
// (spec 6.4, 3), the map's frame and vehicles, and the poll schedule (spec 6.6).
#include <math.h>
#include <stdio.h>
#include <string.h>
#include <unity.h>

#include "../fixture.h"
#include "bus_picker.h"
#include "cta_config.h"
#include "dock_leds.h"
#include "gesture.h"
#include "dock_status.h"
#include "line_colors.h"
#include "map_frame.h"
#include "picks.h"
#include "schedule.h"

void setUp() {}
void tearDown() {}

// ---------- picks ----------

static void test_default_picks() {
    cta::PickedStop p[cta::kDockStops];
    cta::defaultPicks(p);
    TEST_ASSERT_EQUAL((int)cta::Kind::Train, (int)p[0].type);
    TEST_ASSERT_EQUAL_STRING("Red", p[0].route);
    TEST_ASSERT_EQUAL_STRING("30256", p[0].id);   // Belmont, toward 95th/Dan Ryan
    TEST_ASSERT_EQUAL_STRING("95th/Dan Ryan", p[0].dir);
    TEST_ASSERT_EQUAL_STRING("Belmont", p[0].name);
    TEST_ASSERT_EQUAL_STRING("Brn", p[1].route);
    TEST_ASSERT_EQUAL_STRING("30104", p[1].id);   // Diversey, toward the Loop
    TEST_ASSERT_EQUAL_STRING("Loop", p[1].dir);
    TEST_ASSERT_EQUAL((int)cta::Kind::Bus, (int)p[2].type);
    TEST_ASSERT_EQUAL_STRING("22", p[2].route);
    TEST_ASSERT_EQUAL_STRING("1828", p[2].id);    // Clark & Belmont, southbound
    TEST_ASSERT_EQUAL_STRING("Southbound", p[2].dir);
    TEST_ASSERT_EQUAL_STRING("Clark", p[2].routeName);
}

static void test_picks_round_trip() {
    cta::PickedStop p[cta::kDockStops], q[cta::kDockStops];
    cta::defaultPicks(p);
    memset(&p[1], 0, sizeof(p[1]));   // an empty section
    snprintf(p[0].dir, sizeof(p[0].dir), "%s", "Ashland/63rd & Cottage Grove");   // longer than 24 bytes
    char buf[640];
    const size_t n = cta::encodePicks(p, buf, sizeof(buf));
    TEST_ASSERT_TRUE(n > 0);
    TEST_ASSERT_EQUAL_size_t(strlen(buf), n);
    TEST_ASSERT_TRUE(cta::decodePicks(buf, n, q));
    TEST_ASSERT_EQUAL_STRING("Ashland/63rd & Cottage Grove", q[0].dir);
    TEST_ASSERT_EQUAL_STRING("30256", q[0].id);
    TEST_ASSERT_EQUAL_STRING("", q[1].id);
    TEST_ASSERT_EQUAL_STRING("Clark", q[2].routeName);
    TEST_ASSERT_EQUAL((int)cta::Kind::Bus, (int)q[2].type);
    TEST_ASSERT_EQUAL_size_t(0, cta::encodePicks(p, buf, 20));   // doesn't fit
}

static void test_picks_rejects_bad_json() {
    cta::PickedStop q[cta::kDockStops];
    cta::defaultPicks(q);
    for (const char* bad : {"{", "{}", "[1,2,3]", "[{\"type\":\"boat\",\"id\":\"1\"},{},{}]", "[{},{}]"})
        TEST_ASSERT_FALSE(cta::decodePicks(bad, strlen(bad), q));
    TEST_ASSERT_EQUAL_STRING("30256", q[0].id);   // untouched
    const char* ok = "[{},null,{\"type\":\"bus\",\"route\":\"36\",\"dir\":\"Northbound\",\"id\":\"14811\",\"name\":\"Broadway & Belmont\"}]";
    TEST_ASSERT_TRUE(cta::decodePicks(ok, strlen(ok), q));
    TEST_ASSERT_EQUAL_STRING("", q[0].id);
    TEST_ASSERT_EQUAL_STRING("", q[1].id);
    TEST_ASSERT_EQUAL_STRING("14811", q[2].id);
    TEST_ASSERT_EQUAL_STRING("", q[2].routeName);
}

// ---------- the bus picker's lists ----------

static void test_bus_picker_lists() {
    const std::string r = readFixture("bus_routes.json"), d = readFixture("bus_directions.json"),
                      s = readFixture("bus_stops.json"), e = readFixture("bus_error_route.json");
    cta::BusRoute routes[4];
    TEST_ASSERT_EQUAL_INT(3, cta::parseBusRoutes(r.c_str(), r.size(), routes, 4));
    TEST_ASSERT_EQUAL_STRING("22", routes[1].rt);
    TEST_ASSERT_EQUAL_STRING("Clark", routes[1].name);
    TEST_ASSERT_EQUAL_INT(24, (int)strlen(routes[2].name));   // "Ashland Express (Owl ..." cut to 24 bytes
    TEST_ASSERT_EQUAL_INT(2, cta::parseBusRoutes(r.c_str(), r.size(), routes, 2));
    cta::BusDirection dirs[4];
    TEST_ASSERT_EQUAL_INT(2, cta::parseBusDirections(d.c_str(), d.size(), dirs, 4));
    TEST_ASSERT_EQUAL_STRING("Southbound", dirs[1].name);
    const char* v3 = "{\"bustime-response\":{\"directions\":[{\"id\":\"Eastbound\",\"name\":\"Eastbound\"}]}}";
    TEST_ASSERT_EQUAL_INT(1, cta::parseBusDirections(v3, strlen(v3), dirs, 4));
    TEST_ASSERT_EQUAL_STRING("Eastbound", dirs[0].name);
    cta::BusStop stops[4];
    TEST_ASSERT_EQUAL_INT(2, cta::parseBusStops(s.c_str(), s.size(), stops, 4));
    TEST_ASSERT_EQUAL_STRING("1838", stops[1].id);
    TEST_ASSERT_EQUAL_STRING("Clark & Diversey", stops[1].name);
    TEST_ASSERT_FLOAT_WITHIN(1e-4, 41.932476f, stops[1].lat);
    TEST_ASSERT_EQUAL_INT(-1, cta::parseBusStops(e.c_str(), e.size(), stops, 4));
    TEST_ASSERT_EQUAL_INT(-1, cta::parseBusRoutes(e.c_str(), e.size(), routes, 4));
    TEST_ASSERT_EQUAL_INT(-1, cta::parseBusDirections("{", 1, dirs, 4));
}

// ---------- LED rows ----------

static cta::LedRow row(uint8_t mask, uint32_t rgb, uint32_t ts, bool due) {
    cta::LedRow r;
    r.mask = mask;
    r.rgb = rgb;
    r.ts = ts;
    r.due = due;
    return r;
}

static void test_led_chain_order_and_colors() {
    // Row r, position i is LED 7r + i (spec 3); bit k of the mask is LED k + 1, LED 1 (the farthest) on the left.
    TEST_ASSERT_EQUAL_INT(0, cta::ledIndex(0, 0));
    TEST_ASSERT_EQUAL_INT(13, cta::ledIndex(1, 6));
    TEST_ASSERT_EQUAL_INT(20, cta::ledIndex(2, 6));
    const uint32_t red = cta::lineColor("Red");
    cta::LedRow rows[3] = {row(0b0010010, red, 1000, false), row(0, red, 1000, false), row(0b1000001, cta::kBusColor, 1000, false)};
    uint32_t out[cta::kLedCount];
    cta::ledFrame(rows, 1000, false, 255, 0, out);
    for (int i = 0; i < cta::kLedCount; ++i) {
        const bool lit = i == 1 || i == 4 || i == 14 || i == 20;
        TEST_ASSERT_EQUAL_HEX32(lit ? (i < 7 ? red : cta::kBusColor) : 0, out[i]);
    }
}

static void test_led_brightness_cap() {
    cta::LedRow rows[3] = {row(1, 0xFF8040, 1000, false), row(0, 0, 0, false), row(0, 0, 0, false)};
    uint32_t out[cta::kLedCount];
    cta::ledFrame(rows, 1000, false, 128, 0, out);
    TEST_ASSERT_EQUAL_HEX32(0x804020, out[0]);   // every channel scaled by 128/255, rounded
}

static void test_led_stale_and_night() {
    const uint32_t red = cta::lineColor("Red");
    cta::LedRow rows[3] = {row(1, red, 1000, false), row(1, red, 1000 - cta::kLedStaleSec, false),
                           row(1, red, 0, false)};
    uint32_t out[cta::kLedCount];
    cta::ledFrame(rows, 1000 + 1, false, 255, 0, out);
    TEST_ASSERT_EQUAL_HEX32(red, out[0]);
    TEST_ASSERT_EQUAL_HEX32(0, out[7]);    // just over 2 min old: the row goes dark (spec 6.4)
    TEST_ASSERT_EQUAL_HEX32(0, out[14]);   // never fetched
    cta::ledFrame(rows, 1000 + cta::kLedStaleSec, false, 255, 0, out);
    TEST_ASSERT_EQUAL_HEX32(red, out[0]);  // exactly 2 min: still on
    cta::ledFrame(rows, 1000, true, 255, 0, out);
    for (uint32_t c : out) TEST_ASSERT_EQUAL_HEX32(0, c);   // night: everything off (spec 7.4)
}

static void test_led_seven_pulses_when_due() {
    const uint32_t red = cta::lineColor("Red");
    cta::LedRow rows[3] = {row(0, red, 1000, true), row(0b1000000, red, 1000, false), row(0, 0, 0, false)};
    uint32_t a[cta::kLedCount], b[cta::kLedCount];
    cta::ledFrame(rows, 1000, false, 255, 0, a);
    cta::ledFrame(rows, 1000, false, 255, 471, b);   // a quarter period later (300 ms per radian)
    TEST_ASSERT_TRUE(a[6] != 0 && b[6] != 0);
    TEST_ASSERT_TRUE(a[6] != b[6]);                  // it breathes
    TEST_ASSERT_TRUE(((b[6] >> 16) & 255) <= ((red >> 16) & 255));
    TEST_ASSERT_EQUAL_HEX32(red, a[13]);             // a vehicle at the stop, nothing due: steady
    TEST_ASSERT_EQUAL_HEX32(red, b[13]);
    for (int i = 0; i < 6; ++i) TEST_ASSERT_EQUAL_HEX32(0, a[i]);
}

static void test_led_breath_keeps_its_shape_with_uptime() {
    // The breath's phase is reduced before the float math, so day 35 breathes like boot (and the 49.7-day wrap of
    // millis() costs at most one step).
    const uint32_t red = cta::lineColor("Red");
    cta::LedRow rows[3] = {row(0, red, 1000, true), row(0, 0, 0, false), row(0, 0, 0, false)};
    uint32_t a[cta::kLedCount], b[cta::kLedCount];
    const uint32_t big = cta::kLedBreathMs * 1591511u;   // about 3e9 ms
    for (uint32_t t : {0u, 100u, 471u, 900u, 1500u}) {
        cta::ledFrame(rows, 1000, false, 255, t, a);
        cta::ledFrame(rows, 1000, false, 255, big + t, b);
        TEST_ASSERT_EQUAL_HEX32(a[6], b[6]);
        cta::ledFrame(rows, 1000, false, 255, big + t + cta::kLedBreathMs, b);
        TEST_ASSERT_EQUAL_HEX32(a[6], b[6]);
    }
    // And it still has its full range: 45% to 100%.
    uint32_t lo = 0xFFFFFFFF, hi = 0;
    for (uint32_t t = 0; t < cta::kLedBreathMs; t += 5) {
        cta::ledFrame(rows, 1000, false, 255, big + t, b);
        const uint32_t r8 = (b[6] >> 16) & 255;
        lo = r8 < lo ? r8 : lo;
        hi = r8 > hi ? r8 : hi;
    }
    TEST_ASSERT_EQUAL_UINT32((red >> 16) & 255, hi);
    TEST_ASSERT_UINT32_WITHIN(2, static_cast<uint32_t>(((red >> 16) & 255) * 0.45f + 0.5f), lo);
}

static void test_same_pick() {
    cta::PickedStop p[3], q[3];
    cta::defaultPicks(p);
    cta::defaultPicks(q);
    TEST_ASSERT_TRUE(cta::samePick(p[0], q[0]));
    snprintf(q[0].id, sizeof(q[0].id), "30255");
    TEST_ASSERT_FALSE(cta::samePick(p[0], q[0]));
    TEST_ASSERT_FALSE(cta::samePick(p[1], p[2]));
    memset(&q[1], 0, sizeof(q[1]));
    cta::PickedStop e;
    memset(&e, 0, sizeof(e));
    TEST_ASSERT_TRUE(cta::samePick(q[1], e));
}

// ---------- the map ----------

static void test_map_frame_fits_the_points() {
    const cta::LatLon pts[] = {{41.98f, -87.66f}, {41.93f, -87.65f}};
    const cta::MapFrame f = cta::mapFrame(pts, 2, 480, 80, 800, 56, 50, 130);
    int x0, y0, x1, y1;
    cta::mapProject(f, pts[0].lat, pts[0].lon, 0, 0, x0, y0);
    cta::mapProject(f, pts[1].lat, pts[1].lon, 0, 0, x1, y1);
    // The taller span fills the box between the pads (80 + 50 to 800 - 130), centered on the box.
    TEST_ASSERT_INT_WITHIN(1, 130, y0);
    TEST_ASSERT_INT_WITHIN(1, 670, y1);
    TEST_ASSERT_INT_WITHIN(1, 480, x0 + x1);   // centered across
    TEST_ASSERT_TRUE(x0 < x1);                  // east is right
    int px, py;
    cta::mapProject(f, pts[0].lat, pts[0].lon, 12, -7, px, py);
    TEST_ASSERT_EQUAL_INT(x0 + 12, px);
    TEST_ASSERT_EQUAL_INT(y0 - 7, py);
    const cta::LatLon one[] = {{41.9f, -87.6f}};   // one point: a 0.5 km minimum span, centered on it
    const cta::MapFrame g = cta::mapFrame(one, 1, 480, 80, 800, 56, 50, 130);
    cta::mapProject(g, 41.9f, -87.6f, 0, 0, px, py);
    TEST_ASSERT_EQUAL_INT(240, px);
    TEST_ASSERT_EQUAL_INT(400, py);
}

static void test_vehicle_placement() {
    cta::LatLon zone[7];
    for (int k = 0; k < 7; ++k) zone[k] = {42.0f - k * 0.01f, -87.6f};
    // A vehicle lighting LED k sits 55% of the way from stop k-1 to stop k, its next stop.
    cta::LatLon v;
    TEST_ASSERT_TRUE(cta::vehicleAt(zone, 3, v));
    TEST_ASSERT_FLOAT_WITHIN(1e-5, 42.0f - 0.0155f, v.lat);
    TEST_ASSERT_TRUE(cta::vehicleAt(zone, 1, v));   // LED 1: still short of the first stop, by the same spacing
    TEST_ASSERT_FLOAT_WITHIN(1e-5, 42.0f + 0.0045f, v.lat);
    zone[3] = {0, 0};                               // a missing stop places nothing
    TEST_ASSERT_FALSE(cta::vehicleAt(zone, 4, v));
    TEST_ASSERT_TRUE(cta::vehicleAt(zone, 5, v));   // from the next stop back, as for LED 1
    TEST_ASSERT_FALSE(cta::vehicleAt(zone, 0, v));
    TEST_ASSERT_FALSE(cta::vehicleAt(zone, 8, v));
}

static void test_vehicles_for_a_row() {
    int ks[3];
    // Up to 3, nearest the stop first; a due arrival puts one at the stop (LED 7) even before the positions say so.
    TEST_ASSERT_EQUAL_INT(2, cta::vehiclesFor(0b0010010, false, ks, 3));
    TEST_ASSERT_EQUAL_INT(5, ks[0]);
    TEST_ASSERT_EQUAL_INT(2, ks[1]);
    TEST_ASSERT_EQUAL_INT(3, cta::vehiclesFor(0b0111111, true, ks, 3));
    TEST_ASSERT_EQUAL_INT(7, ks[0]);
    TEST_ASSERT_EQUAL_INT(6, ks[1]);
    TEST_ASSERT_EQUAL_INT(5, ks[2]);
    TEST_ASSERT_EQUAL_INT(1, cta::vehiclesFor(0b1000000, true, ks, 3));   // LED 7 once
    TEST_ASSERT_EQUAL_INT(0, cta::vehiclesFor(0, false, ks, 3));
}

// ---------- the stops' status: countdown, due, stale, mood, reason ----------

static cta::StopRecord rec(const char* route, cta::Kind type, uint32_t ts, int m0, bool live = true) {
    cta::StopRecord r;
    memset(&r, 0, sizeof(r));
    snprintf(r.route, sizeof(r.route), "%s", route);
    r.type = type;
    r.ts = ts;
    if (m0 >= 0) {
        r.arrivals[0].min = static_cast<int16_t>(m0);
        r.arrivals[0].live = live;
        r.arrivals[1].min = static_cast<int16_t>(m0 + 10);
        r.arrivals[1].live = live;
        r.count = 2;
    }
    return r;
}

static void test_minutes_count_down_from_the_fetch() {
    const cta::StopRecord r = rec("Red", cta::Kind::Train, 1000, 5);
    TEST_ASSERT_EQUAL_INT(5, cta::minutesLeft(r, 0, 1000));
    TEST_ASSERT_EQUAL_INT(5, cta::minutesLeft(r, 0, 1059));   // whole minutes only
    TEST_ASSERT_EQUAL_INT(4, cta::minutesLeft(r, 0, 1060));
    TEST_ASSERT_EQUAL_INT(0, cta::minutesLeft(r, 0, 1000 + 9 * 60));   // clamped at 0
    TEST_ASSERT_EQUAL_INT(14, cta::minutesLeft(r, 1, 1060));
    TEST_ASSERT_EQUAL_INT(5, cta::minutesLeft(r, 0, 900));    // a clock behind the fetch counts nothing down
}

static void test_due_at_the_hyped_threshold() {
    // LED 7 breathes and Trackie is Hyped while the next live arrival is within kHypedMaxMin (spec 6.3, 6.4).
    TEST_ASSERT_TRUE(cta::dueNow(rec("Red", cta::Kind::Train, 1000, cta::kHypedMaxMin), 1000));
    TEST_ASSERT_FALSE(cta::dueNow(rec("Red", cta::Kind::Train, 1000, cta::kHypedMaxMin + 1), 1000));
    TEST_ASSERT_TRUE(cta::dueNow(rec("Red", cta::Kind::Train, 1000, cta::kHypedMaxMin + 1), 1060));   // counted down
    TEST_ASSERT_FALSE(cta::dueNow(rec("Red", cta::Kind::Train, 1000, 1, false), 1000));             // timetable only
    TEST_ASSERT_FALSE(cta::dueNow(rec("Red", cta::Kind::Train, 1000, 1), 1000 + cta::kStaleSec + 1));   // stale
    TEST_ASSERT_FALSE(cta::dueNow(rec("Red", cta::Kind::Train, 0, 1), 1000));                       // never fetched
    TEST_ASSERT_FALSE(cta::dueNow(rec("Red", cta::Kind::Train, 1000, -1), 1000));                   // no arrivals
}

static void test_card_stale() {
    const cta::StopRecord r = rec("Red", cta::Kind::Train, 1000, 5);
    TEST_ASSERT_FALSE(cta::cardStale(r, 1030, false));
    TEST_ASSERT_TRUE(cta::cardStale(r, 1030, true));                    // Wi-Fi lost: greyed at once (spec 7.4)
    TEST_ASSERT_TRUE(cta::cardStale(r, 1000 + cta::kStaleSec + 1, false));
    TEST_ASSERT_FALSE(cta::cardStale(rec("Red", cta::Kind::Train, 0, -1), 1030, true));   // nothing to grey
}

static void test_dock_mood_and_reason() {
    cta::StopRecord r[3] = {rec("Red", cta::Kind::Train, 1000, 6), rec("Brn", cta::Kind::Train, 1000, 9),
                            rec("22", cta::Kind::Bus, 1000, 12)};
    char why[48];
    TEST_ASSERT_EQUAL((int)cta::Mood::Happy, (int)cta::dockMood(r, 3, 1000, false));
    cta::moodReason(r, 3, 1000, false, false, "", why, sizeof(why));
    TEST_ASSERT_EQUAL_STRING("Red in 6 min", why);
    TEST_ASSERT_EQUAL((int)cta::Mood::Hyped, (int)cta::dockMood(r, 3, 1000 + 180, false));   // Red counts down to 3
    cta::moodReason(r, 3, 1000 + 180, false, false, "", why, sizeof(why));
    TEST_ASSERT_EQUAL_STRING("Red in 3 min!", why);
    r[2].arrivals[0].delayed = true;
    r[0].arrivals[0].min = 20;
    TEST_ASSERT_EQUAL((int)cta::Mood::Happy, (int)cta::dockMood(r, 3, 1000, false));   // one Worried of three
    r[1].arrivals[0].delayed = true;
    TEST_ASSERT_EQUAL((int)cta::Mood::Worried, (int)cta::dockMood(r, 3, 1000, false));
    cta::moodReason(r, 3, 1000, false, false, "", why, sizeof(why));
    TEST_ASSERT_EQUAL_STRING("Brown is running late", why);
    r[2].alert = true;
    r[0].alert = true;
    TEST_ASSERT_EQUAL((int)cta::Mood::Sad, (int)cta::dockMood(r, 3, 1000, false));
    cta::moodReason(r, 3, 1000, false, false, "", why, sizeof(why));
    TEST_ASSERT_EQUAL_STRING("Red Line alert", why);
    TEST_ASSERT_EQUAL((int)cta::Mood::Sleepy, (int)cta::dockMood(r, 3, 1000, true));
    cta::moodReason(r, 3, 1000, true, false, "", why, sizeof(why));
    TEST_ASSERT_EQUAL_STRING("Night hours", why);
    cta::moodReason(r, 3, 1000 + 7 * 60, false, true, "", why, sizeof(why));
    TEST_ASSERT_EQUAL_STRING("Data is 7 min old", why);
    cta::StopRecord none[1] = {rec("Red", cta::Kind::Train, 0, -1)};
    cta::moodReason(none, 1, 1000, false, false, "Connecting to Wi-Fi", why, sizeof(why));
    TEST_ASSERT_EQUAL_STRING("Connecting to Wi-Fi", why);
    TEST_ASSERT_EQUAL_STRING("Brown", cta::trainLineName("Brn"));
    TEST_ASSERT_EQUAL_STRING("22", cta::trainLineName("22"));
}

// ---------- the poll schedule ----------

static void test_poll_due() {
    TEST_ASSERT_TRUE(cta::pollDue(0, cta::kDockPollSec, 1000, false));   // never run: due
    TEST_ASSERT_FALSE(cta::pollDue(1000, cta::kDockPollSec, 1029, false));
    TEST_ASSERT_TRUE(cta::pollDue(1000, cta::kDockPollSec, 1030, false));
    TEST_ASSERT_FALSE(cta::pollDue(1000, cta::kDockPollSec, 1030, true));   // night: everything every 5 min
    TEST_ASSERT_TRUE(cta::pollDue(1000, cta::kDockPollSec, 1000 + cta::kSlowPollSec, true));
    TEST_ASSERT_FALSE(cta::pollDue(1000, cta::kWeatherPollSec, 1000 + cta::kSlowPollSec, true));   // never faster
    TEST_ASSERT_TRUE(cta::pollDue(0, cta::kWeatherPollSec, 5, true));
}

static void test_geo_lookup_due() {
    // Spec 7.5: at boot whatever is saved, then once a day; a failed lookup is tried again in kGeoRetrySec.
    using cta::geoLookupDue;
    const uint32_t day = cta::kGeoLookupSec, now = 2000000000;
    TEST_ASSERT_TRUE(geoLookupDue(false, now - 60, now - 60, now));      // boot: even with a fresh saved location
    TEST_ASSERT_FALSE(geoLookupDue(true, now - 60, now - 60, now));      // done today
    TEST_ASSERT_TRUE(geoLookupDue(true, now - day, now - day, now));     // a day on
    TEST_ASSERT_FALSE(geoLookupDue(true, now - day - 5, now - 10, now));  // failed 10 s ago: wait
    TEST_ASSERT_TRUE(geoLookupDue(true, now - day - 5, now - cta::kGeoRetrySec, now));
    TEST_ASSERT_TRUE(geoLookupDue(true, 0, now - cta::kGeoRetrySec, now));   // nothing saved yet
    TEST_ASSERT_FALSE(geoLookupDue(true, 0, now - 1, now));
}

static void test_geo_worth_saving() {
    // A flash write upsets the panel (spec 7.5's cache needs one only when the location really moved): save when
    // nothing is saved, or the new fix is more than 1 km from the saved one.
    TEST_ASSERT_TRUE(cta::geoWorthSaving(false, 0, 0, 41.88f, -87.63f));
    TEST_ASSERT_FALSE(cta::geoWorthSaving(true, 41.8781f, -87.6298f, 41.8781f, -87.6298f));
    TEST_ASSERT_FALSE(cta::geoWorthSaving(true, 41.8781f, -87.6298f, 41.8840f, -87.6298f));   // 0.66 km north
    TEST_ASSERT_TRUE(cta::geoWorthSaving(true, 41.8781f, -87.6298f, 41.8880f, -87.6298f));    // 1.1 km north
    TEST_ASSERT_FALSE(cta::geoWorthSaving(true, 41.8781f, -87.6298f, 41.8781f, -87.6400f));   // 0.85 km west
    TEST_ASSERT_TRUE(cta::geoWorthSaving(true, 41.8781f, -87.6298f, 41.8781f, -87.6450f));    // 1.26 km west
}

static void test_wifi_retry_delay() {
    // The first join after boot often fails at once and the next works: 2 s, then 15 s doubling to 5 min.
    TEST_ASSERT_EQUAL_UINT32(2000, cta::wifiRetryDelayMs(1));
    TEST_ASSERT_EQUAL_UINT32(15000, cta::wifiRetryDelayMs(2));
    TEST_ASSERT_EQUAL_UINT32(30000, cta::wifiRetryDelayMs(3));
    TEST_ASSERT_EQUAL_UINT32(240000, cta::wifiRetryDelayMs(6));
    TEST_ASSERT_EQUAL_UINT32(300000, cta::wifiRetryDelayMs(7));
    TEST_ASSERT_EQUAL_UINT32(300000, cta::wifiRetryDelayMs(1000));
    TEST_ASSERT_EQUAL_UINT32(0, cta::wifiRetryDelayMs(0));                // never failed: join now
}

static void test_gestures() {
    using cta::Gesture;
    using cta::classifyRelease;
    TEST_ASSERT_FALSE(cta::pressMoved(5, 6));                             // within 8 px: still a tap
    TEST_ASSERT_TRUE(cta::pressMoved(6, 7));
    TEST_ASSERT_EQUAL((int)Gesture::Tap, (int)classifyRelease(3, 2, false, false));
    TEST_ASSERT_EQUAL((int)Gesture::None, (int)classifyRelease(3, 2, false, true));       // a long press is not a tap
    TEST_ASSERT_EQUAL((int)Gesture::SwipeLeft, (int)classifyRelease(-61, 10, true, false));
    TEST_ASSERT_EQUAL((int)Gesture::SwipeRight, (int)classifyRelease(61, -40, true, false));
    TEST_ASSERT_EQUAL((int)Gesture::None, (int)classifyRelease(-60, 0, true, false));     // not past 60 px
    TEST_ASSERT_EQUAL((int)Gesture::None, (int)classifyRelease(-90, 61, true, false));    // too steep (1.5x)
    TEST_ASSERT_EQUAL((int)Gesture::None, (int)classifyRelease(20, 200, true, false));    // a drag
    TEST_ASSERT_FALSE(cta::longPressDue(1000, 1599));
    TEST_ASSERT_TRUE(cta::longPressDue(1000, 1600));
    TEST_ASSERT_FALSE(cta::longPressDue(1000, 990));   // a press stamped after the frame's clock: not 4 billion ms old
}

static void test_picker_list_matches() {
    TEST_ASSERT_TRUE(cta::pickerListFor("22", "Southbound", "22", "Southbound"));
    TEST_ASSERT_FALSE(cta::pickerListFor("22", "Southbound", "22", "Northbound"));   // an earlier request's answer
    TEST_ASSERT_FALSE(cta::pickerListFor("36", "", "22", ""));
    TEST_ASSERT_TRUE(cta::pickerListFor("22", "", "22", ""));
}

int main() {
    UNITY_BEGIN();
    RUN_TEST(test_default_picks);
    RUN_TEST(test_picks_round_trip);
    RUN_TEST(test_picks_rejects_bad_json);
    RUN_TEST(test_bus_picker_lists);
    RUN_TEST(test_led_chain_order_and_colors);
    RUN_TEST(test_led_brightness_cap);
    RUN_TEST(test_led_stale_and_night);
    RUN_TEST(test_led_seven_pulses_when_due);
    RUN_TEST(test_led_breath_keeps_its_shape_with_uptime);
    RUN_TEST(test_same_pick);
    RUN_TEST(test_map_frame_fits_the_points);
    RUN_TEST(test_vehicle_placement);
    RUN_TEST(test_vehicles_for_a_row);
    RUN_TEST(test_poll_due);
    RUN_TEST(test_minutes_count_down_from_the_fetch);
    RUN_TEST(test_due_at_the_hyped_threshold);
    RUN_TEST(test_card_stale);
    RUN_TEST(test_dock_mood_and_reason);
    RUN_TEST(test_geo_lookup_due);
    RUN_TEST(test_wifi_retry_delay);
    RUN_TEST(test_geo_worth_saving);
    RUN_TEST(test_gestures);
    RUN_TEST(test_picker_list_matches);
    return UNITY_END();
}
