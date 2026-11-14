#pragma once
// Where cJSON keeps its nodes. A route's bus patterns parse to about 1 MB of nodes, and the default heap's small
// blocks come from internal RAM on the ESP32, which Wi-Fi and TLS need. The boards point this at PSRAM; the default
// (nullptr) is malloc. Call it at boot, before anything is parsed.
// The hooks put a size header in front of every block, so cJSON output is freed only with cJSON_free/cJSON_Delete,
// never free(); and IDF's wifi_provisioning must not be linked, since it free()s cJSON_Print output.
#include <stddef.h>

namespace cta {

struct JsonHeap {
    void* (*allocate)(size_t);
    void (*deallocate)(void*);
    void* (*reallocate)(void*, size_t);   // unused: cJSON has no realloc hook
};
void setJsonHeap(const JsonHeap* heap);

}  // namespace cta
