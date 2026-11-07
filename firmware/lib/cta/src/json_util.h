#pragma once
// Internal helpers shared by the CTA response parsers. Not part of the library's interface.
#include <ArduinoJson.h>
#include <stdint.h>
#include <stdlib.h>

namespace cta {
namespace json {

// Parses with a filter shaped for the common case (lists of objects). CTA sends one object instead of a one-item
// list, which an array filter drops (it comes back null), so this re-parses without a filter when
// listMissing(doc) says a list came back null. Those responses are small: one item, or none.
template <typename ListMissing>
bool parseFiltered(const char* json, size_t len, const JsonDocument& filter, JsonDocument& doc,
                   ListMissing listMissing) {
    if (json == nullptr) return false;
    if (deserializeJson(doc, json, len, DeserializationOption::Filter(filter)) != DeserializationError::Ok)
        return false;
    if (listMissing(doc)) {
        doc.clear();
        if (deserializeJson(doc, json, len) != DeserializationError::Ok) return false;
    }
    return true;
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
