"""Run one dock performance stress test and record it (dock performance analysis spec, section 5).

Flashes env:perf (firmware/dock), waits for the boot, starts the stress test (serial "S"), and saves every perf-log
line (`H,`, `P,`, `E,`) to docs/perf/runs/<date>-<change>-<n>.csv until the test says `E,...,done`. Other serial output
(network logs, touch prints) is never saved. Runs on the dock's UART port (the CH343, USB 1A86:55D3).

    python3 tools/dock_perf_run.py --change baseline --note "first working dock firmware"
    python3 tools/dock_perf_run.py --change r1 --no-flash --firmware abc1234
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
FW = os.path.join(ROOT, "firmware", "dock")
RUNS = os.path.join(ROOT, "docs", "perf", "runs")
PIO_PYTHON = os.path.expanduser("~/.platformio/penv/bin/python")
PIO = os.path.expanduser("~/.platformio/penv/bin/pio")
STRESS_VERSION = "dock-stress-v1"   # must match stress::kVersion in the firmware
TIMEOUT_S = 30 * 60   # warm-up (at most 3 min) + 15 min of phases, with room


def git(*args):
    return subprocess.run(["git", "-C", ROOT, *args], capture_output=True, text=True).stdout.strip()


def find_port():
    # The dock's CH343 shows up as usbmodem<serial>; the handheld's native USB (303A:1001) as usbmodem<digits>.
    try:
        from serial.tools import list_ports
        ports = [p.device for p in list_ports.comports() if p.vid == 0x1A86]
    except ImportError:
        ports = []
    if not ports:
        ports = sorted(glob.glob("/dev/cu.usbmodem5B*"))
    if len(ports) != 1:
        sys.exit(f"Found {len(ports)} dock serial ports ({', '.join(ports) or 'none'}); pass --port.")
    return ports[0]


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
    ap.add_argument("--change", default="baseline", help="change ID for the log (baseline, r1, ...)")
    ap.add_argument("--note", default="", help="what changed, one line")
    ap.add_argument("--port")
    ap.add_argument("--no-flash", action="store_true", help="the board already runs this firmware")
    ap.add_argument("--firmware", help="the commit the board runs (default: HEAD, -dirty if firmware/ has changes)")
    ap.add_argument("--fw-dir", default=FW, help="the dock firmware folder to flash (default: this checkout's)")
    args = ap.parse_args()

    try:
        import serial
    except ImportError:
        if os.path.exists(PIO_PYTHON) and sys.executable != PIO_PYTHON:
            os.execv(PIO_PYTHON, [PIO_PYTHON] + sys.argv)
        sys.exit("Needs pyserial: PlatformIO's Python has it.")

    port = args.port or find_port()
    if not args.no_flash:
        print("Flashing env:perf...", file=sys.stderr)
        r = subprocess.run([PIO, "run", "-e", "perf", "-t", "upload", "--upload-port", port, "-d", args.fw_dir],
                           capture_output=True, text=True)
        if r.returncode != 0:
            sys.exit("Flash failed:\n" + r.stdout[-3000:] + r.stderr[-2000:])

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
        f"# flashed: {'no' if args.no_flash else 'yes'} (opening the port reboots the dock: boot times valid)",
        "# lines: H = column names, P = one sample a second, E = events; see the spec, section 2",
    ]

    def stopped(*_):
        raise KeyboardInterrupt
    signal.signal(signal.SIGTERM, stopped)

    # Opening the port must not reset the board: DTR and RTS stay low (the CH343 wires them to EN and IO0).
    s = serial.Serial()
    s.port, s.baudrate, s.timeout = port, 115200, 0.5
    s.dtr = False
    s.rts = False
    s.open()
    with s:
        # Opening the CH343's port resets the dock (seen 2026-11-12), so every run starts from a fresh boot; the
        # warm-up waits for the data.
        time.sleep(8)
        s.reset_input_buffer()
        s.write(b"S")
        started, last_h, kept, phase, aborted, buf = False, None, [], None, None, b""
        t_start = last_print = time.time()
        try:
            while True:
                if time.time() - t_start > TIMEOUT_S:
                    aborted = "timeout"
                    break
                if not started and time.time() - t_start > 20:
                    aborted = "the stress test never started (no E,start line)"
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
                    if line.startswith("dock: reset reason") and started:
                        aborted, done = "the board reset mid-run", True
                        continue
                    if not (line.startswith("P,") or line.startswith("E,")):
                        continue
                    if not started:
                        if line.startswith("E,") and f",start,{STRESS_VERSION}" in line:
                            started = True
                            if last_h:
                                kept.append(last_h)
                            kept.append(line)
                        continue
                    kept.append(line)
                    if line.startswith("E,") and ",boot,reset_reason" in line:
                        aborted, done = "the board reset mid-run", True
                    elif line.startswith("E,") and line.endswith(",done"):
                        done = True
                    elif line.startswith("E,") and ",phase," in line:
                        phase = line.rsplit(",", 1)[1]
                        print(f"  phase {phase} at {time.time() - t_start:.0f} s", file=sys.stderr)
                if started and time.time() - last_print > 60:
                    last_print = time.time()
                    print(f"  {sum(1 for l in kept if l.startswith('P,'))} samples, phase {phase}", file=sys.stderr)
                if done:
                    break
        except KeyboardInterrupt:
            aborted = "stopped by hand"
        except serial.SerialException as e:
            aborted = f"the serial port went away ({e})"
        if aborted and s.is_open:
            try:
                s.write(b"X")
            except serial.SerialException:
                pass

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
