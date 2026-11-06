#pragma once
#include <stddef.h>

#include "cta_types.h"

namespace cta {
// Base spec 6.2 JSON. Returns the length, or 0 if it doesn't fit in cap (one BLE write is 512).
size_t encodeStopRecord(const StopRecord& r, char* buf, size_t cap);
// False on malformed JSON or a missing id. Keeps at most 4 arrivals and cuts names to the record's sizes.
bool decodeStopRecord(const char* json, size_t len, StopRecord& out);
}  // namespace cta
