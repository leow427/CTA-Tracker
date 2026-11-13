#include "json_heap.h"

#include "json_util.h"

namespace cta {

namespace {
const JsonHeap* gHeap = nullptr;

class HeapAllocator : public ArduinoJson::Allocator {
public:
    void* allocate(size_t n) override { return gHeap ? gHeap->allocate(n) : malloc(n); }
    void deallocate(void* p) override { if (gHeap) gHeap->deallocate(p); else free(p); }
    void* reallocate(void* p, size_t n) override { return gHeap ? gHeap->reallocate(p, n) : realloc(p, n); }
};
HeapAllocator gAllocator;
}  // namespace

void setJsonHeap(const JsonHeap* heap) { gHeap = heap; }

namespace json {
ArduinoJson::Allocator* bigAllocator() { return &gAllocator; }
}  // namespace json

}  // namespace cta
