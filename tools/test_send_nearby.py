"""Tests for send_nearby.py: the `nearby` message the laptop sends the handheld (spec 8.3)."""
import json
import os
import sys
import unittest

sys.path.insert(0, os.path.dirname(__file__))
import send_nearby  # noqa: E402

STOPS = [
    {"id": "40001", "name": "Far Station", "lat": 41.95, "lon": -87.65, "kind": "station", "platforms": []},
    {"id": "40002", "name": "Near Station", "lat": 41.9001, "lon": -87.65, "kind": "station", "platforms": []},
    {"id": "100", "name": "Bus A", "lat": 41.9002, "lon": -87.65, "kind": "bus", "lines": []},
    {"id": "101", "name": "Bus B", "lat": 41.9100, "lon": -87.65, "kind": "bus", "lines": []},
]


class SendNearbyTests(unittest.TestCase):
    def test_nearest_first_per_kind(self):
        self.assertEqual([s["id"] for s in send_nearby.nearest(STOPS, "station", 41.9, -87.65)], ["40002", "40001"])
        self.assertEqual([s["id"] for s in send_nearby.nearest(STOPS, "bus", 41.9, -87.65)], ["100", "101"])

    def test_bus_stops_cover_each_route_and_direction(self):
        stops = [
            {"id": "1", "name": "A", "lat": 41.9001, "lon": -87.65, "kind": "bus", "lines": [{"route": "22", "dir": "Northbound"}]},
            {"id": "2", "name": "B", "lat": 41.9002, "lon": -87.65, "kind": "bus", "lines": [{"route": "22", "dir": "Northbound"}]},
            {"id": "3", "name": "C", "lat": 41.9003, "lon": -87.65, "kind": "bus",
             "lines": [{"route": "22", "dir": "Northbound"}, {"route": "36", "dir": "Northbound"}]},
            {"id": "4", "name": "D", "lat": 41.9050, "lon": -87.65, "kind": "bus", "lines": [{"route": "22", "dir": "Southbound"}]},
        ]
        # B repeats A's only route; C is the nearest stop for the 36; D for the 22 southbound.
        self.assertEqual([s["id"] for s in send_nearby.bus_stops(stops, 41.9, -87.65)], ["1", "3", "4"])
        many = [{"id": str(i), "name": "S", "lat": 41.9 + i / 1000, "lon": -87.65, "kind": "bus",
                 "lines": [{"route": str(i), "dir": "Northbound"}]} for i in range(14)]
        self.assertEqual(len(send_nearby.bus_stops(many, 41.9, -87.65)), 5)

    def test_keeps_five(self):
        many = [dict(STOPS[0], id=str(40100 + i), lat=41.9 + i / 1000) for i in range(14)]
        self.assertEqual(len(send_nearby.nearest(many, "station", 41.9, -87.65)), 5)

    def test_message_ranks_close_across_kinds(self):
        trains = send_nearby.nearest(STOPS, "station", 41.9, -87.65)
        buses = send_nearby.nearest(STOPS, "bus", 41.9, -87.65)
        msg = send_nearby.message(41.9, -87.65, trains, buses)
        self.assertEqual(msg["trains"], ["40002", "40001"])
        self.assertEqual(msg["buses"], ["100", "101"])
        self.assertEqual(msg["close"], ["40002", "100", "101"])
        line = send_nearby.encode(msg)
        self.assertTrue(line.startswith("N {") and line.endswith("}\n"))
        self.assertEqual(json.loads(line[2:]), msg)

    def test_real_stops_fit_the_handhelds_line(self):
        with open(send_nearby.STOPS) as f:
            stops = json.load(f)
        send_nearby.self_check(stops)
        lat, lon = 41.8786, -87.6251   # the Loop: the densest stops, the longest IDs
        msg = send_nearby.message(lat, lon, send_nearby.nearest(stops, "station", lat, lon),
                                  send_nearby.bus_stops(stops, lat, lon))
        self.assertLess(len(send_nearby.encode(msg)), send_nearby.LINE_MAX)
        self.assertEqual(len(msg["trains"]), 5)
        self.assertEqual(len(msg["buses"]), 5)

    def test_too_long_is_refused(self):
        with self.assertRaises(ValueError):
            send_nearby.encode({"x": "y" * 700})


if __name__ == "__main__":
    unittest.main()
