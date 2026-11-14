#pragma once
// Internal cJSON helpers shared by the parsers and encoders. Not part of the library's interface.
// They keep the old parser's reading rules: keys are case-sensitive and the last duplicate wins, a string
// read from a number is null, an integer read from 2.5 fails, and nesting deeper than 10 is malformed.
#include <cJSON.h>
#include <math.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include <limits>

namespace cta {
namespace json {

// Parses len bytes (no NUL needed) with nodes from the json_heap hooks. nullptr if json is null or malformed.
cJSON* parse(const char* json, size_t len);

// Writes root minified into buf: with needNul it must fit with its NUL, else only its bytes must fit (the NUL follows
// if there's room). Returns the length, or 0 if it doesn't fit or memory runs out.
size_t printInto(const cJSON* root, char* buf, size_t cap, bool needNul);

// The json_heap hooks' live and peak bytes: a debug counter for the tests and the boards' logs.
size_t liveBytes();
size_t peakBytes();
void resetPeakBytes();   // peak = live

// Owns a tree and deletes it.
struct Doc {
    cJSON* root;
    explicit Doc(cJSON* r) : root(r) {}
    ~Doc() { cJSON_Delete(root); }
    Doc(const Doc&) = delete;
    Doc& operator=(const Doc&) = delete;
};

// The member `key` of an object (the last one if repeated), else nullptr.
inline const cJSON* at(const cJSON* o, const char* key) {
    const cJSON* found = nullptr;
    if (cJSON_IsObject(o))
        for (const cJSON* c = o->child; c != nullptr; c = c->next)
            if (c->string != nullptr && strcmp(c->string, key) == 0) found = c;
    return found;
}

inline bool isNull(const cJSON* v) { return v == nullptr || cJSON_IsNull(v); }

// The string, or nullptr if v isn't a string.
inline const char* str(const cJSON* v) { return cJSON_GetStringValue(v); }
inline const char* strOr(const cJSON* v, const char* fallback) {
    const char* s = str(v);
    return s != nullptr ? s : fallback;
}

// True if v is a number with no fraction that fits in T (the old is<T>() for integers).
template <typename T>
bool isInt(const cJSON* v, T& out) {
    if (!cJSON_IsNumber(v)) return false;
    const double d = v->valuedouble, lim = ldexp(1.0, std::numeric_limits<T>::digits);
    const double lo = std::numeric_limits<T>::is_signed ? -lim : 0;
    if (d != floor(d) || d < lo || d >= lim) return false;
    out = static_cast<T>(d);
    return true;
}

// The old as<T>() for integers: a number truncated (0 if out of range), true as 1, a numeric string parsed,
// anything else 0.
template <typename T>
T asInt(const cJSON* v) {
    double d;
    if (cJSON_IsNumber(v)) {
        d = v->valuedouble;
    } else if (cJSON_IsTrue(v)) {
        return 1;
    } else if (cJSON_IsString(v)) {
        char* end = nullptr;
        d = strtod(v->valuestring, &end);
        if (end == v->valuestring || *end != '\0') return 0;
    } else {
        return 0;
    }
    const double lim = ldexp(1.0, std::numeric_limits<T>::digits);
    const double lo = std::numeric_limits<T>::is_signed ? -lim : 0;
    return (d >= lo && d < lim) ? static_cast<T>(d) : 0;
}

// The old as<bool>(): false for missing, null, false and 0; true for anything else (strings included).
inline bool asBool(const cJSON* v) {
    if (isNull(v) || cJSON_IsFalse(v)) return false;
    if (cJSON_IsNumber(v)) return v->valuedouble != 0;
    return true;
}

// The number, or fallback if v isn't a number.
inline double numOr(const cJSON* v, double fallback) { return cJSON_IsNumber(v) ? v->valuedouble : fallback; }

// Calls f(item) for each object in an array, or once for a lone object.
template <typename F>
void forEachItem(const cJSON* v, F f) {
    if (cJSON_IsArray(v)) {
        for (const cJSON* item = v->child; item != nullptr; item = item->next)
            if (cJSON_IsObject(item)) f(item);
    } else if (cJSON_IsObject(v)) {
        f(v);
    }
}

// An int32 from a JSON integer or a string holding only an integer (Bus Tracker sends both).
inline bool toInt32(const cJSON* v, int32_t& out) {
    if (isInt(v, out)) return true;
    const char* s = str(v);
    if (s == nullptr || *s == '\0') return false;
    char* end = nullptr;
    const long n = strtol(s, &end, 10);
    if (*end != '\0' || n < INT32_MIN || n > INT32_MAX) return false;
    out = static_cast<int32_t>(n);
    return true;
}

// A Bus Tracker reply with no list: 0 if every error names a stop, route, or pattern (nothing there now), −1 for
// any other error (a bad key, the daily limit), so a failed fetch isn't read as "nothing there".
inline int noListResult(const cJSON* errors) {
    int result = 0;
    forEachItem(errors, [&](const cJSON* e) {
        if (isNull(at(e, "stpid")) && isNull(at(e, "rt")) && isNull(at(e, "pid"))) result = -1;
    });
    return result;
}

// Whole minutes from seconds, floored, clamped to 0–32767.
inline int16_t minutesFrom(int64_t secs) {
    if (secs <= 0) return 0;
    const int64_t m = secs / 60;
    return m > 32767 ? 32767 : static_cast<int16_t>(m);
}

}  // namespace json
}  // namespace cta
