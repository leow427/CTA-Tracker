#pragma once
// The stop record JSON (spec 6.2): one BLE snapshot write, and what the app relays to the handheld.
#include <stddef.h>

#include "cta_types.h"

struct cJSON;

namespace cta {

// Writes {id, type:"train"|"bus", route, dir, name, arrivals:[{min,dest,live,delayed,veh}], alert, ts}, keys in that
// order; "veh" only for an arrival with a vehicle. If the record doesn't fit in cap with its vehicles, it's written
// without them (every field at its limit plus vehicles is 523 bytes, over one 512-byte BLE write).
// Returns the length, or 0 (writing nothing) if it doesn't fit in cap; adds a NUL only if there's room.
// Control characters in strings are written as spaces, so the output is always valid JSON.
size_t encodeStopRecord(const StopRecord& r, char* buf, size_t cap);

// Parses a record into out. False (out untouched) on malformed JSON, a missing or non-string id, or a type that
// isn't "train" or "bus". Other missing fields default to empty, 0, or false. Keeps at most kMaxArrivals,
// clamps min to 0–32767, cuts ids, routes, and veh to 7 bytes and names to kNameLen with copyTruncated.
bool decodeStopRecord(const char* json, size_t len, StopRecord& out);
// The same from a parsed node (a relayed stop's records, link_msg.h).
bool decodeStopRecord(const cJSON* node, StopRecord& out);

}  // namespace cta
