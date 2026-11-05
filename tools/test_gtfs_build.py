"""Tests for gtfs_build.py against fixtures/gtfs-mini (plan 1, task 10)."""
import json
import os
import shutil
import sys
import tempfile
import unittest
import zipfile

sys.path.insert(0, os.path.dirname(__file__))
import gtfs_build  # noqa: E402

MINI = os.path.join(os.path.dirname(__file__), "..", "fixtures", "gtfs-mini")


def feed_copy(test):
    """A temp copy of the mini feed, removed after the test."""
    tmp = tempfile.mkdtemp()
    test.addCleanup(shutil.rmtree, tmp)
    for name in os.listdir(MINI):
        shutil.copy(os.path.join(MINI, name), tmp)
    return tmp


class GtfsBuildTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.out = gtfs_build.build(MINI)

    def line(self, route, label):
        return next(l for l in self.out.lines if l.route == route and l.dir == label)

    def test_most_common_pattern(self):
        charlie = self.line("Red", "Charlie")
        self.assertEqual(charlie.stop_ids, ["30001", "30002", "30003"])
        self.assertEqual(charlie.names, ["Alpha", "Bravo", "Charlie"])

    def test_both_directions(self):
        self.assertEqual(sorted(l.dir for l in self.out.lines if l.route == "Red"), ["Alpha", "Charlie"])
        self.assertEqual(self.line("Red", "Alpha").stop_ids, ["30013", "30012", "30011"])

    def test_stops_kinds(self):
        stops = self.out.stops
        self.assertEqual(sum(s["kind"] == "station" for s in stops), 3)
        self.assertEqual(sum(s["kind"] == "bus" for s in stops), 3)
        self.assertFalse([s for s in stops if s["id"].startswith("3")])
        ids = {s["id"] for s in stops}
        self.assertIn("1003", ids)        # a bus bay parented to a station (95th/Dan Ryan, Jefferson Park)
        self.assertNotIn("1009", ids)     # a bus stop no trip serves
        self.assertNotIn("40004", ids)    # a station with no served platform

    def test_stop_lines(self):
        by_id = {s["id"]: s for s in self.out.stops}
        self.assertEqual(by_id["1001"]["lines"], [{"route": "22", "dir": "Northbound"}])
        self.assertEqual(by_id["40002"]["platforms"], [
            {"id": "30002", "lines": [{"route": "Red", "dir": "Charlie"}]},
            {"id": "30012", "lines": [{"route": "Red", "dir": "Alpha"}]},
        ])
        self.assertNotIn("platforms", by_id["1001"])
        self.assertNotIn("lines", by_id["40002"])
        self.assertEqual(by_id["40002"]["name"], "Bravo")
        self.assertAlmostEqual(by_id["40002"]["lat"], 41.91)

    def test_train_shapes(self):
        self.assertEqual(len(self.out.shapes), 1)
        red = self.out.shapes[0]
        self.assertEqual(red["route"], "Red")
        self.assertEqual(red["points"], [[41.9, -87.65], [41.92, -87.65]])   # S1, middle point simplified away

    def test_train_lines_json(self):
        self.assertIn({"route": "Red", "dir": "Charlie",
                       "stops": [{"id": "30001", "name": "Alpha"}, {"id": "30002", "name": "Bravo"},
                                 {"id": "30003", "name": "Charlie"}], "lead": 0},
                      self.out.train_lines_json())

    def test_cpp_output(self):
        cpp = self.out.cpp()
        self.assertIn("kTrainLineCount = 2", cpp)
        self.assertIn('"30001", "30002", "30003"', cpp)
        self.assertIn('{"Red", "Charlie", kIds', cpp)
        self.assertIn(", 3, 0},", cpp)   # count, lead

    def test_reads_a_zip(self):
        tmp = tempfile.mkdtemp()
        self.addCleanup(shutil.rmtree, tmp)
        path = os.path.join(tmp, "feed.zip")
        with zipfile.ZipFile(path, "w") as z:
            for name in os.listdir(MINI):
                z.write(os.path.join(MINI, name), name)
        self.assertEqual(gtfs_build.build(path).stops, self.out.stops)

    def test_missing_direction_column_fails_loudly(self):
        tmp = tempfile.mkdtemp()
        self.addCleanup(shutil.rmtree, tmp)
        for name in os.listdir(MINI):
            shutil.copy(os.path.join(MINI, name), tmp)
        with open(os.path.join(tmp, "trips.txt")) as f:
            rows = [r.split(",") for r in f.read().splitlines()]
        col = rows[0].index("direction")
        with open(os.path.join(tmp, "trips.txt"), "w") as f:
            f.write("\n".join(",".join(r[:col] + r[col + 1:]) for r in rows) + "\n")
        with self.assertRaisesRegex(ValueError, "direction"):
            gtfs_build.build(tmp)

    def test_station_names_drop_line_lists_only(self):
        self.assertEqual(gtfs_build.station_name("Belmont (Red/Brown/Purple)"), "Belmont")
        self.assertEqual(gtfs_build.station_name("Addison (Blue)"), "Addison")
        self.assertEqual(gtfs_build.station_name("Harlem (Blue - Forest Park Branch)"), "Harlem (Blue - Forest Park Branch)")
        self.assertEqual(gtfs_build.station_name("Clark/Lake"), "Clark/Lake")
        self.assertEqual(self.line("Red", "Charlie").names[1], "Bravo")   # fixture: "Bravo (Red/Brown)"

    def test_labels_from_stop_headsigns_when_trips_have_none(self):
        # CTA's real trips.txt has no trip_headsign; stop_times.txt carries stop_headsign instead.
        tmp = tempfile.mkdtemp()
        self.addCleanup(shutil.rmtree, tmp)
        for name in os.listdir(MINI):
            shutil.copy(os.path.join(MINI, name), tmp)
        with open(os.path.join(MINI, "trips.txt")) as f:
            trips = [r.split(",") for r in f.read().splitlines()]
        col = trips[0].index("trip_headsign")
        headsign = {r[2]: r[col] for r in trips[1:]}
        with open(os.path.join(tmp, "trips.txt"), "w") as f:
            f.write("\n".join(",".join(r[:col] + r[col + 1:]) for r in trips) + "\n")
        with open(os.path.join(MINI, "stop_times.txt")) as f:
            times = [r.split(",") for r in f.read().splitlines()]
        hs = times[0].index("stop_headsign")
        for r in times[1:]:
            r[hs] = headsign[r[0]]
        with open(os.path.join(tmp, "stop_times.txt"), "w") as f:
            f.write("\n".join(",".join(r) for r in times) + "\n")
        out = gtfs_build.build(tmp)
        self.assertEqual(sorted((l.route, l.dir) for l in out.lines), [("Red", "Alpha"), ("Red", "Charlie")])

    def test_round_trip_lines_label_each_platform_by_its_headsign(self):
        # Brown, Orange, and Pink run each trip as one round trip in one direction_id (Kimball → Loop → Kimball).
        # Each platform must keep its own direction, or CardBuilder merges both Belmont Brown cards into one.
        tmp = tempfile.mkdtemp()
        self.addCleanup(shutil.rmtree, tmp)
        for name in os.listdir(MINI):
            shutil.copy(os.path.join(MINI, name), tmp)
        with open(os.path.join(tmp, "trips.txt"), "w") as f:
            f.write("route_id,service_id,trip_id,direction_id,shape_id,direction\n"
                    "Red,1,R1,1,S1,North\nRed,1,R2,1,S1,North\n22,1,B1,0,S9,North\n")
        loop = [("30001", "Charlie"), ("30002", "Charlie"), ("30003", "Alpha"), ("30012", "Alpha"), ("30011", "Alpha")]
        with open(os.path.join(tmp, "stop_times.txt"), "w") as f:
            f.write("trip_id,arrival_time,departure_time,stop_id,stop_sequence,stop_headsign\n")
            for trip in ("R1", "R2"):
                for n, (stop, head) in enumerate(loop, 1):
                    f.write(f"{trip},08:00:00,08:00:00,{stop},{n},{head}\n")
            f.write("B1,12:00:00,12:00:00,1001,1,Bravo\nB1,12:03:00,12:03:00,1002,2,Bravo\n")
        out = gtfs_build.build(tmp)
        by_id = {s["id"]: s for s in out.stops}
        self.assertEqual(by_id["40002"]["platforms"], [
            {"id": "30002", "lines": [{"route": "Red", "dir": "Charlie"}]},
            {"id": "30012", "lines": [{"route": "Red", "dir": "Alpha"}]},
        ])
        # Split where the platform labels change; the second line leads in with the platforms before it.
        self.assertEqual([(l.route, l.dir, l.stop_ids, l.lead) for l in out.lines], [
            ("Red", "Charlie", ["30001", "30002"], 0),
            ("Red", "Alpha", ["30001", "30002", "30003", "30012", "30011"], 2),
        ])
        self.assertEqual([s["route"] for s in out.shapes], ["Red"])   # no direction 0, so shape from direction 1
        self.assertIn('{"Red", "Alpha", kIds1, kNames1, 5, 2},', out.cpp())   # count, lead
        self.assertEqual([l["lead"] for l in out.train_lines_json()], [0, 2])
        # A platform held only inside another line's lead-in isn't pickable, and check() says so.
        charlie, alpha = out.lines
        only_lead = gtfs_build.Output(out.stops, [gtfs_build.TrainLine("Red", "Charlie", alpha.stop_ids[:3], alpha.names[:3], 2), alpha], out.shapes)
        self.assertIn("2 Red platform directions in stops.json have no sequence", gtfs_build.check(only_lead))

    def test_opposite_one_way_patterns_in_one_direction_id_both_count(self):
        # CTA files both directions of Purple (Linden↔Howard) and Yellow under direction_id 1.
        tmp = feed_copy(self)
        with open(os.path.join(tmp, "trips.txt")) as f:
            text = f.read()
        with open(os.path.join(tmp, "trips.txt"), "w") as f:
            f.write(text.replace("Red,1,T4,1,", "Red,1,T4,0,"))
        out = gtfs_build.build(tmp)
        self.assertEqual(sorted((l.dir, tuple(l.stop_ids)) for l in out.lines),
                         [("Alpha", ("30013", "30012", "30011")), ("Charlie", ("30001", "30002", "30003"))])

    def test_branch_stretch_gets_its_own_line_and_shape_without_a_lead_in(self):
        # Green's two southern branches: the second branch's platforms become their own line. No lead-in: half
        # the trains on the shared trunk never reach the branch, so their LEDs would light for nothing.
        tmp = feed_copy(self)
        with open(os.path.join(tmp, "stops.txt"), "a") as f:
            f.write("40005,,Echo,,41.915,-87.64,1,,1\n30099,,Echo (Bravo-bound),,41.915,-87.64,0,40005,1\n")
        with open(os.path.join(tmp, "stop_times.txt"), "a") as f:
            f.write("T3,10:04:00,10:04:00,30099,3,,0,0\n")   # T3 (Bravo, shape S2) now runs 30001, 30002, 30099
        out = gtfs_build.build(tmp)
        bravo = [(l.stop_ids, l.lead) for l in out.lines if l.dir == "Bravo"]
        self.assertEqual(bravo, [(["30099"], 0)])
        self.assertFalse([f for f in gtfs_build.check(out) if "platform directions" in f])
        # The branch reaches a station the first shape misses, so its shape is drawn too.
        self.assertEqual([(s["route"], len(s["points"])) for s in out.shapes], [("Red", 2), ("Red", 2)])
        self.assertEqual(out.shapes[1]["points"], [[41.9, -87.65], [41.91, -87.65]])   # S2

    def test_shared_trunk_gets_a_combined_label_and_branches_no_lead_in(self):
        # Green: Harlem/Lake to Garfield carries both branches about equally. An alphabetical tie-break would flip
        # every trunk platform's label with the next feed, so a near tie names both. Half the trunk's trains never
        # reach either branch, so neither branch leads in from the trunk.
        tmp = feed_copy(self)
        with open(os.path.join(tmp, "stops.txt"), "a") as f:
            f.write("40005,,Echo,,41.915,-87.64,1,,1\n30099,,Echo (Bravo-bound),,41.915,-87.64,0,40005,1\n")
        with open(os.path.join(tmp, "trips.txt"), "w") as f:
            f.write("route_id,service_id,trip_id,direction_id,shape_id,direction\n"
                    "Red,1,C1,0,S1,North\nRed,1,C2,0,S1,North\nRed,1,K1,0,S2,North\nRed,1,K2,0,S2,North\n"
                    "22,1,B1,0,S9,North\n")
        with open(os.path.join(tmp, "stop_times.txt"), "w") as f:
            f.write("trip_id,arrival_time,departure_time,stop_id,stop_sequence,stop_headsign\n")
            for trip, last, head in (("C1", "30003", "Charlie"), ("C2", "30003", "Charlie"),
                                     ("K1", "30099", "Bravo"), ("K2", "30099", "Bravo")):
                for n, stop in enumerate(("30001", "30002", last), 1):
                    f.write(f"{trip},08:00:00,08:00:00,{stop},{n},{head}\n")
            f.write("B1,12:00:00,12:00:00,1001,1,Bravo\n")
        out = gtfs_build.build(tmp)
        by_id = {s["id"]: s for s in out.stops}
        self.assertEqual(by_id["40002"]["platforms"], [{"id": "30002", "lines": [{"route": "Red", "dir": "Bravo & Charlie"}]}])
        self.assertEqual(sorted((l.dir, tuple(l.stop_ids), l.lead) for l in out.lines), [
            ("Bravo", ("30099",), 0), ("Bravo & Charlie", ("30001", "30002"), 0), ("Charlie", ("30003",), 0)])
        self.assertFalse([f for f in gtfs_build.check(out) if "shared-trunk" in f])   # 2 vs 2 is a clear tie, not near the cut

    def test_an_express_never_leads_in_from_shuttle_platforms(self):
        # One express trip runs past where the common trips end; its new stretch must not light for them.
        tmp = feed_copy(self)
        with open(os.path.join(tmp, "stops.txt"), "a") as f:
            f.write("40006,,Foxtrot,,41.94,-87.65,1,,1\n30050,,Foxtrot (Zulu-bound),,41.94,-87.65,0,40006,1\n"
                    "40007,,Golf,,41.95,-87.65,1,,1\n30051,,Golf (Zulu-bound),,41.95,-87.65,0,40007,1\n")
        with open(os.path.join(tmp, "trips.txt"), "a") as f:
            f.write("Red,1,X1,0,1,S1,North,1,X1,Zulu\n")
        with open(os.path.join(tmp, "stop_times.txt"), "a") as f:
            for n, stop in enumerate(("30001", "30002", "30003", "30050", "30051"), 1):
                f.write(f"X1,07:00:00,07:00:00,{stop},{n},,0,0\n")
        zulu = [(l.stop_ids, l.lead) for l in gtfs_build.build(tmp).lines if l.dir == "Zulu"]
        self.assertEqual(zulu, [(["30050", "30051"], 0)])

    def test_lead_in_is_six_platforms_at_a_direction_change(self):
        tmp = feed_copy(self)
        with open(os.path.join(tmp, "stops.txt"), "a") as f:
            for n in range(8):
                f.write(f"3002{n},,Lead {n},,41.90{n},-87.66,0,40001,1\n")
        with open(os.path.join(tmp, "trips.txt"), "w") as f:
            f.write("route_id,service_id,trip_id,direction_id,shape_id,direction\nRed,1,R1,1,S1,North\n22,1,B1,0,S9,North\n")
        trip = [(f"3002{n}", "Charlie") for n in range(8)] + [("30012", "Alpha"), ("30011", "Alpha")]
        with open(os.path.join(tmp, "stop_times.txt"), "w") as f:
            f.write("trip_id,arrival_time,departure_time,stop_id,stop_sequence,stop_headsign\n")
            for n, (stop, head) in enumerate(trip, 1):
                f.write(f"R1,08:00:00,08:00:00,{stop},{n},{head}\n")
            f.write("B1,12:00:00,12:00:00,1001,1,Bravo\n")
        alpha = next(l for l in gtfs_build.build(tmp).lines if l.dir == "Alpha")
        self.assertEqual(alpha.lead, 6)
        self.assertEqual(alpha.stop_ids, ["30022", "30023", "30024", "30025", "30026", "30027", "30012", "30011"])

    def test_check_reports_failures(self):
        failures = gtfs_build.check(self.out)   # the mini feed is nothing like the real one
        self.assertTrue(any("train routes" in f for f in failures))
        self.assertTrue(any("bus stops" in f for f in failures))
        self.assertFalse([f for f in failures if "platform directions" in f])   # every mini platform is pickable
        unpickable = gtfs_build.Output(self.out.stops, [], self.out.shapes)
        self.assertIn("6 Red platform directions in stops.json have no sequence", gtfs_build.check(unpickable))
        far = gtfs_build.Output(self.out.stops, self.out.lines, [{"route": "Red", "points": [[40.0, -87.6]]}])
        self.assertTrue(any("outside Chicago" in f for f in gtfs_build.check(far)))

    def test_simplify(self):
        # A 4 m bump off a straight line goes; a 50 m one stays.
        line = [(41.90, -87.65), (41.905, -87.65 + 4 / 83000), (41.91, -87.65)]
        self.assertEqual(len(gtfs_build.simplify(line, 5)), 2)
        line = [(41.90, -87.65), (41.905, -87.65 + 50 / 83000), (41.91, -87.65)]
        self.assertEqual(len(gtfs_build.simplify(line, 5)), 3)

    def test_written_files(self):
        tmp = tempfile.mkdtemp()
        self.addCleanup(shutil.rmtree, tmp)
        self.out.write(tmp)
        with open(os.path.join(tmp, "data", "stops.json")) as f:
            self.assertEqual(json.load(f), self.out.stops)
        for path in ["data/train_lines.json", "data/train_shapes.json", "firmware/lib/cta/src/train_sequences.cpp"]:
            self.assertTrue(os.path.exists(os.path.join(tmp, path)), path)


if __name__ == "__main__":
    unittest.main()
