"""Tests for basemap_build.py: the pixel map's Chicago, from OpenStreetMap into the mockups' data script."""
import gzip
import os
import sys
import tempfile
import unittest

sys.path.insert(0, os.path.dirname(__file__))
import basemap_build as bb  # noqa: E402


def unpack(blob):
    """The inverse of bb.pack, for the round-trip test."""
    assert blob[:4] == b"CBM2"
    p = 4

    def vint():
        nonlocal p
        v, s = 0, 0
        while True:
            c = blob[p]
            p += 1
            v |= (c & 0x7F) << s
            s += 7
            if not c & 0x80:
                return -(v + 1) // 2 if v & 1 else v // 2
    out, la, lo = [], 0, 0
    for _ in range(vint()):
        cls, kind = blob[p], blob[p + 1]
        p += 2
        for _ in range(vint()):
            rings = []
            for _ in range(vint() if kind == bb.AREA else 1):
                ring = []
                for _ in range(vint()):
                    la += vint()
                    lo += vint()
                    ring.append((la, lo))
                rings.append(ring)
            out.append((cls, kind, rings))
    return out


def way(i, tags, pts):
    return {"type": "way", "id": i, "tags": tags, "geometry": [{"lat": a, "lon": b} for a, b in pts]}


# A shore running north (land on the left, so the lake is east), with an island off it.
COAST = [way(1, {"natural": "coastline"}, [(41.80, -87.60), (41.90, -87.62)]),
         way(2, {"natural": "coastline"}, [(41.90, -87.62), (42.00, -87.66)]),
         way(3, {"natural": "coastline"}, [(41.85, -87.55), (41.85, -87.54), (41.86, -87.54), (41.85, -87.55)])]


class ClassesTest(unittest.TestCase):
    def test_roads(self):
        self.assertEqual(bb.road_class({"highway": "motorway"}), bb.EXPRESS)
        self.assertEqual(bb.road_class({"highway": "trunk"}), bb.EXPRESS)
        self.assertEqual(bb.road_class({"highway": "secondary"}), bb.MAIN)
        self.assertEqual(bb.road_class({"highway": "residential"}), bb.SIDE)
        self.assertIsNone(bb.road_class({"highway": "motorway_link"}))
        self.assertIsNone(bb.road_class({"highway": "service"}))
        self.assertIsNone(bb.road_class({"highway": "footway"}))

    def test_hidden_roads_are_left_out(self):
        self.assertIsNone(bb.road_class({"highway": "primary", "tunnel": "yes"}))       # Lower Wacker, say
        self.assertIsNone(bb.road_class({"highway": "primary", "layer": "-1"}))
        self.assertIsNone(bb.road_class({"highway": "residential", "access": "private"}))
        self.assertEqual(bb.road_class({"highway": "primary", "bridge": "yes", "layer": "1"}), bb.MAIN)

    def test_areas(self):
        self.assertEqual(bb.area_class({"leisure": "park"}), bb.GREEN)
        self.assertEqual(bb.area_class({"landuse": "cemetery"}), bb.GREEN)
        self.assertEqual(bb.area_class({"natural": "beach"}), bb.SAND)
        self.assertEqual(bb.area_class({"natural": "water", "water": "river"}), bb.WATER)
        self.assertIsNone(bb.area_class({"building": "yes"}))


class GeometryTest(unittest.TestCase):
    def test_join_rings(self):
        a, b = [(0, 0), (0, 0.001), (0.001, 0.001)], [(0.001, 0.001), (0.001, 0), (0, 0)]
        rings, dropped = bb.join_rings([a, list(reversed(b))])
        self.assertEqual((len(rings), dropped), (1, 0))
        self.assertEqual(rings[0][0], rings[0][-1])

    def test_a_ring_left_wide_open_is_dropped(self):
        rings, dropped = bb.join_rings([[(0, 0), (0, 0.01), (0.01, 0.01), (0.01, 0)]])
        self.assertEqual((rings, dropped), ([], 1))

    def test_lake_is_closed_out_east_with_islands(self):
        rings, extra = bb.lake([[(g["lat"], g["lon"]) for g in w["geometry"]] for w in COAST])
        self.assertEqual(extra, 0)
        self.assertEqual(len(rings), 2)
        shore = rings[0]
        self.assertEqual(shore[0], shore[-1])
        self.assertIn((42.00, bb.FAR_EAST), shore)
        self.assertEqual(rings[1][0], rings[1][-1])

    def test_a_shore_running_south_is_turned_around(self):
        rings, _ = bb.lake([[(42.0, -87.66), (41.9, -87.62)], [(41.9, -87.62), (41.8, -87.60)]],
                           [[(41.85, -87.55), (41.85, -87.54), (41.86, -87.54), (41.85, -87.55)]])
        self.assertEqual(rings[0][0], (41.8, -87.60))
        self.assertEqual(len(rings), 2)   # the shore, and the island as its own ring

    def test_simplified_within_two_meters(self):
        # a straight street with a 1 m wiggle collapses to its ends; a 5 m bend stays
        wiggle = [(41.9, -87.65), (41.9 + 0.5e-5, -87.649), (41.9, -87.648)]
        self.assertEqual(len(bb.simplified(wiggle, False)), 2)
        bend = [(41.9, -87.65), (41.9 + 4.5e-5, -87.649), (41.9, -87.648)]
        self.assertEqual(len(bb.simplified(bend, False)), 3)


class EdgeTest(unittest.TestCase):
    def test_banks_cut_by_the_edge_close_along_it(self):
        n = bb.EXTRACT[2] - 0.001   # both banks run off the north edge and the south edge
        s = bb.EXTRACT[0] + 0.001
        west, east = [(s, -87.85), (41.9, -87.851), (n, -87.85)], [(s, -87.849), (41.9, -87.848), (n, -87.849)]
        ring = bb.stitch_at_edge([west, east])
        self.assertEqual(ring[0], ring[-1])
        self.assertEqual(len(ring), 7)

    def test_a_gap_inside_the_map_stays_open(self):
        self.assertIsNone(bb.stitch_at_edge([[(41.9, -87.7), (41.91, -87.7), (41.91, -87.69)]]))

    def test_inner_rings_without_their_outer_are_dropped(self):
        hole = [{"lat": a, "lon": b} for a, b in [(41.92, -87.64), (41.92, -87.63), (41.93, -87.63), (41.92, -87.64)]]
        wood = {"type": "relation", "id": 9, "tags": {"type": "multipolygon", "natural": "wood"},
                "members": [{"type": "way", "ref": 1, "role": "inner", "geometry": hole}]}
        feats, stats = bb.build_features(COAST + [wood], [])
        self.assertEqual([c for c, _, _ in feats], [bb.WATER])
        self.assertEqual(stats["relation with no outer ring"], 1)

    def test_culverts_are_left_out(self):
        creek = way(30, {"waterway": "river", "tunnel": "culvert"}, [(41.9, -87.8), (41.91, -87.8)])
        feats, _ = bb.build_features(COAST + [creek], [])
        self.assertEqual(len(feats), 1)   # just the lake


class StrokesTest(unittest.TestCase):
    def test_a_street_runs_on_through_a_crossing(self):
        a, b = (41.90, -87.70), (41.90, -87.69)
        c, d = (41.90, -87.68), (41.91, -87.69)
        strokes = bb.strokes([[a, b], [b, c], [b, d]])   # east-west through b, and a side street north from b
        self.assertEqual(sorted(len(s) for s in strokes), [2, 3])
        self.assertIn([a, b, c], [s if s[0] == a else s[::-1] for s in strokes])


class BuildTest(unittest.TestCase):
    def features(self):
        roads = [way(10, {"highway": "primary"}, [(41.90, -87.70), (41.90, -87.65)]),
                 way(11, {"highway": "primary", "tunnel": "yes"}, [(41.88, -87.64), (41.89, -87.63)]),
                 way(12, {"highway": "residential"}, [(41.91, -87.70), (41.91, -87.69)])]
        park = way(20, {"leisure": "park"}, [(41.92, -87.64), (41.92, -87.63), (41.93, -87.63), (41.92, -87.64)])
        feats, stats = bb.build_features(COAST + [park], roads)
        return feats

    def test_classes_and_tunnels(self):
        classes = sorted(c for c, _, _ in self.features())
        self.assertEqual(classes, [bb.GREEN, bb.WATER, bb.SIDE, bb.MAIN])   # the tunnel is gone, the lake is there

    def test_pack_round_trips_and_is_stable(self):
        feats = self.features()
        blob = bb.pack(feats)
        self.assertEqual(blob, bb.pack(self.features()))
        back = unpack(blob)
        self.assertEqual([(c, k, len(r)) for c, k, r in back], [(c, k, len(r)) for c, k, r in feats])
        for (_, _, rings), (_, _, orig) in zip(back, feats):
            for ring, o in zip(rings, orig):
                self.assertEqual(ring, [(bb.q(a, bb.ORIGIN[0]), bb.q(b, bb.ORIGIN[1])) for a, b in o])


XML = """<?xml version='1.0' encoding='UTF-8'?>
<osm version="0.6" generator="test">
  <node id="1" lat="41.9" lon="-87.7"/>
  <node id="2" lat="41.9" lon="-87.69"/>
  <node id="3" lat="41.91" lon="-87.69"/>
  <node id="4" lat="41.91" lon="-87.7"/>
  <node id="9" lat="41.0" lon="-80.0"/>
  <way id="100">
    <nd ref="1"/>
    <nd ref="2"/>
    <tag k="highway" v="residential"/>
    <tag k="name" v="Fish &amp; Chips Ave"/>
  </way>
  <way id="101">
    <nd ref="2"/>
    <nd ref="3"/>
    <nd ref="4"/>
  </way>
  <way id="102">
    <nd ref="4"/>
    <nd ref="1"/>
    <nd ref="2"/>
  </way>
  <relation id="500">
    <member type="way" ref="101" role="outer"/>
    <member type="way" ref="102" role="outer"/>
    <tag k="type" v="multipolygon"/>
    <tag k="leisure" v="park"/>
  </relation>
</osm>
"""


class XmlTest(unittest.TestCase):
    def test_load_layers(self):
        with tempfile.TemporaryDirectory() as d:
            path = os.path.join(d, "t.osm.gz")
            with gzip.open(path, "wt", encoding="utf-8") as fh:
                fh.write(XML)
            areas, roads, _ = bb.load_layers(path)
        self.assertEqual([r["id"] for r in roads], [100])
        self.assertEqual(roads[0]["tags"]["name"], "Fish & Chips Ave")
        self.assertEqual(len(roads[0]["geometry"]), 2)
        rel = [a for a in areas if a["type"] == "relation"]
        self.assertEqual(len(rel), 1)
        rings, dropped = bb.join_rings([[(g["lat"], g["lon"]) for g in m["geometry"]] for m in rel[0]["members"]])
        self.assertEqual((len(rings), dropped), (1, 0))


if __name__ == "__main__":
    unittest.main()
