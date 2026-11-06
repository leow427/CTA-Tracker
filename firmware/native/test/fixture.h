#pragma once
// Reads a shared CTA fixture (fixtures/cta/<name>) for the native tests. Fails the test if it's missing.
#include <unity.h>

#include <fstream>
#include <sstream>
#include <string>

inline std::string readFixture(const char* name) {
    // pio test runs from firmware/native, so the repo root is two levels up.
    const std::string path = std::string("../../fixtures/cta/") + name;
    std::ifstream in(path, std::ios::binary);
    if (!in) {
        const std::string msg = "missing fixture: " + path;
        TEST_FAIL_MESSAGE(msg.c_str());
        return std::string();
    }
    std::ostringstream ss;
    ss << in.rdbuf();
    return ss.str();
}
