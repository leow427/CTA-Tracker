#pragma once
// Big CTA replies must stay bounded in memory. cJSON has no filter, so the least a parse can hold is one tree of the
// reply. Call this right after a parser ran on reply (with the peak reset before it): it checks the parser's peak,
// through the json_heap hooks, was no more than one bare parse of the same reply (no slack: the parsers allocate
// nothing else), and that nothing was left allocated. Then it starts the next measurement.
#include <unity.h>

#include <string>

#include "json_util.h"

inline void assertParseBounded(const std::string& reply) {
    const size_t parserPeak = cta::json::peakBytes();
    TEST_ASSERT_EQUAL_UINT64(0, cta::json::liveBytes());
    cta::json::resetPeakBytes();
    cJSON_Delete(cta::json::parse(reply.c_str(), reply.size()));
    const size_t treePeak = cta::json::peakBytes();
    TEST_ASSERT_TRUE(treePeak > 0);   // the reply parses
    TEST_ASSERT_LESS_OR_EQUAL_UINT64(treePeak, parserPeak);
    cta::json::resetPeakBytes();
}
