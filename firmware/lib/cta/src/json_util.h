#pragma once
// Internal helpers shared by the CTA response parsers. Not part of the library's interface.
#include <ArduinoJson.h>
#include <stdint.h>
#include <stdlib.h>

#include <initializer_list>

namespace cta {
namespace json {

// CTA sends one object instead of a one-item list, and an ArduinoJson array filter drops a lone object (it comes
// back null). So this tries each filter in turn, in the order of the shapes to expect, until shapeFound(doc) says
// the lists it needs came through, and only then re-parses without a filter. Order the filters so that the
// unfiltered pass is left for responses that really are small (one item, or none). False if malformed.
// How many parses fell through to the unfiltered pass: lets the tests check big responses stay filtered.
inline unsigned& unfilteredPasses() {
    static unsigned n = 0;
    return n;
}

template <typename ShapeFound>
bool parseShapes(const char* json, size_t len, JsonDocument& doc, std::initializer_list<const JsonDocument*> filters,
                 ShapeFound shapeFound) {
    if (json == nullptr) return false;
    for (const JsonDocument* filter : filters) {
        doc.clear();
        if (deserializeJson(doc, json, len, DeserializationOption::Filter(*filter)) != DeserializationError::Ok)
            return false;
        if (shapeFound(doc)) return true;
    }
    doc.clear();
    ++unfilteredPasses();
    return deserializeJson(doc, json, len) == DeserializationError::Ok;
}

// One filter for the list shape, then unfiltered when listMissing(doc).
template <typename ListMissing>
bool parseFiltered(const char* json, size_t len, const JsonDocument& filter, JsonDocument& doc,
                   ListMissing listMissing) {
    return parseShapes(json, len, doc, {&filter}, [&](JsonDocument& d) { return !listMissing(d); });
}

// Calls f(item) for each object in an array, or once for a lone object.
template <typename F>
void forEachItem(JsonVariantConst v, F f) {
    if (v.is<JsonArrayConst>()) {
        for (JsonVariantConst item : v.as<JsonArrayConst>()) {
            if (item.is<JsonObjectConst>()) f(item.as<JsonObjectConst>());
        }
    } else if (v.is<JsonObjectConst>()) {
        f(v.as<JsonObjectConst>());
    }
}

// An int32 from a JSON integer or a string holding only an integer (Bus Tracker sends both).
inline bool toInt32(JsonVariantConst v, int32_t& out) {
    if (v.is<int32_t>()) {
        out = v.as<int32_t>();
        return true;
    }
    const char* s = v.as<const char*>();
    if (s == nullptr || *s == '\0') return false;
    char* end = nullptr;
    const long n = strtol(s, &end, 10);
    if (*end != '\0' || n < INT32_MIN || n > INT32_MAX) return false;
    out = static_cast<int32_t>(n);
    return true;
}

// Whole minutes from seconds, floored, clamped to 0–32767.
inline int16_t minutesFrom(int64_t secs) {
    if (secs <= 0) return 0;
    const int64_t m = secs / 60;
    return m > 32767 ? 32767 : static_cast<int16_t>(m);
}

}  // namespace json
}  // namespace cta
