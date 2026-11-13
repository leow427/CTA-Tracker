"""Tests for map_build.py: the dock map's train tracks and platform positions, from data/ into C++."""
import os
import re
import sys
import unittest

sys.path.insert(0, os.path.dirname(__file__))
import map_build  # noqa: E402

SHAPES = [
    {"route": "Red", "points": [[41.99367, -87.65921], [41.93866, -87.65337]]},
    {"route": "P", "points": [[41.1, -87.1], [41.2, -87.2], [41.3, -87.3]]},
    {"route": "P", "points": [[42.0, -87.0], [42.1, -87.1]]},
]
LINES = [
    {"route": "Red", "dir": "95th/Dan Ryan", "lead": 0,
     "stops": [{"id": "30256", "name": "Belmont"}, {"id": "30274", "name": "Fullerton"}]},
    {"route": "Red", "dir": "Howard", "lead": 0,
     "stops": [{"id": "30255", "name": "Belmont"}]},
]
STOPS = [
    {"id": "41320", "name": "Belmont", "lat": 41.939751, "lon": -87.65338, "kind": "station",
     "platforms": [{"id": "30255", "lines": []}, {"id": "30256", "lines": []}]},
    {"id": "41220", "name": "Fullerton", "lat": 41.925051, "lon": -87.652866, "kind": "station",
     "platforms": [{"id": "30274", "lines": []}]},
    {"id": "1828", "name": "Clark & Belmont", "lat": 41.94, "lon": -87.65, "kind": "bus", "lines": []},
]


class MapBuildTest(unittest.TestCase):
    def test_shapes_fixed_point_in_order(self):
        src = map_build.cpp_source(SHAPES, LINES, STOPS)
        self.assertIn("{4199367, -8765921}, {4193866, -8765337}", src)
        # Each run of a line is its own shape, in the file's order.
        runs = re.findall(r'\{"(\w+)", kShape\d+, (\d+)\}', src)
        self.assertEqual([("Red", "2"), ("P", "3"), ("P", "2")], runs)
        self.assertIn("const size_t kTrainShapeCount = 3;", src)

    def test_platforms_sorted_with_station_positions(self):
        src = map_build.cpp_source(SHAPES, LINES, STOPS)
        ids = re.findall(r'\{"(3\d{4})", (-?\d+), (-?\d+)\}', src)
        self.assertEqual(["30255", "30256", "30274"], [i for i, _, _ in ids])
        self.assertEqual(("30256", "4193975", "-8765338"), ids[1])
        self.assertIn("const size_t kPlatformCount = 3;", src)

    def test_platform_missing_from_stops_fails(self):
        lines = LINES + [{"route": "Y", "dir": "Skokie", "lead": 0, "stops": [{"id": "39999", "name": "Nowhere"}]}]
        with self.assertRaises(SystemExit):
            map_build.cpp_source(SHAPES, lines, STOPS)

    def test_real_data_has_every_line_and_the_dock_defaults(self):
        root = os.path.join(os.path.dirname(__file__), "..", "data")
        src = map_build.cpp_source(*map_build.load(root))
        for route in ["Red", "Blue", "Brn", "G", "Org", "Pink", "P", "Y"]:
            self.assertIn('{"%s", kShape' % route, src)
        for platform in ["30256", "30104"]:   # Red at Belmont to 95th, Brown at Diversey to the Loop
            self.assertIn('{"%s", ' % platform, src)


if __name__ == "__main__":
    unittest.main()
