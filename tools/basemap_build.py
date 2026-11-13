"""Builds docs/ideas/2026-11-12-chicago-basemap.js: the pixel map's Chicago, for the dock and handheld mockups.

Input: the OpenStreetMap extract tools/basemap_fetch.py saved in data/osm/ (OSM XML), and CTA's GTFS feed (for the L and the 22 at
the same 2 m precision as the map). Output: one script defining CHI_BASEMAP, which docs/ideas/2026-11-12-pixel-map.js
draws. The map is packed the way a device could carry it: features of one class each, every ring a run of points in
1e-5 degree units, zigzag varint deltas, base64 in the script. Same input, byte-identical output.

Classes (higher draws on top): 1 green, 2 sand, 3 water, 4 side street, 5 main street, 6 expressway.

Run from the repo root: python3 tools/basemap_build.py [path/to/google_transit.zip]
Map data (c) OpenStreetMap contributors, ODbL 1.0.
"""
import argparse
import base64
import gzip
import html
import json
import math
import os
import re
import sys
from collections import Counter, defaultdict

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import gtfs_build  # noqa: E402

ROOT = os.path.normpath(os.path.join(os.path.dirname(os.path.abspath(__file__)), ".."))
OSM = os.path.join(ROOT, "data", "osm", "Chicago.osm.gz")
SNAPSHOT = "2026-11-08"    # the BBBike extract's date (its Last-Modified)
OUT = os.path.join(ROOT, "docs", "ideas", "2026-11-12-chicago-basemap.js")
GTFS = os.path.join(ROOT, "data", "gtfs", "google_transit.zip")
SIMPLIFY_M = 2
BUDGET = 700_000          # bytes of packed map data (what the dock could spare later)
# Every CTA stop in data/stops.json (41.644-42.073 N, 87.904-87.526 W) plus about 2 km each way, within the extract.
BOX = (41.643, -87.928, 42.093, -87.502)   # south, west, north, east
ORIGIN = BOX[:2]
EXTRACT = (41.643, -88.006, 42.093, -87.398)   # the BBBike extract's bounds (its <bounds>)
FAR_EAST = -86.9           # the lake polygon is closed out here, well east of the box

GREEN, SAND, WATER, SIDE, MAIN, EXPRESS = 1, 2, 3, 4, 5, 6
ROAD_CLASS = {"motorway": EXPRESS, "trunk": EXPRESS, "primary": MAIN, "secondary": MAIN, "tertiary": MAIN,
              "residential": SIDE, "unclassified": SIDE, "living_street": SIDE}
LINE, AREA = 0, 1


# ---------- classifying ----------

def area_class(tags):
    """The class of an area's tags, or None."""
    if tags.get("natural") == "water" or tags.get("waterway") == "riverbank" or tags.get("landuse") in ("reservoir", "basin"):
        return WATER
    if tags.get("natural") == "beach":
        return SAND
    if tags.get("leisure") in ("park", "golf_course", "nature_reserve") or tags.get("landuse") in ("cemetery", "forest") \
            or tags.get("natural") == "wood":
        return GREEN
    return None


def road_class(tags):
    """The class of a road, or None when it is left out: tunnels and anything below ground, private roads, areas."""
    c = ROAD_CLASS.get(tags.get("highway"))
    if c is None:
        return None
    if tags.get("tunnel", "no") != "no" or tags.get("covered") == "yes" or tags.get("area") == "yes":
        return None
    try:
        if int(tags.get("layer", "0")) < 0:
            return None
    except ValueError:
        pass
    if tags.get("access") in ("private", "no"):
        return None
    return c


# ---------- geometry ----------

def pts(geom):
    return [(g["lat"], g["lon"]) for g in geom if g]


def join_rings(ways, close_gap_m=50, with_open=False):
    """Joins ways (lists of points) end to end into rings. A ring left open by less than close_gap_m is closed; one
    left open by more is dropped (counted in the second value)."""
    ways = [list(w) for w in ways if len(w) >= 2]
    rings, dropped, opens = [], 0, []
    while ways:
        ring = ways.pop()
        changed = True
        while ring[0] != ring[-1] and changed:
            changed = False
            for k, w in enumerate(ways):
                if w[0] == ring[-1]:
                    ring += w[1:]
                elif w[-1] == ring[-1]:
                    ring += w[-2::-1]
                elif w[-1] == ring[0]:
                    ring = w[:-1] + ring
                elif w[0] == ring[0]:
                    ring = w[:0:-1] + ring
                else:
                    continue
                ways.pop(k)
                changed = True
                break
        if ring[0] != ring[-1]:
            if meters(ring[0], ring[-1]) <= close_gap_m:
                ring.append(ring[0])
            else:
                dropped += 1
                opens.append(ring)
                continue
        if len(ring) >= 4:
            rings.append(ring)
    return (rings, dropped, opens) if with_open else (rings, dropped)


def at_edge(p, margin=0.004):
    """A point within about 400 m of the extract's edge, where an area cut by the edge ends."""
    s, w, n, e = EXTRACT
    return p[0] < s + margin or p[0] > n - margin or p[1] < w + margin or p[1] > e - margin


def stitch_at_edge(chains):
    """An area the extract's edge cut open (a river's two banks, say) closed again: the chains are chained end to
    nearest end and closed, joins only between ends at the edge, so the made-up sides lie along the edge and off the
    map. None when some end isn't at the edge (then the gap is a real hole in the data)."""
    if not chains or not all(at_edge(c[0]) and at_edge(c[-1]) for c in chains):
        return None
    left = [list(c) for c in chains]
    ring = left.pop(0)
    while left:
        k, flip = min(((k, f) for k in range(len(left)) for f in (False, True)),
                      key=lambda kf: meters(ring[-1], left[kf[0]][-1] if kf[1] else left[kf[0]][0]))
        nxt = left.pop(k)
        ring += nxt[::-1] if flip else nxt
    return ring + [ring[0]]


def join_chains(ways):
    """Joins ways end to end into chains, turning a way around where it fits backwards (a multipolygon's ways have
    no common direction)."""
    ways = [list(w) for w in ways if len(w) >= 2]
    chains = []
    while ways:
        c = ways.pop()
        grown = True
        while grown and c[0] != c[-1]:
            grown = False
            for k, w in enumerate(ways):
                if w[0] == c[-1]:
                    c += w[1:]
                elif w[-1] == c[-1]:
                    c += w[-2::-1]
                elif w[-1] == c[0]:
                    c = w[:-1] + c
                elif w[0] == c[0]:
                    c = w[:0:-1] + c
                else:
                    continue
                ways.pop(k)
                grown = True
                break
        chains.append(c)
    return chains


def meters(a, b):
    kx = 111_320 * math.cos(math.radians((a[0] + b[0]) / 2))
    return math.hypot((a[1] - b[1]) * kx, (a[0] - b[0]) * 111_320)


def lake(shore_ways, islands=()):
    """Lake Michigan as rings: the shore (the lake's outer ways that are in the extract, or coastline ways) joined into
    one chain, turned to run south to north and closed far out east; plus every island and breakwater as its own ring
    (filled even-odd, so they cut back out of the lake). The second value counts shore pieces left over."""
    chains = join_chains(shore_ways)
    closed = [c for c in chains if c[0] == c[-1]]
    open_ = sorted((c for c in chains if c[0] != c[-1]), key=len, reverse=True)
    if not open_:
        sys.exit("basemap_build: no open shore (it must cross the map box)")
    shore = open_[0]
    if shore[0][0] > shore[-1][0]:
        shore = shore[::-1]
    if max(p[1] for p in shore) > FAR_EAST:
        sys.exit("basemap_build: the shore reaches past where the lake is closed")
    rings = [shore + [(shore[-1][0], FAR_EAST), (shore[0][0], FAR_EAST), shore[0]]]
    rings += [c for c in closed if len(c) >= 4]
    isl, _ = join_rings(list(islands))
    return rings + isl, len(open_) - 1


# ---------- packing ----------

def q(deg, o):
    return int(round((deg - o) * 1e5))


def varint(n, out):
    n = (n << 1) ^ (n >> 63) if n < 0 else n << 1   # zigzag
    while True:
        b = n & 0x7F
        n >>= 7
        if n:
            out.append(b | 0x80)
        else:
            out.append(b)
            return


def pack(features):
    """features: (class, kind, [ring, ...]) with rings of (lat, lon), already in their stored order. Layout: 'CBM2',
    the block count, then per block (one class and kind) its class, kind and feature count, and per feature its ring
    count (areas only; a line is one ring), then per ring its point count and points. Every point is a delta from
    the one before it, across rings and features (the first from the origin), lat then lon, in 1e-5 degrees."""
    out = bytearray(b"CBM2")
    blocks = []
    for f in features:
        if not blocks or blocks[-1][0][:2] != f[:2]:
            blocks.append([f])
        else:
            blocks[-1].append(f)
    varint(len(blocks), out)
    la, lo = 0, 0
    for block in blocks:
        cls, kind = block[0][0], block[0][1]
        out += bytes([cls, kind])
        varint(len(block), out)
        for _, _, rings in block:
            if kind == AREA:
                varint(len(rings), out)
            for ring in rings:
                varint(len(ring), out)
                for p in ring:
                    a, b = q(p[0], ORIGIN[0]), q(p[1], ORIGIN[1])
                    varint(a - la, out)
                    varint(b - lo, out)
                    la, lo = a, b
    return bytes(out)


def tile_order(feature):
    """Sort key: class and kind, then a 1 km tile (rows south to north, snaking), then the first point."""
    cls, kind, rings = feature
    a, b = rings[0][0]
    i, j = int((a - ORIGIN[0]) / 0.009), int((b - ORIGIN[1]) / 0.012)
    return (cls, kind, i, j if i % 2 == 0 else -j, a, b)


def heading(a, b):
    kx = math.cos(math.radians(a[0]))
    return math.atan2(b[0] - a[0], (b[1] - a[1]) * kx)


def strokes(lines):
    """Joins one class's road pieces into strokes: at a shared end, a piece carries on into the one that turns least,
    if that is under 35 degrees (so a street runs on through its intersections as one line). Nothing moves; only
    the joins between pieces go away."""
    ends = defaultdict(list)
    for k, ln in enumerate(lines):
        ends[ln[0]].append(k)
        ends[ln[-1]].append(k)
    used = [False] * len(lines)

    def extend(chain):
        while True:
            end, before = chain[-1], chain[-2]
            h = heading(before, end)
            best, bturn = None, math.radians(35)
            for k in ends[end]:
                if used[k]:
                    continue
                ln = lines[k] if lines[k][0] == end else lines[k][::-1]
                turn = abs((heading(ln[0], ln[1]) - h + math.pi) % (2 * math.pi) - math.pi)
                if turn < bturn:
                    best, bturn = (k, ln), turn
            if best is None:
                return chain
            used[best[0]] = True
            chain += best[1][1:]

    out = []
    for k in sorted(range(len(lines)), key=lambda k: lines[k][0]):
        if used[k]:
            continue
        used[k] = True
        chain = extend(list(lines[k]))
        chain = extend(chain[::-1])
        out.append(chain)
    return out


def simplified(ring, closed):
    s = gtfs_build.simplify(ring, SIMPLIFY_M)
    if closed and len(s) < 4:
        return None
    # repeated points after quantizing add nothing
    out = []
    for p in s:
        if not out or (q(p[0], 0), q(p[1], 0)) != (q(out[-1][0], 0), q(out[-1][1], 0)):
            out.append(p)
    return out if len(out) >= (4 if closed else 2) else None


# ---------- building ----------

AREA_TAGS = {"natural": {"coastline", "water", "wood", "beach"}, "waterway": {"river", "canal", "riverbank"},
             "landuse": {"reservoir", "basin", "cemetery", "forest"}, "leisure": {"park", "golf_course", "nature_reserve"}}
ATTR = re.compile(r'(\w+)="([^"]*)"')


def wanted(tags):
    if tags.get("highway") in ROAD_CLASS:
        return True
    return any(tags.get(k) in v for k, v in AREA_TAGS.items())


def elements(path, kinds):
    """Yields (kind, attrs, tags, children) for each element of the given kinds in an OSM XML file: one element per
    line, children (nd, member) as attribute dicts. Values are unescaped."""
    with gzip.open(path, "rt", encoding="utf-8") as fh:
        cur = None
        for line in fh:
            t = line.lstrip()
            if cur is None:
                for k in kinds:
                    if t.startswith("<" + k + " "):
                        attrs = dict(ATTR.findall(t))
                        cur = (k, attrs, {}, [])
                        if t.rstrip().endswith("/>"):
                            yield cur
                            cur = None
                        break
            elif t.startswith("<tag "):
                a = dict(ATTR.findall(t))
                cur[2][html.unescape(a["k"])] = html.unescape(a["v"])
            elif t.startswith("<nd ") or t.startswith("<member "):
                cur[3].append(dict(ATTR.findall(t)))
            elif t.startswith("</"):
                yield cur
                cur = None


def load_layers(path):
    """The extract as Overpass-style elements (type, id, tags, geometry / members with geometry), in three passes:
    the wanted relations, the ways they or the map need, then just those ways' nodes. A node missing from the extract
    (a way cut at its edge) is skipped."""
    rels, member_ways = [], set()
    for _, a, tags, kids in elements(path, ("relation",)):
        if tags.get("type") == "multipolygon" and wanted(tags):
            rels.append((int(a["id"]), tags, [(int(m["ref"]), m.get("role", "")) for m in kids if m.get("type") == "way"]))
            member_ways.update(r for r, _ in rels[-1][2])
    ways, need = {}, set()
    for _, a, tags, kids in elements(path, ("way",)):
        wid = int(a["id"])
        if wanted(tags) or wid in member_ways:
            refs = [int(k["ref"]) for k in kids]
            ways[wid] = (tags, refs)
            need.update(refs)
    nodes = {}
    for _, a, _, _ in elements(path, ("node",)):
        nid = int(a["id"])
        if nid in need:
            nodes[nid] = (float(a["lat"]), float(a["lon"]))

    def geom(refs):
        return [{"lat": nodes[r][0], "lon": nodes[r][1]} for r in refs if r in nodes]

    def inside(g):
        s, w, n, e = BOX
        return any(s <= p["lat"] <= n and w <= p["lon"] <= e for p in g)

    areas, roads = [], []
    for wid, (tags, refs) in sorted(ways.items()):
        if not wanted(tags):
            continue
        g = geom(refs)
        if len(g) < 2 or (tags.get("natural") != "coastline" and not inside(g)):
            continue
        (roads if tags.get("highway") in ROAD_CLASS else areas).append({"type": "way", "id": wid, "tags": tags, "geometry": g})
    for rid, tags, members in rels:
        ms = [{"type": "way", "ref": r, "role": role, "geometry": geom(ways[r][1])} for r, role in members if r in ways]
        if any(inside(m["geometry"]) for m in ms):
            areas.append({"type": "relation", "id": rid, "tags": tags, "members": ms})
    return areas, roads, SNAPSHOT


def build_features(area_elements, road_elements, near=None):
    """The map's features, sorted so the output is stable. near(lat, lon) -> bool, when given, keeps a side street
    only if some point of it passes it."""
    feats, stats = [], Counter()
    coast, islands, seen, raw = [], [], set(), defaultdict(list)
    for e in area_elements:
        key = (e["type"], e["id"])
        if key in seen:
            continue
        seen.add(key)
        tags = e.get("tags", {})
        if e["type"] == "way" and tags.get("natural") == "coastline":
            coast.append(pts(e["geometry"]))
            continue
        if e["type"] == "relation" and tags.get("natural") == "water" and tags.get("name") == "Lake Michigan":
            for m in e.get("members", []):
                g = pts(m.get("geometry") or [])
                if len(g) >= 2:
                    (islands if m.get("role") == "inner" else coast).append(g)
            continue
        if e["type"] == "way" and tags.get("waterway") in ("river", "canal"):
            if road_class({**tags, "highway": "primary"}) is None:   # a culvert or tunnel: under ground
                continue
            r = simplified(pts(e["geometry"]), False)
            if r:
                feats.append((WATER, LINE, [r], key))
            continue
        cls = area_class(tags)
        if cls is None:
            continue
        if e["type"] == "way":
            ring = pts(e["geometry"])
            if len(ring) < 4 or ring[0] != ring[-1]:
                stats["open area way"] += 1
                continue
            rings = [ring]
        else:
            if tags.get("type") not in ("multipolygon", None):
                continue
            ms = [m for m in e.get("members", []) if m.get("type") == "way" and m.get("geometry")]
            outer, dropped, opens = join_rings([pts(m["geometry"]) for m in ms if m.get("role") != "inner"], with_open=True)
            stitched = stitch_at_edge(opens)
            if stitched:
                outer.append(stitched)
                stats["outer closed along the extract's edge"] += 1
            elif opens:
                stats["open outer ring dropped"] += dropped
            if not outer:   # its inner rings alone would fill as if they were the area
                stats["relation with no outer ring"] += 1
                continue
            inner, dropped = join_rings([pts(m["geometry"]) for m in ms if m.get("role") == "inner"])
            stats["open inner ring dropped"] += dropped
            rings = outer + inner
        if not rings:
            continue
        span = max(meters((min(p[0] for r in rings for p in r), min(p[1] for r in rings for p in r)),
                          (max(p[0] for r in rings for p in r), max(p[1] for r in rings for p in r))), 0)
        if span > 200_000:   # Lake Michigan itself as a water area: the coastline covers it
            stats["huge area skipped"] += 1
            continue
        rs = [s for s in (simplified(r, True) for r in rings) if s]
        if rs:
            feats.append((cls, AREA, rs, key))
    rings, extra = lake(coast, islands)
    stats["extra coastline chains"] = extra
    feats.append((WATER, AREA, [s for s in (simplified(r, True) for r in rings) if s], ("lake", 0)))
    for e in road_elements:
        key = (e["type"], e["id"])
        if e["type"] != "way" or key in seen:
            continue
        seen.add(key)
        cls = road_class(e.get("tags", {}))
        if cls is None:
            continue
        line = pts(e["geometry"])
        if cls == SIDE and near is not None and not any(near(*p) for p in line):
            stats["side street far from stops"] += 1
            continue
        raw[cls].append([tuple(p) for p in line])
    for cls in sorted(raw):
        for chain in strokes(raw[cls]):
            r = simplified(chain, False)
            if r:
                feats.append((cls, LINE, [r], None))
    out = sorted(((c, k, r) for c, k, r, _ in feats), key=tile_order)
    return out, stats


def near_stops(stops, km=1.0):
    """near(lat, lon): within km of a CTA stop (a grid of cells about km on a side)."""
    cell = defaultdict(list)
    ky = 111.32
    kx = ky * math.cos(math.radians(41.88))
    for s in stops:
        cell[(int(s["lat"] * ky // km), int(s["lon"] * kx // km))].append((s["lat"], s["lon"]))

    def near(lat, lon):
        i, j = int(lat * ky // km), int(lon * kx // km)
        for di in (-1, 0, 1):
            for dj in (-1, 0, 1):
                for a, b in cell.get((i + di, j + dj), ()):
                    if math.hypot((lat - a) * ky, (lon - b) * kx) <= km:
                        return True
        return False
    return near


def transit(gtfs):
    """Every L shape at the map's precision, and the 22's most common southbound shape."""
    gtfs_build.SIMPLIFY_M = SIMPLIFY_M
    out = gtfs_build.build(gtfs)
    lines = [{"route": s["route"], "points": s["points"]} for s in out.shapes]
    counts = Counter(t["shape_id"] for t in gtfs_build.rows(gtfs, "trips.txt")
                     if t["route_id"] == "22" and t.get("direction") == "South")
    shape_id = counts.most_common(1)[0][0]
    path = sorted((int(p["shape_pt_sequence"]), float(p["shape_pt_lat"]), float(p["shape_pt_lon"]))
                  for p in gtfs_build.rows(gtfs, "shapes.txt") if p["shape_id"] == shape_id)
    bus = [[round(a, 5), round(b, 5)] for a, b in gtfs_build.simplify([(a, b) for _, a, b in path], SIMPLIFY_M)]
    return lines, bus


def script(blob, stamp, lines, bus, counts):
    b64 = base64.b64encode(blob).decode()
    wrapped = "\n".join(b64[i:i + 120] for i in range(0, len(b64), 120))
    head = (f"// Generated by tools/basemap_build.py. Do not edit.\n"
            f"// Map data (c) OpenStreetMap contributors, ODbL 1.0 (https://www.openstreetmap.org/copyright),\n"
            f"// snapshot {stamp}. L and bus shapes from CTA's GTFS feed.\n"
            f"// {len(blob)} bytes packed: {counts}.\n")
    body = {"origin": list(ORIGIN), "snapshot": stamp, "lines": lines, "bus22": bus}
    js = json.dumps(body, separators=(",", ":"))
    return head + f"const CHI_BASEMAP = {js[:-1]},\"data\":`\n{wrapped}\n`}};\n"


def main(argv=None):
    ap = argparse.ArgumentParser(description="Writes docs/ideas/2026-11-12-chicago-basemap.js from data/osm/ and GTFS.")
    ap.add_argument("gtfs", nargs="?", default=GTFS, help="CTA's google_transit.zip")
    args = ap.parse_args(argv)
    cache = OSM + ".layers.json.gz"   # the parsed extract, so a rebuild skips the minute-long scan
    if os.path.exists(cache) and os.path.getmtime(cache) > os.path.getmtime(OSM):
        with gzip.open(cache, "rt") as fh:
            areas, roads, stamp = json.load(fh)
    else:
        areas, roads, stamp = load_layers(OSM)
        with gzip.open(cache, "wt") as fh:
            json.dump([areas, roads, stamp], fh)
    feats, stats = build_features(areas, roads)
    blob = pack(feats)
    if len(blob) > BUDGET:
        print(f"basemap_build: {len(blob)} bytes is over {BUDGET}; keeping side streets within 1 km of a stop")
        with open(os.path.join(ROOT, "data", "stops.json")) as fh:
            feats, stats = build_features(areas, roads, near_stops(json.load(fh)))
        blob = pack(feats)
        if len(blob) > BUDGET:
            sys.exit(f"basemap_build: still {len(blob)} bytes, over the {BUDGET} budget")
    lines, bus = transit(args.gtfs)
    counts = ", ".join(f"{n} {name}" for name, n in sorted(Counter(
        {1: "green", 2: "sand", 3: "water", 4: "side", 5: "main", 6: "expressway"}[c] for c, _, _ in feats).items()))
    with open(OUT, "w") as fh:
        fh.write(script(blob, stamp, lines, bus, counts))
    print(f"basemap_build: {len(blob)} bytes ({counts}); {dict(stats)}")
    print(f"basemap_build: wrote {os.path.relpath(OUT, ROOT)}")


if __name__ == "__main__":
    main()
