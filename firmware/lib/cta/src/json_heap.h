#pragma once
// Where cJSON keeps its nodes. A route's bus patterns parse to about 1 MB of nodes, and the default heap's small
// blocks come from internal RAM on the ESP32, which Wi-Fi and TLS need. The boards point this at PSRAM; the default
// (nullptr) is malloc. Call it at boot, before anything is parsed.
#include <stddef.h>

namespace cta {

struct JsonHeap {
    void* (*allocate)(size_t);
    void (*deallocate)(void*);
    void* (*reallocate)(void*, size_t);   // unused: cJSON has no realloc hook
};
void setJsonHeap(const JsonHeap* heap);

}  // namespace cta
