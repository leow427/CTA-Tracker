#pragma once
// Big CTA replies must stay bounded in memory. cJSON has no filter, so this checks the parse that just ran, through
// the json_heap hooks: its nodes peaked at 8 bytes or less per byte of the replyLen-byte reply (about 5 with the
// laptop's 64-bit nodes, less on the ESP32), and nothing was left allocated. Then it starts the next measurement.
#include <unity.h>

#include "json_util.h"

inline void assertParseBounded(size_t replyLen) {
    TEST_ASSERT_LESS_OR_EQUAL_UINT64(8 * replyLen, cta::json::peakBytes());
    TEST_ASSERT_EQUAL_UINT64(0, cta::json::liveBytes());
    cta::json::resetPeakBytes();
}
