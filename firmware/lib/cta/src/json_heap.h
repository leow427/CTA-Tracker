#pragma once
// Where the big parses keep their documents (bus patterns, the picker's route and stop lists): a route's patterns
// parse to about 100 KB, and the default heap's small blocks come from internal RAM on the ESP32, which Wi-Fi and
// TLS need. The dock points this at PSRAM; the default (nullptr) is malloc.
#include <stddef.h>

namespace cta {

struct JsonHeap {
    void* (*allocate)(size_t);
    void (*deallocate)(void*);
    void* (*reallocate)(void*, size_t);
};
void setJsonHeap(const JsonHeap* heap);

}  // namespace cta
