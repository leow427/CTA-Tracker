"""Tests for link_send.py: the request builder and the fake board's answers (ecosystem spec 4.4). Offline: the fake
board's handler is called directly, with no sockets."""
import io
import json
import os
import sys
import unittest

sys.path.insert(0, os.path.dirname(__file__))
import link_send  # noqa: E402

TOKEN = "0123456789abcdef0123456789abcdef"   # a test value, not a real household token


def call(board, method, path, body=b"", token=TOKEN):
    headers = {"Content-Length": str(len(body))}
    if token is not None:
        headers["Authorization"] = "Bearer " + token
    return board.handle(method, path, headers, io.BytesIO(body).read)


class Clock:
    def __init__(self):
        self.t = 1000.0

    def __call__(self):
        return self.t


class GarbageTests(unittest.TestCase):
    def test_each_garbage_case_gets_its_status(self):
        cases = link_send.garbage_cases(TOKEN)
        self.assertEqual([c[3] for c in cases], [400, 413, 401, 401])
        for kind in ("dock", "handheld"):
            board = link_send.FakeBoard(kind, TOKEN)
            before = dict(board.config)
            for name, body, tok, want in cases:
                with self.subTest(kind=kind, case=name):
                    self.assertEqual(call(board, "POST", "/config", body, tok)[0], want)
            self.assertEqual(board.config, before)   # none of them changed anything
            self.assertEqual(call(board, "POST", "/config", b"{}")[0], 200)   # and {} with the token is fine

    def test_oversize_is_refused_before_reading(self):
        board = link_send.FakeBoard("handheld", TOKEN)
        def never(n):
            raise AssertionError("read the body")
        status, _ = board.handle("POST", "/pin", {"Content-Length": "2048", "Authorization": "Bearer " + TOKEN}, never)
        self.assertEqual(status, 413)

    def test_wrong_token_differs_from_token(self):
        self.assertNotEqual(link_send.other_token(TOKEN), TOKEN)
        self.assertNotEqual(link_send.other_token("0" * 32), "0" * 32)


class FakeBoardTests(unittest.TestCase):
    def test_info_needs_no_token_and_shows_reboots(self):
        clock = Clock()
        board = link_send.FakeBoard("handheld", TOKEN, now=clock)
        clock.t += 42
        status, info = call(board, "GET", "/info", token=None)
        self.assertEqual(status, 200)
        self.assertEqual(info["type"], "handheld")
        self.assertEqual(info["uptime"], 42)
        for key in ("id", "fw", "battery", "charging", "wifi", "last_update", "picks_ver", "wake", "docked", "paired"):
            self.assertIn(key, info)
        self.assertEqual(call(board, "POST", "/reset", b"{}")[0], 200)
        self.assertEqual(call(board, "GET", "/info")[1]["uptime"], 0)

    def test_pin_follow_and_clear(self):
        board = link_send.FakeBoard("handheld", TOKEN)
        pin = json.dumps(link_send.DEFAULT_PIN).encode()
        self.assertEqual(call(board, "POST", "/pin", pin)[0], 200)
        self.assertEqual(board.pin["id"], "41320")
        self.assertEqual(call(board, "POST", "/follow", pin)[0], 400)   # no vehicle
        follow = dict(link_send.DEFAULT_PIN, vehicle="812", dest="95th/Dan Ryan")
        self.assertEqual(call(board, "POST", "/follow", json.dumps(follow).encode())[0], 200)
        self.assertEqual(board.follow["vehicle"], "812")
        self.assertEqual(call(board, "DELETE", "/follow")[0], 200)
        self.assertIsNone(board.follow)
        self.assertEqual(call(board, "DELETE", "/pin", token=None)[0], 401)
        self.assertEqual(call(board, "DELETE", "/pin")[0], 200)
        self.assertIsNone(board.pin)

    def test_dock_stops_raise_picks_ver(self):
        board = link_send.FakeBoard("dock", TOKEN)
        self.assertIsNone(call(board, "GET", "/info")[1]["battery"])
        stops = {"stops": [link_send.DEFAULT_PIN, {}, {}]}
        self.assertEqual(call(board, "POST", "/config", json.dumps(stops).encode())[0], 200)
        self.assertEqual(call(board, "GET", "/info")[1]["picks_ver"], 1)
        self.assertEqual(call(board, "GET", "/config")[1]["stops"][0]["id"], "41320")
        self.assertEqual(call(board, "POST", "/pin", b"{}")[0], 404)   # the dock has no pin

    def test_config_subset_keeps_the_rest(self):
        board = link_send.FakeBoard("handheld", TOKEN)
        pet = {"pet": {"name": "Trackie", "colors": [2976742, 8238335]}}
        settings = {"settings": {"screen": 80, "led": 40, "night_start": 60, "night_end": 300}}
        call(board, "POST", "/config", json.dumps(pet).encode())
        call(board, "POST", "/config", json.dumps(settings).encode())
        cfg = call(board, "GET", "/config")[1]
        self.assertEqual(cfg, {**pet, **settings})   # and no stops on the wearable


class RejectsWhatTheBoardsRejectTests(unittest.TestCase):
    """One bad body per rule in link_msg.h: 400, and nothing changes."""

    def assertRejected(self, board, path, msg):
        before = (dict(board.config), board.pin, board.follow, board.picks_ver)
        self.assertEqual(call(board, "POST", path, json.dumps(msg).encode())[0], 400, msg)
        self.assertEqual((dict(board.config), board.pin, board.follow, board.picks_ver), before)

    def test_stop_rules(self):
        board = link_send.FakeBoard("handheld", TOKEN)
        for change in ({"type": "boat"}, {"route": ""}, {"route": "Redddddd"}, {"id": ""}, {"id": "41320123"},
                       {"dir": None}, {"name": 5}):
            with self.subTest(change):
                self.assertRejected(board, "/pin", {**link_send.DEFAULT_PIN, **change})
        self.assertRejected(board, "/pin", {k: v for k, v in link_send.DEFAULT_PIN.items() if k != "dir"})

    def test_follow_rules(self):
        board = link_send.FakeBoard("handheld", TOKEN)
        follow = dict(link_send.DEFAULT_PIN, vehicle="812", dest="95th/Dan Ryan")
        for change in ({"vehicle": ""}, {"vehicle": "12345678"}, {"vehicle": 812}):
            with self.subTest(change):
                self.assertRejected(board, "/follow", {**follow, **change})
        self.assertRejected(board, "/follow", {k: v for k, v in follow.items() if k != "dest"})

    def test_settings_rules(self):
        board = link_send.FakeBoard("dock", TOKEN)
        good = {"screen": 80, "led": 40, "night_start": 60, "night_end": 300}
        for change in ({"screen": 101}, {"led": -1}, {"night_start": 1440}, {"night_end": "300"}, {"led": True}):
            with self.subTest(change):
                self.assertRejected(board, "/config", {"settings": {**good, **change}})
        self.assertRejected(board, "/config", {"settings": {k: v for k, v in good.items() if k != "night_end"}})

    def test_pet_rules(self):
        board = link_send.FakeBoard("handheld", TOKEN)
        for pet in ({"name": 1, "colors": [1, 2]}, {"colors": [1, 2]}, {"name": "T", "colors": [1]},
                    {"name": "T", "colors": [1, 2, 3]}, {"name": "T", "colors": [1, 0x1000000]},
                    {"name": "T", "colors": [1, 2.5]}):
            with self.subTest(pet):
                self.assertRejected(board, "/config", {"pet": pet})

    def test_nearby_rules(self):
        board = link_send.FakeBoard("handheld", TOKEN)
        good = {"lat": 41.94, "lon": -87.65, "trains": ["41320"], "buses": ["1828"], "close": ["41320"]}
        self.assertEqual(call(board, "POST", "/nearby", json.dumps(good).encode())[0], 200)
        bad = [
            {"trains": ["41320", "41320"], "close": []},                     # a duplicate station
            {"trains": ["30256"], "close": []},                              # a platform, not a station
            {"close": ["1"]},                                                # close not in either list
            {"trains": [str(40000 + i) for i in range(6)], "close": []},     # 6 trains
            {"buses": ["12a"]}, {"buses": [1828]}, {"buses": ["12345678"]},  # not digits, a number, too long
            {"close": ["41320", "1828", "41320"]}, {"lat": 91}, {"lat": True}, {"lon": "x"},
        ]
        for change in bad:
            with self.subTest(change):
                before = board.nearby
                self.assertEqual(call(board, "POST", "/nearby", json.dumps({**good, **change}).encode())[0], 400)
                self.assertIs(board.nearby, before)
        no_lat = {k: v for k, v in good.items() if k != "lat"}
        self.assertEqual(call(board, "POST", "/nearby", json.dumps(no_lat).encode())[0], 400)

    def test_empty_config_body_is_bad_json(self):
        board = link_send.FakeBoard("dock", TOKEN)
        self.assertEqual(call(board, "POST", "/config", b"")[0], 400)
        self.assertEqual(call(board, "POST", "/reset", b"")[0], 200)

    def test_stops_rules(self):
        board = link_send.FakeBoard("dock", TOKEN)
        self.assertRejected(board, "/config", {"stops": [{}]})
        self.assertRejected(board, "/config", {"stops": [{"id": "1", "type": "boat"}, {}, {}]})
        # one bad part fails the whole body: the good settings aren't kept either
        self.assertRejected(board, "/config", {"stops": [{}], "settings": {"screen": 1, "led": 1, "night_start": 1,
                                                                            "night_end": 1}})


def record(stop_id, kind="train", route="Red"):
    return {"id": stop_id, "type": kind, "route": route, "dir": "Howard", "name": "Belmont",
            "arrivals": [{"min": 2, "dest": "Howard", "live": True, "delayed": False, "veh": "812"}],
            "alert": False, "ts": 1800000000}


class RelayTests(unittest.TestCase):
    """POST /relay and /stop as cta::parseRelayTick and cta::parseRelayStop (test_link)."""

    def post(self, board, path, msg):
        return call(board, "POST", path, json.dumps(msg).encode())[0]

    def test_relay_tick(self):
        board = link_send.FakeBoard("handheld", TOKEN)
        self.assertEqual(self.post(board, "/relay", {"now": 1800000000}), 200)
        self.assertEqual(self.post(board, "/relay", {"now": 1800000000, "wmo": 61, "temp": -3.5}), 200)
        self.assertEqual(board.tick["wmo"], 61)
        for bad in ({}, {"now": 1700000000}, {"now": "1800000000"}, {"now": 1800000000.5}, {"now": 2 ** 32},
                    {"now": True}, {"now": 1800000000, "wmo": 61}, {"now": 1800000000, "temp": 2},
                    {"now": 1800000000, "wmo": 100, "temp": 2}, {"now": 1800000000, "wmo": 1, "temp": "2"},
                    {"now": 1800000000, "wmo": 1, "temp": 101}):
            with self.subTest(bad):
                self.assertEqual(self.post(board, "/relay", bad), 400)
        self.assertEqual(board.tick["wmo"], 61)   # untouched
        self.assertFalse(call(board, "GET", "/info")[1]["relaying"])   # HTTP ticks don't mark relaying
        self.assertEqual(call(board, "POST", "/relay", b"{}", token=None)[0], 401)
        self.assertEqual(self.post(link_send.FakeBoard("dock", TOKEN), "/relay", {"now": 1800000000}), 404)

    def test_stop_keeps_only_listed_stops(self):
        board = link_send.FakeBoard("handheld", TOKEN)
        stop = {"id": "41320", "type": "train", "records": [record("41320"), record("41320", route="Brn")]}
        self.assertEqual(self.post(board, "/stop", stop), 200)
        self.assertEqual(board.relayed, {})   # not in a nearby or the pin: ignored, still 200
        self.post(board, "/pin", link_send.DEFAULT_PIN)
        self.assertEqual(self.post(board, "/stop", stop), 200)
        self.assertEqual(len(board.relayed["41320"]["records"]), 2)
        nearby = {"lat": 41.94, "lon": -87.65, "trains": [], "buses": ["1828"], "close": []}
        self.post(board, "/nearby", nearby)
        many = {"id": "1828", "type": "bus", "records": [record("1828", "bus", "22")] * 20}
        self.assertEqual(self.post(board, "/stop", many), 413)   # over 1 KB on HTTP
        few = {"id": "1828", "type": "bus", "records": []}
        self.assertEqual(self.post(board, "/stop", few), 200)
        self.assertEqual(board.relayed["1828"]["records"], [])

    def test_stop_rules(self):
        board = link_send.FakeBoard("handheld", TOKEN)
        self.post(board, "/pin", link_send.DEFAULT_PIN)
        good = {"id": "41320", "type": "train", "records": [record("41320")]}
        for change in ({"id": ""}, {"id": "12345678"}, {"id": 41320}, {"type": "boat"}, {"records": {}},
                       {"records": None}, {"records": [record("40380")]}, {"records": [record("41320", "bus")]},
                       {"records": [5]}, {"records": [{"type": "train"}]}):
            with self.subTest(change):
                self.assertEqual(self.post(board, "/stop", {**good, **change}), 400)
        self.assertEqual(board.relayed, {})

    def test_info_reports_rssi_settings_relaying(self):
        board = link_send.FakeBoard("handheld", TOKEN)
        info = call(board, "GET", "/info")[1]
        self.assertIn("rssi", info["wifi"])
        self.assertNotIn("settings", info)
        self.assertFalse(info["relaying"])
        settings = {"screen": 80, "led": 40, "night_start": 60, "night_end": 300}
        self.post(board, "/config", {"settings": settings})
        self.assertEqual(call(board, "GET", "/info")[1]["settings"], settings)


class PairTests(unittest.TestCase):
    """POST /pair as cta::PairGate (pair.h, test_pair): the same answers in the same order."""

    CODE = "042917"

    def fresh(self, clock=None):
        return link_send.FakeBoard("dock", None, now=clock or Clock(), code=self.CODE)

    def pair(self, board, code, token=TOKEN):
        return call(board, "POST", "/pair", json.dumps({"code": code, "token": token}).encode(), token=None)[0]

    def test_right_code_pairs_once(self):
        board = self.fresh()
        self.assertFalse(call(board, "GET", "/info")[1]["paired"])
        self.assertEqual(call(board, "POST", "/config", b"{}")[0], 401)   # no token yet: every write 401
        self.assertEqual(self.pair(board, self.CODE), 200)
        self.assertTrue(call(board, "GET", "/info")[1]["paired"])
        self.assertEqual(call(board, "POST", "/config", b"{}")[0], 200)   # the token works at once
        self.assertEqual(self.pair(board, self.CODE), 409)

    def test_locks_after_5_wrong(self):
        board = self.fresh()
        self.assertEqual([self.pair(board, "000000") for _ in range(5)], [403] * 5)
        self.assertEqual(self.pair(board, self.CODE), 423)   # the right code, too late
        self.assertFalse(call(board, "GET", "/info")[1]["paired"])

    def test_closed_after_5_minutes(self):
        clock = Clock()
        late = self.fresh(clock)
        clock.t += 300.001
        self.assertEqual(self.pair(late, self.CODE), 423)
        self.assertEqual(self.pair(late, "000000"), 423)
        self.assertEqual(late.wrong, 0)
        board = self.fresh(clock)
        clock.t += 300
        self.assertEqual(self.pair(board, self.CODE), 200)   # the last moment is in

    def test_bad_bodies_are_400_and_count_nothing(self):
        board = self.fresh()
        for body in (b"{oops", b"[]", b"{}", json.dumps({"code": 42917, "token": TOKEN}).encode(),
                     json.dumps({"code": "04291", "token": TOKEN}).encode(),
                     json.dumps({"code": self.CODE, "token": TOKEN.upper()}).encode()):
            with self.subTest(body):
                self.assertEqual(call(board, "POST", "/pair", body, token=None)[0], 400)
        self.assertEqual(board.wrong, 0)
        self.assertEqual(call(board, "POST", "/pair", b"{" + b" " * 2046 + b"}", token=None)[0], 413)
        self.assertEqual(self.pair(board, self.CODE), 200)

    def test_paired_answers_409_before_anything(self):
        board = link_send.FakeBoard("handheld", TOKEN)   # paired from the start
        self.assertEqual(self.pair(board, "000000"), 409)
        self.assertEqual(board.wrong, 0)

    def test_reset_unpairs_with_a_new_code(self):
        board = link_send.FakeBoard("dock", TOKEN, now=Clock())
        self.assertEqual(call(board, "POST", "/reset", b"{}")[0], 200)
        self.assertFalse(call(board, "GET", "/info")[1]["paired"])
        self.assertRegex(board.code, r"^[0-9]{6}$")
        self.assertEqual(call(board, "POST", "/config", b"{}")[0], 401)
        self.assertEqual(self.pair(board, board.code), 200)


class BuildTests(unittest.TestCase):
    def test_commands(self):
        self.assertEqual(link_send.build("info", None), ("GET", "/info", None))
        self.assertEqual(link_send.build("config", None), ("GET", "/config", None))
        self.assertEqual(link_send.build("unpin", None), ("DELETE", "/pin", None))
        method, path, body = link_send.build("pair", "042917", TOKEN)
        self.assertEqual((method, path), ("POST", "/pair"))
        self.assertEqual(json.loads(body), {"code": "042917", "token": TOKEN})
        for code in (None, "42917", "04291a"):
            with self.assertRaises(ValueError):
                link_send.build("pair", code, TOKEN)
        with self.assertRaises(ValueError):
            link_send.build("pair", "042917")   # no token
        method, path, body = link_send.build("pet", '{"name":"T","colors":[1,2]}')
        self.assertEqual((method, path), ("POST", "/config"))
        self.assertEqual(json.loads(body), {"pet": {"name": "T", "colors": [1, 2]}})
        with self.assertRaises(ValueError):
            link_send.build("pin", None)
        self.assertEqual(link_send.build("relay", "1800000000"), ("POST", "/relay", b'{"now":1800000000}'))
        method, path, body = link_send.build("relay", None)
        self.assertEqual((method, path), ("POST", "/relay"))
        self.assertGreater(json.loads(body)["now"], link_send.RELAY_MIN_NOW)
        with self.assertRaises(ValueError):
            link_send.build("relay", "soon")
        stop = '{"id":"41320","type":"train","records":[]}'
        self.assertEqual(link_send.build("stop", stop), ("POST", "/stop", stop.encode()))


if __name__ == "__main__":
    unittest.main()
