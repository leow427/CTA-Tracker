#include "json_heap.h"

#include <stddef.h>
#include <stdlib.h>
#include <string.h>

#include <atomic>

#include "json_util.h"

namespace cta {

namespace {

const JsonHeap* gHeap = nullptr;
// Live and peak bytes. Peak is updated without a lock: it's a debug number, and a race only blurs it.
std::atomic<size_t> gLive{0}, gPeak{0};

// Each block carries its size in front, so the free hook can count it. The header keeps malloc's alignment.
constexpr size_t kHeader = alignof(max_align_t);

void* hookMalloc(size_t n) {
    if (n > SIZE_MAX - kHeader) return nullptr;
    char* p = static_cast<char*>(gHeap ? gHeap->allocate(n + kHeader) : malloc(n + kHeader));
    if (p == nullptr) return nullptr;
    memcpy(p, &n, sizeof(n));
    const size_t live = gLive.fetch_add(n) + n;
    if (live > gPeak.load()) gPeak.store(live);
    return p + kHeader;
}

void hookFree(void* q) {
    if (q == nullptr) return;
    char* p = static_cast<char*>(q) - kHeader;
    size_t n;
    memcpy(&n, p, sizeof(n));
    gLive.fetch_sub(n);
    if (gHeap) gHeap->deallocate(p);
    else free(p);
}

void installHooks() {
    cJSON_Hooks hooks = {hookMalloc, hookFree};
    cJSON_InitHooks(&hooks);
}

// Installed before main/app_main, so no node is ever made by one allocator and freed by the other. Every binary that
// parses or prints links this file (json::parse, json::printInto).
const bool gInstalled = (installHooks(), true);

// Rejects nesting deeper than 10, as the old parser did: cJSON recurses once per level (up to 1000), and a 1 KB body
// of brackets would overflow a task stack.
bool nestingOk(const char* s, size_t len) {
    constexpr int kMaxDepth = 10;
    int depth = 0;
    bool inString = false;
    for (size_t i = 0; i < len; ++i) {
        const char c = s[i];
        if (inString) {
            if (c == '\\') ++i;
            else if (c == '"') inString = false;
        } else if (c == '"') {
            inString = true;
        } else if (c == '[' || c == '{') {
            if (++depth > kMaxDepth) return false;
        } else if (c == ']' || c == '}') {
            --depth;
        }
    }
    return true;
}

}  // namespace

void setJsonHeap(const JsonHeap* heap) {
    gHeap = heap;
    installHooks();
}

namespace json {

cJSON* parse(const char* json, size_t len) {
    if (json == nullptr || !nestingOk(json, len)) return nullptr;
    return cJSON_ParseWithLength(json, len);
}

size_t printInto(const cJSON* root, char* buf, size_t cap, bool needNul) {
    if (root == nullptr || buf == nullptr) return 0;
    char* s = cJSON_PrintUnformatted(root);
    if (s == nullptr) return 0;
    const size_t n = strlen(s);
    const bool fits = needNul ? n < cap : n <= cap;
    if (fits) memcpy(buf, s, n < cap ? n + 1 : n);
    cJSON_free(s);
    return fits ? n : 0;
}

size_t liveBytes() { return gLive.load(); }
size_t peakBytes() { return gPeak.load(); }
void resetPeakBytes() { gPeak.store(gLive.load()); }

}  // namespace json

}  // namespace cta
