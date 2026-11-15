"""Drive a board's HTTP link (ecosystem spec 4.4) from this Mac, or stand in for one.

    python3 tools/link_send.py cta-pet-a1b2.local info
    python3 tools/link_send.py cta-dock-a1b2.local pair 042917                 # the code on the board's screen
    python3 tools/link_send.py cta-pet-a1b2.local pin '{"type":"train","route":"Red","dir":"95th/Dan Ryan","id":"41320","name":"Belmont"}'
    python3 tools/link_send.py cta-dock-a1b2.local config                       # GET /config
    python3 tools/link_send.py cta-dock-a1b2.local config '{"settings":{...}}'  # POST /config
    python3 tools/link_send.py cta-pet-a1b2.local pet '{"name":"Trackie","colors":[2976742,8238335]}'
    python3 tools/link_send.py cta-pet-a1b2.local relay                        # POST /relay {"now": this Mac's clock}
    python3 tools/link_send.py cta-pet-a1b2.local relay 1800000000
    python3 tools/link_send.py cta-pet-a1b2.local stop '{"id":"41320","type":"train","records":[...]}'
    python3 tools/link_send.py cta-pet-a1b2.local --burst     # 20 pins in 2 s: each answers 200 within 500 ms
    python3 tools/link_send.py cta-pet-a1b2.local --garbage    # to /config: bad JSON 400, 2 KB 413, no token 401, wrong token 401
    python3 tools/link_send.py --serve --type handheld         # a fake board on this Mac, for the app

Writes need the household token: --token <32 hex> or LINK_TOKEN. It's never printed. pair hands the board that token.
--serve with no token is a fresh board: it prints its pairing code (its "screen") and takes POST /pair.
"""
import argparse
import hmac
import http.client
import http.server
import json
import math
import os
import re
import secrets
import shutil
import subprocess
import sys
import time

from send_nearby import CLOSE, PER_TAB

MAX_BODY = 1024  # cta::kMaxBody
TOKEN_RE = re.compile(r"^[0-9a-f]{32}$")
CODE_RE = re.compile(r"^[0-9]{6}$")
PAIR_MAX_WRONG, PAIR_COOLDOWN_S = 5, 30   # cta::kPairMaxWrong, cta::kPairCooldownMs
BURST_N, BURST_SECONDS, BURST_LIMIT_MS = 20, 2.0, 500
FAKE_ID = "f4ce"
DEFAULT_PIN = {"type": "train", "route": "Red", "dir": "95th/Dan Ryan", "id": "41320", "name": "Belmont"}

# command -> (method, path, how the json argument is used: None none, "body" as is, "pet" wrapped as config.pet)
COMMANDS = {
    "info": ("GET", "/info", None),
    "config": ("POST", "/config", "body"),   # GET /config when no json is given
    "pet": ("POST", "/config", "pet"),
    "nearby": ("POST", "/nearby", "body"),
    "pin": ("POST", "/pin", "body"),
    "follow": ("POST", "/follow", "body"),
    "unpin": ("DELETE", "/pin", None),
    "unfollow": ("DELETE", "/follow", None),
    "pair": ("POST", "/pair", "pair"),   # the json argument is the 6-digit code
    "reset": ("POST", "/reset", None),
    "relay": ("POST", "/relay", "now"),   # the json argument is the Unix time, default now
    "stop": ("POST", "/stop", "body"),
}
RELAY_MIN_NOW = 1700000000   # cta::parseRelayTick: now must be later
MAX_RELAY_RECORDS = 16       # cta::kMaxRelayRecords


def request(host, method, path, body=None, token=None, timeout=5):
    """(status, response text). body is bytes or None."""
    conn = http.client.HTTPConnection(host, timeout=timeout)
    headers = {"Content-Type": "application/json"} if body is not None else {}
    if token is not None:
        headers["Authorization"] = "Bearer " + token
    try:
        conn.request(method, path, body=body, headers=headers)
        r = conn.getresponse()
        return r.status, r.read().decode(errors="replace")
    finally:
        conn.close()


def build(cmd, arg, token=None):
    """(method, path, body bytes or None) for a command and its optional json argument (pair: the code)."""
    method, path, use = COMMANDS[cmd]
    if cmd == "config" and arg is None:
        return "GET", path, None
    if use == "pair":
        if arg is None or not CODE_RE.match(arg):
            raise ValueError("pair needs the 6-digit code")
        if token is None:
            raise ValueError("pair needs --token or LINK_TOKEN")
        return method, path, json.dumps({"code": arg, "token": token}, separators=(",", ":")).encode()
    if use == "now":
        now = int(time.time()) if arg is None else int(arg)
        return method, path, json.dumps({"now": now}, separators=(",", ":")).encode()
    if use is None:
        return method, path, (b"{}" if method == "POST" else None)
    if arg is None:
        raise ValueError(f"{cmd} needs a json argument")
    value = json.loads(arg)
    if use == "pet":
        value = {"pet": value}
    return method, path, json.dumps(value, separators=(",", ":")).encode()


def other_token(token):
    return "0" * 32 if token != "0" * 32 else "1" * 32


def garbage_cases(token):
    """(name, body, token to send, expected status) for --garbage, all POST /config (both boards serve it)."""
    return [
        ("bad json", b'{"pet":', token, 400),
        ("2 KB body", b"{" + b" " * 2046 + b"}", token, 413),
        ("no token", b"{}", None, 401),
        ("wrong token", b"{}", other_token(token), 401),
    ]


def run_garbage(host, token):
    ok = True
    for name, body, tok, want in garbage_cases(token):
        try:
            status, _ = request(host, "POST", "/config", body, tok)
        except OSError as e:
            status = f"error ({e.__class__.__name__})"
        good = status == want
        ok &= good
        print(f"{'ok  ' if good else 'FAIL'} {name}: {status} (want {want})")
    return ok


def run_burst(host, token, pin):
    body = json.dumps(pin).encode()
    ok = True
    start = time.monotonic()
    for i in range(BURST_N):
        t0 = time.monotonic()
        try:
            status, _ = request(host, "POST", "/pin", body, token)
        except OSError as e:
            status = f"error ({e.__class__.__name__})"
        ms = (time.monotonic() - t0) * 1000
        good = status == 200 and ms <= BURST_LIMIT_MS
        ok &= good
        print(f"{'ok  ' if good else 'FAIL'} pin {i + 1:2}: {status} in {ms:.0f} ms")
        wait = start + (i + 1) * BURST_SECONDS / BURST_N - time.monotonic()
        if wait > 0:
            time.sleep(wait)
    return ok


def valid(path, msg):
    """Whether the boards' parsers (link_msg.h, nearby_msg.h) would take this body; the fake answers 400 otherwise.
    Text that the boards cut to fit (dir, name, dest) only has to be a string here."""
    def text(v, lo=0, hi=None):
        return isinstance(v, str) and lo <= len(v.encode()) and (hi is None or len(v.encode()) <= hi)

    def num(v, lo, hi):
        return isinstance(v, int) and not isinstance(v, bool) and lo <= v <= hi

    def stop(m):
        return (isinstance(m, dict) and m.get("type") in ("train", "bus") and text(m.get("route"), 1, 7)
                and text(m.get("id"), 1, 7) and text(m.get("dir")) and text(m.get("name")))

    def pick(m):   # decodePicks: null, {}, or an entry without an id is an empty section
        return m is None or (isinstance(m, dict) and (not m.get("id") or m.get("type") in ("train", "bus")))

    if path == "/pin":
        return stop(msg)
    if path == "/follow":
        return stop(msg) and text(msg.get("vehicle"), 1, 7) and text(msg.get("dest"))
    if path == "/nearby":   # cta::parseNearby
        def ids(v, most, ok):
            return (isinstance(v, list) and len(v) <= most and len(set(map(str, v))) == len(v)
                    and all(isinstance(i, str) and ok(i) for i in v))

        def coord(v, lim):
            return isinstance(v, (int, float)) and not isinstance(v, bool) and math.isfinite(v) and -lim <= v <= lim

        station = re.compile(r"^4[0-9]{4}$").match
        bus = re.compile(r"^[0-9]{1,7}$").match
        trains, buses, close = msg.get("trains"), msg.get("buses"), msg.get("close")
        return (coord(msg.get("lat"), 90) and coord(msg.get("lon"), 180) and ids(trains, PER_TAB, station)
                and ids(buses, PER_TAB, bus) and ids(close, CLOSE, lambda i: station(i) or bus(i))
                and all(i in trains or i in buses for i in close))
    if path == "/relay":   # cta::parseRelayTick
        wmo, temp = msg.get("wmo"), msg.get("temp")
        weather = (wmo is None and temp is None) or (
            num(wmo, 0, 99) and isinstance(temp, (int, float)) and not isinstance(temp, bool) and -100 <= temp <= 100)
        return num(msg.get("now"), RELAY_MIN_NOW + 1, 2 ** 32 - 1) and weather
    if path == "/stop":   # cta::parseRelayStop: every record is this stop's (decodeStopRecord cuts ids to 7 bytes)
        sid, kind, records = msg.get("id"), msg.get("type"), msg.get("records")
        return (text(sid, 1, 7) and kind in ("train", "bus") and isinstance(records, list)
                and all(isinstance(r, dict) and isinstance(r.get("id"), str) and r.get("type") == kind
                        and r["id"].encode()[:7] == sid.encode() for r in records))
    if path != "/config":
        return True
    stops, pet, settings = msg.get("stops"), msg.get("pet"), msg.get("settings")
    if stops is not None and not (isinstance(stops, list) and len(stops) == 3 and all(map(pick, stops))):
        return False
    if pet is not None and not (isinstance(pet, dict) and text(pet.get("name")) and isinstance(pet.get("colors"), list)
                                and len(pet["colors"]) == 2 and all(num(c, 0, 0xFFFFFF) for c in pet["colors"])):
        return False
    limits = {"screen": 100, "led": 100, "night_start": 1439, "night_end": 1439}
    return settings is None or (isinstance(settings, dict) and all(num(settings.get(k), 0, hi) for k, hi in limits.items()))


class FakeBoard:
    """A board's link API answered from memory (spec 4.4), for building the app without the hardware."""

    def __init__(self, kind, token, now=time.time, code=None):
        """token None is a fresh board: unpaired, with code (random if None) open for POST /pair."""
        if kind not in ("dock", "handheld"):
            raise ValueError(kind)
        self.kind, self.token, self.now = kind, token, now
        self.code = code or f"{secrets.randbelow(10 ** 6):06d}"
        self.wrong = 0   # wrong codes since this code was set
        self.cool_start = None   # when the cooldown began, while one runs (code None)
        self.booted = now()
        self.picks_ver = 0
        self.wake = 0
        self.config = {"pet": None, "settings": None}
        if kind == "dock":
            self.config["stops"] = [{}, {}, {}]
        self.pin = self.follow = self.nearby = None
        self.tick = None    # the last /relay body
        self.relayed = {}   # stop id -> the last /stop body for it

    def info(self):
        t = self.now()
        return {"type": self.kind, "id": FAKE_ID, "fw": "fake", "battery": None if self.kind == "dock" else 100,
                "charging": False, "wifi": {"connected": True, "ssid": "", "rssi": -55}, "last_update": int(t),
                "uptime": int(t - self.booted), "picks_ver": self.picks_ver, "wake": self.wake, "docked": False,
                "paired": self.token is not None,
                **({"settings": self.config["settings"]} if self.config["settings"] else {}),
                "relaying": False}   # only a tick over Bluetooth marks relaying, and the fake is HTTP

    def handle(self, method, path, headers, read):
        """(status, reply dict). headers: a dict-like; read(n) gives the body. Checks run in the boards' order:
        size before reading, then the token, then the JSON."""
        handheld_only = ("/nearby", "/pin", "/follow", "/relay", "/stop")
        routes = {("GET", "/info"), ("GET", "/config"), ("POST", "/config"), ("POST", "/pair"), ("POST", "/reset")}
        if self.kind == "handheld":
            routes |= {("POST", p) for p in handheld_only} | {("DELETE", "/pin"), ("DELETE", "/follow")}
        if (method, path) not in routes:
            return 404, {"error": "not found"}
        try:
            length = int(headers.get("Content-Length") or 0)
        except ValueError:
            return 400, {"error": "bad length"}
        if length > MAX_BODY:
            return 413, {"error": "too large"}
        body = read(length) if length > 0 else b""
        if method == "GET":
            return 200, (self.info() if path == "/info" else {k: v for k, v in self.config.items() if v is not None})
        if path == "/pair":
            return self._pair(body)
        if not self._authorized(headers.get("Authorization")):
            return 401, {"error": "unauthorized"}
        if method == "DELETE":
            setattr(self, path[1:], None)
            return 200, {}
        if not body and path == "/reset":
            body = b"{}"
        try:
            msg = json.loads(body)
        except ValueError:
            return 400, {"error": "bad json"}
        if not isinstance(msg, dict):
            return 400, {"error": "bad json"}
        return self._apply(path, msg)

    def _authorized(self, auth):
        return (self.token is not None and isinstance(auth, str) and auth.startswith("Bearer ")
                and hmac.compare_digest(auth[7:], self.token))

    def _pair(self, body):
        """POST /pair as cta::PairGate (pair.h): no bearer token; a bad body is 400 and counts nothing."""
        try:
            msg = json.loads(body)
        except ValueError:
            return 400, {"error": "bad json"}
        if not (isinstance(msg, dict) and isinstance(msg.get("code"), str) and CODE_RE.match(msg["code"])
                and isinstance(msg.get("token"), str) and TOKEN_RE.match(msg["token"])):
            return 400, {"error": "bad body"}
        if self.token is not None:
            return 409, {"error": "already paired"}
        self.refresh_code()
        if self.cool_start is not None:
            return 423, {"error": "cooling down",
                         "retry_after": math.ceil(PAIR_COOLDOWN_S - (self.now() - self.cool_start))}
        if not hmac.compare_digest(msg["code"], self.code):
            self.wrong += 1
            if self.wrong >= PAIR_MAX_WRONG:   # drop the guessed-at code; a new one after the cooldown
                self.code, self.wrong, self.cool_start = None, 0, self.now()
            return 403, {"error": "wrong code"}
        self.token = msg["token"]
        return 200, {}

    def refresh_code(self):
        """cta::PairGate::needsCode: once the cooldown is over, the board picks a new code."""
        if self.cool_start is not None and self.now() - self.cool_start >= PAIR_COOLDOWN_S:
            self.code, self.cool_start = f"{secrets.randbelow(10 ** 6):06d}", None

    def _apply(self, path, msg):
        if not valid(path, msg):
            return 400, {"error": "bad body"}
        if path in ("/pin", "/follow"):
            setattr(self, path[1:], msg)
            if path == "/follow":
                self.pin = {k: msg.get(k) for k in ("type", "route", "dir", "id", "name")}
            return 200, {}
        if path == "/nearby":
            self.nearby = msg
            return 200, {}
        if path == "/relay":
            self.tick = msg
            return 200, {}
        if path == "/stop":   # a stop that isn't in the last nearby or the pin is ignored, still 200
            near = self.nearby or {}
            if msg["id"] in near.get("trains", []) + near.get("buses", []) or (self.pin or {}).get("id") == msg["id"]:
                self.relayed[msg["id"]] = dict(msg, records=msg["records"][:MAX_RELAY_RECORDS])
            return 200, {}
        if path == "/config":
            if msg.get("stops") is not None and self.kind == "dock":
                self.config["stops"] = msg["stops"]
                self.picks_ver += 1
            for key in ("pet", "settings"):
                if msg.get(key) is not None:
                    self.config[key] = msg[key]
            return 200, {}
        self.__init__(self.kind, None, self.now)   # /reset: forgets the token, back to pairing with a new code
        return 200, {}


def serve(board, port):
    shown = [None]

    def show_code():   # the fake's screen: the code while it's unpaired, again after each /reset or new code
        board.refresh_code()
        if board.token is None and board.code != shown[0]:
            print(f"Unpaired: pairing code {board.code}" if board.code else
                  f"Unpaired: {PAIR_MAX_WRONG} wrong codes, a new one in {PAIR_COOLDOWN_S} s", file=sys.stderr)
        shown[0] = board.code if board.token is None else None

    class Handler(http.server.BaseHTTPRequestHandler):
        def _any(self):
            status, reply = board.handle(self.command, self.path, self.headers, self.rfile.read)
            show_code()
            data = json.dumps(reply).encode()
            self.send_response(status)
            self.send_header("Content-Type", "application/json")
            self.send_header("Content-Length", str(len(data)))
            self.send_header("Connection", "close")
            self.end_headers()
            self.wfile.write(data)

        do_GET = do_POST = do_DELETE = _any

        def log_message(self, fmt, *args):   # the default log is fine: it never includes headers
            sys.stderr.write("%s %s\n" % (self.address_string(), fmt % args))

    server = http.server.ThreadingHTTPServer(("", port), Handler)
    name = f"cta-{'dock' if board.kind == 'dock' else 'pet'}-{FAKE_ID}"
    adv = None
    if shutil.which("dns-sd"):
        adv = subprocess.Popen(["dns-sd", "-R", name, "_ctatracker._tcp", "local", str(port),
                                f"type={board.kind}", f"id={FAKE_ID}"], stdout=subprocess.DEVNULL)
    else:
        print("dns-sd not found: not advertising over Bonjour", file=sys.stderr)
    print(f"Fake {board.kind} '{name}' on port {port}. Ctrl-C to stop.", file=sys.stderr)
    show_code()
    try:
        server.serve_forever()
    except KeyboardInterrupt:
        pass
    finally:
        server.server_close()
        if adv:
            adv.terminate()


def main():
    ap = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    ap.add_argument("host", nargs="?", help="the board: cta-pet-xxxx.local, an IP, or host:port")
    ap.add_argument("command", nargs="?", choices=sorted(COMMANDS))
    ap.add_argument("json", nargs="?", help="the body (for config, pet, nearby, pin, follow, stop), pair's code, or relay's time")
    ap.add_argument("--token", help="the household token (default: LINK_TOKEN)")
    ap.add_argument("--burst", action="store_true", help=f"send {BURST_N} pins in {BURST_SECONDS:g} s")
    ap.add_argument("--garbage", action="store_true", help="send bad bodies; expect 400, 413, 401, 401")
    ap.add_argument("--serve", action="store_true", help="be a fake board on this Mac")
    ap.add_argument("--type", choices=("dock", "handheld"), default="handheld", help="the fake board's type")
    ap.add_argument("--port", type=int, default=80, help="the fake board's port (default 80)")
    args = ap.parse_args()

    token = args.token or os.environ.get("LINK_TOKEN")
    if token is not None and not TOKEN_RE.match(token):
        ap.error("the token must be 32 lowercase hex characters")
    if args.serve:
        serve(FakeBoard(args.type, token), args.port)
        return
    if not args.host:
        ap.error("give a host")
    if (args.burst or args.garbage) and token is None:
        ap.error("needs --token or LINK_TOKEN")
    if args.garbage:
        sys.exit(0 if run_garbage(args.host, token) else 1)
    if args.burst:
        pin = json.loads(args.json) if args.json else DEFAULT_PIN
        sys.exit(0 if run_burst(args.host, token, pin) else 1)
    if not args.command:
        ap.error("give a command, --burst, or --garbage")
    try:
        method, path, body = build(args.command, args.json, token)
    except ValueError as e:
        ap.error(str(e))
    if method != "GET" and token is None:
        ap.error("writes need --token or LINK_TOKEN")
    try:
        status, text = request(args.host, method, path, body, token if method != "GET" else None)
    except OSError as e:
        sys.exit(f"{args.host}: {e}")
    print(status, text)
    sys.exit(0 if status == 200 else 1)


if __name__ == "__main__":
    main()
