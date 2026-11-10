#include <unity.h>

#include "compass.h"

void setUp() {}
void tearDown() {}

void test_bus_directions() {
    TEST_ASSERT_EQUAL_CHAR('S', cta::busCompass("Southbound"));
    TEST_ASSERT_EQUAL_CHAR('N', cta::busCompass("Northbound"));
    TEST_ASSERT_EQUAL_CHAR('E', cta::busCompass("Eastbound"));
    TEST_ASSERT_EQUAL_CHAR('W', cta::busCompass("westbound"));
    TEST_ASSERT_EQUAL_CHAR(0, cta::busCompass(""));
    TEST_ASSERT_EQUAL_CHAR(0, cta::busCompass(nullptr));
    TEST_ASSERT_EQUAL_CHAR(0, cta::busCompass("Clockwise"));
}

void test_train_ends() {
    TEST_ASSERT_EQUAL_CHAR('N', cta::trainCompass("Red", "Howard"));
    TEST_ASSERT_EQUAL_CHAR('S', cta::trainCompass("Red", "95th/Dan Ryan"));
    TEST_ASSERT_EQUAL_CHAR('N', cta::trainCompass("Blue", "O'Hare"));
    TEST_ASSERT_EQUAL_CHAR('W', cta::trainCompass("Blue", "Forest Park"));
    TEST_ASSERT_EQUAL_CHAR('N', cta::trainCompass("Brn", "Kimball"));
    TEST_ASSERT_EQUAL_CHAR('S', cta::trainCompass("Brn", "Loop"));
    TEST_ASSERT_EQUAL_CHAR('W', cta::trainCompass("G", "Harlem/Lake"));
    TEST_ASSERT_EQUAL_CHAR('S', cta::trainCompass("G", "Ashland/63rd"));
    TEST_ASSERT_EQUAL_CHAR('S', cta::trainCompass("Org", "Midway"));
    TEST_ASSERT_EQUAL_CHAR('N', cta::trainCompass("Org", "Loop"));
    TEST_ASSERT_EQUAL_CHAR('W', cta::trainCompass("Pink", "54th/Cermak"));
    TEST_ASSERT_EQUAL_CHAR('E', cta::trainCompass("Pink", "Loop"));
    TEST_ASSERT_EQUAL_CHAR('N', cta::trainCompass("P", "Linden"));
    TEST_ASSERT_EQUAL_CHAR('S', cta::trainCompass("P", "Howard"));
    TEST_ASSERT_EQUAL_CHAR('N', cta::trainCompass("Y", "Dempster-Skokie"));
    TEST_ASSERT_EQUAL_CHAR('S', cta::trainCompass("Y", "Howard"));
}

void test_same_destination_depends_on_line() {   // Howard is north on the Red Line, south on Purple and Yellow
    TEST_ASSERT_EQUAL_CHAR('N', cta::trainCompass("red", "Howard"));
    TEST_ASSERT_EQUAL_CHAR('S', cta::trainCompass("P", "Howard"));
}

void test_unknown_gives_no_letter() {
    TEST_ASSERT_EQUAL_CHAR(0, cta::trainCompass("Red", "Somewhere"));
    TEST_ASSERT_EQUAL_CHAR(0, cta::trainCompass("Bus", "Loop"));
    TEST_ASSERT_EQUAL_CHAR(0, cta::trainCompass(nullptr, "Loop"));
    TEST_ASSERT_EQUAL_CHAR(0, cta::trainCompass("Brn", nullptr));
    TEST_ASSERT_EQUAL_CHAR(0, cta::trainCompass("P", "Lo"));   // a prefix of an end isn't the end
}

int main(int, char**) {
    UNITY_BEGIN();
    RUN_TEST(test_bus_directions);
    RUN_TEST(test_train_ends);
    RUN_TEST(test_same_destination_depends_on_line);
    RUN_TEST(test_unknown_gives_no_letter);
    return UNITY_END();
}
