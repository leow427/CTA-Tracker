"""Send the handheld its `nearby` list (spec 8.3) from this Mac, standing in for the app's BLE write.

Finds where the Mac is (CoreLocation, through a small app bundle so macOS can ask for permission), picks the 10
nearest train stations and 10 bus stops from data/stops.json, and writes the message to the handheld over USB
serial. The bus stops are each route and direction's nearest stop, the 10 nearest of those: the 10 nearest stops
outright are often one corner's few routes, both ways, on every side of the street. The handheld keeps it in flash, so this is needed once per place, not once per boot.

    python3 tools/send_nearby.py                         # where this Mac is
    python3 tools/send_nearby.py --lat 41.94 --lon -87.65
    python3 tools/send_nearby.py --dry-run               # show the stops, send nothing

The location goes only to the handheld (which passes it to Open-Meteo for weather, as the app would). Nothing here
prints the coordinates or writes them to the repo.
"""
import argparse
import glob
import json
import math
import os
import shutil
import subprocess
import sys
import tempfile
import time

ROOT = os.path.join(os.path.dirname(os.path.abspath(__file__)), "..")
STOPS = os.path.join(ROOT, "data", "stops.json")
LOCATE_SRC = os.path.join(ROOT, "tools", "mac_location")
LOCATE_APP = os.path.join(LOCATE_SRC, "build", "Trackie Locate.app")
PIO_PYTHON = os.path.expanduser("~/.platformio/penv/bin/python")
PER_TAB = 10  # cta::kNearbyPerTab
CLOSE = 3  # cta::kHypedClosestStops
LINE_MAX = 639  # the handheld's serial line buffer, less the newline


def km(lat0, lon0, lat, lon):
    kx = 111.32 * math.cos(math.radians(lat0))
    return math.hypot((lon - lon0) * kx, (lat - lat0) * 111.32)


def nearest(stops, kind, lat, lon):
    found = [s for s in stops if s["kind"] == kind]
    found.sort(key=lambda s: km(lat, lon, s["lat"], s["lon"]))
    return found[:PER_TAB]


def bus_stops(stops, lat, lon):
    """Each route and direction's nearest stop, then the 10 nearest of those stops (spec 8.3)."""
    best = {}
    for s in stops:
        if s["kind"] != "bus":
            continue
        d = km(lat, lon, s["lat"], s["lon"])
        for line in s.get("lines", []):
            key = (line["route"], line["dir"])
            if key not in best or d < best[key][0]:
                best[key] = (d, s)
    picked = {}
    for d, s in sorted(best.values(), key=lambda r: r[0]):
        picked.setdefault(s["id"], (d, s))
    return [s for _, s in sorted(picked.values(), key=lambda r: r[0])[:PER_TAB]]


def message(lat, lon, trains, buses):
    """The `nearby` message: each list nearest first, and the 3 closest stops of either kind."""
    both = sorted(trains + buses, key=lambda s: km(lat, lon, s["lat"], s["lon"]))
    return {
        "lat": round(lat, 6),
        "lon": round(lon, 6),
        "trains": [s["id"] for s in trains],
        "buses": [s["id"] for s in buses],
        "close": [s["id"] for s in both[:CLOSE]],
    }


def encode(msg):
    line = "N " + json.dumps(msg, separators=(",", ":"))
    if len(line) > LINE_MAX:
        raise ValueError(f"message is {len(line)} bytes; the handheld reads at most {LINE_MAX}")
    return line + "\n"


def build_locate():
    """Builds the location helper app once (needs the Xcode command line tools)."""
    exe = os.path.join(LOCATE_APP, "Contents", "MacOS", "Locate")
    src = os.path.join(LOCATE_SRC, "Locate.swift")
    if os.path.exists(exe) and os.path.getmtime(exe) >= os.path.getmtime(src):
        return
    shutil.rmtree(LOCATE_APP, ignore_errors=True)
    os.makedirs(os.path.dirname(exe))
    shutil.copy(os.path.join(LOCATE_SRC, "Info.plist"), os.path.join(LOCATE_APP, "Contents", "Info.plist"))
    subprocess.run(["swiftc", "-O", src, "-o", exe], check=True)
    subprocess.run(["codesign", "-s", "-", "--force", LOCATE_APP], check=True, capture_output=True)


def mac_location():
    """(lat, lon, accuracy in m) from CoreLocation. macOS asks for permission the first time."""
    build_locate()
    with tempfile.TemporaryDirectory() as tmp:
        out = os.path.join(tmp, "fix.txt")
        print("Finding this Mac's location (allow Trackie Locate if macOS asks)...", file=sys.stderr)
        code = subprocess.run(["open", "-W", "-n", LOCATE_APP, "--args", out]).returncode
        if not os.path.exists(out):
            sys.exit("No location. Check System Settings > Privacy & Security > Location Services: it must be on, "
                     "with Trackie Locate allowed. Or pass --lat and --lon." + (f" (exit {code})" if code else ""))
        lat, lon, acc = (float(x) for x in open(out).read().split())
    return lat, lon, acc


def find_port():
    ports = sorted(glob.glob("/dev/cu.usbmodem*"))
    if len(ports) != 1:
        sys.exit(f"Found {len(ports)} USB serial ports ({', '.join(ports) or 'none'}); pass --port.")
    return ports[0]


def send(port, line):
    import serial  # pyserial: in PlatformIO's Python (this script re-runs itself there when needed)

    with serial.Serial(port, 115200, timeout=0.2) as s:
        s.reset_input_buffer()
        s.write(line.encode())
        end = time.time() + 4
        while time.time() < end:
            reply = s.readline().decode(errors="replace").strip()
            if reply.startswith("nearby: "):
                return reply == "nearby: ok"
    sys.exit("The handheld didn't answer. Is it running the live build (pio run -e live -t upload)?")


def self_check(stops):
    trains = nearest(stops, "station", 41.9386, -87.6515)
    assert trains[0]["id"] == "41320", trains[0]  # Belmont is the nearest station to Belmont & Sheffield
    assert len(trains) == PER_TAB


def main():
    ap = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    ap.add_argument("--lat", type=float)
    ap.add_argument("--lon", type=float)
    ap.add_argument("--port", help="the handheld's serial port (default: the one /dev/cu.usbmodem*)")
    ap.add_argument("--dry-run", action="store_true", help="show the stops; send nothing")
    args = ap.parse_args()
    if (args.lat is None) != (args.lon is None):
        ap.error("pass both --lat and --lon, or neither")
    if not args.dry_run:
        try:
            import serial  # noqa: F401
        except ImportError:
            if os.path.exists(PIO_PYTHON) and sys.executable != PIO_PYTHON:
                os.execv(PIO_PYTHON, [PIO_PYTHON] + sys.argv)
            sys.exit("Needs pyserial: pip install pyserial (PlatformIO's Python has it).")

    with open(STOPS) as f:
        stops = json.load(f)
    self_check(stops)
    if args.lat is None:
        lat, lon, acc = mac_location()
        print(f"Location fix: within about {acc:.0f} m", file=sys.stderr)
    else:
        lat, lon = args.lat, args.lon
    trains = nearest(stops, "station", lat, lon)
    buses = bus_stops(stops, lat, lon)
    msg = message(lat, lon, trains, buses)
    for title, rows in (("Trains", trains), ("Buses", buses)):
        print(title + ":", file=sys.stderr)
        for s in rows:
            routes = sorted({ln["route"] + " " + ln["dir"][0] for ln in s.get("lines", [])})
            mark = "*" if s["id"] in msg["close"] else " "
            print(f" {mark}{km(lat, lon, s['lat'], s['lon']) * 1000:5.0f} m  {s['id']:>6}  {s['name']}"
                  + (f"  ({', '.join(routes)})" if routes else ""), file=sys.stderr)
    print("  * the 3 closest: only these can make Trackie Hyped", file=sys.stderr)
    line = encode(msg)
    if args.dry_run:
        return
    port = args.port or find_port()
    if not send(port, line):
        sys.exit("The handheld rejected the list.")
    print(f"Sent to {port}: the handheld is polling these stops now.", file=sys.stderr)


if __name__ == "__main__":
    main()
