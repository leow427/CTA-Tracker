#pragma once
#include <stddef.h>

#include "cta_types.h"
#include "train_sequences.h"

namespace cta {
// Spec 6.4. stops[6] = stopId, stops[0..5] = the 6 before it (farthest first); missing slots stay "".
// False if stopId isn't in seq.
bool buildTrainRow(const char* const* seq, size_t n, const char* stopId, ApproachRow& out);
// The sequence a picked platform's row comes from: the line of that route holding the platform past its
// lead-in (plan 1 task 10 lead-in ruling). nullptr if none.
const TrainLine* trainLineFor(const char* route, const char* stopId);
// The picker's stations for a (line, direction): every sequence with that route and full dirLabel (a direction
// can span several, e.g. Green to Harlem/Lake), lead-in skipped (not picks), each platform once, in travel order.
// Returns how many refs were written (at most max).
struct StationRef { const TrainLine* line; uint8_t index; };
size_t trainStations(const char* route, const char* dirLabel, StationRef* out, size_t max);
// One row per pattern containing stopId, from "S" points only. Returns the row count, or -1 if malformed.
int buildBusRows(const char* patternsJson, size_t len, const char* stopId, ApproachRow* out, size_t max);
// Bit k set when some train of `route` has stops[k] next. Lines share platforms (Brown and Purple to the Loop),
// so other lines' trains never light the row.
uint8_t litMaskTrains(const ApproachRow& row, const char* route, const TrainPos* t, size_t n);
// For a bus on a row's pattern, bit k set when pdist[k-1] < v.pdist <= pdist[k] (the first filled slot's
// lower bound is `before`; -1 means none). Buses past the picked stop light nothing.
uint8_t litMaskBuses(const ApproachRow* rows, size_t nRows, const BusVehicle* v, size_t n);
}  // namespace cta
