#pragma once
// Reads fixtures/cta/<name> (tests run from firmware/native). Fails the test if it's missing.
#include <unity.h>

#include <fstream>
#include <sstream>
#include <string>

inline std::string readFixture(const char* name) {
    std::ifstream f(std::string("../../fixtures/cta/") + name, std::ios::binary);
    if (!f) TEST_FAIL_MESSAGE(name);
    std::stringstream ss;
    ss << f.rdbuf();
    return ss.str();
}
