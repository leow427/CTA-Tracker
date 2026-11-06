#include <string.h>
#include <unity.h>

#include "../fixture.h"
#include "approach.h"

void setUp() {}
void tearDown() {}

static void test_train_rows() {
    const char* seq[] = {"30001", "30002", "30003", "30004", "30005", "30006", "30007", "30008", "30009", "30010"};
    cta::ApproachRow row;
    TEST_ASSERT_TRUE(cta::buildTrainRow(seq, 10, "30008", row));
    TEST_ASSERT_EQUAL_STRING("30002", row.stops[0]);
    TEST_ASSERT_EQUAL_STRING("30008", row.stops[6]);
    cta::TrainPos t[] = {{"30005", "red"}, {"30001", "red"}, {"30008", "red"}};
    TEST_ASSERT_EQUAL_HEX8(0b1001000, cta::litMaskTrains(row, "Red", t, 3));   // LED 3 and LED 6; 30001 outside the row
    cta::TrainPos shared[] = {{"30005", "p"}, {"30006", "brn"}};   // Brown and Purple share Loop-bound platforms
    TEST_ASSERT_EQUAL_HEX8(0b0010000, cta::litMaskTrains(row, "Brn", shared, 2));   // only Brown's own train
    TEST_ASSERT_TRUE(cta::buildTrainRow(seq, 10, "30003", row));        // only 2 stops before it
    TEST_ASSERT_EQUAL_STRING("", row.stops[3]);
    TEST_ASSERT_EQUAL_STRING("30001", row.stops[4]);
    TEST_ASSERT_EQUAL_HEX8(0, cta::litMaskTrains(row, "Red", t + 2, 1));
    TEST_ASSERT_TRUE(cta::buildTrainRow(seq, 10, "30001", row));
    TEST_ASSERT_EQUAL_STRING("", row.stops[0]);
    TEST_ASSERT_FALSE(cta::buildTrainRow(seq, 10, "39999", row));
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

static void test_picker_stations() {
    // Every (line, direction) offers stations, each once, none from a lead-in, including Green's long
    // "Ashland/63rd & Cottage Grove" (over 24 bytes, so it must not be compared truncated).
    for (size_t l = 0; l < cta::kTrainLineCount; l++) {
        const cta::TrainLine& line = cta::kTrainLines[l];
        cta::StationRef refs[64];
        size_t n = cta::trainStations(line.route, line.dirLabel, refs, 64);
        TEST_ASSERT_TRUE_MESSAGE(n > 0, line.dirLabel);
        for (size_t i = 0; i < n; i++) {
            const char* id = refs[i].line->stopIds[refs[i].index];
            TEST_ASSERT_TRUE(refs[i].index >= refs[i].line->lead);
            TEST_ASSERT_NOT_NULL(cta::trainLineFor(line.route, id));
            for (size_t j = 0; j < i; j++) TEST_ASSERT_TRUE(strcmp(id, refs[j].line->stopIds[refs[j].index]) != 0);
        }
    }
    cta::StationRef few[2];
    const cta::TrainLine* red = cta::trainLineFor("Red", "30255");   // Belmont, toward 95th
    TEST_ASSERT_NOT_NULL(red);
    TEST_ASSERT_EQUAL_size_t(2, cta::trainStations("Red", red->dirLabel, few, 2));   // capacity
    TEST_ASSERT_EQUAL_size_t(0, cta::trainStations("Red", "Nowhere", few, 2));
}

static void test_bus_rows() {
    std::string p = readFixture("bus_patterns.json");
    cta::ApproachRow rows[4];
    int n = cta::buildBusRows(p.c_str(), p.size(), "2009", rows, 4);
    TEST_ASSERT_EQUAL_INT(2, n);
    TEST_ASSERT_EQUAL_STRING("2003", rows[0].stops[0]);
    TEST_ASSERT_EQUAL_INT32(1000, rows[0].before);   // waypoint skipped
    TEST_ASSERT_EQUAL_INT32(100, rows[0].pid);
    TEST_ASSERT_EQUAL_STRING("", rows[1].stops[3]);
    TEST_ASSERT_EQUAL_INT32(-1, rows[1].before);
    cta::BusVehicle v[] = {{100, 4500}, {100, 1500}, {100, 500}, {100, 8000}, {100, 8500}, {200, 600}};
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

int main() {
    UNITY_BEGIN();
    RUN_TEST(test_train_rows);
    RUN_TEST(test_train_line_for_platform);
    RUN_TEST(test_picker_stations);
    RUN_TEST(test_bus_rows);
    return UNITY_END();
}
