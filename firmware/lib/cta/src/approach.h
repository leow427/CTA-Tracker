#pragma once
// Approach rows: the dock's LED rows (spec 6.4) and the stops its map frames (plan 2, milestone 5; plan 1 task 8).
#include <stddef.h>
#include <stdint.h>

#include "cta_types.h"
#include "train_sequences.h"

namespace cta {

// stops[6] = stopId and stops[0..5] the 6 stops before it in seq (farthest first); missing leading slots stay "".
// pid = -1, before = -1, lat/lon from trainPlatformLatLon (0 when unknown). False if stopId isn't in seq.
bool buildTrainRow(const char* const* seq, size_t n, const char* stopId, ApproachRow& out);

// The sequence a picked platform's row comes from: the line of that route holding the platform past its lead-in
// (plan 1 task 10 lead-in ruling). nullptr if none.
const TrainLine* trainLineFor(const char* route, const char* stopId);
// buildTrainRow over trainLineFor's sequence. Green's Cottage Grove and Ashland/63rd branches and the Loop-bound
// Purple Express are short sequences of their own, so theirs starts with the last stops of the run that feeds them
// (the trunk, or Linden-Howard). False if no sequence has the platform.
bool trainRowFor(const char* route, const char* stopId, ApproachRow& out);

// The picker's stations for a (line, direction): every sequence with that route and full dirLabel (a direction can
// span several, as Green's do), lead-in skipped (not picks), each platform once, in travel order. Returns how many
// refs were written (at most max).
struct StationRef {
    const TrainLine* line;
    uint8_t index;
};
size_t trainStations(const char* route, const char* dirLabel, StationRef* out, size_t max);
// The distinct direction labels of a line, in kTrainLines order (2 for most, 3 for Green and Purple). Returns the
// count (at most max).
size_t trainDirections(const char* route, const char** out, size_t max);

// One row per getpatterns pattern holding stopId as a stop (typ "S"), from its "S" points only, up to max: stops,
// pdist (rounded; Bus Tracker sends decimals), lat/lon, before (the pdist of the stop before stops[0], or -1 if
// none) and pid. Returns the row count, or -1 if malformed or an error reply that isn't about the route (a bad key,
// the daily limit: json::noListResult's rule), so a failed fetch is tried again rather than kept as "no rows".
int buildBusRows(const char* patternsJson, size_t len, const char* stopId, ApproachRow* out, size_t max);

// The points (stops and waypoints) of pattern pid in order, for drawing the route on the map: every point when they
// fit in max, else evenly spaced ones, always keeping the first and last. Returns the count written, 0 if pid isn't
// there, or -1 if malformed or an error reply, as buildBusRows.
struct LatLon {
    float lat, lon;
};
int busPatternPath(const char* patternsJson, size_t len, int32_t pid, LatLon* out, size_t max);

// Bit k set when some train has stops[k] as its next stop (ttpositions is fetched per line, so every train given
// is the row's line; Brown and Purple share Loop-bound platforms, and a Purple train must not light Brown's row).
uint8_t litMaskTrains(const ApproachRow& row, const TrainPos* t, size_t n);
// For a bus on a row's pattern, bit k set when pdist[k-1] < v.pdist <= pdist[k] (the first filled slot's lower
// bound is `before`; -1 means none). Buses past the picked stop light nothing. ORed over the rows.
uint8_t litMaskBuses(const ApproachRow* rows, size_t nRows, const BusVehicle* v, size_t n);

}  // namespace cta
