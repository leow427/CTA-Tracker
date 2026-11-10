"""Run one power analysis stress test on the handheld and record it (power analysis spec, section 5).

Flashes env:power, sends the nearby list (fixed test coordinates, or this Mac's location with --here), starts the stress test (serial "S"), and saves every power-log line
(`H,`, `P,`, `E,`) to docs/power/runs/<date>-<change>-<n>.csv until the test says `E,...,done`. Other serial output
(network logs, which name the Wi-Fi network and stops) is never saved.

    python3 tools/power_run.py --change baseline --note "first working version"
    python3 tools/power_run.py --change c01 --no-flash
"""
import argparse
import datetime
import glob
import os
import re
import signal
import subprocess
import sys
import time

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
FW = os.path.join(ROOT, "firmware", "handheld", "mock")
RUNS = os.path.join(ROOT, "docs", "power", "runs")
PIO_PYTHON = os.path.expanduser("~/.platformio/penv/bin/python")
STRESS_VERSION = "stress-v2"   # must match stress::kVersion in the firmware
TIMEOUT_S = 35 * 60   # warm-up (at most 3 min) + 19 min of phases, with room


def git(*args):
    return subprocess.run(["git", "-C", ROOT, *args], capture_output=True, text=True).stdout.strip()


def find_port():
    ports = sorted(glob.glob("/dev/cu.usbmodem*"))
    if len(ports) != 1:
        sys.exit(f"Found {len(ports)} USB serial ports ({', '.join(ports) or 'none'}); pass --port.")
    return ports[0]


def wait_port(port, timeout=20):
    end = time.time() + timeout
    while time.time() < end:
        if os.path.exists(port):
            return True
        time.sleep(0.25)
    return False


def out_path(change):
    os.makedirs(RUNS, exist_ok=True)
    day = datetime.date.today().isoformat()
    safe = re.sub(r"[^A-Za-z0-9_-]", "-", change)
    n = 1
    while os.path.exists(os.path.join(RUNS, f"{day}-{safe}-{n}.csv")):
        n += 1
    return os.path.join(RUNS, f"{day}-{safe}-{n}.csv")


def main():
    ap = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    ap.add_argument("--change", default="baseline", help="change ID for the log (baseline, c01, ...)")
    ap.add_argument("--note", default="", help="what changed, one line")
    ap.add_argument("--port")
    ap.add_argument("--lat", type=float, default=41.94)
    ap.add_argument("--lon", type=float, default=-87.65)
    ap.add_argument("--here", action="store_true",
                    help="the nearby list for where this Mac is (CoreLocation), not the fixed test coordinates; "
                         "the coordinates are never printed or saved")
    ap.add_argument("--no-flash", action="store_true", help="the board already runs this firmware")
    ap.add_argument("--firmware", help="with --no-flash: the commit the board is running (default: HEAD)")
    args = ap.parse_args()

    try:
        import serial
    except ImportError:
        if os.path.exists(PIO_PYTHON) and sys.executable != PIO_PYTHON:
            os.execv(PIO_PYTHON, [PIO_PYTHON] + sys.argv)
        sys.exit("Needs pyserial: PlatformIO's Python has it.")

    # stress-v2 keeps daytime polling and moods during a run, so night hours (1-5 AM) no longer matter.

    port = args.port or find_port()
    if not args.no_flash:
        print("Flashing env:power...", file=sys.stderr)
        r = subprocess.run(["pio", "run", "-e", "power", "-t", "upload", "-d", FW], capture_output=True, text=True)
        if r.returncode != 0:
            sys.exit("Flash failed:\n" + r.stdout[-3000:] + r.stderr[-2000:])
        time.sleep(2)
        if not wait_port(port):
            sys.exit(f"{port} didn't come back after flashing.")
        time.sleep(4)   # boot

    where = [] if args.here else ["--lat", str(args.lat), "--lon", str(args.lon)]
    print("Sending the nearby list for " + ("this Mac's location" if args.here else "the fixed test coordinates")
          + "...", file=sys.stderr)
    r = subprocess.run([sys.executable, os.path.join(ROOT, "tools", "send_nearby.py")] + where + ["--port", port],
                       capture_output=True, text=True)
    if r.returncode != 0:
        sys.exit("send_nearby.py failed:\n" + r.stderr[-2000:])

    if args.firmware:
        commit = args.firmware
    else:
        commit = git("rev-parse", "--short", "HEAD")
        if git("status", "--porcelain", "--", "firmware"):
            commit += "-dirty"
    path = out_path(args.change)
    header = [
        f"# change: {args.change}",
        f"# note: {args.note}",
        f"# firmware: {commit}",
        f"# stress: {STRESS_VERSION}",
        f"# date: {datetime.datetime.now().isoformat(timespec='seconds')}",
        "# nearby: " + ("this Mac's real location (coordinates not recorded)" if args.here
                        else "the fixed test coordinates (power_run.py defaults)"),
        "# lines: H = column names, P = one sample a second, E = events; see the spec, section 2",
    ]

    # A stopped run (Ctrl-C or a kill) still saves what it recorded, marked aborted.
    def stopped(*_):
        raise KeyboardInterrupt
    signal.signal(signal.SIGTERM, stopped)

    with serial.Serial(port, 115200, timeout=0.5) as s:
        time.sleep(0.5)
        s.reset_input_buffer()
        s.write(b"S\n")
        started = False
        last_h = None
        kept = []
        t_start = time.time()
        last_print = 0
        phase = None
        aborted = None
        buf = b""
        try:
            while True:
                if time.time() - t_start > TIMEOUT_S:
                    aborted = "timeout"
                    break
                chunk = s.read(4096)
                if not chunk:
                    continue
                buf += chunk
                *lines, buf = buf.split(b"\n")
                done = False
                for raw in lines:
                    line = raw.decode(errors="replace").strip()
                    if line.startswith("H,"):
                        last_h = line
                        continue
                    if not (line.startswith("P,") or line.startswith("E,")):
                        if line.startswith("reset reason") and started:
                            aborted = "the board reset mid-run"
                            done = True
                        continue
                    if not started:
                        if line.startswith("E,") and f",start,{STRESS_VERSION}" in line:
                            started = True
                            if last_h:
                                kept.append(last_h)
                            kept.append(line)
                        continue
                    kept.append(line)
                    if line.startswith("E,") and ",boot," in line:
                        aborted = "the board reset mid-run"
                        done = True
                    elif line.startswith("E,") and line.endswith(",done"):
                        done = True
                    elif line.startswith("E,") and ",phase," in line:
                        phase = line.rsplit(",", 1)[1]
                        print(f"  phase {phase} at {time.time() - t_start:.0f} s", file=sys.stderr)
                if time.time() - last_print > 60 and started:
                    last_print = time.time()
                    p = [l for l in kept if l.startswith("P,")]
                    print(f"  {len(p)} samples, phase {phase}", file=sys.stderr)
                if done:
                    break
        except KeyboardInterrupt:
            aborted = "stopped by hand"
        if aborted:
            s.write(b"X\n")

    if aborted:
        header.append(f"# aborted: {aborted}")
    with open(path, "w") as f:
        f.write("\n".join(header + kept) + "\n")
    samples = sum(1 for l in kept if l.startswith("P,"))
    print(f"{'ABORTED (' + aborted + '): ' if aborted else ''}{samples} samples -> {os.path.relpath(path, ROOT)}",
          file=sys.stderr)
    print(path)
    sys.exit(1 if aborted else 0)


if __name__ == "__main__":
    main()
