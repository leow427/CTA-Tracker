#pragma once
// Internal helpers for CTA's JSON quirks.
#include <ArduinoJson.h>
#include <stdlib.h>
#include <strings.h>

#include <initializer_list>

#if defined(ESP32)
#include <esp_heap_caps.h>
#endif

namespace cta {

// JSON documents live in PSRAM on the ESP32s, so big CTA responses (bus patterns) never eat internal RAM.
struct JsonHeap : ArduinoJson::Allocator {
    void* allocate(size_t n) override {
#if defined(ESP32)
        if (void* p = heap_caps_malloc(n, MALLOC_CAP_SPIRAM)) return p;
#endif
        return malloc(n);
    }
    void deallocate(void* p) override { free(p); }
    void* reallocate(void* p, size_t n) override {
#if defined(ESP32)
        if (void* q = heap_caps_realloc(p, n, MALLOC_CAP_SPIRAM)) return q;
#endif
        return realloc(p, n);
    }
    static JsonHeap* instance() {
        static JsonHeap heap;
        return &heap;
    }
};

struct Doc : JsonDocument {
    Doc() : JsonDocument(JsonHeap::instance()) {}
};

// A list node in a filter: its first element when CTA sends an array, the node itself when it sends one object.
// `node` is a member of the filter being built, e.g. f["ctatt"]["eta"].
template <typename Node>
JsonVariant listItem(Node node, bool many) {
    JsonVariant v = node.template to<JsonVariant>();
    return many ? v.template to<JsonArray>().template add<JsonVariant>() : v;
}

// CTA sends one object instead of an array when a list has one item, and ArduinoJson's array filters drop
// that object. So: filter with the list as an array; if the list came out missing, filter again as an object.
// build(filter, many) fills the filter; list(doc) returns the list's node.
template <typename Build, typename List>
DeserializationError deserializeList(JsonDocument& doc, const char* json, size_t len, Build build, List list) {
    DeserializationError err;
    for (bool many : {true, false}) {
        Doc filter;
        build(filter, many);
        err = deserializeJson(doc, json, len, DeserializationOption::Filter(filter));
        if (err || !list(doc).isNull()) break;
    }
    return err;
}

// Calls f for each item of a list that may be one object.
template <typename F>
void forEachItem(JsonVariantConst v, F f) {
    if (v.is<JsonArrayConst>()) {
        for (JsonVariantConst item : v.as<JsonArrayConst>()) f(item);
    } else if (v.is<JsonObjectConst>()) {
        f(v);
    }
}

// A number CTA may send as a number or a numeric string. False for junk ("", "n/a", null).
inline bool looseLong(JsonVariantConst v, long& out) {
    if (v.is<long>()) { out = v.as<long>(); return true; }
    if (v.is<double>()) { out = (long)v.as<double>(); return true; }
    const char* s = v.as<const char*>();
    if (!s || !*s) return false;
    char* end;
    double d = strtod(s, &end);
    if (*end != '\0') return false;
    out = (long)d;
    return true;
}

inline bool sameRoute(const char* a, const char* b) { return a && b && strcasecmp(a, b) == 0; }

}  // namespace cta
