// Approach rows and LED masks (spec 6.4; plan 1 task 8, moved to plan 2), and the map's platform positions.
#include <math.h>
#include <string.h>
#include <unity.h>

#include "../fixture.h"
#include "approach.h"
#include "train_map.h"

void setUp() {}
void tearDown() {}

static void test_train_rows() {
    const char* seq[] = {"30001", "30002", "30003", "30004", "30005", "30006", "30007", "30008", "30009", "30010"};
    cta::ApproachRow row;
    TEST_ASSERT_TRUE(cta::buildTrainRow(seq, 10, "30008", row));
    TEST_ASSERT_EQUAL_STRING("30002", row.stops[0]);
    TEST_ASSERT_EQUAL_STRING("30008", row.stops[6]);
    TEST_ASSERT_EQUAL_INT32(-1, row.pid);
    TEST_ASSERT_EQUAL_INT32(-1, row.before);
    cta::TrainPos t[] = {{"30005"}, {"30001"}, {"30008"}};
    TEST_ASSERT_EQUAL_HEX8(0b1001000, cta::litMaskTrains(row, t, 3));   // LED 3 and LED 6; 30001 is outside the row
    TEST_ASSERT_TRUE(cta::buildTrainRow(seq, 10, "30003", row));        // only 2 stops before it
    TEST_ASSERT_EQUAL_STRING("", row.stops[3]);
    TEST_ASSERT_EQUAL_STRING("30001", row.stops[4]);
    TEST_ASSERT_EQUAL_HEX8(0, cta::litMaskTrains(row, t + 2, 1));
    cta::TrainPos none[] = {{""}};
    TEST_ASSERT_EQUAL_HEX8(0, cta::litMaskTrains(row, none, 1));         // an empty slot never matches
    TEST_ASSERT_TRUE(cta::buildTrainRow(seq, 10, "30001", row));
    TEST_ASSERT_EQUAL_STRING("", row.stops[0]);
    TEST_ASSERT_FALSE(cta::buildTrainRow(seq, 10, "39999", row));
}

static void test_dock_default_train_rows() {
    // Red at Belmont toward 95th: Berwyn ... Addison, then Belmont (the mockup's zone), each placed on the map.
    cta::ApproachRow row;
    TEST_ASSERT_TRUE(cta::trainRowFor("Red", "30256", row));
    TEST_ASSERT_EQUAL_STRING("30256", row.stops[6]);
    TEST_ASSERT_EQUAL_STRING("30067", row.stops[0]);   // Berwyn, 95th-bound
    for (int k = 0; k < 7; ++k) TEST_ASSERT_TRUE(row.lat[k] > 41.9f && row.lon[k] < -87.6f);
    TEST_ASSERT_FLOAT_WITHIN(1e-4, 41.939751f, row.lat[6]);
    // Brown at Diversey toward the Loop: from the Loop-bound sequence, not Kimball's lead-in.
    TEST_ASSERT_TRUE(cta::trainRowFor("Brn", "30104", row));
    TEST_ASSERT_EQUAL_STRING("30104", row.stops[6]);
    TEST_ASSERT_EQUAL_STRING("30258", row.stops[4]);   // Belmont, Loop-bound
    TEST_ASSERT_FALSE(cta::trainRowFor("Red", "39999", row));
    TEST_ASSERT_FALSE(cta::trainRowFor("22", "30256", row));
}

static void test_branch_rows_take_the_trunk_before_them() {
    // Green's Cottage Grove and Ashland/63rd branches and Purple's Loop-bound run are short sequences of their own; their
    // rows start on the trunk that feeds them (spec 6.4).
    cta::ApproachRow row;
    TEST_ASSERT_TRUE(cta::trainRowFor("G", "30217", row));   // King Drive, toward Cottage Grove
    TEST_ASSERT_EQUAL_STRING("30214", row.stops[0]);         // 35th-Bronzeville
    TEST_ASSERT_EQUAL_STRING("30100", row.stops[5]);         // Garfield, the last trunk stop
    TEST_ASSERT_EQUAL_STRING("30217", row.stops[6]);
    for (int k = 0; k < 7; ++k) TEST_ASSERT_TRUE(row.lat[k] != 0);
    TEST_ASSERT_TRUE(cta::trainRowFor("G", "30057", row));   // Ashland/63rd: Halsted, then the trunk
    TEST_ASSERT_EQUAL_STRING("30184", row.stops[5]);
    TEST_ASSERT_EQUAL_STRING("30100", row.stops[4]);
    TEST_ASSERT_EQUAL_STRING("30059", row.stops[0]);         // Indiana
    TEST_ASSERT_TRUE(cta::trainRowFor("P", "30258", row));   // Belmont, Loop-bound: Wilson, then Linden-Howard
    TEST_ASSERT_EQUAL_STRING("30385", row.stops[5]);         // Wilson
    TEST_ASSERT_EQUAL_STRING("30176", row.stops[4]);         // Howard
    TEST_ASSERT_EQUAL_STRING("30011", row.stops[0]);         // Davis
    // A terminal's own line has nothing before it, and a line's reverse never feeds it.
    TEST_ASSERT_TRUE(cta::trainRowFor("G", "30218", row));   // King Drive toward Harlem/Lake, just after Cottage Grove
    TEST_ASSERT_EQUAL_STRING("", row.stops[4]);
    TEST_ASSERT_EQUAL_STRING("30140", row.stops[5]);
}

static void test_platform_positions() {
    float lat = 1, lon = 2;
    TEST_ASSERT_TRUE(cta::trainPlatformLatLon("30104", lat, lon));   // Diversey
    TEST_ASSERT_FLOAT_WITHIN(1e-4, 41.932732f, lat);
    TEST_ASSERT_FLOAT_WITHIN(1e-4, -87.653131f, lon);
    lat = 1;
    TEST_ASSERT_FALSE(cta::trainPlatformLatLon("39999", lat, lon));
    TEST_ASSERT_FALSE(cta::trainPlatformLatLon(nullptr, lat, lon));
    TEST_ASSERT_EQUAL_FLOAT(1, lat);
    for (size_t i = 1; i < cta::kPlatformCount; ++i) TEST_ASSERT_TRUE(strcmp(cta::kPlatforms[i - 1].id, cta::kPlatforms[i].id) < 0);
    TEST_ASSERT_TRUE(cta::kTrainShapeCount >= 8);
}

static void test_train_line_for_platform() {
    // Every line's picks resolve to a line of their own route; lead-in stops of round trips don't.
    for (size_t l = 0; l < cta::kTrainLineCount; l++) {
        const cta::TrainLine& line = cta::kTrainLines[l];
        for (size_t i = line.lead; i < line.count; i++) {
            const cta::TrainLine* found = cta::trainLineFor(line.route, line.stopIds[i]);
            TEST_ASSERT_NOT_NULL(found);
            TEST_ASSERT_EQUAL_STRING(line.route, found->route);
        }
    }
    TEST_ASSERT_NULL(cta::trainLineFor("Red", "39999"));
    TEST_ASSERT_NULL(cta::trainLineFor("22", "30001"));
}

static void test_picker_stations_and_directions() {
    // Every (line, direction) offers stations, each once, none from a lead-in, including Green's long
    // "Ashland/63rd & Cottage Grove" (over 24 bytes, so it must not be compared truncated).
    for (size_t l = 0; l < cta::kTrainLineCount; l++) {
        const cta::TrainLine& line = cta::kTrainLines[l];
        cta::StationRef refs[64];
        const size_t n = cta::trainStations(line.route, line.dirLabel, refs, 64);
        TEST_ASSERT_TRUE_MESSAGE(n > 0, line.dirLabel);
        for (size_t i = 0; i < n; i++) {
            const char* id = refs[i].line->stopIds[refs[i].index];
            TEST_ASSERT_TRUE(refs[i].index >= refs[i].line->lead);
            TEST_ASSERT_NOT_NULL(cta::trainLineFor(line.route, id));
            for (size_t j = 0; j < i; j++) TEST_ASSERT_TRUE(strcmp(id, refs[j].line->stopIds[refs[j].index]) != 0);
        }
    }
    cta::StationRef few[2];
    TEST_ASSERT_EQUAL_size_t(2, cta::trainStations("Red", "95th/Dan Ryan", few, 2));   // capacity
    TEST_ASSERT_EQUAL_size_t(0, cta::trainStations("Red", "Nowhere", few, 2));
    const char* dirs[4];
    TEST_ASSERT_EQUAL_size_t(2, cta::trainDirections("Red", dirs, 4));
    TEST_ASSERT_EQUAL_STRING("95th/Dan Ryan", dirs[0]);
    TEST_ASSERT_EQUAL_STRING("Howard", dirs[1]);
    TEST_ASSERT_EQUAL_size_t(2, cta::trainDirections("Brn", dirs, 4));   // Kimball's two sequences count once
    TEST_ASSERT_EQUAL_size_t(1, cta::trainDirections("G", dirs, 1));      // capacity
    TEST_ASSERT_EQUAL_size_t(0, cta::trainDirections("22", dirs, 4));
}

static void test_bus_rows() {
    std::string p = readFixture("bus_patterns.json");
    cta::ApproachRow rows[4];
    int n = cta::buildBusRows(p.c_str(), p.size(), "2009", rows, 4);
    TEST_ASSERT_EQUAL_INT(2, n);
    TEST_ASSERT_EQUAL_STRING("2003", rows[0].stops[0]);
    TEST_ASSERT_EQUAL_INT32(1000, rows[0].before);   // waypoint skipped
    TEST_ASSERT_EQUAL_INT32(100, rows[0].pid);
    TEST_ASSERT_EQUAL_INT32(8000, rows[0].pdist[6]);
    TEST_ASSERT_FLOAT_WITHIN(1e-4, 41.954f, rows[0].lat[0]);
    TEST_ASSERT_FLOAT_WITHIN(1e-4, -87.643f, rows[0].lon[6]);
    TEST_ASSERT_EQUAL_STRING("", rows[1].stops[3]);
    TEST_ASSERT_EQUAL_INT32(-1, rows[1].before);
    TEST_ASSERT_EQUAL_FLOAT(0, rows[1].lat[3]);
    cta::BusVehicle v[] = {{100, 4500}, {100, 1500}, {100, 500}, {100, 8000}, {100, 8500}, {200, 600}};
    // rows[0]: 4500 -> LED 3, 1500 -> LED 0, 500 -> none, 8000 -> LED 6, 8500 -> none; rows[1] (pid 200): 600 -> LED 6
    TEST_ASSERT_EQUAL_HEX8(0b1001001, cta::litMaskBuses(rows, 1, v, 6));
    TEST_ASSERT_EQUAL_HEX8(0b1000000, cta::litMaskBuses(rows + 1, 1, v, 6));
    TEST_ASSERT_EQUAL_HEX8(0b1001001, cta::litMaskBuses(rows, 2, v, 6));
    TEST_ASSERT_EQUAL_INT(2, cta::buildBusRows(p.c_str(), p.size(), "2007", rows, 4));   // pid 100: exactly 6 before it
    TEST_ASSERT_EQUAL_STRING("2001", rows[0].stops[0]);
    TEST_ASSERT_EQUAL_INT32(-1, rows[0].before);   // nothing before the first stop
    TEST_ASSERT_EQUAL_INT(2, cta::buildBusRows(p.c_str(), p.size(), "2008", rows, 4));   // 7 before: 2001 is "before"
    TEST_ASSERT_EQUAL_INT32(0, rows[0].before);
    TEST_ASSERT_EQUAL_INT(1, cta::buildBusRows(p.c_str(), p.size(), "2002", rows, 4));
    TEST_ASSERT_EQUAL_INT(0, cta::buildBusRows(p.c_str(), p.size(), "9999", rows, 4));
    TEST_ASSERT_EQUAL_INT(1, cta::buildBusRows(p.c_str(), p.size(), "2009", rows, 1));   // capacity respected
    TEST_ASSERT_EQUAL_INT(-1, cta::buildBusRows("nope", 4, "2009", rows, 4));
}

static void test_bus_rows_from_error_replies() {
    // A route Bus Tracker doesn't know has no rows (0, kept); a bad key or the daily limit is a failed fetch (-1,
    // tried again), not a stop with no buses.
    cta::ApproachRow rows[4];
    cta::LatLon pts[4];
    for (const char* name : {"bus_error_key.json", "bus_error_limit.json"}) {
        const std::string e = readFixture(name);
        TEST_ASSERT_EQUAL_INT_MESSAGE(-1, cta::buildBusRows(e.c_str(), e.size(), "2009", rows, 4), name);
        TEST_ASSERT_EQUAL_INT_MESSAGE(-1, cta::busPatternPath(e.c_str(), e.size(), 100, pts, 4), name);
    }
    const std::string r = readFixture("bus_error_route.json");
    TEST_ASSERT_EQUAL_INT(0, cta::buildBusRows(r.c_str(), r.size(), "2009", rows, 4));
    TEST_ASSERT_EQUAL_INT(0, cta::busPatternPath(r.c_str(), r.size(), 100, pts, 4));
    const char* empty = "{\"bustime-response\":{}}";   // neither patterns nor an error: nothing there
    TEST_ASSERT_EQUAL_INT(0, cta::buildBusRows(empty, strlen(empty), "2009", rows, 4));
}

static void test_bus_pattern_path() {
    std::string p = readFixture("bus_patterns.json");
    cta::LatLon pts[16];
    TEST_ASSERT_EQUAL_INT(11, cta::busPatternPath(p.c_str(), p.size(), 100, pts, 16));   // waypoint included
    TEST_ASSERT_FLOAT_WITHIN(1e-4, 41.96f, pts[0].lat);
    TEST_ASSERT_FLOAT_WITHIN(1e-4, 41.9455f, pts[4].lat);
    TEST_ASSERT_FLOAT_WITHIN(1e-4, -87.6415f, pts[10].lon);
    TEST_ASSERT_EQUAL_INT(4, cta::busPatternPath(p.c_str(), p.size(), 100, pts, 4));     // thinned, ends kept
    TEST_ASSERT_FLOAT_WITHIN(1e-4, 41.96f, pts[0].lat);
    TEST_ASSERT_FLOAT_WITHIN(1e-4, 41.933f, pts[3].lat);
    TEST_ASSERT_EQUAL_INT(0, cta::busPatternPath(p.c_str(), p.size(), 999, pts, 16));
    TEST_ASSERT_EQUAL_INT(-1, cta::busPatternPath("{", 1, 100, pts, 16));
}

int main() {
    UNITY_BEGIN();
    RUN_TEST(test_train_rows);
    RUN_TEST(test_dock_default_train_rows);
    RUN_TEST(test_branch_rows_take_the_trunk_before_them);
    RUN_TEST(test_platform_positions);
    RUN_TEST(test_train_line_for_platform);
    RUN_TEST(test_picker_stations_and_directions);
    RUN_TEST(test_bus_rows);
    RUN_TEST(test_bus_pattern_path);
    RUN_TEST(test_bus_rows_from_error_replies);
    return UNITY_END();
}
